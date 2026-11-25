// OPUS-MT Marian inference for SnipLite.
// Architecture and GGUF naming follow CrispASR's m2m100.cpp and
// convert-marian-to-gguf.py (MIT). Marian tokenizer behavior follows
// CrispASR's core/marian_tokenizer.h (MIT); this implementation is kept
// local so the engine has no CrispASR build-time dependency.
#include "opusmt_translate.h"
#include "../shared/core/gguf_loader.h"
#include "ggml-cpu.h"
#include "ggml-backend.h"
#include "ggml.h"
#include "gguf.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
struct Layer {
    ggml_tensor * q=nullptr,*qb=nullptr,*k=nullptr,*kb=nullptr,*v=nullptr,*vb=nullptr,*o=nullptr,*ob=nullptr;
    ggml_tensor * ln=nullptr,*lnb=nullptr,*cq=nullptr,*cqb=nullptr,*ck=nullptr,*ckb=nullptr,*cv=nullptr,*cvb=nullptr,*co=nullptr,*cob=nullptr,*cln=nullptr,*clnb=nullptr;
    ggml_tensor * up=nullptr,*upb=nullptr,*down=nullptr,*downb=nullptr,*fln=nullptr,*flnb=nullptr;
};
struct Norm { std::vector<uint32_t> trie; std::string pool; bool dummy=true,collapse=true,escape=true;
    static int utf8len(unsigned char c) { return c<0x80?1:(c<0xE0?2:(c<0xF0?3:4)); }
    size_t match(const std::string& s,size_t p,uint32_t& value) const {
        if(trie.empty())return 0;
        auto off=[](uint32_t x){return (x>>10)<<((x&(1u<<9))>>6);};
        size_t node=0,best=0; uint32_t u=trie[0]; node^=off(u);
        for(size_t i=p;i<s.size();++i){uint32_t c=(unsigned char)s[i];node^=c;if(node>=trie.size())break;u=trie[node];if((u&((1u<<31)|255u))!=c)break;node^=off(u);if((u>>8)&1u){if(node>=trie.size())break;value=trie[node]&0x7fffffffu;best=i-p+1;}}
        return best;
    }
    size_t step(const std::string&s,size_t p,std::string& out) const {
        uint32_t v=0;size_t n=match(s,p,v);if(n){if(v<pool.size())out.assign(pool.c_str()+v);else out.clear();return n;}
        unsigned char c=(unsigned char)s[p];int l=utf8len(c);if(p+l>s.size())l=1;
        for(int i=1;i<l;i++)if(((unsigned char)s[p+i]&0xc0)!=0x80){l=1;break;}
        out.assign(s,p,l);return l;
    }
    std::string apply(const std::string& in) const {
        const std::string marker="\xe2\x96\x81"; size_t p=0; std::string part,out;
        if(collapse)while(p<in.size()){size_t n=step(in,p,part);if(part!=" ")break;p+=n;}
        if(p==in.size())return out;if(dummy)out+=escape?marker:" "; bool prev=collapse;
        while(p<in.size()){p+=step(in,p,part);size_t k=0;while(prev&&k<part.size()&&part[k]==' ')++k;
            for(;k<part.size();++k){if(escape&&part[k]==' ')out+=marker;else out+=part[k];}prev=collapse&&!part.empty()&&part.back()==' ';}
        if(collapse){const std::string sp=escape?marker:" ";while(out.size()>=sp.size()&&out.compare(out.size()-sp.size(),sp.size(),sp)==0)out.resize(out.size()-sp.size());}return out;
    }
};
struct Tokenizer {
    std::unordered_map<std::string,int> vocab,sp; std::vector<std::string> idtok;std::vector<float> scores;std::vector<uint8_t> user;int unk=0,eos=0,pad=0;Norm norm;int maxlen=0;float minscore=0,maxscore=0;
    std::vector<int> encode(const std::string& text, const std::string& target_lang) const {
        std::string s = norm.apply(text);
        const int n = (int)s.size();
        std::vector<double> best((size_t)n + 1, -std::numeric_limits<double>::infinity());
        std::vector<int> prev((size_t)n + 1, -1);
        best[0] = 0;
        std::string candidate_piece;
        candidate_piece.reserve((size_t)std::max(0, maxlen));
        for (int i = 0; i < n; i++) {
            if (!std::isfinite(best[i])) continue;
            int lim = std::min(n, i + maxlen);
            bool single = false;
            for (int j = i + 1; j <= lim; j++) {
                candidate_piece.assign(s.data() + i, (size_t)(j - i));
                auto it = sp.find(candidate_piece);
                if (it == sp.end()) continue;
                int id = it->second;
                int typ = (size_t)id < user.size() && user[id] ? 1 : 0;
                double score = typ ? double(j - i) * maxscore - .1 : scores[id];
                double candidate = best[i] + score;
                if (candidate > best[j]) { best[j] = candidate; prev[j] = i; }
                if (j - i == Norm::utf8len((unsigned char)s[i])) single = true;
            }
            if (!single) {
                int j = std::min(n, i + Norm::utf8len((unsigned char)s[i]));
                double candidate = best[i] + minscore - 10;
                if (candidate > best[j]) { best[j] = candidate; prev[j] = i; }
            }
        }
        struct EncodedPiece { std::string surface; int id; bool unknown; };
        std::vector<EncodedPiece> reverse;
        if (n > 0 && prev[n] >= 0) {
            for (int end = n; end > 0;) {
                int begin = prev[end];
                if (begin < 0 || begin >= end) break;
                std::string surface = s.substr(begin, end - begin);
                auto it = vocab.find(surface);
                if (it != vocab.end()) reverse.push_back({{}, it->second, false});
                else if (!reverse.empty() && reverse.back().unknown) reverse.back().surface = surface + reverse.back().surface;
                else reverse.push_back({std::move(surface), unk, true});
                end = begin;
            }
        }
        std::reverse(reverse.begin(), reverse.end());
        std::vector<int> tokens;
        tokens.reserve(reverse.size() + 2);
        bool has_language_prefix = text.rfind(">>", 0) == 0 && text.find("<<", 2) != std::string::npos;
        if (!has_language_prefix && !target_lang.empty()) {
            auto lang = vocab.find(">>" + target_lang + "<<");
            if (lang != vocab.end()) tokens.push_back(lang->second);
        }
        for (const auto& piece : reverse) {
            if (!piece.unknown) tokens.push_back(piece.id);
            else {
                auto it = vocab.find(piece.surface);
                tokens.push_back(it == vocab.end() ? unk : it->second);
            }
        }
        tokens.push_back(eos);
        return tokens;
    }
    std::string decode(const std::vector<int>& ids) const {
        std::string out;for(int id:ids){if(id<0||id>=(int)idtok.size()||id==eos||id==pad||id==unk)continue;out+=idtok[id];}
        const std::string mark="\xe2\x96\x81";size_t p=0;while((p=out.find(mark,p))!=std::string::npos){out.replace(p,3," ");++p;}while(!out.empty()&&out.front()==' ')out.erase(out.begin());while(!out.empty()&&out.back()==' ')out.pop_back();return out;
    }
};
struct Context {
    int D=0,V=0,ne=0,nd=0,he=0,hdc=0,fe=0,fd=0,maxpos=0,genmax=0,beams=1,activation=0,start=0,eos=0,pad=0,unk=0,threads=1;
    bool scale=true,pre_norm=false;std::string src,tgt;std::vector<int> suppress;ggml_backend_t backend=nullptr,cpu_backend=nullptr;ggml_backend_sched_t sched=nullptr;ggml_context* wctx=nullptr;ggml_backend_buffer_t wbuf=nullptr;core_gguf::tensor_map tensors;Tokenizer tok;
    ggml_tensor * embed=nullptr,*epos=nullptr,*dpos=nullptr,*bias=nullptr;std::vector<Layer> enc,dec;
    ~Context(){if(sched)ggml_backend_sched_free(sched);core_gguf::release_weight_buffer(wbuf);if(wctx)ggml_free(wctx);if(backend)ggml_backend_free(backend);if(cpu_backend)ggml_backend_free(cpu_backend);}
};
static int key(gguf_context*g,const char*n){return gguf_find_key(g,n);}
static bool req(gguf_context*g,const char*n,int&v){int i=key(g,n);if(i<0||gguf_get_kv_type(g,i)!=GGUF_TYPE_UINT32)return false;v=(int)gguf_get_val_u32(g,i);return true;}
static std::string str(gguf_context*g,const char*n,const char*d=""){int i=key(g,n);return i>=0&&gguf_get_kv_type(g,i)==GGUF_TYPE_STRING?gguf_get_val_str(g,i):d;}
static bool load_tok(Context&c,gguf_context*g){
    int i=key(g,"tokenizer.ggml.tokens");if(i<0||gguf_get_kv_type(g,i)!=GGUF_TYPE_ARRAY||gguf_get_arr_type(g,i)!=GGUF_TYPE_STRING||(int)gguf_get_arr_n(g,i)!=c.V)return false;
    c.tok.idtok.resize(c.V);c.tok.vocab.reserve(c.V*2);for(int n=0;n<c.V;n++){auto s=gguf_get_arr_str(g,i,n);c.tok.idtok[n]=s;c.tok.vocab.emplace(s,n);}
    c.tok.eos=c.eos;c.tok.pad=c.pad;c.tok.unk=c.unk;
    const char*base="tokenizer.marian.source.";auto arr=[&](const char*leaf){return key(g,(std::string(base)+leaf).c_str());};
    int pi=arr("pieces"),si=arr("scores"),ti=arr("types"),ni=arr("normalizer");if(pi<0||si<0||ti<0||ni<0||gguf_get_arr_type(g,pi)!=GGUF_TYPE_STRING||gguf_get_arr_type(g,si)!=GGUF_TYPE_FLOAT32||gguf_get_arr_type(g,ti)!=GGUF_TYPE_INT32)return false;
    size_t n=gguf_get_arr_n(g,pi);if(!n||gguf_get_arr_n(g,si)!=n||gguf_get_arr_n(g,ti)!=n)return false;auto*sc=(const float*)gguf_get_arr_data(g,si);auto*ty=(const int32_t*)gguf_get_arr_data(g,ti);c.tok.scores.assign(sc,sc+n);c.tok.user.assign(n,0);c.tok.maxlen=0;bool have=false;
    for(size_t j=0;j<n;j++){std::string p=gguf_get_arr_str(g,pi,j);if((ty[j]!=1&&ty[j]!=4)||p.empty())continue;c.tok.sp.emplace(p,(int)j);c.tok.user[j]=ty[j]==4;c.tok.maxlen=std::max(c.tok.maxlen,(int)p.size());if(ty[j]==1){if(!have)c.tok.minscore=c.tok.maxscore=sc[j];c.tok.minscore=std::min(c.tok.minscore,sc[j]);c.tok.maxscore=std::max(c.tok.maxscore,sc[j]);have=true;}}
    if(!have)return false;std::string nn=gguf_get_val_str(g,ni);int ci=arr("charsmap");if(ci>=0){if(gguf_get_arr_type(g,ci)!=GGUF_TYPE_UINT8)return false;auto*b=(const uint8_t*)gguf_get_arr_data(g,ci);size_t z=gguf_get_arr_n(g,ci);if(z<4)return false;uint32_t tb;memcpy(&tb,b,4);if(tb%4||tb>z-4)return false;c.tok.norm.trie.resize(tb/4);memcpy(c.tok.norm.trie.data(),b+4,tb);c.tok.norm.pool.assign((const char*)b+4+tb,z-4-tb);}else if(nn!="identity")return false;
    const char*flags[]={"add_dummy_prefix","remove_extra_whitespaces","escape_whitespaces"};bool*dst[]={&c.tok.norm.dummy,&c.tok.norm.collapse,&c.tok.norm.escape};for(int j=0;j<3;j++){int x=arr(flags[j]);if(x<0||gguf_get_kv_type(g,x)!=GGUF_TYPE_BOOL)return false;*dst[j]=gguf_get_val_bool(g,x);}return true;
}
static ggml_tensor* get(Context&c,const std::string&n){return core_gguf::try_get(c.tensors,n.c_str());}
static bool bind_layer(Context&c,Layer&l,int i,bool decoder){std::string p=(decoder?"dec":"enc");p+=".blk."+std::to_string(i)+".";auto t=[&](const char*n){return get(c,p+n);};
#define B(field,name) l.field=t(name); if(!l.field)return false
    B(q,"attn_q.weight");B(qb,"attn_q.bias");B(k,"attn_k.weight");B(kb,"attn_k.bias");B(v,"attn_v.weight");B(vb,"attn_v.bias");B(o,"attn_o.weight");B(ob,"attn_o.bias");B(ln,"attn_ln.weight");B(lnb,"attn_ln.bias");B(up,"ffn_up.weight");B(upb,"ffn_up.bias");B(down,"ffn_down.weight");B(downb,"ffn_down.bias");B(fln,"ffn_ln.weight");B(flnb,"ffn_ln.bias");
    if(decoder){B(cq,"cross_q.weight");B(cqb,"cross_q.bias");B(ck,"cross_k.weight");B(ckb,"cross_k.bias");B(cv,"cross_v.weight");B(cvb,"cross_v.bias");B(co,"cross_o.weight");B(cob,"cross_o.bias");B(cln,"cross_ln.weight");B(clnb,"cross_ln.bias");}
#undef B
    return true;
}
static ggml_tensor* norm(ggml_context*x,ggml_tensor*y,ggml_tensor*w,ggml_tensor*b){return ggml_add(x,ggml_mul(x,ggml_norm(x,y,1e-5f),w),b);}
static ggml_tensor* act(ggml_context*x,ggml_tensor*y,int a){return a==1?ggml_silu(x,y):(a==2?ggml_gelu_erf(x,y):ggml_relu(x,y));}
static ggml_cgraph* encoder(Context&c,int T,ggml_context*&x){x=ggml_init({8*1024*1024,nullptr,true});if(!x)return nullptr;auto*g=ggml_new_graph_custom(x,8192,false);int hd=c.D/c.he;
    auto*ids=ggml_new_tensor_1d(x,GGML_TYPE_I32,T);ggml_set_name(ids,"ids");ggml_set_input(ids);auto*pos=ggml_new_tensor_1d(x,GGML_TYPE_I32,T);ggml_set_name(pos,"pos");ggml_set_input(pos);
    auto*y=ggml_get_rows(x,c.embed,ids);if(c.scale)y=ggml_scale(x,y,sqrtf((float)c.D));y=ggml_add(x,y,ggml_get_rows(x,c.epos,pos));
    for(auto&l:c.enc){auto*r=y;if(c.pre_norm)y=norm(x,y,l.ln,l.lnb);auto*q=ggml_add(x,ggml_mul_mat(x,l.q,y),l.qb);auto*k=ggml_add(x,ggml_mul_mat(x,l.k,y),l.kb);auto*v=ggml_add(x,ggml_mul_mat(x,l.v,y),l.vb);q=ggml_cont(x,ggml_permute(x,ggml_reshape_3d(x,q,hd,c.he,T),0,2,1,3));k=ggml_cont(x,ggml_permute(x,ggml_reshape_3d(x,k,hd,c.he,T),0,2,1,3));v=ggml_cont(x,ggml_permute(x,ggml_reshape_3d(x,v,hd,c.he,T),0,2,1,3));auto*a=ggml_flash_attn_ext(x,q,k,v,nullptr,1.f/sqrtf((float)hd),0,0);a=ggml_reshape_2d(x,a,c.D,T);y=ggml_add(x,ggml_add(x,ggml_mul_mat(x,l.o,a),l.ob),r);if(!c.pre_norm)y=norm(x,y,l.ln,l.lnb);r=y;if(c.pre_norm)y=norm(x,y,l.fln,l.flnb);y=ggml_add(x,ggml_mul_mat(x,l.up,y),l.upb);y=act(x,y,c.activation);y=ggml_add(x,ggml_mul_mat(x,l.down,y),l.downb);y=ggml_add(x,y,r);if(!c.pre_norm)y=norm(x,y,l.fln,l.flnb);}
    ggml_set_name(y,"encout");ggml_build_forward_expand(g,y);return g;
}
static ggml_cgraph* decoder(Context&c,const std::vector<int>&ids,const std::vector<float>&enc,ggml_context*&x){int T=(int)ids.size(),S=(int)enc.size()/c.D,hd=c.D/c.hdc;x=ggml_init({16*1024*1024,nullptr,true});if(!x)return nullptr;auto*g=ggml_new_graph_custom(x,16384,false);auto*in=ggml_new_tensor_1d(x,GGML_TYPE_I32,T);ggml_set_name(in,"ids");ggml_set_input(in);auto*pos=ggml_new_tensor_1d(x,GGML_TYPE_I32,T);ggml_set_name(pos,"pos");ggml_set_input(pos);auto*ei=ggml_new_tensor_2d(x,GGML_TYPE_F32,c.D,S);ggml_set_name(ei,"enc");ggml_set_input(ei);
    auto*y=ggml_get_rows(x,c.embed,in);if(c.scale)y=ggml_scale(x,y,sqrtf((float)c.D));y=ggml_add(x,y,ggml_get_rows(x,c.dpos,pos));auto*mask=ggml_new_tensor_2d(x,GGML_TYPE_F16,T,T);ggml_set_name(mask,"mask");ggml_set_input(mask);
    for(auto&l:c.dec){auto*r=y;if(c.pre_norm)y=norm(x,y,l.ln,l.lnb);auto*q=ggml_add(x,ggml_mul_mat(x,l.q,y),l.qb);auto*k=ggml_add(x,ggml_mul_mat(x,l.k,y),l.kb);auto*v=ggml_add(x,ggml_mul_mat(x,l.v,y),l.vb);q=ggml_cont(x,ggml_permute(x,ggml_reshape_3d(x,q,hd,c.hdc,T),0,2,1,3));k=ggml_cont(x,ggml_permute(x,ggml_reshape_3d(x,k,hd,c.hdc,T),0,2,1,3));v=ggml_cont(x,ggml_permute(x,ggml_reshape_3d(x,v,hd,c.hdc,T),0,2,1,3));auto*a=ggml_flash_attn_ext(x,q,k,v,mask,1.f/sqrtf((float)hd),0,0);a=ggml_reshape_2d(x,a,c.D,T);y=ggml_add(x,ggml_add(x,ggml_mul_mat(x,l.o,a),l.ob),r);if(!c.pre_norm)y=norm(x,y,l.ln,l.lnb);r=y;
        if(c.pre_norm)y=norm(x,y,l.cln,l.clnb);auto*cq=ggml_add(x,ggml_mul_mat(x,l.cq,y),l.cqb);auto*ck=ggml_add(x,ggml_mul_mat(x,l.ck,ei),l.ckb);auto*cv=ggml_add(x,ggml_mul_mat(x,l.cv,ei),l.cvb);cq=ggml_cont(x,ggml_permute(x,ggml_reshape_3d(x,cq,hd,c.hdc,T),0,2,1,3));ck=ggml_cont(x,ggml_permute(x,ggml_reshape_3d(x,ck,hd,c.hdc,S),0,2,1,3));cv=ggml_cont(x,ggml_permute(x,ggml_reshape_3d(x,cv,hd,c.hdc,S),0,2,1,3));auto*ca=ggml_flash_attn_ext(x,cq,ck,cv,nullptr,1.f/sqrtf((float)hd),0,0);ca=ggml_reshape_2d(x,ca,c.D,T);y=ggml_add(x,ggml_add(x,ggml_mul_mat(x,l.co,ca),l.cob),r);if(!c.pre_norm)y=norm(x,y,l.cln,l.clnb);r=y;if(c.pre_norm)y=norm(x,y,l.fln,l.flnb);y=ggml_add(x,ggml_mul_mat(x,l.up,y),l.upb);y=act(x,y,c.activation);y=ggml_add(x,ggml_mul_mat(x,l.down,y),l.downb);y=ggml_add(x,y,r);if(!c.pre_norm)y=norm(x,y,l.fln,l.flnb);}
    auto*last=ggml_view_2d(x,y,c.D,1,y->nb[1],(size_t)(T-1)*y->nb[1]);auto*logits=ggml_add(x,ggml_mul_mat(x,c.embed,last),c.bias);ggml_set_name(logits,"logits");ggml_build_forward_expand(g,logits);return g;
}
struct DecoderGraph {
    ggml_context* context = nullptr;
    ggml_cgraph* graph = nullptr;
    int token_count = 0;
    int source_count = 0;

