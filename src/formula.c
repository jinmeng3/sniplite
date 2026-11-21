#pragma GCC optimize("O2")
#include "formula.h"
#include "ort.h"
#include <emmintrin.h>

#define TOKENS 687
#define STEPS 1024
#define SIDE 384

typedef struct { OrtSession *session; } Net;
static struct {
    HMODULE dll;
    Ort o;
    OrtEnv *env;
    bool ready, bad;
    OrtMemoryInfo *mem;
    OrtAllocator *alloc;
    Net enc, dec;
    WCHAR *vocab[TOKENS];
    WCHAR err[200];
} F;

const WCHAR *formula_error(void) { return F.err; }
static void error(const WCHAR *s) { lstrcpynW(F.err, s, COUNTOF(F.err)); }
static bool ort_ok(OrtStatus *s)
{
    if (!s) return true;
    const char *m = F.o.GetErrorMessage(s);
    int i = 0;
    for (; m && m[i] && i < COUNTOF(F.err)-1; i++) F.err[i] = (WCHAR)(unsigned char)m[i];
    F.err[i] = 0; F.o.ReleaseStatus(s); return false;
}
static bool find_path(const WCHAR *name, WCHAR *path, int cap)
{
    DWORD n = GetModuleFileNameW(NULL, path, cap-16);
    if (!n || n >= (DWORD)(cap-16)) return false;
    while (n && path[n-1] != L'\\' && path[n-1] != L'/') n--;
    lstrcpyW(path+n, L"formula\\");
    if (lstrlenW(path)+lstrlenW(name)+1 >= cap) return false;
    lstrcatW(path, name); return true;
}

static char *json_token(char **cursor)
{
    char *p = *cursor;
    if (*p++ != '"') return NULL;
    char *start = p;
    size_t size = 0;
    while (*p && *p != '"') {
        if (*p == '\\') {
            p++;
            if (*p != '"' && *p != '\\' && *p != '/') return NULL;
        }
        p++;
        size++;
    }
    if (*p != '"') return NULL;
    char *token = mem_alloc(size + 1);
    if (!token) return NULL;
    size_t n = 0;
    while (start < p) {
        if (*start == '\\') start++;
        token[n++] = *start++;
    }
    token[n] = 0;
    *cursor = p + 1;
    return token;
}

/* Tokenizer JSON contains a simple WordLevel vocabulary with UTF-8 string tokens. */
static bool tokenizer_load(void)
{
    WCHAR path[MAX_PATH*2]; if (!find_path(L"tokenizer.json",path,COUNTOF(path))) return false;
    HANDLE f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL); if(f==INVALID_HANDLE_VALUE)return false;
    DWORD size=GetFileSize(f,NULL), got=0; char *b=mem_alloc((size_t)size+1);
    bool ok=b && ReadFile(f,b,size,&got,NULL) && got==size; CloseHandle(f); if(!ok){mem_free(b);return false;} b[size]=0;
    static const char marker[] = "\"vocab\"";
    char *p=b;
    while(*p && memcmp(p,marker,sizeof marker-1))p++;
    if(!*p){mem_free(b);return false;}
    while(*p && *p!='{')p++;
    if(!*p){mem_free(b);return false;}
    p++;
    bool valid = true;
    while(*p && *p!='}') {
        while(*p==' '||*p=='\n'||*p=='\r'||*p=='\t'||*p==',')p++;
        if(*p=='}')break;
        char *token=json_token(&p);
        if(!token){valid=false;break;}
        while(*p==' '||*p=='\n'||*p=='\r'||*p=='\t')p++;
        if(*p++!=':'){mem_free(token);valid=false;break;}
        while(*p==' '||*p=='\n'||*p=='\r'||*p=='\t')p++;
        if(*p<'0'||*p>'9'){mem_free(token);valid=false;break;}
        unsigned id=0;
        while(*p>='0'&&*p<='9') { if(id>=TOKENS){valid=false;break;} id=id*10u+(unsigned)(*p++-'0'); }
        if(!valid||id>=TOKENS||F.vocab[id]){mem_free(token);valid=false;break;}
        int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, token, -1, NULL, 0);
        WCHAR *wide = n > 0 ? mem_alloc((size_t)n * sizeof(WCHAR)) : NULL;
        if (!wide || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, token, -1, wide, n)) {
            mem_free(wide); mem_free(token); valid = false; break;
        }
        mem_free(token);
        F.vocab[id] = wide;
    }
    valid = valid && *p=='}' && F.vocab[0] && F.vocab[2];
    mem_free(b); return valid;
}

