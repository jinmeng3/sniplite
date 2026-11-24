// Context-owned storage for immutable convolution weights and CPU tile workers.
#pragma once

#include <algorithm>
#include <array>
#include <cfenv>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <mutex>
#include <utility>
#include <thread>
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
#include <xmmintrin.h>
#endif

namespace core_cpu {

// One serialized model context owns one runtime. Weight addresses must remain
// immutable and alive until clear_weights() or runtime destruction. No process-
// global cache holds those addresses. Clear before replacing any model weights.
class Conv2dRuntime {
public:
    static constexpr size_t weight_limit_bytes = 64u * 1024u * 1024u;
    static constexpr size_t scratch_limit_bytes = 32u * 1024u * 1024u;
    static constexpr size_t weight_entry_limit = 256;
    static constexpr int worker_limit = 32; // Includes the calling thread.

    struct WeightKey {
        const float * identity;
        int in_ch, out_ch, kh, kw, groups, ocb, mode;
        bool operator==(const WeightKey & b) const noexcept {
            return identity == b.identity && in_ch == b.in_ch && out_ch == b.out_ch &&
                   kh == b.kh && kw == b.kw && groups == b.groups && ocb == b.ocb && mode == b.mode;
        }
    };
    using TileJob = void (*)(void *, size_t, size_t, float *);

    Conv2dRuntime() = default;
    Conv2dRuntime(const Conv2dRuntime &) = delete;
    Conv2dRuntime & operator=(const Conv2dRuntime &) = delete;
    ~Conv2dRuntime() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        for (auto & worker : workers_)
            if (worker.thread.joinable()) worker.thread.join();
    }

    void clear_weights() noexcept {
        for (auto & entry : weights_) entry = WeightEntry{};
        weight_bytes_ = 0;
        weight_clock_ = 0;
    }

    // Returns nullptr for an oversized pack; the caller keeps its uncached path.
    const float * packed_weights(const WeightKey & key) {
        for (auto & entry : weights_) {
            if (entry.data && entry.key == key) {
                entry.used = ++weight_clock_;
                return entry.data.get();
            }
        }
        const size_t k = (size_t)(key.in_ch / key.groups) * key.kh * key.kw;
        const int group_out = key.out_ch / key.groups;
        const int blocks = group_out / key.ocb;
        const size_t count = (size_t)key.groups * blocks * key.ocb * k;
        if (!count || count > weight_limit_bytes / sizeof(float)) return nullptr;
        const size_t bytes = count * sizeof(float);
        while (weight_bytes_ > weight_limit_bytes - bytes) evict_oldest();
        WeightEntry * slot = nullptr;
        for (auto & entry : weights_) {
            if (!entry.data) { slot = &entry; break; }
        }
        if (!slot) slot = evict_oldest();
        std::unique_ptr<float[]> data(new float[count]);
        for (int g = 0; g < key.groups; ++g) {
            const float * group = key.identity + (size_t)g * group_out * k;
            for (int block = 0; block < blocks; ++block) {
                float * dst = data.get() + ((size_t)g * blocks + block) * key.ocb * k;
                for (int row = 0; row < key.ocb; ++row) {
                    const float * src = group + (size_t)(block * key.ocb + row) * k;
                    for (size_t i = 0; i < k; ++i) dst[i * key.ocb + row] = src[i];
                }
            }
        }
        slot->key = key;
        slot->data = std::move(data);
        slot->bytes = bytes;
        slot->used = ++weight_clock_;
        weight_bytes_ += bytes;
        return slot->data.get();
    }

