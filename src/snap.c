/* DETECT_ELEMENTS combines child-window candidates with UI Automation results.
 * The overlay thread follows the pointer; a persistent worker searches the latest
 * request asynchronously and caches sibling elements. Cancellation and capture
 * never wait for a provider call. */
#include "snap.h"
#include "shot.h"
#include "ov_layout.inc"
#include <uiautomationclient.h>
#include <dwmapi.h>
#include <stddef.h>

#define SNAP_INTERVAL 24               /* throttle work, never debounce movement */
#define SNAP_ANIMATION_MS 100.0
#define SNAP_BUDGET 150
#define SNAP_TIMEOUT 200
#define SNAP_DEPTH 32
#define SNAP_NODES 512
#define SNAP_RECTS 32
#define SNAP_CACHE 512
#define SNAP_INPUTS (SNAP_RECTS + 16)
#ifndef SNAP_TRACE
#define SNAP_TRACE(...) ((void)0)
#endif

extern uint8_t O[];
#define STATE(type, offset) (*(type *)(O + (offset)))

_Static_assert(sizeof(RECT) == OV_CHAIN_BYTES, "candidate layout");
_Static_assert(sizeof(Shot) == SHOT_BYTES, "capture layout");
_Static_assert(offsetof(Shot, tile) == SHOT_TILE, "monitor layout");
_Static_assert(sizeof(Tile) == TILE_BYTES, "monitor size");
_Static_assert(WM_APP_SNAP_READY == OV_WM_SNAP_READY, "completion message");

typedef struct {
    HWND target, notify;
    RECT bounds;
    POINT point;
    DWORD pid;
    uint64_t version, sequence;        /* target generation vs. pointer update */
} Request;

static struct {
    SRWLOCK lock;
    HANDLE wake;
    bool active, pending, ready;
    Request request;
    uint64_t version, result_version, sequence;
    int nresult;
    RECT result[SNAP_CACHE];
} W;

/* Only the overlay thread accesses U. */
static struct {
    HWND overlay, target;
    RECT bounds, leaf, selected;
    RECT cached[SNAP_CACHE], incoming[SNAP_CACHE];
    int ncached, next_cache;
    bool manual, have_leaf;
} U;

/* Presentation state is separate from the exact rectangle used by all actions. */
static struct {
    RECT target, frame;
    double from[4], start;
    bool initialized, running, timer;
    BOOL enabled;
} A;

static void animation_stop(void)
{
    if (A.timer && U.overlay)
        KillTimer(U.overlay, TIMER_SNAP_ANIMATION);
    A.timer = A.running = false;
}

static LONG animation_edge(double from, LONG target, double eased)
{
    double value = from + ((double)target - from) * eased;
    return (LONG)(value + (value < 0 ? -0.5 : 0.5));
}

static RECT animation_sample(double now)
{
    if (!A.running) return A.frame;
    double t = (now - A.start) / SNAP_ANIMATION_MS;
    if (t >= 1.0) {
        A.frame = A.target;
        animation_stop();
    } else {
        if (t < 0.0) t = 0.0;
        double remaining = 1.0 - t;
        double eased = 1.0 - remaining * remaining * remaining;
        A.frame = (RECT){animation_edge(A.from[0], A.target.left, eased),
                         animation_edge(A.from[1], A.target.top, eased),
                         animation_edge(A.from[2], A.target.right, eased),
                         animation_edge(A.from[3], A.target.bottom, eased)};
    }
    return A.frame;
}