static bool load(void)
{
    if(F.ready)return true;
    WCHAR path[MAX_PATH*2];
    if(!find_path(L"onnxruntime.dll",path,COUNTOF(path))){error(L"formula runtime is not installed");return false;}
    F.dll=LoadLibraryExW(path,NULL,LOAD_WITH_ALTERED_SEARCH_PATH);
    if(!F.dll){error(L"formula onnxruntime.dll could not be loaded");return false;}
    const OrtApiBase *(*base)(void)=(const OrtApiBase *(*)(void))(void*)GetProcAddress(F.dll,"OrtGetApiBase");
    const void *api=base?base()->GetApi(ORT_API_WANTED):NULL;
    if(!api){error(L"formula onnxruntime.dll is too old");return false;} ort_bind(&F.o,api);
    if(!ort_ok(F.o.CreateEnv(ORT_LOG_WARNING+1,"sniplite-formula",&F.env)) ||
       !ort_ok(F.o.GetAllocatorWithDefaultOptions(&F.alloc)) ||
       !ort_ok(F.o.CreateCpuMemoryInfo(ORT_ARENA_ALLOCATOR,ORT_MEM_DEFAULT,&F.mem)))return false;
    OrtSessionOptions *opts=NULL;
    if(!ort_ok(F.o.CreateSessionOptions(&opts)))return false;
    DWORD processors = GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
    int threads = processors > 4 ? 4 : (processors ? (int)processors : 1);
    F.o.SetIntraOpNumThreads(opts,threads); F.o.SetSessionGraphOptimizationLevel(opts,ORT_OPTIMIZE_ALL);
    bool good=find_path(L"onnx\\encoder_model.onnx",path,COUNTOF(path)) && ort_ok(F.o.CreateSession(F.env,path,opts,&F.enc.session));
    good=good&&find_path(L"onnx\\decoder_model_merged.onnx",path,COUNTOF(path))&&ort_ok(F.o.CreateSession(F.env,path,opts,&F.dec.session));
    F.o.ReleaseSessionOptions(opts);
    if(!good || !tokenizer_load()){error(L"formula model files could not be loaded");return false;}
    F.ready=true; return true;
}

static bool tensor(const void *data,size_t bytes,const int64_t *shape,size_t rank,int type,OrtValue **v)
{ return ort_ok(F.o.CreateTensorWithDataAsOrtValue(F.mem,(void*)data,bytes,shape,rank,type,v)); }
static bool dims(OrtValue *v,int64_t *d,size_t want)
{
    OrtShape *s=NULL; size_t n=0; bool ok=ort_ok(F.o.GetTensorTypeAndShape(v,&s))&&ort_ok(F.o.GetDimensionsCount(s,&n))&&n==want&&ort_ok(F.o.GetDimensions(s,d,n));
    if (s) F.o.ReleaseShape(s);
    return ok;
}
static void free_values(OrtValue **v,int n){for(int i=0;i<n;i++)if(v[i])F.o.ReleaseValue(v[i]);}