    // No allocation or thread creation once lane size and concurrency are warm.
    // Limits may reduce parallelism, never exceed the caller's thread limit.
    // False means even one lane exceeds the retained-scratch budget.
    bool run(TileJob job, void * context, size_t tiles, int requested, size_t lane_floats) {
        if (!tiles) return true;
        if (!lane_floats || lane_floats > scratch_limit_bytes / sizeof(float)) return false;
        const size_t lane_bytes = lane_floats * sizeof(float);
        int lanes = (int)std::min(tiles, (size_t)std::max(1, std::min(requested, worker_limit)));
        lanes = std::min(lanes, (int)(scratch_limit_bytes / lane_bytes));
        // Retained buffers have one shared high-water size. Drop old buffers
        // before growing so changing shapes cannot accumulate scratch storage.
        if (lane_floats > scratch_floats_ ||
            (size_t)lanes > scratch_limit_bytes / sizeof(float) / scratch_floats_) {
            for (auto & worker : workers_) worker.scratch.reset();
            scratch_floats_ = lane_floats;
        }
        const size_t retained_lanes = scratch_limit_bytes / sizeof(float) / scratch_floats_;
        for (size_t i = retained_lanes; i < workers_.size(); ++i) workers_[i].scratch.reset();
        for (int i = 0; i < lanes; ++i) {
            if (!workers_[i].scratch) workers_[i].scratch.reset(new float[scratch_floats_]);
        }
        // Start before publishing the job. A thread-creation exception leaves
        // all previously started workers idle and safely owned by this runtime.
        while (started_ < lanes - 1) {
            const int lane = started_ + 1;
            const uint64_t observed = generation_;
            workers_[lane].thread = std::thread([this, lane, observed] { worker_loop(lane, observed); });
            ++started_;
        }
        if (lanes == 1) {
            job(context, 0, tiles, workers_[0].scratch.get());
            return true;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            job_ = job;
            job_context_ = context;
            tiles_ = tiles;
            lanes_ = lanes;
            remaining_ = lanes - 1;
            failure_ = nullptr;
            std::fegetenv(&fp_env_);
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
            mxcsr_ = _mm_getcsr();
#endif
            ++generation_;
        }
        ready_.notify_all();
        std::exception_ptr caller_failure;
        try {
            job(context, 0, (tiles + lanes - 1) / lanes, workers_[0].scratch.get());
        } catch (...) { caller_failure = std::current_exception(); }
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [this] { return remaining_ == 0; });
        job_ = nullptr;
        job_context_ = nullptr;
        const std::exception_ptr failure = caller_failure ? caller_failure : failure_;
        lock.unlock();
        if (failure) std::rethrow_exception(failure);
        return true;
    }

private:
    struct WeightEntry {
        WeightKey key{};
        std::unique_ptr<float[]> data;
        size_t bytes = 0;
        uint64_t used = 0;
    };
    struct Worker {
        std::thread thread;
        std::unique_ptr<float[]> scratch;
    };
    WeightEntry * evict_oldest() noexcept {
        WeightEntry * oldest = nullptr;
        for (auto & entry : weights_)
            if (entry.data && (!oldest || entry.used < oldest->used)) oldest = &entry;
        if (oldest) {
            weight_bytes_ -= oldest->bytes;
            *oldest = WeightEntry{};
        }
        return oldest;
    }
    void worker_loop(int lane, uint64_t observed) noexcept {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            ready_.wait(lock, [this, &observed] { return stopping_ || generation_ != observed; });
            if (stopping_) return;
            observed = generation_;
            if (lane >= lanes_) continue;
            const TileJob job = job_;
            void * const context = job_context_;
            const size_t chunk = (tiles_ + lanes_ - 1) / lanes_;
            const size_t begin = std::min(tiles_, (size_t)lane * chunk);
            const size_t end = std::min(tiles_, begin + chunk);
            const std::fenv_t fp_env = fp_env_;
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
            const unsigned int mxcsr = mxcsr_;
#endif
            lock.unlock();
            std::exception_ptr failure;
            try {
                std::fesetenv(&fp_env);
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
                _mm_setcsr(mxcsr);
#endif
                if (begin < end) job(context, begin, end, workers_[lane].scratch.get());
            } catch (...) { failure = std::current_exception(); }
            lock.lock();
            if (failure && !failure_) failure_ = failure;
            if (--remaining_ == 0) done_.notify_one();
        }
    }

    std::array<WeightEntry, weight_entry_limit> weights_{};
    size_t weight_bytes_ = 0;
    uint64_t weight_clock_ = 0;
    std::array<Worker, worker_limit> workers_{};
    size_t scratch_floats_ = 0;
    int started_ = 0;
    std::mutex mutex_;
    std::condition_variable ready_, done_;
    bool stopping_ = false;
    uint64_t generation_ = 0;
    TileJob job_ = nullptr;
    void * job_context_ = nullptr;
    size_t tiles_ = 0;
    int lanes_ = 0, remaining_ = 0;
    std::exception_ptr failure_;
    std::fenv_t fp_env_{};
#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
    unsigned int mxcsr_ = 0;
#endif
};

} // namespace core_cpu