static bool same_rect(RECT a, RECT b)
{
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

static bool has_point(RECT r, POINT p)
{
    return p.x >= r.left && p.y >= r.top && p.x < r.right && p.y < r.bottom;
}

static bool contains(RECT outer, RECT inner)
{
    return outer.left <= inner.left && outer.top <= inner.top && outer.right >= inner.right &&
           outer.bottom >= inner.bottom;
}

static RECT intersect(RECT a, RECT b)
{
    return (RECT){MAXI(a.left, b.left), MAXI(a.top, b.top), MINI(a.right, b.right), MINI(a.bottom, b.bottom)};
}

static uint64_t area(RECT r)
{
    return (uint64_t)((int64_t)r.right - r.left) * (uint64_t)((int64_t)r.bottom - r.top);
}

static void add_rect(RECT *rects, int *n, int cap, RECT r, RECT clip, POINT point)
{
    r = intersect(r, clip);
    if (!has_point(r, point))
        return;
    for (int i = 0; i < *n; ++i)
        if (same_rect(rects[i], r))
            return;
    if (*n < cap) {
        rects[(*n)++] = r;
    } else {
        /* The window cache contains siblings too. Retain the finest hits if a
         * deep provider contributes more containing nodes than the chain holds. */
        int largest = 0;
        for (int i = 1; i < *n; ++i)
            if (area(rects[i]) > area(rects[largest])) largest = i;
        if (area(r) < area(rects[largest])) rects[largest] = r;
    }
}

/* Stable area order, then a strictly expanding chain. Reserve the three coarse
 * choices even if a provider exposes more fine components than we can display.
 * A window may span monitors: the monitor is deliberately a final fallback,
 * rather than being required to contain that window. */
static int merge_rects(RECT *out, const RECT *input, int count, RECT clip, POINT point,
                       RECT client, RECT window, RECT monitor, int capacity)
{
    RECT sorted[SNAP_INPUTS];
    int n = 0, used = 0;
    for (int i = 0; i < count; ++i)
        add_rect(sorted, &n, SNAP_INPUTS, input[i], clip, point);
    for (int i = 1; i < n; ++i) {
        RECT r = sorted[i];
        int j = i;
        while (j && area(sorted[j - 1]) > area(r)) {
            sorted[j] = sorted[j - 1];
            --j;
        }
        sorted[j] = r;
    }
    int reserve = MINI(3, capacity);
    for (int i = 0; i < n && used < capacity - reserve; ++i) {
        RECT r = sorted[i];
        if (same_rect(r, intersect(client, clip)) || same_rect(r, intersect(window, clip)))
            continue;
        if (!used || contains(r, out[used - 1]))
            out[used++] = r;
    }
    RECT coarse[3] = {intersect(client, clip), intersect(window, clip), monitor};
    for (int i = 0; i < 3 && used < capacity; ++i) {
        RECT r = coarse[i];
        /* In a gap between displays, shot_tile_at supplies the nearest monitor. */
        if (r.right <= r.left || r.bottom <= r.top || (i != 2 && !has_point(r, point)))
            continue;
        bool duplicate = false;
        for (int j = 0; j < used; ++j)
            duplicate |= same_rect(out[j], r);
        if (!duplicate && (i == 2 || !used || contains(r, out[used - 1])))
            out[used++] = r;
    }
    return used;
}

static bool window_bounds(HWND target, RECT *bounds)
{
    return SUCCEEDED(DwmGetWindowAttribute(target, DWMWA_EXTENDED_FRAME_BOUNDS, bounds, sizeof *bounds)) ||
           GetWindowRect(target, bounds);
}

static bool current_request(const Request *r)
{
    AcquireSRWLockShared(&W.lock);
    bool current = W.active && W.version == r->version;
    ReleaseSRWLockShared(&W.lock);
    return current;
}

static bool valid_target(const Request *r)
{
    DWORD pid = 0;
    RECT bounds;
    return IsWindow(r->target) && IsWindowVisible(r->target) && !IsIconic(r->target) &&
           GetWindowThreadProcessId(r->target, &pid) && pid == r->pid && window_bounds(r->target, &bounds) &&
           same_rect(bounds, r->bounds);
}

typedef struct {
    IUIAutomation2 *automation;
    IUIAutomationCacheRequest *cache;
    IUIAutomationCondition *all;
    bool initialized;
} Provider;

typedef struct {
    const Request *request;
    Provider *provider;
    ULONGLONG start;
    int visited, n;
    RECT rects[SNAP_CACHE];
} Search;

static void search_cache(Search *s, RECT bounds)
{
    RECT r = intersect(bounds, s->request->bounds);
    if (r.right <= r.left || r.bottom <= r.top) return;
    for (int i = 0; i < s->n; ++i)
        if (same_rect(s->rects[i], r)) return;
    if (s->n < SNAP_CACHE) s->rects[s->n++] = r;
}

static bool search_more(Search *s)
{
    return s->visited < SNAP_NODES && GetTickCount64() - s->start < SNAP_BUDGET &&
           current_request(s->request);
}

static bool component_type(CONTROLTYPEID type)
{
    return type != UIA_TextControlTypeId && type != UIA_ToolTipControlTypeId &&
           type != UIA_SeparatorControlTypeId;
}

/* Walk raw structural nodes, collect control-view components. Each direct-child
 * batch carries its own cached properties: no per-property provider round trips.
 * Empty-bound structural nodes are traversed, not mistaken for invisible UI. */
static void search_children(Search *s, IUIAutomationElement *parent, int depth)
{
    if (depth >= SNAP_DEPTH || !search_more(s))
        return;
    IUIAutomationElementArray *children = NULL;
    HRESULT batch_hr = IUIAutomationElement_FindAllBuildCache(parent, TreeScope_Children, s->provider->all,
                                                             s->provider->cache, &children);
    SNAP_TRACE("children depth=%d hr=%lx elapsed=%llu\n", depth, (unsigned long)batch_hr,
               (unsigned long long)(GetTickCount64() - s->start));
    if (FAILED(batch_hr) || !children)
        return;
    int length = 0;
    IUIAutomationElementArray_get_Length(children, &length);
    /* A batch can itself exceed the soft budget. Its cached results are local:
     * retain them, but search_more prevents starting another provider call. */
    for (int i = 0; i < length && s->visited < SNAP_NODES && current_request(s->request); ++i) {
        IUIAutomationElement *element = NULL;
        ++s->visited;
        if (FAILED(IUIAutomationElementArray_GetElement(children, i, &element)) || !element)
            continue;
        RECT bounds = {0};
        BOOL offscreen = FALSE, control = FALSE;
        CONTROLTYPEID type = 0;
        HRESULT hr = IUIAutomationElement_get_CachedBoundingRectangle(element, &bounds);
        bool empty = bounds.right <= bounds.left || bounds.bottom <= bounds.top;
        bool visible = SUCCEEDED(IUIAutomationElement_get_CachedIsOffscreen(element, &offscreen)) && !offscreen;
        SNAP_TRACE("node hr=%lx bounds=%ld,%ld,%ld,%ld visible=%d\n", (unsigned long)hr,
                   bounds.left, bounds.top, bounds.right, bounds.bottom, visible);
        if (SUCCEEDED(hr) && visible) {
            if (!empty && SUCCEEDED(IUIAutomationElement_get_CachedIsControlElement(element, &control)) &&
                control && SUCCEEDED(IUIAutomationElement_get_CachedControlType(element, &type)) && component_type(type)) {
                /* Cache siblings from the same batch as well as the hovered
                 * path. Crossing to a neighbor is then a local hit test. */
                search_cache(s, bounds);
            }
            if (empty || has_point(bounds, s->request->point))
                search_children(s, element, depth + 1);
        }
        IUIAutomationElement_Release(element);
    }
    IUIAutomationElementArray_Release(children);
}

static bool provider_open(Provider *p, HMODULE *module)
{
    typedef HRESULT(WINAPI *Init)(void *, DWORD);
    typedef HRESULT(WINAPI *Create)(REFCLSID, IUnknown *, DWORD, REFIID, void **);
    static const CLSID cls = {0xe22ad333, 0xb25f, 0x460c, {0x83,0xd0,0x05,0x81,0x10,0x73,0x95,0xc9}};
    static const IID iid = {0x34723aff, 0x0c9d, 0x49d0, {0x98,0x96,0x7a,0xb5,0x2d,0xf8,0xcd,0x8a}};
    *module = LoadLibraryW(L"ole32.dll");
    Init init = *module ? (Init)(void *)GetProcAddress(*module, "CoInitializeEx") : NULL;
    Create create = *module ? (Create)(void *)GetProcAddress(*module, "CoCreateInstance") : NULL;
    if (!init || !create || FAILED(init(NULL, COINIT_MULTITHREADED)))
        return false;
    p->initialized = true;
    if (FAILED(create(&cls, NULL, CLSCTX_INPROC_SERVER, &iid, (void **)&p->automation)))
        return false;
    if (FAILED(IUIAutomation2_put_ConnectionTimeout(p->automation, SNAP_TIMEOUT)) ||
        FAILED(IUIAutomation2_put_TransactionTimeout(p->automation, SNAP_TIMEOUT)) ||
        FAILED(IUIAutomation2_CreateCacheRequest(p->automation, &p->cache)) ||
        FAILED(IUIAutomation2_CreateTrueCondition(p->automation, &p->all)))
        return false;
    PROPERTYID properties[] = {UIA_BoundingRectanglePropertyId, UIA_ControlTypePropertyId,
                               UIA_IsControlElementPropertyId, UIA_IsOffscreenPropertyId};
    for (int i = 0; i < COUNTOF(properties); ++i)
        if (FAILED(IUIAutomationCacheRequest_AddProperty(p->cache, properties[i])))
            return false;
    return SUCCEEDED(IUIAutomationCacheRequest_put_TreeScope(p->cache, TreeScope_Element)) &&
           SUCCEEDED(IUIAutomationCacheRequest_put_TreeFilter(p->cache, p->all)) &&
           SUCCEEDED(IUIAutomationCacheRequest_put_AutomationElementMode(p->cache, AutomationElementMode_Full));
}

static DWORD WINAPI snap_worker(void *unused)
{
    Provider provider = {0};
    HMODULE module = NULL;
    if (!provider_open(&provider, &module)) {
        if (provider.all) IUIAutomationCondition_Release(provider.all);
        if (provider.cache) IUIAutomationCacheRequest_Release(provider.cache);
        if (provider.automation) IUIAutomation2_Release(provider.automation);
        if (module) {
            void (WINAPI *uninit)(void) = (void *)(GetProcAddress(module, "CoUninitialize"));
            if (uninit && provider.initialized) uninit();
            FreeLibrary(module);
        }
        return 0; /* Win32 candidates continue to work. */
    }
    /* Large snapshot lives off the stack; this worker is its sole owner. */
    static Search search;
    ULONGLONG last_start = 0;
    for (;;) {
        WaitForSingleObject(W.wake, INFINITE);
        for (;;) {
            AcquireSRWLockShared(&W.lock);
            bool pending = W.active && W.pending;
            Request request = W.request;
            ReleaseSRWLockShared(&W.lock);
            if (!pending)
                break;
            ULONGLONG now = GetTickCount64(), due = last_start + SNAP_INTERVAL;
            if (now < due) {
                WaitForSingleObject(W.wake, (DWORD)(due - now));
                continue;
            }
            AcquireSRWLockExclusive(&W.lock);
            pending = W.active && W.pending && W.version == request.version;
            if (pending) {
                request = W.request;   /* latest point, not the first in a burst */
                W.pending = false;
            }
            ReleaseSRWLockExclusive(&W.lock);
            if (!pending)
                continue;
            last_start = GetTickCount64();
            memset(&search, 0, sizeof search);
            search.request = &request;
            search.provider = &provider;
            search.start = last_start;
            if (valid_target(&request)) {
                IUIAutomationElement *root = NULL;
                HRESULT root_hr = IUIAutomation2_ElementFromHandle(provider.automation, request.target, &root);
                SNAP_TRACE("root hr=%lx elapsed=%llu\n", (unsigned long)root_hr,
                           (unsigned long long)(GetTickCount64() - search.start));
                if (SUCCEEDED(root_hr) && root) {
                    search_children(&search, root, 0);
                    IUIAutomationElement_Release(root);
                }
            }
            bool valid = valid_target(&request);
            AcquireSRWLockExclusive(&W.lock);
            bool publish = valid && W.active && W.version == request.version;
            if (publish) {
                W.nresult = search.n;
                memcpy(W.result, search.rects, search.n * sizeof(RECT));
                W.result_version = request.version;
                W.ready = true;
            }
            ReleaseSRWLockExclusive(&W.lock);
            if (publish)
                PostMessageW(request.notify, WM_APP_SNAP_READY, 0, 0);
        }
    }
}

void snap_end(void)
{
    animation_stop();
    memset(&A, 0, sizeof A);
    AcquireSRWLockExclusive(&W.lock);
    W.active = W.pending = W.ready = false;
    ++W.version;
    ReleaseSRWLockExclusive(&W.lock);
    memset(&U, 0, sizeof U);
    if (W.wake) SetEvent(W.wake);
}

void snap_begin(void)
{
    snap_end();
    U.overlay = STATE(HWND, OV_HWND);
    A.enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &A.enabled, 0);
    AcquireSRWLockExclusive(&W.lock);
    W.active = true;
    ReleaseSRWLockExclusive(&W.lock);
    if (!W.wake && STATE(int, OV_PHASE) == PH_SELECT && STATE(int, OV_DETECT) == DETECT_ELEMENTS) {
        W.wake = CreateEventW(NULL, FALSE, FALSE, NULL);
        HANDLE thread = W.wake ? CreateThread(NULL, 0, snap_worker, NULL, 0, NULL) : NULL;
        if (thread) CloseHandle(thread);
        else if (W.wake) {
            CloseHandle(W.wake);
            W.wake = NULL;
        }
    }
}