/* Convert BGRA to the model's centered, normalized 384x384 grayscale tensor. */
static bool image_tensor(const uint32_t *src,int w,int h,float *out)
{
    if(!src||w<=0||h<=0||(uint64_t)w*h>16000000)return false;
    size_t count=(size_t)w*h; uint8_t *g=mem_alloc(count);if(!g)return false;
    uint64_t hist[256]={0};uint8_t mn=255,mx=0;
    for(size_t i=0;i<count;i++){uint32_t p=src[i];int v=((p>>16&255)*77+(p>>8&255)*150+(p&255)*29+128)>>8;g[i]=(uint8_t)v;hist[v]++;if(v<mn)mn=(uint8_t)v;if(v>mx)mx=(uint8_t)v;}
    if (mn == mx) { mem_free(g); F.bad = false; return false; }
    uint64_t dark=0,light=0;for(int i=0;i<200;i++)dark+=hist[i];for(int i=200;i<256;i++)light+=hist[i];bool invert=dark>=light;
    int x0=w,y0=h,x1=-1,y1=-1;
    for(int y=0;y<h;y++)for(int x=0;x<w;x++){int v=g[(size_t)y*w+x];int contrast=(invert?mx-v:v-mn)*255/(mx-mn);if(contrast<200){if(x<x0)x0=x;if(x>x1)x1=x;if(y<y0)y0=y;if(y>y1)y1=y;}}
    if (x1 < x0 || y1 < y0) { mem_free(g); F.bad = false; return false; }
    int cw=x1-x0+1,ch=y1-y0+1;
    double scale=(double)SIDE/(cw>ch?cw:ch);
    int rw=MAXI(1,(int)(cw*scale+0.5)),rh=MAXI(1,(int)(ch*scale+0.5));
    memset(out,0,3u*SIDE*SIDE*sizeof(float));int ox=(SIDE-rw)/2,oy=(SIDE-rh)/2;
    for(int y=0;y<rh;y++){
        double fy=(y+0.5)*ch/rh-0.5;if(fy<0)fy=0;if(fy>ch-1)fy=ch-1;
        int iy=(int)fy,iy1=MINI(iy+1,ch-1);double wy=fy-iy;
        for(int x=0;x<rw;x++){
            double fx=(x+0.5)*cw/rw-0.5;if(fx<0)fx=0;if(fx>cw-1)fx=cw-1;
            int ix=(int)fx,ix1=MINI(ix+1,cw-1);double wx=fx-ix;
            const uint8_t *a=g+(size_t)(y0+iy)*w+x0,*b=g+(size_t)(y0+iy1)*w+x0;
            int gray=(int)((a[ix]*(1-wx)+a[ix1]*wx)*(1-wy)+(b[ix]*(1-wx)+b[ix1]*wx)*wy+0.5);
            if(invert)gray=255-gray;
            float v=((float)gray/255.0f-0.7931f)/0.1738f;
            size_t at=(size_t)(oy+y)*SIDE+ox+x;out[at]=out[SIDE*SIDE+at]=out[2*SIDE*SIDE+at]=v;
        }
    }
    mem_free(g);return true;
}

static bool run(const float *pixels,Str *latex)
{
    if(!load())return false;
    F.bad=true;
    int64_t ishape[4]={1,3,SIDE,SIDE};OrtValue *in=NULL,*encout[1]={0};
    bool ok=tensor(pixels,3u*SIDE*SIDE*sizeof(float),ishape,4,ORT_FLOAT,&in);
    const char *ein[]={"pixel_values"},*eout[]={"last_hidden_state"};
    ok=ok&&ort_ok(F.o.Run(F.enc.session,NULL,ein,(const OrtValue*const*)&in,1,eout,1,encout));
    if (in) F.o.ReleaseValue(in);
    if (!ok) { F.bad=true; free_values(encout,1); return false; }
    int64_t ed[4]={0};if(!dims(encout[0],ed,3)||ed[0]!=1||ed[1]!=144||ed[2]!=2048){free_values(encout,1);error(L"formula encoder returned an unexpected shape");return false;}
    OrtValue *cache[8]={0};float empty=0;int64_t cd[4]={1,16,0,24};
    for(int i=0;i<8;i++)if(!tensor(&empty,0,cd,4,ORT_FLOAT,&cache[i])){free_values(cache,8);free_values(encout,1);return false;}
    latex->n=0;if(latex->p)latex->p[0]=0;
    const char *cache_in[]={"past_key_values.0.decoder.key","past_key_values.0.decoder.value","past_key_values.0.encoder.key","past_key_values.0.encoder.value","past_key_values.1.decoder.key","past_key_values.1.decoder.value","past_key_values.1.encoder.key","past_key_values.1.encoder.value"};
    int64_t token=0,ts[2]={1,1},bs[1]={1};uint8_t branch=0;OrtValue *ids=NULL,*use=NULL;
    bool done=false;
    if (!tensor(&token,sizeof token,ts,2,ORT_INT64,&ids) || !tensor(&branch,sizeof branch,bs,1,ORT_BOOL,&use)) {
        if (ids) F.o.ReleaseValue(ids);
        if (use) F.o.ReleaseValue(use);
        free_values(cache,8);free_values(encout,1);return false;
    }
    for(int step=0;step<STEPS-1;step++){
        const char *names[11];const OrtValue *vals[11];names[0]="input_ids";vals[0]=ids;names[1]="encoder_hidden_states";vals[1]=encout[0];
        for(int i=0;i<8;i++){names[i+2]=cache_in[i];vals[i+2]=cache[i];}names[10]="use_cache_branch";vals[10]=use;
        OrtValue *outs[9]={0};const char *on[9]={"logits","present.0.decoder.key","present.0.decoder.value","present.0.encoder.key","present.0.encoder.value","present.1.decoder.key","present.1.decoder.value","present.1.encoder.key","present.1.encoder.value"};
        if(!ort_ok(F.o.Run(F.dec.session,NULL,names,vals,11,on,9,outs))){F.bad=true;free_values(outs,9);break;}
        float *logits=NULL;bool good=ort_ok(F.o.GetTensorMutableData(outs[0],(void**)&logits));int64_t ld[3]={0};good=good&&dims(outs[0],ld,3)&&ld[0]==1&&ld[1]==1&&ld[2]==TOKENS;
        if(!good){F.bad=true;free_values(outs,9);error(L"formula decoder returned unexpected logits");break;}
        int best=0;for(int i=1;i<TOKENS;i++)if(logits[i]>logits[best])best=i;
        for(int i=0;i<8;i++){if(branch&&(i&3)>=2)continue;F.o.ReleaseValue(cache[i]);cache[i]=outs[i+1];outs[i+1]=NULL;}
        free_values(outs,9);branch=1;
        if(best==2){done=true;F.bad=false;break;}
        if(best!=0&&best!=1){
            const WCHAR *t=F.vocab[best];if(!t){error(L"formula tokenizer is missing a token");break;}
            int n=lstrlenW(t),before=latex->n,space=before?1:0;
            if(space)str_addn(latex,L" ",1);
            str_addn(latex,t,n);
            if(latex->n!=before+space+n){error(L"out of memory");break;}
        }
        token=best;
    }
    if (ids) F.o.ReleaseValue(ids);
    if (use) F.o.ReleaseValue(use);
    free_values(cache,8); free_values(encout,1);
    if(!done){latex->n=0;if(latex->p)latex->p[0]=0;return false;}
    return latex->n>0;
}