    int scratch_token_count = -1;
    std::vector<int32_t> positions;
    std::vector<ggml_fp16_t> mask;
    ~DecoderGraph() { clear(); }
    void clear() {
        if (context) ggml_free(context);
        context = nullptr;
        graph = nullptr;
        token_count = 0;
        source_count = 0;
    }
    bool prepare(Context& c, const std::vector<int>& ids, const std::vector<float>& enc) {
        const int tokens = (int)ids.size();
        const int source = (int)enc.size() / c.D;
        // Reused Vulkan beam graphs changed translations; rebuild on non-CPU backends.
        if (graph && token_count == tokens && source_count == source && (!c.cpu_backend || c.backend == c.cpu_backend)) return true;
        if (graph) ggml_backend_sched_reset(c.sched);
        clear();
        graph = decoder(c, ids, enc, context);
        if (!graph) { clear(); return false; }
        token_count = tokens;
        source_count = source;
        return true;
    }
};
static bool decode_logits(Context& c, const std::vector<int>& ids, const std::vector<float>& enc,
                          std::vector<float>& logits, DecoderGraph& cached) {
    if (!cached.prepare(c, ids, enc)) return false;
    ggml_cgraph* graph = cached.graph;
    ggml_backend_sched_reset(c.sched);
    if (!ggml_backend_sched_alloc_graph(c.sched, graph)) return false;
    if (cached.scratch_token_count != (int)ids.size()) {
        cached.positions.resize(ids.size());
        for (size_t i = 0; i < cached.positions.size(); i++) cached.positions[i] = (int32_t)i;
        cached.mask.assign(ids.size() * ids.size(), ggml_fp32_to_fp16(0.0f));
        for (size_t q = 0; q < ids.size(); q++) {
            for (size_t k = q + 1; k < ids.size(); k++) cached.mask[q * ids.size() + k] = ggml_fp32_to_fp16(-INFINITY);
        }
        cached.scratch_token_count = (int)ids.size();
    }
    ggml_backend_tensor_set(ggml_graph_get_tensor(graph, "ids"), ids.data(), 0, ids.size() * sizeof(int));
    ggml_backend_tensor_set(ggml_graph_get_tensor(graph, "pos"), cached.positions.data(), 0, cached.positions.size() * sizeof(int32_t));
    ggml_backend_tensor_set(ggml_graph_get_tensor(graph, "enc"), enc.data(), 0, enc.size() * sizeof(float));
    ggml_backend_tensor_set(ggml_graph_get_tensor(graph, "mask"), cached.mask.data(), 0, cached.mask.size() * sizeof(ggml_fp16_t));
    if (ggml_backend_sched_graph_compute(c.sched, graph) != GGML_STATUS_SUCCESS) return false;
    ggml_backend_tensor_get(ggml_graph_get_tensor(graph, "logits"), logits.data(), 0, logits.size() * sizeof(float));
    for (int id : c.suppress) logits[id] = -INFINITY;
    return true;
}
struct Beam { std::vector<int> ids; double score = 0; bool done = false; };
struct BeamCandidate { size_t parent; int token; double score; double rank; };
#ifdef SNIPLITE_TRANSLATION_VULKAN
static ggml_backend_t init_vulkan_backend(const char *runtime_path) {
    bool registered = false;
    for (size_t i = 0; i < ggml_backend_reg_count(); ++i) {
        ggml_backend_reg_t reg = ggml_backend_reg_get(i);
        const char *name = ggml_backend_reg_name(reg);
        if (name && std::strcmp(name, "Vulkan") == 0) { registered = true; break; }
    }
    if (!registered && runtime_path && *runtime_path) ggml_backend_load_all_from_path(runtime_path);
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        ggml_backend_dev_t dev = ggml_backend_dev_get(i);
        ggml_backend_reg_t reg = dev ? ggml_backend_dev_backend_reg(dev) : nullptr;
        const char *name = reg ? ggml_backend_reg_name(reg) : nullptr;
        if (name && std::strcmp(name, "Vulkan") == 0) {
            ggml_backend_t backend = ggml_backend_dev_init(dev, nullptr);
            if (backend) return backend;
        }
    }
    return nullptr;
}
#endif
static bool initialize(Context&c,const char*path,const char*runtime_path){
    gguf_context*g=core_gguf::open_metadata(path);if(!g)return false;bool ok=str(g,"general.architecture")=="marian";
    ok=ok&&req(g,"marian.vocab_size",c.V)&&req(g,"marian.d_model",c.D)&&req(g,"marian.encoder.n_layers",c.ne)&&req(g,"marian.encoder.n_heads",c.he)&&req(g,"marian.encoder.ffn_dim",c.fe)&&req(g,"marian.decoder.n_layers",c.nd)&&req(g,"marian.decoder.n_heads",c.hdc)&&req(g,"marian.decoder.ffn_dim",c.fd)&&req(g,"marian.max_position_embeddings",c.maxpos)&&req(g,"marian.eos_token_id",c.eos)&&req(g,"marian.pad_token_id",c.pad)&&req(g,"marian.unk_token_id",c.unk)&&req(g,"marian.decoder_start_token_id",c.start)&&req(g,"marian.gen.max_length",c.genmax)&&req(g,"marian.gen.num_beams",c.beams);
    int z=0;ok=ok&&req(g,"marian.normalize_before",z);c.pre_norm=z!=0;ok=ok&&req(g,"marian.scale_embedding",z);c.scale=z!=0;c.src=str(g,"marian.source_lang");c.tgt=str(g,"marian.target_lang");std::string a=str(g,"marian.activation_function");if(a=="swish"||a=="silu")c.activation=1;else if(a=="gelu")c.activation=2;else if(a=="relu")c.activation=0;else ok=false;
    int si=key(g,"marian.suppress_token_ids");if(si>=0){if(gguf_get_kv_type(g,si)!=GGUF_TYPE_ARRAY||gguf_get_arr_type(g,si)!=GGUF_TYPE_INT32)ok=false;else{auto*p=(const int32_t*)gguf_get_arr_data(g,si);for(size_t j=0;j<gguf_get_arr_n(g,si);j++)if(p[j]>=0&&p[j]<c.V)c.suppress.push_back(p[j]);}}c.suppress.push_back(c.pad);
    if(!ok||c.V<=0||c.D<=0||c.he<=0||c.he!=c.hdc||c.D%c.he||c.ne<=0||c.nd<=0||c.maxpos<=1||c.genmax<=1||c.beams<=0||c.beams>16||!load_tok(c,g)){core_gguf::free_metadata(g);return false;}core_gguf::free_metadata(g);
#ifdef SNIPLITE_TRANSLATION_VULKAN
    c.backend=init_vulkan_backend(runtime_path);if(!c.backend)return false;c.cpu_backend=ggml_backend_cpu_init();if(!c.cpu_backend)return false;ggml_backend_cpu_set_n_threads(c.cpu_backend,c.threads);ggml_backend_t backends[]={c.backend,c.cpu_backend};c.sched=ggml_backend_sched_new(backends,nullptr,2,GGML_DEFAULT_GRAPH_SIZE,false,false);
#else
    c.backend=ggml_backend_cpu_init();if(!c.backend)return false;ggml_backend_cpu_set_n_threads(c.backend,c.threads);ggml_backend_t backends[]={c.backend};c.sched=ggml_backend_sched_new(backends,nullptr,1,GGML_DEFAULT_GRAPH_SIZE,false,false);
#endif
    core_gguf::WeightLoad wl;if(!core_gguf::load_weights(path,c.backend,"opusmt",wl))return false;c.wctx=wl.ctx;c.wbuf=wl.buf;c.tensors=std::move(wl.tensors);wl.ctx=nullptr;wl.buf=nullptr;
    c.embed=core_gguf::require(c.tensors,"shared.embed.weight","opusmt");c.epos=core_gguf::require(c.tensors,"enc.pos_emb","opusmt");c.dpos=core_gguf::require(c.tensors,"dec.pos_emb","opusmt");c.bias=core_gguf::require(c.tensors,"final_logits_bias","opusmt");if(!c.embed||!c.epos||!c.dpos||!c.bias)return false;
    for(int i=0;i<c.ne;i++){Layer l;if(!bind_layer(c,l,i,false))return false;c.enc.push_back(l);}for(int i=0;i<c.nd;i++){Layer l;if(!bind_layer(c,l,i,true))return false;c.dec.push_back(l);}
    return (int)c.embed->ne[1]==c.V&&(int)c.embed->ne[0]==c.D&&(int)c.bias->ne[0]==c.V&&(int)c.epos->ne[0]==c.D&&(int)c.dpos->ne[0]==c.D&&(int)c.epos->ne[1]>=c.maxpos&&(int)c.dpos->ne[1]>=c.maxpos;
}
}
extern "C" void * sniplite_opusmt_init(const char*path,const char*runtime_path,int threads){try{if(!path||!*path)return nullptr;auto c=std::unique_ptr<Context>(new Context);c->threads=std::max(1,threads);if(!initialize(*c,path,runtime_path))return nullptr;return c.release();}catch(...){return nullptr;}}
static constexpr int kMaxInputChunkBytes = 128;
struct InputChunk { std::string text, separator; };
static bool input_space(unsigned char ch) { return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v'; }
static bool input_closer(unsigned char ch) { return ch == ')' || ch == ']' || ch == '}' || ch == '\'' || ch == '"'; }
static std::vector<InputChunk> split_input(const std::string& input, int maxpos) {
    const size_t limit_bytes = (size_t)std::max(1, std::min(kMaxInputChunkBytes, maxpos - 3));
    const size_t sentence_limit_bytes = (size_t)std::max(1, std::min(512, maxpos - 3));
    std::vector<InputChunk> chunks;
    size_t begin = 0;
    while (begin < input.size()) {
        while (begin < input.size() && input_space((unsigned char)input[begin])) ++begin;
        if (begin == input.size()) break;
        size_t cut = 0;
        const size_t soft_limit = std::min(input.size(), begin + limit_bytes);
        const size_t hard_limit = std::min(input.size(), begin + sentence_limit_bytes);
        if (input.size() - begin <= limit_bytes) cut = input.size();
        else {
            for (size_t p = begin; p < hard_limit; ++p) {
                const unsigned char ch = (unsigned char)input[p];
                if (ch != '.' && ch != '!' && ch != '?') continue;
                if (ch == '.' && p > begin && p + 1 < input.size() && input[p - 1] >= '0' && input[p - 1] <= '9' && input[p + 1] >= '0' && input[p + 1] <= '9') continue;
                size_t end = p + 1;
                while (end < hard_limit && input_closer((unsigned char)input[end])) ++end;
                if (end == input.size() || input_space((unsigned char)input[end])) {
                    if (end <= soft_limit || !cut) cut = end;
                }
            }
            if (!cut) for (size_t p = begin; p < soft_limit; ++p) if (input_space((unsigned char)input[p])) cut = p;
            if (!cut) {
                cut = soft_limit;
                while (cut > begin && cut < input.size() && ((unsigned char)input[cut] & 0xc0) == 0x80) --cut;
                if (cut == begin) {
                    cut = begin + 1;
                    while (cut < input.size() && ((unsigned char)input[cut] & 0xc0) == 0x80) ++cut;
                }
            }
        }
        if (cut <= begin) return {};
        size_t next = cut;
        while (next < input.size() && input_space((unsigned char)input[next])) ++next;
        chunks.push_back({input.substr(begin, cut - begin), input.substr(cut, next - cut)});
        begin = next;
    }
    return chunks;
}
static bool translate_one(Context& c, const std::string& input, std::string& result) {
    auto ids = c.tok.encode(input, c.tgt);
    if (ids.empty() || (int)ids.size() > c.maxpos) return false;
    ggml_context *x = nullptr;
    auto *g = encoder(c, (int)ids.size(), x);
    if (!g) return false;
    ggml_backend_sched_reset(c.sched);
    if (!ggml_backend_sched_alloc_graph(c.sched, g)) { ggml_free(x); return false; }
    std::vector<int32_t> pos(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) pos[i] = (int32_t)i;
    ggml_backend_tensor_set(ggml_graph_get_tensor(g, "ids"), ids.data(), 0, ids.size() * sizeof(int));
    ggml_backend_tensor_set(ggml_graph_get_tensor(g, "pos"), pos.data(), 0, pos.size() * sizeof(int32_t));
    if (ggml_backend_sched_graph_compute(c.sched, g) != GGML_STATUS_SUCCESS) { ggml_free(x); return false; }
    std::vector<float> enc(ids.size() * c.D);
    ggml_backend_tensor_get(ggml_graph_get_tensor(g, "encout"), enc.data(), 0, enc.size() * sizeof(float));
    ggml_free(x);
    std::vector<Beam> beams(1);
    beams[0].ids.push_back(c.start);
    const int maxlen = std::min(c.maxpos - 1, c.genmax - 1);
    std::vector<float> logits(c.V);
    auto better = [](const BeamCandidate& a, const BeamCandidate& b) { return a.rank != b.rank ? a.rank > b.rank : a.score > b.score; };
    const size_t parent_keep = (size_t)c.beams;
    std::vector<BeamCandidate> candidates;
    candidates.reserve(parent_keep * parent_keep);
    std::vector<BeamCandidate> parent_top;
    parent_top.reserve(parent_keep);
    DecoderGraph cached_decoder;
    for (int step = 0; step < maxlen; ++step) {
        candidates.clear();
        for (size_t bi = 0; bi < beams.size(); ++bi) {
            const Beam& beam = beams[bi];
            if (beam.done) {
                double length = (double)std::max<size_t>(1, beam.ids.size() - 1);
                candidates.push_back({bi, -1, beam.score, beam.score / length});
                continue;
            }
            if (!decode_logits(c, beam.ids, enc, logits, cached_decoder)) return false;
            float top = -INFINITY;
            for (float value : logits) if (value > top) top = value;
            if (!std::isfinite(top)) return false;
            double sum = 0;
            for (float value : logits) if (std::isfinite(value)) sum += std::exp((double)value - top);
            double lognorm = (double)top + std::log(sum);
            // Tokens from one parent share a length penalty; anything below its local top beam width cannot enter the global top beam width.
            parent_top.clear();
            for (int token = 0; token < c.V; ++token) if (std::isfinite(logits[token])) {
                double score = beam.score + (double)logits[token] - lognorm;
                double length = (double)beam.ids.size();
                BeamCandidate candidate{bi, token, score, score / length};
                if (parent_top.size() < parent_keep) {
                    parent_top.push_back(candidate);
                    std::push_heap(parent_top.begin(), parent_top.end(), better);
                } else if (better(candidate, parent_top.front())) {
                    std::pop_heap(parent_top.begin(), parent_top.end(), better);
                    parent_top.back() = candidate;
                    std::push_heap(parent_top.begin(), parent_top.end(), better);
                }
            }
            candidates.insert(candidates.end(), parent_top.begin(), parent_top.end());
        }
        if (candidates.empty()) return false;
        size_t keep = std::min((size_t)c.beams, candidates.size());
        std::partial_sort(candidates.begin(), candidates.begin() + keep, candidates.end(), better);
        std::vector<Beam> next;
        next.reserve(keep);
        for (size_t i = 0; i < keep; ++i) {
            const auto& candidate = candidates[i];
            Beam beam = beams[candidate.parent];
            if (candidate.token >= 0) { beam.ids.push_back(candidate.token); beam.score = candidate.score; beam.done = candidate.token == c.eos; }
            next.push_back(std::move(beam));
        }
        beams = std::move(next);
        if (std::all_of(beams.begin(), beams.end(), [](const Beam& beam) { return beam.done; })) break;
    }
    auto best = std::max_element(beams.begin(), beams.end(), [](const Beam& a, const Beam& b) {
        double al = a.score / (double)std::max<size_t>(1, a.ids.size() - 1);
        double bl = b.score / (double)std::max<size_t>(1, b.ids.size() - 1);
        return al < bl;
    });
    result = c.tok.decode(best->ids);
    return true;
}
extern "C" int sniplite_opusmt_translate(void*opaque,const char*input,char*out,int cap) {
    if (!opaque || !input || cap < 0) return 0;
    try {
        auto& c = *static_cast<Context*>(opaque);
        std::string source(input), result;
        const size_t chunk_limit = (size_t)std::max(1, std::min(kMaxInputChunkBytes, c.maxpos - 3));
        if (source.size() <= chunk_limit) {
            if (!translate_one(c, source, result)) return 0;
        } else {
            auto chunks = split_input(source, c.maxpos);
            if (chunks.empty()) return 0;
            for (size_t i = 0; i < chunks.size(); ++i) {
                std::string translated;
                if (!translate_one(c, chunks[i].text, translated)) return 0;
                result += translated;
                if (i + 1 < chunks.size() && chunks[i].separator.find('\n') != std::string::npos) result += chunks[i].separator;
            }
        }
        int n = (int)result.size();
        if (out && cap > 0) {
            int copy = std::min(n, cap - 1);
            while (copy > 0 && copy < n && (((unsigned char)result[copy] & 0xc0) == 0x80)) --copy;
            memcpy(out, result.data(), copy);
            out[copy] = 0;
        }
        return n;
    } catch (...) { if (out && cap > 0) out[0] = 0; return 0; }
}
extern "C" void sniplite_opusmt_free(void*ctx){try{delete static_cast<Context*>(ctx);}catch(...){}}