void snap_request(HWND target, const RECT *bounds, POINT screen)
{
    DWORD pid = 0;
    if (target && GetWindowThreadProcessId(target, &pid) && pid == GetCurrentProcessId())
        target = NULL; /* UI Automation should not search windows owned by this process. */
    AcquireSRWLockExclusive(&W.lock);
    Request *r = &W.request;
    bool new_target = r->version != W.version || r->target != target || r->pid != pid ||
                      !same_rect(r->bounds, *bounds);
    bool changed = new_target || r->point.x != screen.x || r->point.y != screen.y;
    if (W.active && changed) {
        if (new_target) {
            ++W.version;
            W.ready = false;
        }
        *r = (Request){.target = target, .notify = U.overlay, .bounds = *bounds, .point = screen,
                       .pid = pid, .version = W.version, .sequence = ++W.sequence};
        W.pending = target && W.wake;
    }
    ReleaseSRWLockExclusive(&W.lock);
    if (changed && W.wake) SetEvent(W.wake);
}

int snap_take(RECT *rects, int capacity)
{
    int n = -1;
    AcquireSRWLockExclusive(&W.lock);
    if (W.active && W.ready && W.result_version == W.version) {
        n = MINI(capacity, W.nresult);
        memcpy(rects, W.result, n * sizeof(RECT));
        W.ready = false;
    }
    ReleaseSRWLockExclusive(&W.lock);
    return n; /* -1: no new result; 0: queried, no components. */
}