bool formula_run(const uint32_t *bgra,int w,int h,Str *latex)
{
    if (!latex) return false;
    F.bad=true;
    latex->n=0;if(latex->p)latex->p[0]=0;
    float *pixels=mem_alloc(3u*SIDE*SIDE*sizeof(float));
    if (!pixels) { error(L"out of memory"); return false; }
    bool ok=image_tensor(bgra,w,h,pixels)&&run(pixels,latex);
    mem_free(pixels);
    return ok;
}

static bool read_exact(HANDLE h,void *p,uint32_t n)
{uint8_t *b=p;while(n){DWORD got=0;if(!ReadFile(h,b,n,&got,NULL)||!got)return false;b+=got;n-=got;}return true;}
static bool write_all(HANDLE h,const void *p,uint32_t n)
{const uint8_t *b=p;while(n){DWORD wrote=0;if(!WriteFile(h,b,n,&wrote,NULL)||!wrote)return false;b+=wrote;n-=wrote;}return true;}
static bool parse_dimension(const WCHAR *s,int *value)
{
    int n=0;if(!*s)return false;
    while(*s){if(*s<L'0'||*s>L'9')return false;int d=*s++-L'0';if(n>(16000000-d)/10)return false;n=n*10+d;}
    if (!n) return false;
    *value=n; return true;
}
int formula_helper(int argc,WCHAR **argv)
{
    if(argc<4||!arg_eq(argv[1],L"--formula-raw"))return 1;
    int w,h;if(!parse_dimension(argv[2],&w)||!parse_dimension(argv[3],&h)||(uint64_t)w*h>16000000)return 1;
    size_t bytes=(size_t)w*h*4;uint32_t *px=mem_alloc(bytes);if(!px)return 1;
    HANDLE in=GetStdHandle(STD_INPUT_HANDLE);if(!read_exact(in,px,(uint32_t)bytes)){mem_free(px);return 4;}
    Str out={0};bool ok=formula_run(px,(int)w,(int)h,&out);mem_free(px);if(!ok){str_free(&out);return F.bad?1:4;}
    int n=WideCharToMultiByte(CP_UTF8,0,out.p,out.n,NULL,0,NULL,NULL);char *u=n>0?mem_alloc(n):NULL;bool wrote=u&&WideCharToMultiByte(CP_UTF8,0,out.p,out.n,u,n,NULL,NULL)==n&&write_all(GetStdHandle(STD_OUTPUT_HANDLE),u,(uint32_t)n);
    mem_free(u);str_free(&out);return wrote?0:1;
}