static RECT screen_rect(RECT r, int vx, int vy)
{
    r.left += vx; r.right += vx;
    r.top += vy; r.bottom += vy;
    return r;
}

void snap_visual(RECT *out)
{
    RECT target = STATE(RECT, OV_HL);
    double now = now_ms();
    if (!A.initialized || !A.enabled || STATE(bool, OV_LDOWN) || STATE(int, OV_PHASE) != PH_SELECT) {
        animation_stop();
        A.initialized = true;
        A.frame = A.target = target;
    } else if (!same_rect(target, A.target)) {
        /* Retarget from the current presentation, never the previous endpoint.
         * Rapid pointer changes therefore do not produce discontinuities. */
        RECT current = animation_sample(now);
        A.from[0] = current.left; A.from[1] = current.top;
        A.from[2] = current.right; A.from[3] = current.bottom;
        A.target = target;
        A.start = now;
        A.running = true;
        if (!A.timer) {
            A.timer = U.overlay && SetTimer(U.overlay, TIMER_SNAP_ANIMATION, 10, NULL);
            if (!A.timer) {
                A.running = false;
                A.frame = target;
            }
        }
    }
    *out = animation_sample(now);
}

static void cache_merge(const RECT *rects, int n)
{
    for (int i = 0; i < n; ++i) {
        bool duplicate = false;
        for (int j = 0; j < U.ncached; ++j)
            if (same_rect(rects[i], U.cached[j])) { duplicate = true; break; }
        if (duplicate) continue;
        if (U.ncached < SNAP_CACHE) U.cached[U.ncached++] = rects[i];
        else {
            U.cached[U.next_cache] = rects[i];
            U.next_cache = (U.next_cache + 1) % SNAP_CACHE;
        }
    }
}

void snap_remember(void)
{
    Shot *shot = (Shot *)(O + OV_SHOT);
    U.selected = screen_rect(STATE(RECT, OV_HL), shot->vx, shot->vy);
    U.manual = true;
}

void snap_update(void)
{
    if (STATE(int, OV_PHASE) != PH_SELECT || STATE(bool, OV_LDOWN))
        return;
    Shot *shot = (Shot *)(O + OV_SHOT);
    POINT point = STATE(POINT, OV_CUR);
    point.x += shot->vx;
    point.y += shot->vy;
    RECT desktop = {shot->vx, shot->vy, shot->vx + shot->vw, shot->vy + shot->vh};
    RECT monitor = desktop, window = {0}, client = {0};
    int tile = shot_tile_at(shot, point.x - shot->vx, point.y - shot->vy);
    if (tile >= 0 && tile < shot->ntile)
        monitor = screen_rect(shot->tile[tile].rc, shot->vx, shot->vy);
    HWND target = NULL;
    uint8_t *wins = STATE(uint8_t *, OV_WIN);
    for (int i = 0; i < STATE(int, OV_NWIN); ++i) {
        RECT r;
        memcpy(&r, wins + i * WIN_BYTES + WIN_RC, sizeof r);
        if (has_point(r, point)) {
            memcpy(&target, wins + i * WIN_BYTES + WIN_H, sizeof target);
            window = r;
            break;
        }
    }
    bool changed_target = target != U.target || !same_rect(window, U.bounds);
    if (changed_target) {
        U.ncached = 0;
        U.next_cache = 0;
    }
    if (changed_target || (U.have_leaf && !has_point(U.leaf, point))) {
        U.manual = U.have_leaf = false;
    }
    U.target = target;
    U.bounds = window;
    snap_request(target, &window, point);
    int received = snap_take(U.incoming, SNAP_CACHE);
    if (received >= 0) cache_merge(U.incoming, received);

    RECT input[SNAP_INPUTS];
    int n = 0;
    RECT clip = target ? intersect(desktop, window) : desktop;
    for (int i = 0; i < U.ncached; ++i)
        add_rect(input, &n, SNAP_INPUTS, U.cached[i], clip, point);
    HWND child = target;
    for (int depth = 0; child && depth < 12; ++depth) {
        POINT local = point;
        if (!ScreenToClient(child, &local)) break;
        HWND next = ChildWindowFromPointEx(child, local, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);
        if (!next || next == child) break;
        RECT r;
        if (!GetWindowRect(next, &r)) break;
        add_rect(input, &n, SNAP_INPUTS, r, clip, point);
        child = next;
    }
    if (target && GetClientRect(target, &client)) {
        POINT origin = {0};
        if (ClientToScreen(target, &origin))
            client = screen_rect(client, origin.x, origin.y);
        else client = (RECT){0};
    }
    RECT chain[MAX_CHAIN];
    int count = merge_rects(chain, input, n, clip, point, client, window, monitor, MAX_CHAIN);
    int level = 0;
    if (count) {
        bool same_leaf = U.have_leaf && same_rect(chain[0], U.leaf);
        /* Refinement at the same hovered component must not steal a user's
         * explicit wheel choice. Leaving its leaf cleared manual above. */
        if (!same_leaf && received < 0) U.manual = false;
        if (U.manual)
            for (int i = 0; i < count; ++i)
                if (same_rect(chain[i], U.selected)) { level = i; break; }
        U.leaf = chain[0];
        U.have_leaf = true;
    }
    for (int i = 0; i < count; ++i)
        STATE(RECT, OV_CHAIN + i * OV_CHAIN_BYTES) = screen_rect(chain[i], -shot->vx, -shot->vy);
    STATE(int, OV_NCHAIN) = count;
    STATE(int, OV_LEVEL) = level;
    STATE(RECT, OV_HL) = count ? STATE(RECT, OV_CHAIN + level * OV_CHAIN_BYTES) :
                               (RECT){0, 0, shot->vw, shot->vh};
}
