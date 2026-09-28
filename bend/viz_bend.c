
// Imports
// =======

// The Objective-C headers take #include, not #import: a build
// (-o) reads an #import as the framework of an effect.

#pragma clang fp contract(off)

#if defined(__CUDACC_RTC__)
#define BEND_RTC 1
#endif

#ifdef __METAL_VERSION__
#include <metal_stdlib>
using namespace metal;
#elif !defined(BEND_RTC)
#ifdef __APPLE__
#define _DARWIN_UNLIMITED_SELECT
#else
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <stdbool.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define POLLIN  1
#define POLLOUT 4
#else
#include <unistd.h>
#include <signal.h>
#include <sys/mman.h>
#include <poll.h>
#include <sys/select.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif
#ifdef __OBJC__
#include <Metal/Metal.h>
#include <Foundation/Foundation.h>
#elif BEND_CUDA
#ifndef _WIN32
#include <dlfcn.h>
#endif

// The driver and NVRTC are loaded when first needed, not linked, so a binary
// starts on a machine without them and runs its bangs on the CPU, and building
// one needs no CUDA toolkit: the host declares the few calls it makes (as
// cuda.h 13 and nvrtc.h do), and each entry of the tables below pairs a name
// with the symbol the libraries export for it.

#ifdef _WIN32
#define CUDAAPI __stdcall
#else
#define CUDAAPI
#endif
#define CUDA_VERSION 13000
#define CUDA_SUCCESS 0
#define NVRTC_SUCCESS 0
#define CU_MEM_ATTACH_GLOBAL 1
#define CU_MEM_ADVISE_SET_PREFERRED_LOCATION 3
#define CU_MEM_LOCATION_TYPE_DEVICE 1

typedef int                 CUresult;
typedef int                 CUdevice;
typedef unsigned long long  CUdeviceptr;
typedef struct CUctx_st*    CUcontext;
typedef struct CUmod_st*    CUmodule;
typedef struct CUfunc_st*   CUfunction;
typedef struct CUstream_st* CUstream;
typedef int                 CUmem_advise;
typedef struct { int type; int id; } CUmemLocation;
typedef enum {
  CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT = 16,
  CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE = 38,
  CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR = 75,
  CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR = 76,
  CU_DEVICE_ATTRIBUTE_MANAGED_MEMORY = 83,
  CU_DEVICE_ATTRIBUTE_CONCURRENT_MANAGED_ACCESS = 89
} CUdevice_attribute;
typedef int                   nvrtcResult;
typedef struct _nvrtcProgram* nvrtcProgram;

CUresult CUDAAPI cuInit(unsigned int flags);
CUresult CUDAAPI cuDeviceGet(CUdevice* dev, int ordinal);
CUresult CUDAAPI cuDeviceGetAttribute(int* v, CUdevice_attribute a, CUdevice dev);
CUresult CUDAAPI cuDevicePrimaryCtxRetain(CUcontext* ctx, CUdevice dev);
CUresult CUDAAPI cuDevicePrimaryCtxSetFlags(CUdevice dev, unsigned int flags);
CUresult CUDAAPI cuCtxSetCurrent(CUcontext ctx);
CUresult CUDAAPI cuCtxSynchronize(void);
CUresult CUDAAPI cuMemAllocManaged(CUdeviceptr* p, size_t bytes, unsigned int flags);
CUresult CUDAAPI cuMemAdvise(CUdeviceptr p, size_t bytes, CUmem_advise advice,
  CUmemLocation at);
CUresult CUDAAPI cuMemsetD8(CUdeviceptr p, unsigned char v, size_t n);
CUresult CUDAAPI cuDeviceTotalMem(size_t* bytes, CUdevice dev);
CUresult CUDAAPI cuModuleLoadData(CUmodule* m, const void* image);
CUresult CUDAAPI cuModuleGetFunction(CUfunction* f, CUmodule m, const char* name);
CUresult CUDAAPI cuLaunchKernel(CUfunction f, unsigned int gx, unsigned int gy,
  unsigned int gz, unsigned int bx, unsigned int by, unsigned int bz,
  unsigned int shared, CUstream stream, void** params, void** extra);
CUresult CUDAAPI cuMemAlloc(CUdeviceptr* p, size_t bytes);
CUresult CUDAAPI cuMemcpyDtoH(void* dst, CUdeviceptr src, size_t bytes);
CUresult CUDAAPI cuMemcpyHtoD(CUdeviceptr dst, const void* src, size_t bytes);
CUresult CUDAAPI cuMemcpyHtoDAsync(CUdeviceptr dst, const void* src, size_t bytes, CUstream st);
CUresult CUDAAPI cuMemcpyDtoHAsync(void* dst, CUdeviceptr src, size_t bytes, CUstream st);
CUresult CUDAAPI cuMemFree(CUdeviceptr p);
CUresult CUDAAPI cuMemcpyDtoD(CUdeviceptr dst, CUdeviceptr src, size_t bytes);
CUresult CUDAAPI cuMemAllocHost(void** p, size_t bytes);
nvrtcResult nvrtcCreateProgram(nvrtcProgram* p, const char* src, const char* name,
  int nh, const char* const* headers, const char* const* names);
nvrtcResult nvrtcCompileProgram(nvrtcProgram p, int n, const char* const* opts);
nvrtcResult nvrtcGetProgramLogSize(nvrtcProgram p, size_t* n);
nvrtcResult nvrtcGetProgramLog(nvrtcProgram p, char* log);
nvrtcResult nvrtcGetCUBINSize(nvrtcProgram p, size_t* n);
nvrtcResult nvrtcGetCUBIN(nvrtcProgram p, char* bin);
nvrtcResult nvrtcDestroyProgram(nvrtcProgram* p);

#define GPU_CU_FNS(X) \
  X(cuInit, cuInit) X(cuDeviceGet, cuDeviceGet) \
  X(cuDeviceGetAttribute, cuDeviceGetAttribute) \
  X(cuDevicePrimaryCtxRetain, cuDevicePrimaryCtxRetain) \
  X(cuDevicePrimaryCtxSetFlags, cuDevicePrimaryCtxSetFlags_v2) \
  X(cuCtxSetCurrent, cuCtxSetCurrent) X(cuCtxSynchronize, cuCtxSynchronize) \
  X(cuMemAllocManaged, cuMemAllocManaged) X(cuMemAdvise, cuMemAdvise_v2) \
  X(cuMemsetD8, cuMemsetD8_v2) X(cuDeviceTotalMem, cuDeviceTotalMem_v2) \
  X(cuModuleLoadData, cuModuleLoadData) \
  X(cuModuleGetFunction, cuModuleGetFunction) \
  X(cuLaunchKernel, cuLaunchKernel) X(cuMemAlloc, cuMemAlloc_v2) \
  X(cuMemcpyDtoH, cuMemcpyDtoH_v2) X(cuMemcpyHtoD, cuMemcpyHtoD_v2) \
  X(cuMemcpyHtoDAsync, cuMemcpyHtoDAsync_v2) \
  X(cuMemcpyDtoHAsync, cuMemcpyDtoHAsync_v2) \
  X(cuMemFree, cuMemFree_v2) \
  X(cuMemcpyDtoD, cuMemcpyDtoD_v2) X(cuMemAllocHost, cuMemAllocHost_v2)

#define GPU_RTC_FNS(X) \
  X(nvrtcCreateProgram, nvrtcCreateProgram) \
  X(nvrtcCompileProgram, nvrtcCompileProgram) \
  X(nvrtcGetProgramLogSize, nvrtcGetProgramLogSize) \
  X(nvrtcGetProgramLog, nvrtcGetProgramLog) \
  X(nvrtcGetCUBINSize, nvrtcGetCUBINSize) X(nvrtcGetCUBIN, nvrtcGetCUBIN) \
  X(nvrtcDestroyProgram, nvrtcDestroyProgram)

#define GPU_FN_PTR(api, sym) static __typeof__(api)* gpu_fn_##api;
GPU_CU_FNS(GPU_FN_PTR)
GPU_RTC_FNS(GPU_FN_PTR)

#define cuInit                   (*gpu_fn_cuInit)
#define cuDeviceGet              (*gpu_fn_cuDeviceGet)
#define cuDeviceGetAttribute     (*gpu_fn_cuDeviceGetAttribute)
#define cuDevicePrimaryCtxRetain (*gpu_fn_cuDevicePrimaryCtxRetain)
#define cuDevicePrimaryCtxSetFlags (*gpu_fn_cuDevicePrimaryCtxSetFlags)
#define cuCtxSetCurrent          (*gpu_fn_cuCtxSetCurrent)
#define cuCtxSynchronize         (*gpu_fn_cuCtxSynchronize)
#define cuMemAllocManaged        (*gpu_fn_cuMemAllocManaged)
#define cuMemAdvise              (*gpu_fn_cuMemAdvise)
#define cuMemsetD8               (*gpu_fn_cuMemsetD8)
#define cuDeviceTotalMem         (*gpu_fn_cuDeviceTotalMem)
#define cuModuleLoadData         (*gpu_fn_cuModuleLoadData)
#define cuModuleGetFunction      (*gpu_fn_cuModuleGetFunction)
#define cuLaunchKernel           (*gpu_fn_cuLaunchKernel)
#define cuMemAlloc               (*gpu_fn_cuMemAlloc)
#define cuMemcpyDtoH             (*gpu_fn_cuMemcpyDtoH)
#define cuMemcpyHtoD             (*gpu_fn_cuMemcpyHtoD)
#define cuMemcpyHtoDAsync        (*gpu_fn_cuMemcpyHtoDAsync)
#define cuMemcpyDtoHAsync        (*gpu_fn_cuMemcpyDtoHAsync)
#define cuMemFree                (*gpu_fn_cuMemFree)
#define cuMemcpyDtoD             (*gpu_fn_cuMemcpyDtoD)
#define cuMemAllocHost           (*gpu_fn_cuMemAllocHost)
#define nvrtcCreateProgram       (*gpu_fn_nvrtcCreateProgram)
#define nvrtcCompileProgram      (*gpu_fn_nvrtcCompileProgram)
#define nvrtcGetProgramLogSize   (*gpu_fn_nvrtcGetProgramLogSize)
#define nvrtcGetProgramLog       (*gpu_fn_nvrtcGetProgramLog)
#define nvrtcGetCUBINSize        (*gpu_fn_nvrtcGetCUBINSize)
#define nvrtcGetCUBIN            (*gpu_fn_nvrtcGetCUBIN)
#define nvrtcDestroyProgram      (*gpu_fn_nvrtcDestroyProgram)

static void* gpu_sym(void* lib, const char* name) {
#ifdef _WIN32
  return lib == NULL ? NULL : (void*)GetProcAddress((HMODULE)lib, name);
#else
  return lib == NULL ? NULL : dlsym(lib, name);
#endif
}

// The first library of the list that opens, or NULL.
static void* gpu_lib_open(const char* const* names) {
  for (; *names != NULL; names += 1) {
#ifdef _WIN32
    void* lib = (void*)LoadLibraryA(*names);
#else
    void* lib = dlopen(*names, RTLD_NOW | RTLD_LOCAL);
#endif
    if (lib != NULL) {
      return lib;
    }
  }
  return NULL;
}

static bool gpu_open_cu(void) {
#ifdef _WIN32
  static const char* const names[] = { "nvcuda.dll", NULL };
#else
  static const char* const names[] = { "libcuda.so.1", "libcuda.so", NULL };
#endif
  void* lib = gpu_lib_open(names);
  bool  ok  = lib != NULL;
#define GPU_FN_LOAD(api, sym) \
  ok = ok && (gpu_fn_##api = (__typeof__(gpu_fn_##api))gpu_sym(lib, #sym));
  GPU_CU_FNS(GPU_FN_LOAD)
  return ok;
}

static bool gpu_open_rtc(void) {
#ifdef _WIN32
  static const char* const names[] = { "nvrtc64_130_0.dll", "nvrtc64_120_0.dll",
    NULL };
#else
  static const char* const names[] = { "libnvrtc.so", "libnvrtc.so.13",
    "libnvrtc.so.12", NULL };
#endif
  void* lib = gpu_lib_open(names);
  bool  ok  = lib != NULL;
  GPU_RTC_FNS(GPU_FN_LOAD)
  return ok;
}
#endif
#endif

// Dialect
// =======

// Metal needs coherent(device) (MSL 3.2), or M1-class parts lose stores
// across the threadgroups of a dispatch. CUDA keeps plain data cacheable
// in L1: lanes hand off through a32 and FENCE. Only clang 19+ has both
// preserve_none and preserve_most, and compiles preserve_most soundly. A
// segment is a case of the device's switch; on the host, a preserve_none
// function (WL_SIG) entered by musttail, its words fresh at WL_OPEN. The Env
// crosses as its two pointers: Windows x64 passes a 16-byte struct through a
// pointer to a copy, which a musttail call leaves dangling.

#ifdef __METAL_VERSION__
#if __METAL_VERSION__ >= 320
#define DEV     coherent(device) device
#else
#define DEV     device
#endif
#define THR     thread
#define TG      threadgroup
#define INLINE  inline
#define OUTLINE static
#define CONSTV  constant
#define DEVICE  1
#define CLZ(x)  clz(x)
#define FENCE() atomic_thread_fence(mem_flags::mem_device, memory_order_seq_cst)
#define BAR()   threadgroup_barrier(mem_flags::mem_threadgroup)
#define BARD()  threadgroup_barrier(mem_flags::mem_device \
  | mem_flags::mem_threadgroup)
#else
#define DEV
#define THR
#define TG
#define INLINE  static inline
#define CONSTV  static const
#ifdef BEND_RTC
#define OUTLINE static __attribute__((noinline))
#define DEVICE  1
#define CLZ(x)  (u32)__clz((int)(x))
#define FENCE() __threadfence()
#define BAR()   __syncthreads()
#define BARD()  \
  { __threadfence(); __syncthreads(); }
#else
#if __has_attribute(preserve_none) && __has_attribute(preserve_most)
#define PRESERVE(A) __attribute__((A))
#else
#define PRESERVE(A)
#endif
#define OUTLINE static __attribute__((noinline, cold)) PRESERVE(preserve_most)
#define DEVICE  0
#define CLZ(x)  (u32)__builtin_clz(x)
#define FENCE() ((void)0)
#endif
#endif
#undef FAR  // windows.h has its own
#define FAR static __attribute__((noinline))

#if DEVICE
#define LOCK(l)
#define UNLOCK(l)
#define WL_CASE(F) case F:
#define WL_OPEN    {
#define WL_JMP(F)  { fid = (F); break; }
#define WL_DYN     WL_JMP
#else
#define LOCK(l)    while (__atomic_exchange_n(&(l), 1, __ATOMIC_ACQUIRE)) {}
#define UNLOCK(l)  __atomic_store_n(&(l), 0, __ATOMIC_RELEASE)
#define WL_FN      static PRESERVE(preserve_none) __attribute__((noinline)) Term
#define WL_CASE(F) WL_FN WL_##F(WL_SIG)
#define WL_OPEN    { Env e = { wl_mem, wl_alc }; WL_BANK u32 rn;
#define WL_JMP(F)  __attribute__((musttail)) return WL_##F(WL_ALL)
#define WL_DYN(F)  __attribute__((musttail)) return wl_tab[F](WL_ALL)
#endif
#define WL_SPIN     for (;;) { if (err_spun(e.mem, &wpoll)) { return 0; }
#define WL_SPUN     } break;
#define WL_AGAIN(F) continue

#define LANE_STEP (DEVICE ? (long)CUBE : 1)
#define STK(I)    sp[(long)(I) * LANE_STEP]

#define WL_RETN(N)  { rn = (N); sp -= LANE_STEP; WL_DYN((u32)STK(0)); }
#define WL_CONT     STK(-3)
#define WL_IDX      STK(-2)
#define WL_POPN(N)  sp -= N * LANE_STEP
#define WL_PUSHN(N) sp += N * LANE_STEP
#define WL_FRAME(T) \
  u64 wtl = task_tail(T); \
  u64 wtw = e.mem[wtl + 1]; \
  STK(0) = e.mem[wtl]; \
  STK(1) = (wtw >> 32) & 0xFFFF; \
  STK(2) = FID_EXIT; \
  sp += 3 * LANE_STEP;
#define WL_ARGS(A, N) \
  for (u32 wi = 0; wi + 1 < N; wi += 1) { \
    STK(wi) = e.mem[A + wi]; \
  } \
  sp += (N - 1) * LANE_STEP;
#define WL_ROOM(N) \
  if (DEVICE && sp + (N) * CUBE >= e.mem + STAT_OFF + CUBE) { \
    err_post(e.mem, ERR_DEEP); \
    return 0; \
  }

// Types
// =====

#ifdef __METAL_VERSION__
typedef ulong u64;
typedef uint  u32;
typedef uchar u8;
#elif defined(BEND_RTC)
typedef unsigned long long u64;
typedef unsigned int       u32;
typedef unsigned char      u8;
#else
typedef uint64_t u64;
typedef uint32_t u32;
typedef uint8_t  u8;
#endif
typedef float f32;

typedef u64 Term;

typedef struct {
  DEV u64* mem;
  DEV u64* alc;
} Env;

typedef struct {
  u64 off;
  u32 rd;
  u32 wr;
  u32 top;
} Bank;

#if DEVICE
typedef u32 u32a;
#else
typedef u32 __attribute__((may_alias)) u32a;
#endif

// Constants
// =========

#define TAG_PAK 1ull
#define TAG_CTR 2ull
#define TAG_CLO 3ull
#define TAG_BUF 4ull
#define TAG_TSK 5ull
#define TAG_ARR 6ull
#define TAG_SPR 7ull

#define TERM_HOLE (~0ull)
#define LOC_MASK  ((1ull << 40) - 1)
#define RFC_BIT   (1ull << 63)
#define RFC_CNT   ((1u << 24) - 1)
#define NAT_IMM   ((1ull << 48) - 1)

#define ERR_RING 1
#define ERR_TAGS 2
#define ERR_HEAP 3
#define ERR_FIDS 4
#define ERR_NATS 5
#define ERR_RFCS 6
#define ERR_DEEP 7
#define ERR_ARRS 8

#define LINE      16
#define PAGE_BITS 7
#define PAGE_LEN  (1ull << PAGE_BITS)
#define CUBE_T    256
#define CUBE      ((u64)CUBE_T << 7)
#define CUBE_G    (1u << CUBE_LOG)
#define LANES     ((u64)CUBE_T << CUBE_LOG)
#define RING_LOG  (16 - CUBE_LOG)
#define RING_LEN  (1ull << RING_LOG)
#define STAK_LEN  (1ull << 10)
#define NCLS      8
#define NCLS_ALL  32
#define IO_HELP   64

#define TG_HOLD   2304
#define CHUNK     256
#define CAP_WORDS 32768
#define QUANTUM   (DEVICE ? PAGE_LEN \
  : KEEP_WORDS < 32 * PAGE_LEN ? KEEP_WORDS : 32 * PAGE_LEN)
#if DEVICE
#define KEEP_WORDS CHUNK
#endif
#define RING_WORDS ((1ull << 9) + 2)

#define H_BUMP       0
#define H_CAP        1
#define H_CURSOR     LINE
#define H_ROOT_DONE  (2 * LINE)
#define H_ERROR_CODE (3 * LINE)
#define H_ROOT_WORD  (4 * LINE)
#define H_BANK       (H_ROOT_WORD + WL_RESW)

#define PAGE_UP(n) (((n) + PAGE_LEN - 1) & ~(PAGE_LEN - 1))
#define ALC_OFF  PAGE_UP(H_BANK + 3 * NCLS_ALL)
#define RING_OFF (ALC_OFF + CUBE * 3 * NCLS_ALL)
#define STAK_OFF (RING_OFF + CUBE * RING_WORDS)
#define STAT_OFF (STAK_OFF + CUBE * STAK_LEN)
#define HEAP_OFF (STAT_OFF + PAGE_UP(STAT_LEN))

// Globals
// =======

// The bag is 2^CUBE_LOG groups of CUBE_T lanes (a -D constant on the
// device). The device program compiles from the binary's own text.

#if !DEVICE

static u64*    CORPUS;
static u64    ALC[CUBE_T + 1][3 * NCLS_ALL] __attribute__((aligned(128)));
static u32    KEEP_WORDS;
static u32    CUBE_LOG = 7;
static u32    bank_lock;

static u32             pool_size;
static u32             pool_row;
static bool            pool_grow;
static u32             pool_tick;
static u32             pool_done;
static pthread_mutex_t pool_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  pool_wake = PTHREAD_COND_INITIALIZER;

#if BEND_METAL || BEND_CUDA
#pragma clang diagnostic ignored "-Wc23-extensions"
static const char BEND_SRC[] = {
#embed __FILE__
, 0 };
#endif

#ifdef __OBJC__
static id<MTLDevice>               gpu_dev;
static id<MTLCommandQueue>         gpu_que;
static id<MTLComputePipelineState> gpu_pso;
static id<MTLBuffer>               gpu_buf;
static id<MTLComputeCommandEncoder> gpu_enc;
#elif BEND_CUDA
static CUdevice   gpu_dev;
static CUmodule   gpu_lib;
static CUfunction gpu_pso;
#endif
static bool io_gpu;
static DEV Term*  io_stk;

static const char* CLI_HELP =
  "usage: %s [options] [arguments]\n"
  "  --threads N       worker threads, 1 to 128 (default: the CPU count)\n"
  "  --gpu on|off|4GB  run ! calls on the GPU, over this much of its memory\n"
  "                    (default: on if present, over 2GB on Metal)\n"
  "  --gpu-build       write the GPU program and exit\n"
  "  --bend-help       show this text\n"
  "  --                the rest are the program's arguments (IO.args)\n";

#endif

// Tables
// ======

#define CID_TUPLE 0
#define CID_SNIL 1
#define CID_SCON 2
#define CID_WCON 3
#define CID_EMIT 4
#define CID_HALT 5
#define CID_FAIL 6
#define CID_DONE 7
#define CID_NONE 8
#define CID_SOME 9
#define CID_FALSE 10
#define CID_TRUE 11
#define CID_UNIT 12
#define CID_CHR 13
#define CID_K 14
#define CID_VIZ_NEXT 15
#define CID_VIZ_PARAM 16
#define CID_VIZ_SHOW 17
#define FID_ARRAY_SPREAD_RUN_0 0
#define FID_VIZ_FILL 1
#define FID_VIZ_FILL_K48 2
#define FID_VIZ_FILL_K49 3
#define FID_VIZ_FILL_K50 4
#define FID_VIZ_FILL_K51 5
#define FID_VIZ_FILL_J48 6
#define FID_VIZ_FRAME_GPU 7
#define FID_VIZ_FRAME_GPU_K58 8
#define FID_VIZ_FRAME_GPU_J58 9
#define FID_VIZ_DRAW 10
#define FID_VIZ_STEP 11
#define FID_VIZ_STEP_K62 12
#define FID_IO_BIND 13
#define FID_IO_BIND_C64 14
#define FID_IO_BIND_K65 15
#define FID_VIZ_BLANK 16
#define FID_VIZ_LOOP 17
#define FID_VIZ_LOOP_C68 18
#define FID_VIZ_LOOP_C69 19
#define FID_VIZ_LOOP_C70 20
#define FID_VIZ_LOOP_C71 21
#define FID_VIZ_LOOP_C72 22
#define FID_VIZ_LOOP_C73 23
#define FID_VIZ_LOOP_C74 24
#define FID_VIZ_LOOP_C75 25
#define FID_VIZ_LOOP_C76 26
#define FID_VIZ_LOOP_C77 27
#define FID_VIZ_LOOP_C78 28
#define FID_VIZ_LOOP_C79 29
#define FID_VIZ_LOOP_K80 30
#define FID_VIZ_LOOP_C81 31
#define FID_VIZ_LOOP_C82 32
#define FID_MAIN 33
#define FID_MAIN_K84 34
#define FID_MAIN_K85 35
#define FID_VIZ_NEXT 36
#define FID_VIZ_PARAM 37
#define FID_VIZ_SHOW 38
#define FID_IO_EMIT 39
#define FID_CLO_APPLY 40
#define FID_EXIT 41
#define FID_ENTER 42
CONSTV u8 FID_T[][3] = { { 11, 0, 6 }, { 11, 0, 0 }, { 16, 1, 0 }, { 15, 1, 0 }, { 14, 1, 0 }, { 4, 1, 2 }, { 4, 0, 2 }, { 8, 0, 1 }, { 1, 1, 2 }, { 1, 0, 2 }, { 9, 0, 0 }, { 7, 0, 0 }, { 1, 1, 2 }, { 3, 0, 0 }, { 3, 0, 0 }, { 2, 1, 0 }, { 1, 0, 3 }, { 2, 0, 0 }, { 3, 0, 0 }, { 3, 0, 0 }, { 4, 0, 0 }, { 4, 0, 0 }, { 5, 0, 0 }, { 5, 0, 0 }, { 6, 0, 0 }, { 6, 0, 0 }, { 7, 0, 0 }, { 7, 0, 0 }, { 8, 0, 0 }, { 8, 0, 0 }, { 2, 1, 0 }, { 3, 0, 0 }, { 2, 0, 0 }, { 0, 0, 0 }, { 1, 1, 0 }, { 2, 1, 0 }, { 1, 0, 2 }, { 2, 0, 2 }, { 2, 0, 2 }, { 1, 0, 2 }, { 2, 0, 0 } };
CONSTV u8 CID_T[][2] = { { 2, 0 }, { 0, 0 }, { 2, 0 }, { 2, 1 }, { 1, 0 }, { 2, 0 }, { 1, 0 }, { 1, 0 }, { 0, 0 }, { 1, 0 }, { 0, 1 }, { 0, 1 }, { 0, 0 }, { 1, 0 }, { 7, 0 }, { 1, 0 }, { 2, 0 }, { 2, 0 } };
#define STAT_LEN 0

#define WL_RESW 1
#define BANGS   2

#define WL_BANK Term r0, r1, r2, r3, r4, r5, rp, r6, r7, r8, r9, r10;

#define WL_LOAD(A, N) \
  do { \
    if ((N) <= 0) break; r0 = e.mem[(A) + 0]; \
    if ((N) <= 1) break; r1 = e.mem[(A) + 1]; \
    if ((N) <= 2) break; r2 = e.mem[(A) + 2]; \
    if ((N) <= 3) break; r3 = e.mem[(A) + 3]; \
    if ((N) <= 4) break; r4 = e.mem[(A) + 4]; \
    if ((N) <= 5) break; r5 = e.mem[(A) + 5]; \
    if ((N) <= 6) break; r6 = e.mem[(A) + 6]; \
    if ((N) <= 7) break; r7 = e.mem[(A) + 7]; \
    if ((N) <= 8) break; r8 = e.mem[(A) + 8]; \
    if ((N) <= 9) break; r9 = e.mem[(A) + 9]; \
    if ((N) <= 10) break; r10 = e.mem[(A) + 10]; \
  } while (0);

#define WL_LAST(X) \
  switch (war) { \
    case 0: r0 = (X); \
      break; \
    case 1: r1 = (X); \
      break; \
    case 2: r2 = (X); \
      break; \
    case 3: r3 = (X); \
      break; \
    case 4: r4 = (X); \
      break; \
    case 5: r5 = (X); \
      break; \
    case 6: r6 = (X); \
      break; \
    case 7: r7 = (X); \
      break; \
    case 8: r8 = (X); \
      break; \
    case 9: r9 = (X); \
      break; \
    case 10: r10 = (X); \
      break; \
  }

#define WL_SAVE(V) (V)[0] = r0;

#define WL_TAKE(V) r0 = (V)[0];

#define WL_SIG u64* wl_mem, u64* wl_alc, DEV Term* sp, u32 seq, u32 rn, Term r0, Term r1, Term r2, Term r3, Term r4, Term r5, Term rp, Term r6, Term r7, Term r8, Term r9, Term r10

#define WL_ALL e.mem, e.alc, sp, seq, rn, r0, r1, r2, r3, r4, r5, rp, r6, r7, r8, r9, r10

#define WL_TABLE WL_X(FID_ARRAY_SPREAD_RUN_0) WL_X(FID_VIZ_FILL) WL_X(FID_VIZ_FILL_K48) WL_X(FID_VIZ_FILL_K49) WL_X(FID_VIZ_FILL_K50) WL_X(FID_VIZ_FILL_K51) WL_X(FID_VIZ_FILL_J48) WL_X(FID_VIZ_FRAME_GPU) WL_X(FID_VIZ_FRAME_GPU_K58) WL_X(FID_VIZ_FRAME_GPU_J58) WL_X(FID_VIZ_DRAW) WL_X(FID_VIZ_STEP) WL_X(FID_VIZ_STEP_K62) WL_X(FID_IO_BIND) WL_X(FID_IO_BIND_C64) WL_X(FID_IO_BIND_K65) WL_X(FID_VIZ_BLANK) WL_X(FID_VIZ_LOOP) WL_X(FID_VIZ_LOOP_C68) WL_X(FID_VIZ_LOOP_C69) WL_X(FID_VIZ_LOOP_C70) WL_X(FID_VIZ_LOOP_C71) WL_X(FID_VIZ_LOOP_C72) WL_X(FID_VIZ_LOOP_C73) WL_X(FID_VIZ_LOOP_C74) WL_X(FID_VIZ_LOOP_C75) WL_X(FID_VIZ_LOOP_C76) WL_X(FID_VIZ_LOOP_C77) WL_X(FID_VIZ_LOOP_C78) WL_X(FID_VIZ_LOOP_C79) WL_X(FID_VIZ_LOOP_K80) WL_X(FID_VIZ_LOOP_C81) WL_X(FID_VIZ_LOOP_C82) WL_X(FID_MAIN) WL_X(FID_MAIN_K84) WL_X(FID_MAIN_K85) WL_X(FID_VIZ_NEXT) WL_X(FID_VIZ_PARAM) WL_X(FID_VIZ_SHOW) WL_X(FID_IO_EMIT) WL_X(FID_CLO_APPLY) WL_X(FID_EXIT)
#define MAIN_FID FID_MAIN
#define MAIN_PURE 0
#define BLK_SHR 1
#define SPRD_USED 1

#define TAB_AT(T, S, I) T[S < I ? S : I]

#define fid_arity(x) ((u32)FID_T[x][0])
#define fid_resw(x)  ((u32)FID_T[x][1])
#define fid_bangs(x) ((bool)(FID_T[x][2] & 1))
#define fid_nofk(x)  ((bool)(FID_T[x][2] & 2))
#define fid_sprd(x)  ((bool)(FID_T[x][2] & 4))
#define cid_arity(x) ((u32)CID_T[x][0])
#define cid_hot(x)   ((bool)CID_T[x][1])

// A32
// ===

// C11's atomics on every lane; a device FENCE releases or acquires.
// Metal's a32_load reads through a volatile local, or the M1 pipeline
// build dies. A weak CAS may fail with the cell still x: a32_cmpx loops.

#define A32_LOOP(k, x) \
  INLINE u32 a32_##k(DEV u32* p, u32 v) { \
    u32 o = a32_load(p); \
    while (!a32_cas(p, &o, x)) { \
    } \
    return o; \
  }

#ifdef __METAL_VERSION__

INLINE DEV atomic_uint* A32(DEV u32* p) {
  return (DEV atomic_uint*)p;
}

INLINE TG atomic_uint* A32(TG u32* p) {
  return (TG atomic_uint*)p;
}

#define a32_load(p) \
  ({ volatile thread u32 _a32v = atomic_load_explicit(A32(p), RLX); _a32v; })

#else

#define a32_load(p) atomic_load_explicit(A32(p), RLX)

#ifdef BEND_RTC

#define A32(p) (p)
#define atomic_load_explicit(p, o)     (*(volatile u32*)(p))
#define atomic_store_explicit(p, v, o) (*(volatile u32*)(p) = (v))
#define atomic_fetch_add_explicit(p, v, o) atomicAdd((u32*)(p), v)
#define atomic_fetch_sub_explicit(p, v, o) atomicSub((u32*)(p), v)
#define atomic_fetch_and_explicit(p, v, o) atomicAnd((u32*)(p), v)
#define atomic_fetch_or_explicit(p, v, o) atomicOr((u32*)(p), v)
#define atomic_fetch_xor_explicit(p, v, o) atomicXor((u32*)(p), v)
#define atomic_fetch_min_explicit(p, v, o) atomicMin((u32*)(p), v)
#define atomic_fetch_max_explicit(p, v, o) atomicMax((u32*)(p), v)
#define atomic_compare_exchange_weak_explicit(p, e, v, s, f) a32_swp(p, e, v)

INLINE bool a32_swp(DEV u32* p, u32* e, u32 v) {
  u32 x = *e;
  *e = atomicCAS((u32*)p, x, v);
  return *e == x;
}

#else

#define A32(p) ((_Atomic u32*)(p))
#define atomic_fetch_min_explicit __c11_atomic_fetch_min
#define atomic_fetch_max_explicit __c11_atomic_fetch_max

#endif

#endif

#define RLX memory_order_relaxed

#if DEVICE
#define REL RLX
#define ACQ RLX
#define ACR RLX
#define a32_acq(p) FENCE()
#else
#define REL memory_order_release
#define ACQ memory_order_acquire
#define ACR memory_order_acq_rel
#define a32_acq(p) ((void)a32_load_acq(p))
#endif

#define a32_store(p, v)     atomic_store_explicit(A32(p), v, RLX)
#define a32_add(p, v) atomic_fetch_add_explicit(A32(p), v, RLX)
#define a32_sub(p, v) atomic_fetch_sub_explicit(A32(p), v, RLX)
#define a32_and(p, v) atomic_fetch_and_explicit(A32(p), v, RLX)
#define a32_or(p, v) atomic_fetch_or_explicit(A32(p), v, RLX)
#define a32_xor(p, v) atomic_fetch_xor_explicit(A32(p), v, RLX)
#define a32_min(p, v) atomic_fetch_min_explicit(A32(p), v, RLX)
#define a32_max(p, v) atomic_fetch_max_explicit(A32(p), v, RLX)
#define a32_sub_rel(p, v)   (FENCE(), atomic_fetch_sub_explicit(A32(p), v, REL))
#define a32_store_rel(p, v) (FENCE(), atomic_store_explicit(A32(p), v, REL))
#define a32_at(H, word)     ((DEV u32*)&(H)[word])

INLINE u32 a32_load_acq(DEV u32* p) {
  u32 v = atomic_load_explicit(A32(p), ACQ);
  FENCE();
  return v;
}

INLINE bool a32_cas(DEV u32* p, THR u32* e, u32 v) {
  FENCE();
  bool ok = atomic_compare_exchange_weak_explicit(A32(p), e, v, ACR, ACQ);
  FENCE();
  return ok;
}

A32_LOOP(exch, v)

INLINE u32 a32_cmpx(DEV u32* p, u32 x, u32 v) {
  u32 o = x;
  while (!a32_cas(p, &o, v) && o == x) {
  }
  return o;
}

// Err
// ===

#if DEVICE

INLINE void err_post(DEV u64* H, u32 code) {
  a32_cmpx(a32_at(H, H_ERROR_CODE), 0, code);
}

#else

static const char* ERR_TEXT[] = { "",
  "runtime fail-stop",
  "runtime fail-stop",
  "out of memory: run again with a bigger span, as in --gpu 8GB",
  "a function the device does not hold",
  "a Nat past the largest immediate 2^48-1",
  "runtime fail-stop",
  "memory fault (machine stack overflow?)",
  "an array past the deepest block class 31" };

static void err_fail(const char* msg) {
  fflush(stdout);
  fprintf(stderr, "bend: %s\n", msg);
  fflush(stderr);  // _exit drops buffers, and Windows buffers stderr into a pipe
  _exit(1);
}

static void err_post(u64* H, u32 code) {
  err_fail(ERR_TEXT[code]);
}

static void err_trap(int sig) {
  err_post(NULL, ERR_DEEP);
}

#endif

#define err_seen(H)    (DEVICE && a32_load(a32_at(H, H_ERROR_CODE)) != 0)
#define err_spun(H, n) ((++*(n) & 4095) == 0 && err_seen(H))

#ifdef __METAL_VERSION__
INLINE f32 atan2_c99(f32 y, f32 x) {
  return y == 0.0f && x == x
    ? copysign(signbit(x) ? M_PI_F : 0.0f, y) : atan2(y, x);
}
#define sqrt  precise::sqrt
#define exp   precise::exp
#define log   precise::log
#define log2  precise::log2
#define log10 precise::log10
#define sin   fast::sin
#define cos   fast::cos
#define tan   fast::tan
#define pow   precise::pow
#define fmod  precise::fmod
#define atan2 atan2_c99
#endif

#define U32_BIN(a, o, b) ((u64)((u32)(a) o (u32)(b)))

#define U32_QUO(a, b) \
  ((a) / 2 / (b) * 2 + ((a) - (a) / 2 / (b) * 2 * (b) >= (b)))

INLINE f32 f32_unbox(u64 x) {
  union { u32 u; f32 f; } p = { (u32)x };
  return p.f;
}

INLINE u64 f32_rewrap(f32 x) {
  union { f32 f; u32 u; } p = { x };
  return p.u;
}

INLINE u64 f32_to_u32(u64 a) {
  f32 v = f32_unbox(a);
  return v >= 0.0f && v < 4294967296.0f ? (u32)v : 0;
}

INLINE u64 nat_chk(Env e, u64 n) {
  if (n > NAT_IMM) {
    err_post(e.mem, ERR_NATS);
    return NAT_IMM;
  }
  return n;
}

INLINE u64 nat_mul(Env e, u64 a, u64 b) {
  return nat_chk(e, b != 0 && a > NAT_IMM / b ? NAT_IMM + 1 : a * b);
}

#if DEVICE

#define f32_show(e, x) (err_post(e.mem, ERR_FIDS), 0)
#define f32_read(e, s) (err_post(e.mem, ERR_FIDS), 0)

#else

static Term f32_show(Env e, Term x);
static Term f32_read(Env e, Term s);

#endif

A32_LOOP(fadd, f32_rewrap(f32_unbox(o) + f32_unbox(v)))

// Bank
// ====

// A stack of exact generations per class. The host pops and pushes at rd;
// a device pass pops below rd and pushes above top, compacted after it.

#define bank_at(H, c) ((DEV Bank*)((H) + H_BANK) + (c))

INLINE u64 bank_pop(DEV u64* H, u32 c) {
  DEV Bank* b = bank_at(H, c);
  u64 got = 0;
  LOCK(bank_lock);
  u32 t = a32_sub(&b->rd, 1);
  if ((int)t > 0) {
    got = H[b->off + t - 1];
  } else {
    a32_add(&b->rd, 1);
  }
  if (!DEVICE) {
    b->wr = b->top = b->rd;
  }
  UNLOCK(bank_lock);
  return got;
}

INLINE void bank_push(DEV u64* H, u32 c, u64 head) {
  DEV Bank* b = bank_at(H, c);
  LOCK(bank_lock);
  H[b->off + a32_add(&b->wr, 1)] = head;
  if (!DEVICE) {
    b->rd = b->top = b->wr;
  }
  UNLOCK(bank_lock);
}

// Heap
// ====

// Per lane and class: HOT, a LIFO free chain; LEN, its length in words;
// COLD, one parked generation. A free reaching KEEP_WORDS parks HOT as COLD
// and banks the old COLD. A miss takes COLD, a bank entry or a fresh
// quantum. A device lane also banks its COLD at the kernel end (dev_cut), so
// it keeps less than a generation between kernels; handing generations over
// as they fill, rather than cutting them off HOT then, spares that a walk
// down the chain (0.02 to 0.04 ms a kernel at 32,768 lanes). The bump grows
// only when all of these are empty.

#define ALC_AT(e, i)   (e).alc[(i) * LANE_STEP]
#define ALC_LEN(e, c)  ALC_AT(e, NCLS_ALL + (c))
#define ALC_COLD(e, c) ALC_AT(e, 2 * NCLS_ALL + (c))
#define KEEP(c)        (KEEP_WORDS >> (c) ? KEEP_WORDS >> (c) : 1)

INLINE u32 cls_fit(u32 words) {
  return words > 1 ? 32 - CLZ(words - 1) : 0;
}

OUTLINE void heap_hand(Env e, u32 cls) {
  u64 cold = ALC_COLD(e, cls);
  if (cold) {
    bank_push(e.mem, cls, cold);
  }
  ALC_COLD(e, cls) = ALC_AT(e, cls);
  ALC_AT(e, cls)   = 0;
  ALC_LEN(e, cls)  = 0;
}

#if DEVICE
#define corpus_grow(H, n) false
#else
static bool corpus_grow(u64* H, u64 need);
#endif

OUTLINE u64 heap_alloc_miss(Env e, u32 cls) {
  DEV u64* H = e.mem;
  u64  got = ALC_COLD(e, cls);
  ALC_COLD(e, cls) = 0;
  if (!got) {
    got = bank_pop(H, cls);
  }
  u32 n = got ? KEEP(cls) : cls < NCLS ? QUANTUM >> cls : 1;
  if (!got) {
    u32 pages = (n << cls) >> PAGE_BITS;
    u32 p     = a32_add(a32_at(H, H_BUMP), pages);
    if ((u64)p + pages > a32_load_acq(a32_at(H, H_CAP))
      && !corpus_grow(H, (u64)p + pages)) {
      err_post(H, ERR_HEAP);
      return HEAP_OFF;
    }
    got = HEAP_OFF + ((u64)p << PAGE_BITS);
    for (u32 i = 1; i <= n; i += 1) {
      H[got + ((u64)(i - 1) << cls)] = i < n ? got + ((u64)i << cls) : 0;
    }
  }
  ALC_AT(e, cls)  = H[got];
  ALC_LEN(e, cls) = (u64)(n - 1) << cls;
  return got;
}

INLINE u64 heap_alloc(Env e, u32 cls) {
  u64 h = ALC_AT(e, cls);
  if (h) {
    ALC_AT(e, cls)   = e.mem[h];
    ALC_LEN(e, cls) -= 1ull << cls;
    return h;
  }
  return heap_alloc_miss(e, cls);
}

INLINE void heap_free(Env e, u32 cls, u64 loc) {
  if (err_seen(e.mem)) {
    return;
  }
  e.mem[loc]       = ALC_AT(e, cls);
  ALC_AT(e, cls)   = loc;
  ALC_LEN(e, cls) += 1ull << cls;
  if (ALC_LEN(e, cls) >= KEEP_WORDS) {
    heap_hand(e, cls);
  }
}

INLINE void spare_free(Env e, u32 cls, u64 loc) {
  if (loc >= HEAP_OFF) {
    heap_free(e, cls, loc);
  }
}

// Term
// ====

// A static node (below the heap) is trivial, as is a captureless
// closure. A fork's Array handle (BLK_SHR: an Array binder is hot)
// is a redirect: loaded plainly, copied and dropped by a match.

#define term_make(tag, aux, loc) \
  (((u64)(tag) << 56) | ((u64)(aux) << 40) | (u64)(loc))

#define term_ctr(cid, loc) term_make(TAG_CTR, cid, loc)
#define term_pak(cid, loc) term_make(TAG_PAK, cid, loc)
#define term_clo(fid, loc) term_make(TAG_CLO, fid, loc)
#define term_buf(cls, loc) term_make(TAG_BUF, cls, loc)
#define term_tsk(fid, loc) term_make(TAG_TSK, fid, loc)

INLINE Term term_blk(bool arr, u32 cls, u64 loc) {
  return term_buf(cls, loc) | ((u64)arr << 57);
}

INLINE u64 term_tag(Term t) {
  return (t >> 56) & 0x7f;
}

INLINE bool term_rfc(Term t) {
  return (t & RFC_BIT) != 0;
}

INLINE u64 term_aux(Term t) {
  return (t >> 40) & 0xFFFF;
}

INLINE u64 term_loc(Term t) {
  return t & LOC_MASK;
}

INLINE bool term_triv(Term t) {
  return term_tag(t) <= TAG_PAK || t == TERM_HOLE || term_loc(t) < HEAP_OFF;
}

OUTLINE Term rfc_wrap(Env e, Term t, u32 cnt) {
  if (term_tag(t) == TAG_CLO || term_tag(t) == TAG_TSK) {
    err_post(e.mem, ERR_RFCS);
    return t;
  }
  u64 r = heap_alloc(e, 0);
  e.mem[r] = ((u64)term_loc(t) << 24) | cnt;
  return (t & ~LOC_MASK) | RFC_BIT | r;
}

INLINE Term rfc_seal(Env e, Term t) {
  if (term_tag(t) != TAG_CTR || term_rfc(t)) {
    return t;
  }
  return rfc_wrap(e, t, 1);
}

INLINE u64 rfc_view(Env e, u64 r) {
  DEV u32* w = a32_at(e.mem, r);
  u64 cell = ((u64)a32_load(w + 1) << 32) | a32_load(w);
  if ((cell & RFC_CNT) == 1) {
    a32_acq(w);
  }
  return cell;
}

INLINE void rfc_bump(Env e, u64 r, u32 k) {
  u32 c = a32_add(a32_at(e.mem, r), k);
  if ((c & RFC_CNT) >= RFC_CNT - k) {
    err_post(e.mem, ERR_RFCS);
  }
}

INLINE Term term_keep(Env e, Term t, u32 k) {
  if (term_rfc(t)) {
    rfc_bump(e, term_loc(t), k);
    return t;
  }
  if (term_triv(t)) {
    return t;
  }
  return rfc_wrap(e, t, 1 + k);
}

INLINE u64 term_peek(Env e, Term t) {
  if (term_rfc(t)) {
    return rfc_view(e, term_loc(t)) >> 24;
  }
  return term_loc(t);
}

#define blk_shr(t) (BLK_SHR && term_rfc(t))

INLINE u64 blk_loc(DEV u64* H, Term a) {
  return blk_shr(a) ? H[term_loc(a)] >> 24 : term_loc(a);
}

INLINE u32 blk_cls(Term t) {
  return (u32)term_aux(t) & 31;
}

#define buf_wcls(c) ((c) == 0 ? 0 : (c) - 1)

INLINE u32 blk_span(Term t) {
  u32 c = blk_cls(t);
  return term_tag(t) == TAG_ARR ? c : buf_wcls(c);
}

FAR void term_drop(Env e, Term t) {
  DEV u64* H = e.mem;
  u64  cur = 0;
  Term c0  = 0;
  u32  step = 0;
  for (;;) {
    if (!term_triv(t) && term_rfc(t)) {
      u64      r = term_loc(t);
      DEV u32* p = a32_at(H, r);
      if ((a32_sub_rel(p, 1) & RFC_CNT) != 1) {
        t = 0;
      } else {
        a32_acq(p);
        t = (t & ~(RFC_BIT | LOC_MASK)) | (H[r] >> 24);
        heap_free(e, 0, r);
      }
    }
    if (!term_triv(t)) {
      u64 tag = term_tag(t);
      if (tag == TAG_BUF) {
        heap_free(e, blk_span(t), term_loc(t));
      } else {
        u32 aux = (u32)term_aux(t);
        u64 loc = term_loc(t);
        u32 n   = tag == TAG_ARR ? 0 : tag == TAG_CTR ? cid_arity(aux)
          : fid_arity(aux) - (tag == TAG_CLO);
        u32 cls = tag == TAG_ARR ? 64 | blk_cls(t)
          : n > 247 ? 64 | (n - 240)
          : cls_fit(tag == TAG_TSK ? n + 2 : n);
        c0 = H[loc];
        H[loc] = cur;
        cur = loc | ((u64)n << 48) | ((u64)cls << 56);
      }
    }
    for (;;) {
      if (err_spun(H, &step)) {
        return;
      }
      if (cur == 0) {
        return;
      }
      u64  loc = cur & LOC_MASK;
      u32  i   = (u8)(cur >> 40);
      u32  n   = (u8)(cur >> 48);
      u32  cls = (u32)(cur >> 56);
      bool arr = cls > 63;
      u32  j   = i;
      if (arr) {
        cls &= 63;
        n   = 1u << cls;
        if (i == 2) {
          j = (u32)H[loc + 1];
        }
      }
      if (j < n) {
        Term c = j == 0 ? c0 : H[loc + j];
        if (arr && j > 0) {
          H[loc + 1] = j + 1;
        }
        if (!arr || i < 2) {
          cur += 1ull << 40;
        }
        if (!term_triv(c)) {
          t = c;
          break;
        }
      } else {
        u64 up = H[loc];
        heap_free(e, cls, loc);
        cur = up;
      }
    }
  }
}

INLINE void term_sink(Env e, Term t) {
  if (!term_triv(t)) {
    term_drop(e, t);
  }
}

OUTLINE void span_fade(Env e, Term t, u64 src, u32 n) {
  for (u32 j = 0; j < n; j += 1) {
    Term f = e.mem[src + j];
    if (term_rfc(f)) {
      rfc_bump(e, term_loc(f), 1);
    } else if (!term_triv(f)) {
      err_post(e.mem, ERR_RFCS);
    }
  }
  term_drop(e, t);
}

INLINE u64 ctr_take(Env e, Term t, u32 n, THR Term* out) {
  DEV u64* H = e.mem;
  if (!term_rfc(t)) {
    for (u32 j = 0; j < n; j += 1) {
      out[j] = H[term_loc(t) + j];
    }
    return term_loc(t);
  }
  u64 r    = term_loc(t);
  u64 cell = rfc_view(e, r);
  u64 src  = cell >> 24;
  for (u32 j = 0; j < n; j += 1) {
    out[j] = H[src + j];
  }
  if ((cell & RFC_CNT) == 1) {
    heap_free(e, 0, r);
    return src;
  }
  span_fade(e, t, src, n);
  return 0;
}

INLINE Term term_word(Env e, Term w) {
  u32 x = 0;
  Term t = w;
  for (u32 i = 0; i < 32 && term_aux(t) == CID_WCON; i += 1) {
    u64 l = term_peek(e, t);
    x |= (u32)(e.mem[l] & 1) << i;
    t = e.mem[l + 1];
  }
  term_sink(e, w);
  return x;
}

// Blk
// ===

// A block owns one allocation in its class (an ARR 2^c Terms, a BUF 2^c
// u32). Matching ANode is blk_half twice (the high call frees the source);
// ANode{l, r} is blk_node; Array.clone is blk_copy.

#define BLK_ALLOC(n, w) \
  u64 n = heap_alloc(e, w); \
  if (err_seen(e.mem)) { \
    return term_buf(0, n); \
  }

INLINE DEV u32a* blk_ptr(DEV u64* H, u64 loc, u32 i) {
  return (DEV u32a*)(H + loc) + i;
}

INLINE Term blk_read(DEV u64* H, bool arr, u64 loc, u32 i) {
  if (arr) {
    return H[loc + i];
  }
  return (u64)*blk_ptr(H, loc, i);
}

INLINE void blk_write(DEV u64* H, bool arr, u64 loc, u32 i, Term v) {
  if (arr) {
    H[loc + i] = v;
  } else {
    *blk_ptr(H, loc, i) = (u32)v;
  }
}

INLINE u32 blk_at(Term a, u64 i, u32 lgs) {
  return ((u32)i & (u32)((1ull << (blk_cls(a) - lgs)) - 1)) << lgs;
}

INLINE Term blk_keep(Env e, u64 at) {
  Term w = e.mem[at];
  Term v = term_keep(e, w, 1);
  if (v != w) {
    e.mem[at] = v;
  }
  return v;
}

INLINE void blk_fill(Env e, u64 dst, u64 src, u64 n, bool keep) {
  for (u64 j = 0; j < n; j += 1) {
    e.mem[dst + j] = keep ? blk_keep(e, src + j) : e.mem[src + j];
  }
}

INLINE void blk_free(Env e, Term t) {
  blk_shr(t) ? term_drop(e, t) : heap_free(e, blk_span(t), term_loc(t));
}

OUTLINE Term blk_copy(Env e, Term a) {
  bool arr = term_tag(a) == TAG_ARR;
  u32 cls = blk_span(a);
  BLK_ALLOC(dst, cls)
  blk_fill(e, dst, blk_loc(e.mem, a), 1ull << cls, arr);
  return term_blk(arr, blk_cls(a), dst);
}

INLINE Term blk_node(Env e, Term l, Term r) {
  DEV u64* H = e.mem;
  bool arr = term_tag(l) == TAG_ARR;
  u32 c = blk_cls(l);
  if (c != blk_cls(r) || c + 1 >= NCLS_ALL) {
    err_post(H, ERR_TAGS);
    return l;
  }
  u64 pl = blk_loc(H, l);
  u64 pr = blk_loc(H, r);
  BLK_ALLOC(n, arr ? c + 1 : c)
  if (!arr && c == 0) {
    H[n] = (u64)*blk_ptr(H, pl, 0) | ((u64)*blk_ptr(H, pr, 0) << 32);
  } else {
    u64 cw = 1ull << blk_span(l);
    blk_fill(e, n, pl, cw, arr && blk_shr(l));
    blk_fill(e, n + cw, pr, cw, arr && blk_shr(r));
  }
  blk_free(e, l);
  blk_free(e, r);
  return term_blk(arr, c + 1, n);
}

INLINE Term blk_half(Env e, Term a, u32 hi) {
  DEV u64* H = e.mem;
  bool arr = term_tag(a) == TAG_ARR;
  u32 c = blk_cls(a);
  if (c == 0) {
    err_post(H, ERR_TAGS);
    return a;
  }
  c -= 1;
  u32 cw = arr ? c : buf_wcls(c);
  u64 src = blk_loc(H, a);
  BLK_ALLOC(n, cw)
  if (!arr && c == 0) {
    H[n] = (u64)*blk_ptr(H, src, hi);
  } else {
    blk_fill(e, n, src + ((u64)hi << cw), 1ull << cw, arr && blk_shr(a));
  }
  if (hi) {
    blk_free(e, a);
  }
  return term_blk(arr, c, n);
}

INLINE Term blk_new(Env e, bool arr, u64 d, u32 lgs, u32 n, THR Term* v) {
  DEV u64* H = e.mem;
  if (d + lgs > 31) {
    err_post(H, ERR_ARRS);
    d = 0;
  }
  u32 c = (u32)d + lgs;
  BLK_ALLOC(l, arr ? c : buf_wcls(c))
  for (u32 j = 0; arr && d > 0 && j < n; j += 1) {
    if (d >= 24 && !term_triv(v[j])) {
      err_post(H, ERR_RFCS);
    }
    v[j] = term_keep(e, v[j], (1u << d) - 1);
  }
  for (u64 i = 0; i < (1ull << c); i += 1) {
    blk_write(H, arr, l, (u32)i, i % (1u << lgs) < n ? v[i % (1u << lgs)] : 0);
  }
  return term_blk(arr, c, l);
}

// Ring
// ====

// planes LANES wide: a smaller bag has deeper rings in the same region
#define ring_word(H, r, w) ((H) + RING_OFF + (w) * LANES + (r))
#define ring_slot(H, r, p) ring_word(H, r, (p) & (RING_LEN - 1))
#define ring_get(H, r)     ((DEV u32*)ring_word(H, r, RING_LEN))
#define ring_put(H, r)     ((DEV u32*)ring_word(H, r, RING_LEN + 1))

INLINE u32 ring_lap(u32 pos) {
  return ~(u32)(pos / RING_LEN) & 1;
}

INLINE void ring_push(DEV u64* H, u32 r, Term tsk) {
  u32 pos = a32_add(ring_put(H, r), 1);
  if (pos - a32_load(ring_get(H, r)) >= RING_LEN) {
    err_post(H, ERR_RING);
    return;
  }
  DEV u32* lo = (DEV u32*)ring_slot(H, r, pos);
  a32_store(lo, (u32)tsk);
  a32_store_rel(lo + 1, (u32)(tsk >> 32) | (ring_lap(pos) << 31));
}

INLINE u32 ring_flip(u32 i) {
  return (i % CUBE_T << CUBE_LOG) + i / CUBE_T;
}

#define ring_pick(b, s, c) ((b) + (s) * (a32_add(c, 1) & (CUBE_T - 1)))

// Task
// ====

INLINE u64 task_node(Env e, u32 fid, Term cont, u32 idx, u32 rem) {
  u32 ar  = fid_arity(fid);
  u64 loc = heap_alloc(e, cls_fit(ar + 2));
  for (u32 i = 0; rem && i < ar; i += 1) {
    e.mem[loc + i] = TERM_HOLE;
  }
  e.mem[loc + ar]     = cont;
  e.mem[loc + ar + 1] = ((u64)idx << 32) | rem;
  return loc;
}

INLINE u64 task_tail(Term t) {
  return term_loc(t) + fid_arity((u32)term_aux(t));
}

INLINE Term task_deliver(DEV u64* H, Term cont, u32 idx, THR Term* v, u32 n) {
  u64 at = cont == TERM_HOLE ? H_ROOT_WORD : term_loc(cont) + idx;
  for (u32 j = 0; j < WL_RESW; j += 1) {
    if (j < n) {
      H[at + j] = v[j];
    }
  }
  if (cont == TERM_HOLE) {
    a32_store_rel(a32_at(H, H_ROOT_DONE), n + 1);
    return 0;
  }
  u64 tl = task_tail(cont);
  if (a32_sub_rel(a32_at(H, tl + 1), 1) == 1) {
    a32_acq(a32_at(H, tl + 1));
    return cont;
  }
  return 0;
}

// A spread's loop (Array.spread.run in base.bend) keeps its words in this
// order. Split, a loop becomes the join of its parts (spread_split), and a
// part is a cell rather than a task: the loop, and its own i, st and c
// (tagged TAG_SPR; FID_ENTER reads the rest of its words off the loop). The
// loop keeps c at 0 (resumed, it returns the array), counts in I the parts
// giving back its array's handle, and holds the cells in ST (their address,
// and the count of parts above bit 48). Each part owns a reference to the
// array; those giving back the same handle are only counted, and the join
// returns their references at once when the last part is in, rather than
// one atomic each.
#define SPRD_C  0
#define SPRD_A  1
#define SPRD_I  2
#define SPRD_ST 3
#define SPRD_CELL 3  // words a part's cell

INLINE Term spread_give(Env e, Term cont, Term v) {
  DEV u64* H   = e.mem;
  u64      loc = term_loc(cont);
  if (v == H[loc + SPRD_A]) {
    a32_add(a32_at(H, loc + SPRD_I), 1);
  } else {
    term_sink(e, v);
  }
  u64 tl = task_tail(cont);
  if (a32_sub_rel(a32_at(H, tl + 1), 1) == 1) {
    a32_acq(a32_at(H, tl + 1));
    u32  back  = a32_load(a32_at(H, loc + SPRD_I));
    Term a     = H[loc + SPRD_A];
    u64  cells = H[loc + SPRD_ST];
    if (back != 0 && term_rfc(a)) {
      a32_sub(a32_at(H, term_loc(a)), back);  // (its own reference stays)
    }
    heap_free(e, cls_fit((u32)(cells >> 48) * SPRD_CELL), cells & LOC_MASK);
    return cont;
  }
  return 0;
}

// A fork's children go out as ring_push would push them, but the high words
// (which a reader loads first, acquiring, and which make an entry whole)
// wait until every child's slot is claimed and its low word written, after
// one fence rather than one per child. (Claiming all the slots first, so the
// claims overlap, made the kernel slower: the arrays it needs cost registers
// throughout the one big kernel.)
#define DEAL_BATCH 16

INLINE void deal_publish(DEV u32* THR* hi, THR u32* hv, u32 n) {
  if (n == 0) {
    return;
  }
  FENCE();
  for (u32 i = 0; i < n; i += 1) {
    atomic_store_explicit(A32(hi[i]), hv[i], REL);
  }
}

INLINE void task_deal(DEV u64* H, Term join, u32 base, u32 stride, TG u32* cur) {
  u64 loc = term_loc(join);
  u32 ar  = fid_arity((u32)term_aux(join));
  u32 g   = 0;
  if (stride == 0) {
    u32 rem = (u32)H[loc + ar + 1];
    g = a32_add(a32_at(H, H_CURSOR), rem);
  }
  DEV u32* hi[DEAL_BATCH];
  u32      hv[DEAL_BATCH];
  u32      n = 0;
  for (u32 i = 0; i < ar; i += 1) {
    Term k = H[loc + i];
    if (term_tag(k) == TAG_TSK) {
      H[loc + i] = TERM_HOLE;
      u32 to;
      if (stride != 0) {
        to = ring_pick(base, stride, cur);
      } else {
        to = ring_flip(g & (u32)(LANES - 1));
        g += 1;
      }
      u32 pos = a32_add(ring_put(H, to), 1);
      if (pos - a32_load(ring_get(H, to)) >= RING_LEN) {
        err_post(H, ERR_RING);
        continue;
      }
      DEV u32* lo = (DEV u32*)ring_slot(H, to, pos);
      a32_store(lo, (u32)k);
      hi[n] = lo + 1, hv[n] = (u32)(k >> 32) | (ring_lap(pos) << 31), n += 1;
      if (n == DEAL_BATCH) {
        deal_publish(hi, hv, n);
        n = 0;
      }
    }
  }
  deal_publish(hi, hv, n);
}

// Root
// ====

INLINE bool root_done(DEV u64* H) {
  return a32_load_acq(a32_at(H, H_ROOT_DONE)) != 0;
}

static u32 root_take(DEV u64* H, THR Term* v) {
  u32 n = a32_load_acq(a32_at(H, H_ROOT_DONE)) - 1;
  for (u32 j = 0; j < n; j += 1) {
    v[j] = H[H_ROOT_WORD + j];
  }
  a32_store(a32_at(H, H_ROOT_DONE), 0);
  return n;
}

// Spins
// =====

CONSTV u64 STAT_IMG[] = { 0 };

INLINE Term spin_0(Env e, THR Term* o, u32 r0, Term r1, Term r2) {
  u32 wpoll = 0;
  Term _v_1 = 0;
  u32 _c_0 = r0;
  Term _a_1 = r1;
  Term _b_1 = r2;
  WL_SPIN
    if (_c_0 == 0) {
      term_sink(e, _a_1);
      _v_1 = _b_1;
    } else {
      term_sink(e, _b_1);
      _v_1 = _a_1;
    }
  break;
  }
  o[0] = _v_1;
  return 1;
}

INLINE Term spin_2(Env e, THR Term* o, u32 r0, u32 r1) {
  u32 wpoll = 0;
  u32 _v_5 = 0;
  u32 _x_2 = r0;
  u32 _y_2 = r1;
  WL_SPIN
    _v_5 = f32_rewrap((f32)sqrt(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(_x_2) * f32_unbox(_x_2))) + f32_unbox(f32_rewrap(f32_unbox(_y_2) * f32_unbox(_y_2)))))));
  break;
  }
  o[0] = _v_5;
  return 1;
}

INLINE Term spin_4(Env e, THR Term* o, u32 r0, u32 r1) {
  u32 wpoll = 0;
  u32 _v_10 = 0;
  u32 _a_1 = r0;
  u32 _b_0 = r1;
  WL_SPIN
    Term _v_11 = 0;
    Term _o_1[1];
    if (spin_0(e, _o_1, ((u64)(f32_unbox(_a_1) < f32_unbox(_b_0))), _a_1, _b_0) == 0) {
      return 0;
    }
    _v_11 = _o_1[0];
    _v_10 = _v_11;
  break;
  }
  o[0] = _v_10;
  return 1;
}

INLINE Term spin_3(Env e, THR Term* o, u32 r0, u32 r1, u32 r2, u32 r3) {
  u32 wpoll = 0;
  u32 _v_7 = 0;
  u32 _f_0 = r0;
  u32 _t_0 = r1;
  u32 _hue_0 = r2;
  u32 _beat_0 = r3;
  WL_SPIN
    u32 _v_8 = 0;
    u32 _v_9 = 0;
    Term _o_2[1];
    if (spin_4(e, _o_2, 1065353216ull, f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(1058642330ull) + f32_unbox(f32_rewrap(f32_unbox(1048576000ull) * f32_unbox(f32_rewrap((f32)sin(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(_f_0) * f32_unbox(1095306838ull))) + f32_unbox(_t_0)))))))))) + f32_unbox(f32_rewrap(f32_unbox(_beat_0) * f32_unbox(1048576000ull))))) == 0) {
      return 0;
    }
    _v_9 = _o_2[0];
    _v_8 = _v_9;
    u32 _r_0 = f32_to_u32(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(1132396544ull) * f32_unbox(_v_8))) * f32_unbox(f32_rewrap(f32_unbox(1056964608ull) + f32_unbox(f32_rewrap(f32_unbox(1056964608ull) * f32_unbox(f32_rewrap((f32)cos(f32_unbox(f32_rewrap(f32_unbox(1086918649ull) * f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(_f_0) * f32_unbox(1058642330ull))) + f32_unbox(_hue_0))))))))))))));
    u32 _g_0 = f32_to_u32(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(1132396544ull) * f32_unbox(_v_8))) * f32_unbox(f32_rewrap(f32_unbox(1056964608ull) + f32_unbox(f32_rewrap(f32_unbox(1056964608ull) * f32_unbox(f32_rewrap((f32)cos(f32_unbox(f32_rewrap(f32_unbox(1086918649ull) * f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(_f_0) * f32_unbox(1058642330ull))) + f32_unbox(_hue_0))) + f32_unbox(1051260355ull))))))))))))));
    u32 _b_1 = f32_to_u32(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(1132396544ull) * f32_unbox(_v_8))) * f32_unbox(f32_rewrap(f32_unbox(1056964608ull) + f32_unbox(f32_rewrap(f32_unbox(1056964608ull) * f32_unbox(f32_rewrap((f32)cos(f32_unbox(f32_rewrap(f32_unbox(1086918649ull) * f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(_f_0) * f32_unbox(1058642330ull))) + f32_unbox(_hue_0))) + f32_unbox(1059816735ull))))))))))))));
    Term _a_2 = 16ull;
    Term _a_3 = 8ull;
    _v_7 = U32_BIN(U32_BIN((_a_2 >= 32 ? 0 : U32_BIN(_r_0, <<, _a_2)), |, (_a_3 >= 32 ? 0 : U32_BIN(_g_0, <<, _a_3))), |, _b_1);
  break;
  }
  o[0] = _v_7;
  return 1;
}

INLINE Term spin_1(Env e, THR Term* o, u32 r0, u32 r1, u32 r2, u32 r3, u32 r4, u32 r5, u32 r6, u32 r7, u32 r8) {
  u32 wpoll = 0;
  u32 _v_2 = 0;
  u32 _x_1 = r0;
  u32 _y_1 = r1;
  u32 _k_7 = r2;
  u32 _k_8 = r3;
  u32 _k_9 = r4;
  u32 _k_10 = r5;
  u32 _k_11 = r6;
  u32 _k_12 = r7;
  u32 _k_13 = r8;
  WL_SPIN
    u32 _unit_0 = f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(1128792064ull) / f32_unbox(f32_rewrap((f32)(u32)(_k_13))))) * f32_unbox(_k_8));
    u32 _dx_0 = f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap((f32)(u32)(_x_1))) - f32_unbox(f32_rewrap(f32_unbox(f32_rewrap((f32)(u32)(_k_12))) * f32_unbox(1056964608ull))))) * f32_unbox(_unit_0));
    u32 _dy_0 = f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap((f32)(u32)(_y_1))) - f32_unbox(f32_rewrap(f32_unbox(f32_rewrap((f32)(u32)(_k_13))) * f32_unbox(1056964608ull))))) * f32_unbox(_unit_0));
    u32 _w1_0 = f32_rewrap((f32)sin(f32_unbox(f32_rewrap(f32_unbox(_dx_0) + f32_unbox(_k_7)))));
    u32 _w2_0 = f32_rewrap((f32)sin(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(_dy_0) * f32_unbox(1067869798ull))) + f32_unbox(f32_rewrap(f32_unbox(_k_7) * f32_unbox(1066192077ull)))))));
    u32 _w3_0 = f32_rewrap((f32)sin(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(_dx_0) + f32_unbox(_dy_0))) * f32_unbox(1060320051ull))) + f32_unbox(f32_rewrap(f32_unbox(_k_7) * f32_unbox(1060320051ull)))))));
    u32 _v_3 = 0;
    u32 _v_4 = 0;
    Term _o_0[1];
    if (spin_2(e, _o_0, _dx_0, _dy_0) == 0) {
      return 0;
    }
    _v_4 = _o_0[0];
    _v_3 = _v_4;
    u32 _w4_0 = f32_rewrap((f32)sin(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(_v_3) * f32_unbox(1069547520ull))) - f32_unbox(f32_rewrap(f32_unbox(_k_7) * f32_unbox(1073741824ull))))) - f32_unbox(f32_rewrap(f32_unbox(_k_9) * f32_unbox(1077936128ull)))))));
    u32 _v_6 = 0;
    Term _o_3[1];
    if (spin_3(e, _o_3, f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(f32_rewrap(f32_unbox(_w1_0) + f32_unbox(_w2_0))) + f32_unbox(_w3_0))) + f32_unbox(_w4_0))) + f32_unbox(1082130432ull))) * f32_unbox(1040187392ull))) + f32_unbox(f32_rewrap(f32_unbox(_k_7) * f32_unbox(1041865114ull)))), _k_7, _k_10, _k_11) == 0) {
      return 0;
    }
    _v_6 = _o_3[0];
    _v_2 = _v_6;
  break;
  }
  o[0] = _v_2;
  return 1;
}

INLINE Term spin_5(Env e, THR Term* o, Term r0, Term r1) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  Term _a_1 = r0;
  Term _b_0 = r1;
  WL_SPIN
    term_sink(e, _b_0);
    _v_2 = _a_1;
  break;
  }
  o[0] = _v_2;
  return 1;
}

INLINE Term spin_6(Env e, THR Term* o, u32 r0, u32 r1, u32 r2, u32 r3, u32 r4, u32 r5, u32 r6, u32 r7, u32 r8, Term r9) {
  u32 wpoll = 0;
  Term _v_1 = 0;
  u32 _x_1 = r0;
  u32 _y_1 = r1;
  u32 _k_7 = r2;
  u32 _k_8 = r3;
  u32 _k_9 = r4;
  u32 _k_10 = r5;
  u32 _k_11 = r6;
  u32 _k_12 = r7;
  u32 _k_13 = r8;
  Term _a_1 = r9;
  WL_SPIN
    u32 _v_2 = 0;
    u32 _v_3 = 0;
    Term _o_0[1];
    if (spin_1(e, _o_0, _x_1, _y_1, _k_7, _k_8, _k_9, _k_10, _k_11, _k_12, _k_13) == 0) {
      return 0;
    }
    _v_3 = _o_0[0];
    _v_2 = _v_3;
    Term _at_0 = blk_loc(e.mem, _a_1);
    Term _at_1 = blk_at(_a_1, U32_BIN(U32_BIN(_y_1, *, _k_12), +, _x_1), 0);
    u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
    blk_write(e.mem, 0, _at_0, _at_1 + 0, _v_2);
    _v_1 = _a_1;
  break;
  }
  o[0] = _v_1;
  return 1;
}

INLINE Term spin_7(Env e, THR Term* o, Term r0) {
  u32 wpoll = 0;
  Term _v_4 = 0;
  Term _v_5 = 0;
  Term _a_0 = r0;
  WL_SPIN
    _a_0 = term_keep(e, _a_0, 1);
    _v_4 = _a_0;
    _v_5 = _a_0;
  break;
  }
  o[0] = _v_4;
  o[1] = _v_5;
  return 1;
}

INLINE Term spin_8(Env e, THR Term* o, u32 r0, u32 r1) {
  u32 wpoll = 0;
  u32 _v_3 = 0;
  u32 _a_1 = r0;
  u32 _b_0 = r1;
  WL_SPIN
    if (_a_1 == 0) {
      _v_3 = 0;
    } else {
      _v_3 = _b_0;
    }
  break;
  }
  o[0] = _v_3;
  return 1;
}

INLINE Term spin_9(Env e, THR Term* o, u32 r0, u32 r1, u32 r2, u32 r3, u32 r4, u32 r5, u32 r6, u32 r7, u32 r8, u32 r9, Term r10) {
  u32 wpoll = 0;
  Term _v_5 = 0;
  u32 _inside_0 = r0;
  u32 _x_1 = r1;
  u32 _y_1 = r2;
  u32 _k_7 = r3;
  u32 _k_8 = r4;
  u32 _k_9 = r5;
  u32 _k_10 = r6;
  u32 _k_11 = r7;
  u32 _k_12 = r8;
  u32 _k_13 = r9;
  Term _a_2 = r10;
  WL_SPIN
    if (_inside_0 == 1) {
      Term _v_6 = 0;
      Term _o_1[1];
      if (spin_6(e, _o_1, _x_1, _y_1, _k_7, _k_8, _k_9, _k_10, _k_11, _k_12, _k_13, _a_2) == 0) {
        return 0;
      }
      _v_6 = _o_1[0];
      _v_5 = _v_6;
    } else {
      _v_5 = _a_2;
    }
  break;
  }
  o[0] = _v_5;
  return 1;
}

INLINE Term spin_10(Env e, THR Term* o, Term r0, u32 r1, u32 r2, u32 r3, u32 r4, u32 r5, u32 r6, u32 r7, u32 r8, u32 r9, Term r10) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  Term _n_1 = r0;
  u32 _x_1 = r1;
  u32 _y_1 = r2;
  u32 _k_7 = r3;
  u32 _k_8 = r4;
  u32 _k_9 = r5;
  u32 _k_10 = r6;
  u32 _k_11 = r7;
  u32 _k_12 = r8;
  u32 _k_13 = r9;
  Term _a_1 = r10;
  WL_SPIN
    if (_n_1 == 0) {
      _v_2 = _a_1;
    } else {
      Term _r_1 = (_n_1 - 1);
      Term _v_3 = 0;
      u32 _v_4 = 0;
      u32 _v_5 = 0;
      Term _o_0[1];
      if (spin_8(e, _o_0, U32_BIN(_x_1, <, _k_12), U32_BIN(_y_1, <, _k_13)) == 0) {
        return 0;
      }
      _v_5 = _o_0[0];
      _v_4 = _v_5;
      Term _v_6 = 0;
      Term _o_1[1];
      if (spin_9(e, _o_1, _v_4, _x_1, _y_1, _k_7, _k_8, _k_9, _k_10, _k_11, _k_12, _k_13, _a_1) == 0) {
        return 0;
      }
      _v_6 = _o_1[0];
      _v_3 = _v_6;
      r0 = _r_1;
      r1 = U32_BIN(_x_1, +, 1ull);
      r2 = _y_1;
      r3 = _k_7;
      r4 = _k_8;
      r5 = _k_9;
      r6 = _k_10;
      r7 = _k_11;
      r8 = _k_12;
      r9 = _k_13;
      r10 = _v_3;
      _n_1 = r0;
      _x_1 = r1;
      _y_1 = r2;
      _k_7 = r3;
      _k_8 = r4;
      _k_9 = r5;
      _k_10 = r6;
      _k_11 = r7;
      _k_12 = r8;
      _k_13 = r9;
      _a_1 = r10;
      WL_AGAIN(spin_10);
    }
  break;
  }
  o[0] = _v_2;
  return 1;
}

INLINE Term spin_11(Env e, THR Term* o, u32 r0) {
  u32 wpoll = 0;
  u32 _v_2 = 0;
  u32 _b_0 = r0;
  WL_SPIN
    if (_b_0 == 0) {
      _v_2 = 0ull;
    } else {
      _v_2 = 1ull;
    }
  break;
  }
  o[0] = _v_2;
  return 1;
}

INLINE Term spin_12(Env e, THR Term* o, u32 r0, u32 r1, u32 r2, u32 r3, u32 r4, u32 r5, u32 r6, u32 r7, u32 r8, u32 r9, u32 r10, Term r11) {
  u32 wpoll = 0;
  Term _v_5 = 0;
  u32 _inside_0 = r0;
  u32 _i_1 = r1;
  u32 _x_1 = r2;
  u32 _y_1 = r3;
  u32 _k_7 = r4;
  u32 _k_8 = r5;
  u32 _k_9 = r6;
  u32 _k_10 = r7;
  u32 _k_11 = r8;
  u32 _k_12 = r9;
  u32 _k_13 = r10;
  Term _a_1 = r11;
  WL_SPIN
    if (_inside_0 == 1) {
      u32 _v_6 = 0;
      u32 _v_7 = 0;
      Term _o_1[1];
      if (spin_1(e, _o_1, _x_1, _y_1, _k_7, _k_8, _k_9, _k_10, _k_11, _k_12, _k_13) == 0) {
        return 0;
      }
      _v_7 = _o_1[0];
      _v_6 = _v_7;
      Term _at_0 = blk_loc(e.mem, _a_1);
      Term _at_1 = blk_at(_a_1, _i_1, 0);
      u32 _c_0 = blk_read(e.mem, 0, _at_0, _at_1 + 0);
      blk_write(e.mem, 0, _at_0, _at_1 + 0, _v_6);
      _v_5 = _a_1;
    } else {
      _v_5 = _a_1;
    }
  break;
  }
  o[0] = _v_5;
  return 1;
}

INLINE Term spin_13(Env e, THR Term* o, Term r0, u32 r1, u32 r2, u32 r3, u32 r4, u32 r5, u32 r6, u32 r7, u32 r8, u32 r9, Term r10) {
  u32 wpoll = 0;
  Term _v_1 = 0;
  Term _n_0 = r0;
  u32 _y_1 = r1;
  u32 _x_1 = r2;
  u32 _k_7 = r3;
  u32 _k_8 = r4;
  u32 _k_9 = r5;
  u32 _k_10 = r6;
  u32 _k_11 = r7;
  u32 _k_12 = r8;
  u32 _k_13 = r9;
  Term _a_1 = r10;
  WL_SPIN
    if (_n_0 == 0) {
      _v_1 = _a_1;
    } else {
      Term _r_0 = (_n_0 - 1);
      Term _v_2 = 0;
      Term _v_3 = 0;
      Term _o_0[1];
      if (spin_10(e, _o_0, 16ull, _x_1, _y_1, _k_7, _k_8, _k_9, _k_10, _k_11, _k_12, _k_13, _a_1) == 0) {
        return 0;
      }
      _v_3 = _o_0[0];
      _v_2 = _v_3;
      r0 = _r_0;
      r1 = U32_BIN(_y_1, +, 1ull);
      r2 = _x_1;
      r3 = _k_7;
      r4 = _k_8;
      r5 = _k_9;
      r6 = _k_10;
      r7 = _k_11;
      r8 = _k_12;
      r9 = _k_13;
      r10 = _v_2;
      _n_0 = r0;
      _y_1 = r1;
      _x_1 = r2;
      _k_7 = r3;
      _k_8 = r4;
      _k_9 = r5;
      _k_10 = r6;
      _k_11 = r7;
      _k_12 = r8;
      _k_13 = r9;
      _a_1 = r10;
      WL_AGAIN(spin_13);
    }
  break;
  }
  o[0] = _v_1;
  return 1;
}

INLINE Term spin_14(Env e, THR Term* o, Term r0, u32 r1, u32 r2, u32 r3, u32 r4, u32 r5, u32 r6, u32 r7, u32 r8, u32 r9, u32 r10, u32 r11, u32 r12, Term r13) {
  u32 wpoll = 0;
  Term _v_1 = 0;
  Term _n_0 = r0;
  u32 _i_0 = r1;
  u32 _x_0 = r2;
  u32 _y_0 = r3;
  u32 _d_0 = r4;
  u32 _e_0 = r5;
  u32 _k_7 = r6;
  u32 _k_8 = r7;
  u32 _k_9 = r8;
  u32 _k_10 = r9;
  u32 _k_11 = r10;
  u32 _k_12 = r11;
  u32 _k_13 = r12;
  Term _a_5 = r13;
  WL_SPIN
    if (_n_0 == 0) {
      _v_1 = _a_5;
    } else {
      Term _r_0 = (_n_0 - 1);
      u32 _v_2 = 0;
      u32 _v_3 = 0;
      Term _o_0[1];
      if (spin_11(e, _o_0, U32_BIN(U32_BIN(_x_0, +, _d_0), >=, _k_12)) == 0) {
        return 0;
      }
      _v_3 = _o_0[0];
      _v_2 = _v_3;
      Term _v_4 = 0;
      Term _v_5 = 0;
      Term _o_1[1];
      if (spin_12(e, _o_1, U32_BIN(_i_0, <, U32_BIN(_k_12, *, _k_13)), _i_0, _x_0, _y_0, _k_7, _k_8, _k_9, _k_10, _k_11, _k_12, _k_13, _a_5) == 0) {
        return 0;
      }
      _v_5 = _o_1[0];
      _v_4 = _v_5;
      r0 = _r_0;
      r1 = U32_BIN(_i_0, +, 32768ull);
      r2 = U32_BIN(U32_BIN(_x_0, +, _d_0), -, U32_BIN(_v_2, *, _k_12));
      r3 = U32_BIN(U32_BIN(_y_0, +, _e_0), +, _v_2);
      r4 = _d_0;
      r5 = _e_0;
      r6 = _k_7;
      r7 = _k_8;
      r8 = _k_9;
      r9 = _k_10;
      r10 = _k_11;
      r11 = _k_12;
      r12 = _k_13;
      r13 = _v_4;
      _n_0 = r0;
      _i_0 = r1;
      _x_0 = r2;
      _y_0 = r3;
      _d_0 = r4;
      _e_0 = r5;
      _k_7 = r6;
      _k_8 = r7;
      _k_9 = r8;
      _k_10 = r9;
      _k_11 = r10;
      _k_12 = r11;
      _k_13 = r12;
      _a_5 = r13;
      WL_AGAIN(spin_14);
    }
  break;
  }
  o[0] = _v_1;
  return 1;
}

INLINE Term spin_15(Env e, THR Term* o, u32 r0, u32 r1, u32 r2, u32 r3, u32 r4, u32 r5, u32 r6, u32 r7, Term r8) {
  u32 wpoll = 0;
  Term _v_2 = 0;
  u32 _j_0 = r0;
  u32 _k_0 = r1;
  u32 _k_1 = r2;
  u32 _k_2 = r3;
  u32 _k_3 = r4;
  u32 _k_4 = r5;
  u32 _k_5 = r6;
  u32 _k_6 = r7;
  Term _a_1 = r8;
  WL_SPIN
    Term _a_2 = U32_BIN(U32_BIN(_k_5, *, _k_6), +, 32767ull);
    Term _a_3 = 32768ull;
    Term _a_4 = 32768ull;
    Term _a_5 = 32768ull;
    Term _v_3 = 0;
    Term _o_0[1];
    if (spin_14(e, _o_0, ((u32)(_a_3) == 0 ? 0 : (u64)U32_QUO((u32)(_a_2), (u32)(_a_3))), _j_0, ((u32)(_k_5) == 0 ? _j_0 : U32_BIN(_j_0, -, U32_QUO((u32)(_j_0), (u32)(_k_5)) * _k_5)), ((u32)(_k_5) == 0 ? 0 : (u64)U32_QUO((u32)(_j_0), (u32)(_k_5))), ((u32)(_k_5) == 0 ? _a_4 : U32_BIN(_a_4, -, U32_QUO((u32)(_a_4), (u32)(_k_5)) * _k_5)), ((u32)(_k_5) == 0 ? 0 : (u64)U32_QUO((u32)(_a_5), (u32)(_k_5))), _k_0, _k_1, _k_2, _k_3, _k_4, _k_5, _k_6, _a_1) == 0) {
      return 0;
    }
    _v_3 = _o_0[0];
    _v_2 = _v_3;
  break;
  }
  o[0] = _v_2;
  return 1;
}

// Work
// ====

// A host self-jump is a tail call: as a loop, clang hoisted constants into
// symreg's entry (3.05 s against 2.51 s).
#if !DEVICE
#undef  WL_SPIN
#undef  WL_SPUN
#undef  WL_AGAIN
#define WL_SPIN
#define WL_SPUN
#define WL_AGAIN(F) __attribute__((musttail)) return WL_##F(WL_ALL)

typedef Term (PRESERVE(preserve_none) *WlFn)(WL_SIG);
#define WL_X(F) WL_FN WL_##F(WL_SIG);
WL_TABLE WL_X(FID_ENTER)
#undef WL_X
#define WL_X(F) WL_##F,
static const WlFn wl_tab[] = { WL_TABLE };
#undef WL_X
#endif

static Term work_loop(Env e, DEV Term* sp, Term t, u32 seq) {
  WL_BANK
  u32 rn = 0;
  r0 = t;
#if DEVICE
  u32 fid   = FID_ENTER;
  u32 wpoll = 0;
  for (;;) {
  if (err_spun(e.mem, &wpoll)) {
    return 0;
  }
  switch (fid) {
#else
  return WL_FID_ENTER(WL_ALL);
}
#endif

// Segments
// ========

// A task enters through its words: a continuation's results ride r0..
// and its parameters the stack; any other segment's parameters ride r0..

  WL_CASE(FID_ARRAY_SPREAD_RUN_0)
  {
    Term _c_0 = r0;
    Term _a_0 = r1;
    u32 _i_0 = r2;
    u32 _st_0 = r3;
    u32 _x_0 = r4;
    u32 _x_1 = r5;
    u32 _x_2 = r6;
    u32 _x_3 = r7;
    u32 _x_4 = r8;
    u32 _x_5 = r9;
    u32 _x_6 = r10;
    WL_OPEN
    WL_SPIN
    if (_c_0 == 0) {
      r0 = _a_0;
      WL_RETN(1);
    } else {
      Term _r_0 = (_c_0 - 1);
      Term _v_0 = 0;
      Term _v_1 = 0;
      Term _o_1[1];
      if (spin_15(e, _o_1, _i_0, _x_0, _x_1, _x_2, _x_3, _x_4, _x_5, _x_6, _a_0) == 0) {
        return 0;
      }
      _v_1 = _o_1[0];
      _v_0 = _v_1;
      r0 = _r_0;
      r1 = _v_0;
      r2 = U32_BIN(_i_0, +, _st_0);
      r3 = _st_0;
      r4 = _x_0;
      r5 = _x_1;
      r6 = _x_2;
      r7 = _x_3;
      r8 = _x_4;
      r9 = _x_5;
      r10 = _x_6;
      _c_0 = r0;
      _a_0 = r1;
      _i_0 = r2;
      _st_0 = r3;
      _x_0 = r4;
      _x_1 = r5;
      _x_2 = r6;
      _x_3 = r7;
      _x_4 = r8;
      _x_5 = r9;
      _x_6 = r10;
      WL_AGAIN(FID_ARRAY_SPREAD_RUN_0);
    }
    WL_SPUN
  }}

#if !DEVICE
  WL_CASE(FID_VIZ_FILL)
  {
    Term _d_0 = r0;
    u32 _x_0 = r1;
    u32 _y_0 = r2;
    u32 _k_0 = r3;
    u32 _k_1 = r4;
    u32 _k_2 = r5;
    u32 _k_3 = r6;
    u32 _k_4 = r7;
    u32 _k_5 = r8;
    u32 _k_6 = r9;
    Term _a_0 = r10;
    WL_OPEN
    WL_SPIN
    u32 _v_0 = 0;
    u32 _v_1 = 0;
    Term _o_0[1];
    if (spin_8(e, _o_0, U32_BIN(_x_0, <, _k_5), U32_BIN(_y_0, <, _k_6)) == 0) {
      return 0;
    }
    _v_1 = _o_0[0];
    _v_0 = _v_1;
    if (_v_0 == 0) {
      r0 = _a_0;
      WL_RETN(1);
    } else {
      if (_d_0 == 0) {
        Term _v_2 = 0;
        Term _o_1[1];
        if (spin_13(e, _o_1, 16ull, _y_0, _x_0, _k_0, _k_1, _k_2, _k_3, _k_4, _k_5, _k_6, _a_0) == 0) {
          return 0;
        }
        _v_2 = _o_1[0];
        r0 = _v_2;
        WL_RETN(1);
      } else {
        Term _e_0 = (_d_0 - 1);
        Term _v_3 = 0;
        Term _v_4 = 0;
        Term _v_5 = 0;
        Term _v_6 = 0;
        Term _o_2[2];
        if (spin_7(e, _o_2, _a_0) == 0) {
          return 0;
        }
        _v_5 = _o_2[0];
        _v_6 = _o_2[1];
        _v_3 = _v_5;
        _v_4 = _v_6;
        Term _v_7 = 0;
        Term _v_8 = 0;
        Term _v_9 = 0;
        Term _v_10 = 0;
        Term _o_3[2];
        if (spin_7(e, _o_3, _v_4) == 0) {
          return 0;
        }
        _v_9 = _o_3[0];
        _v_10 = _o_3[1];
        _v_7 = _v_9;
        _v_8 = _v_10;
        Term _v_11 = 0;
        Term _v_12 = 0;
        Term _v_13 = 0;
        Term _v_14 = 0;
        Term _o_4[2];
        if (spin_7(e, _o_4, _v_8) == 0) {
          return 0;
        }
        _v_13 = _o_4[0];
        _v_14 = _o_4[1];
        _v_11 = _v_13;
        _v_12 = _v_14;
        Term _a_1 = 16ull;
        u32 _h_0 = (_e_0 >= 32 ? 0 : U32_BIN(_a_1, <<, _e_0));
        u32 _x1_0 = U32_BIN(_x_0, +, _h_0);
        u32 _y1_0 = U32_BIN(_y_0, +, _h_0);
        if (!seq) {
          u64 _t_0 = task_node(e, FID_VIZ_FILL_J48, WL_CONT, WL_IDX, 4);
          u64 _t_1 = task_node(e, FID_VIZ_FILL, term_tsk(FID_VIZ_FILL_J48, _t_0), 0, 0);
          e.mem[_t_1 + 0] = _e_0;
          e.mem[_t_1 + 1] = _x_0;
          e.mem[_t_1 + 2] = _y_0;
          e.mem[_t_1 + 3] = _k_0;
          e.mem[_t_1 + 4] = _k_1;
          e.mem[_t_1 + 5] = _k_2;
          e.mem[_t_1 + 6] = _k_3;
          e.mem[_t_1 + 7] = _k_4;
          e.mem[_t_1 + 8] = _k_5;
          e.mem[_t_1 + 9] = _k_6;
          e.mem[_t_1 + 10] = _v_3;
          e.mem[_t_0 + 0] = term_tsk(FID_VIZ_FILL, _t_1);
          u64 _t_2 = task_node(e, FID_VIZ_FILL, term_tsk(FID_VIZ_FILL_J48, _t_0), 1, 0);
          e.mem[_t_2 + 0] = _e_0;
          e.mem[_t_2 + 1] = _x1_0;
          e.mem[_t_2 + 2] = _y_0;
          e.mem[_t_2 + 3] = _k_0;
          e.mem[_t_2 + 4] = _k_1;
          e.mem[_t_2 + 5] = _k_2;
          e.mem[_t_2 + 6] = _k_3;
          e.mem[_t_2 + 7] = _k_4;
          e.mem[_t_2 + 8] = _k_5;
          e.mem[_t_2 + 9] = _k_6;
          e.mem[_t_2 + 10] = _v_7;
          e.mem[_t_0 + 1] = term_tsk(FID_VIZ_FILL, _t_2);
          u64 _t_3 = task_node(e, FID_VIZ_FILL, term_tsk(FID_VIZ_FILL_J48, _t_0), 2, 0);
          e.mem[_t_3 + 0] = _e_0;
          e.mem[_t_3 + 1] = _x_0;
          e.mem[_t_3 + 2] = _y1_0;
          e.mem[_t_3 + 3] = _k_0;
          e.mem[_t_3 + 4] = _k_1;
          e.mem[_t_3 + 5] = _k_2;
          e.mem[_t_3 + 6] = _k_3;
          e.mem[_t_3 + 7] = _k_4;
          e.mem[_t_3 + 8] = _k_5;
          e.mem[_t_3 + 9] = _k_6;
          e.mem[_t_3 + 10] = _v_11;
          e.mem[_t_0 + 2] = term_tsk(FID_VIZ_FILL, _t_3);
          u64 _t_4 = task_node(e, FID_VIZ_FILL, term_tsk(FID_VIZ_FILL_J48, _t_0), 3, 0);
          e.mem[_t_4 + 0] = _e_0;
          e.mem[_t_4 + 1] = _x1_0;
          e.mem[_t_4 + 2] = _y1_0;
          e.mem[_t_4 + 3] = _k_0;
          e.mem[_t_4 + 4] = _k_1;
          e.mem[_t_4 + 5] = _k_2;
          e.mem[_t_4 + 6] = _k_3;
          e.mem[_t_4 + 7] = _k_4;
          e.mem[_t_4 + 8] = _k_5;
          e.mem[_t_4 + 9] = _k_6;
          e.mem[_t_4 + 10] = _v_12;
          e.mem[_t_0 + 3] = term_tsk(FID_VIZ_FILL, _t_4);
          return term_tsk(FID_VIZ_FILL_J48, _t_0);
        }
        WL_ROOM(16);
        STK(0) = _e_0;
        STK(1) = _x_0;
        STK(2) = _y_0;
        STK(3) = _k_0;
        STK(4) = _k_1;
        STK(5) = _k_2;
        STK(6) = _k_3;
        STK(7) = _k_4;
        STK(8) = _k_5;
        STK(9) = _k_6;
        STK(10) = _v_7;
        STK(11) = _v_11;
        STK(12) = _v_12;
        STK(13) = _x1_0;
        STK(14) = _y1_0;
        STK(15) = FID_VIZ_FILL_K48;
        WL_PUSHN(16);
        r0 = _e_0;
        r1 = _x_0;
        r2 = _y_0;
        r3 = _k_0;
        r4 = _k_1;
        r5 = _k_2;
        r6 = _k_3;
        r7 = _k_4;
        r8 = _k_5;
        r9 = _k_6;
        r10 = _v_3;
        _d_0 = r0;
        _x_0 = r1;
        _y_0 = r2;
        _k_0 = r3;
        _k_1 = r4;
        _k_2 = r5;
        _k_3 = r6;
        _k_4 = r7;
        _k_5 = r8;
        _k_6 = r9;
        _a_0 = r10;
        WL_AGAIN(FID_VIZ_FILL);
      }
    }
    WL_SPUN
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_FILL_K48)
  {
    Term _e_1 = STK(-15);
    u32 _x_1 = STK(-14);
    u32 _y_1 = STK(-13);
    u32 _k_7 = STK(-12);
    u32 _k_8 = STK(-11);
    u32 _k_9 = STK(-10);
    u32 _k_10 = STK(-9);
    u32 _k_11 = STK(-8);
    u32 _k_12 = STK(-7);
    u32 _k_13 = STK(-6);
    Term _v_15 = STK(-5);
    Term _v_16 = STK(-4);
    Term _v_17 = STK(-3);
    u32 _x1_1 = STK(-2);
    u32 _y1_1 = STK(-1);
    Term _q_0 = r0;
    WL_OPEN
    WL_ROOM(2);
    STK(0) = _q_0;
    STK(1) = FID_VIZ_FILL_K49;
    WL_PUSHN(2);
    if (!DEVICE && !seq && fid_nofk(FID_VIZ_FILL)) {
      u64 _t_5 = task_node(e, FID_VIZ_FILL, WL_CONT, WL_IDX, 0);
      e.mem[_t_5 + 0] = _e_1;
      e.mem[_t_5 + 1] = _x1_1;
      e.mem[_t_5 + 2] = _y_1;
      e.mem[_t_5 + 3] = _k_7;
      e.mem[_t_5 + 4] = _k_8;
      e.mem[_t_5 + 5] = _k_9;
      e.mem[_t_5 + 6] = _k_10;
      e.mem[_t_5 + 7] = _k_11;
      e.mem[_t_5 + 8] = _k_12;
      e.mem[_t_5 + 9] = _k_13;
      e.mem[_t_5 + 10] = _v_15;
      return term_tsk(FID_VIZ_FILL, _t_5);
    }
    r0 = _e_1;
    r1 = _x1_1;
    r2 = _y_1;
    r3 = _k_7;
    r4 = _k_8;
    r5 = _k_9;
    r6 = _k_10;
    r7 = _k_11;
    r8 = _k_12;
    r9 = _k_13;
    r10 = _v_15;
    WL_JMP(FID_VIZ_FILL);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_FILL_K49)
  {
    Term _e_2 = STK(-16);
    u32 _x_2 = STK(-15);
    u32 _k_14 = STK(-13);
    u32 _k_15 = STK(-12);
    u32 _k_16 = STK(-11);
    u32 _k_17 = STK(-10);
    u32 _k_18 = STK(-9);
    u32 _k_19 = STK(-8);
    u32 _k_20 = STK(-7);
    Term _v_18 = STK(-5);
    Term _v_19 = STK(-4);
    u32 _x1_2 = STK(-3);
    u32 _y1_2 = STK(-2);
    Term _q_1 = STK(-1);
    Term _w_0 = r0;
    WL_OPEN
    WL_ROOM(2);
    STK(0) = _w_0;
    STK(1) = FID_VIZ_FILL_K50;
    WL_PUSHN(2);
    if (!DEVICE && !seq && fid_nofk(FID_VIZ_FILL)) {
      u64 _t_6 = task_node(e, FID_VIZ_FILL, WL_CONT, WL_IDX, 0);
      e.mem[_t_6 + 0] = _e_2;
      e.mem[_t_6 + 1] = _x_2;
      e.mem[_t_6 + 2] = _y1_2;
      e.mem[_t_6 + 3] = _k_14;
      e.mem[_t_6 + 4] = _k_15;
      e.mem[_t_6 + 5] = _k_16;
      e.mem[_t_6 + 6] = _k_17;
      e.mem[_t_6 + 7] = _k_18;
      e.mem[_t_6 + 8] = _k_19;
      e.mem[_t_6 + 9] = _k_20;
      e.mem[_t_6 + 10] = _v_18;
      return term_tsk(FID_VIZ_FILL, _t_6);
    }
    r0 = _e_2;
    r1 = _x_2;
    r2 = _y1_2;
    r3 = _k_14;
    r4 = _k_15;
    r5 = _k_16;
    r6 = _k_17;
    r7 = _k_18;
    r8 = _k_19;
    r9 = _k_20;
    r10 = _v_18;
    WL_JMP(FID_VIZ_FILL);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_FILL_K50)
  {
    Term _e_3 = STK(-17);
    u32 _k_21 = STK(-14);
    u32 _k_22 = STK(-13);
    u32 _k_23 = STK(-12);
    u32 _k_24 = STK(-11);
    u32 _k_25 = STK(-10);
    u32 _k_26 = STK(-9);
    u32 _k_27 = STK(-8);
    Term _v_20 = STK(-5);
    u32 _x1_3 = STK(-4);
    u32 _y1_3 = STK(-3);
    Term _q_2 = STK(-2);
    Term _w_1 = STK(-1);
    Term _r_0 = r0;
    WL_OPEN
    WL_ROOM(2);
    STK(0) = _r_0;
    STK(1) = FID_VIZ_FILL_K51;
    WL_PUSHN(2);
    if (!DEVICE && !seq && fid_nofk(FID_VIZ_FILL)) {
      u64 _t_7 = task_node(e, FID_VIZ_FILL, WL_CONT, WL_IDX, 0);
      e.mem[_t_7 + 0] = _e_3;
      e.mem[_t_7 + 1] = _x1_3;
      e.mem[_t_7 + 2] = _y1_3;
      e.mem[_t_7 + 3] = _k_21;
      e.mem[_t_7 + 4] = _k_22;
      e.mem[_t_7 + 5] = _k_23;
      e.mem[_t_7 + 6] = _k_24;
      e.mem[_t_7 + 7] = _k_25;
      e.mem[_t_7 + 8] = _k_26;
      e.mem[_t_7 + 9] = _k_27;
      e.mem[_t_7 + 10] = _v_20;
      return term_tsk(FID_VIZ_FILL, _t_7);
    }
    r0 = _e_3;
    r1 = _x1_3;
    r2 = _y1_3;
    r3 = _k_21;
    r4 = _k_22;
    r5 = _k_23;
    r6 = _k_24;
    r7 = _k_25;
    r8 = _k_26;
    r9 = _k_27;
    r10 = _v_20;
    WL_JMP(FID_VIZ_FILL);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_FILL_K51)
  {
    WL_POPN(18);
    Term _q_3 = STK(15);
    Term _w_2 = STK(16);
    Term _r_1 = STK(17);
    Term _z_0 = r0;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_VIZ_FILL_J48)) {
      u64 _t_8 = task_node(e, FID_VIZ_FILL_J48, WL_CONT, WL_IDX, 0);
      e.mem[_t_8 + 0] = _q_3;
      e.mem[_t_8 + 1] = _w_2;
      e.mem[_t_8 + 2] = _r_1;
      e.mem[_t_8 + 3] = _z_0;
      return term_tsk(FID_VIZ_FILL_J48, _t_8);
    }
    r0 = _q_3;
    r1 = _w_2;
    r2 = _r_1;
    r3 = _z_0;
    WL_JMP(FID_VIZ_FILL_J48);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_FILL_J48)
  {
    Term _q_4 = r0;
    Term _w_3 = r1;
    Term _r_2 = r2;
    Term _z_1 = r3;
    WL_OPEN
    Term _v_21 = 0;
    Term _v_22 = 0;
    Term _o_5[1];
    if (spin_5(e, _o_5, _q_4, _w_3) == 0) {
      return 0;
    }
    _v_22 = _o_5[0];
    _v_21 = _v_22;
    Term _v_23 = 0;
    Term _v_24 = 0;
    Term _o_6[1];
    if (spin_5(e, _o_6, _r_2, _z_1) == 0) {
      return 0;
    }
    _v_24 = _o_6[0];
    _v_23 = _v_24;
    Term _v_25 = 0;
    Term _o_7[1];
    if (spin_5(e, _o_7, _v_21, _v_23) == 0) {
      return 0;
    }
    _v_25 = _o_7[0];
    r0 = _v_25;
    WL_RETN(1);
  }}
#endif

  WL_CASE(FID_VIZ_FRAME_GPU)
  {
    u32 _k_0 = r0;
    u32 _k_1 = r1;
    u32 _k_2 = r2;
    u32 _k_3 = r3;
    u32 _k_4 = r4;
    u32 _k_5 = r5;
    u32 _k_6 = r6;
    Term _a_0 = r7;
    WL_OPEN
    u32 _n_0 = 32768ull;
    if (!seq) {
      u64 _t_0 = task_node(e, FID_VIZ_FRAME_GPU_J58, WL_CONT, WL_IDX, 1);
      u64 _t_1 = task_node(e, FID_ARRAY_SPREAD_RUN_0, term_tsk(FID_VIZ_FRAME_GPU_J58, _t_0), 0, 0);
      e.mem[_t_1 + 0] = _n_0;
      e.mem[_t_1 + 1] = _a_0;
      e.mem[_t_1 + 2] = 0ull;
      e.mem[_t_1 + 3] = 1ull;
      e.mem[_t_1 + 4] = _k_0;
      e.mem[_t_1 + 5] = _k_1;
      e.mem[_t_1 + 6] = _k_2;
      e.mem[_t_1 + 7] = _k_3;
      e.mem[_t_1 + 8] = _k_4;
      e.mem[_t_1 + 9] = _k_5;
      e.mem[_t_1 + 10] = _k_6;
      e.mem[_t_0 + 0] = term_tsk(FID_ARRAY_SPREAD_RUN_0, _t_1);
      return term_tsk(FID_VIZ_FRAME_GPU_J58, _t_0);
    }
    WL_ROOM(1);
    STK(0) = FID_VIZ_FRAME_GPU_K58;
    WL_PUSHN(1);
    if (!DEVICE && !seq && fid_nofk(FID_ARRAY_SPREAD_RUN_0)) {
      u64 _t_2 = task_node(e, FID_ARRAY_SPREAD_RUN_0, WL_CONT, WL_IDX, 0);
      e.mem[_t_2 + 0] = _n_0;
      e.mem[_t_2 + 1] = _a_0;
      e.mem[_t_2 + 2] = 0ull;
      e.mem[_t_2 + 3] = 1ull;
      e.mem[_t_2 + 4] = _k_0;
      e.mem[_t_2 + 5] = _k_1;
      e.mem[_t_2 + 6] = _k_2;
      e.mem[_t_2 + 7] = _k_3;
      e.mem[_t_2 + 8] = _k_4;
      e.mem[_t_2 + 9] = _k_5;
      e.mem[_t_2 + 10] = _k_6;
      return term_tsk(FID_ARRAY_SPREAD_RUN_0, _t_2);
    }
    r0 = _n_0;
    r1 = _a_0;
    r2 = 0ull;
    r3 = 1ull;
    r4 = _k_0;
    r5 = _k_1;
    r6 = _k_2;
    r7 = _k_3;
    r8 = _k_4;
    r9 = _k_5;
    r10 = _k_6;
    WL_JMP(FID_ARRAY_SPREAD_RUN_0);
  }}

  WL_CASE(FID_VIZ_FRAME_GPU_K58)
  {
    Term _r_0 = r0;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_VIZ_FRAME_GPU_J58)) {
      u64 _t_3 = task_node(e, FID_VIZ_FRAME_GPU_J58, WL_CONT, WL_IDX, 0);
      e.mem[_t_3 + 0] = _r_0;
      return term_tsk(FID_VIZ_FRAME_GPU_J58, _t_3);
    }
    r0 = _r_0;
    WL_JMP(FID_VIZ_FRAME_GPU_J58);
  }}

  WL_CASE(FID_VIZ_FRAME_GPU_J58)
  {
    Term _r_1 = r0;
    WL_OPEN
    r0 = _r_1;
    WL_RETN(1);
  }}

#if !DEVICE
  WL_CASE(FID_VIZ_DRAW)
  {
    u32 _gpu_0 = r0;
    u32 _k_0 = r1;
    u32 _k_1 = r2;
    u32 _k_2 = r3;
    u32 _k_3 = r4;
    u32 _k_4 = r5;
    u32 _k_5 = r6;
    u32 _k_6 = r7;
    Term _a_0 = r8;
    WL_OPEN
    if (_gpu_0 == 1) {
      if (!seq) {
        u64 _t_0 = task_node(e, FID_VIZ_FRAME_GPU, WL_CONT, WL_IDX, 0);
        e.mem[_t_0 + 0] = _k_0;
        e.mem[_t_0 + 1] = _k_1;
        e.mem[_t_0 + 2] = _k_2;
        e.mem[_t_0 + 3] = _k_3;
        e.mem[_t_0 + 4] = _k_4;
        e.mem[_t_0 + 5] = _k_5;
        e.mem[_t_0 + 6] = _k_6;
        e.mem[_t_0 + 7] = _a_0;
        return term_tsk(FID_VIZ_FRAME_GPU, _t_0);
      }
      r0 = _k_0;
      r1 = _k_1;
      r2 = _k_2;
      r3 = _k_3;
      r4 = _k_4;
      r5 = _k_5;
      r6 = _k_6;
      r7 = _a_0;
      WL_JMP(FID_VIZ_FRAME_GPU);
    } else {
      if (!DEVICE && !seq && fid_nofk(FID_VIZ_FILL)) {
        u64 _t_1 = task_node(e, FID_VIZ_FILL, WL_CONT, WL_IDX, 0);
        e.mem[_t_1 + 0] = 8ull;
        e.mem[_t_1 + 1] = 0ull;
        e.mem[_t_1 + 2] = 0ull;
        e.mem[_t_1 + 3] = _k_0;
        e.mem[_t_1 + 4] = _k_1;
        e.mem[_t_1 + 5] = _k_2;
        e.mem[_t_1 + 6] = _k_3;
        e.mem[_t_1 + 7] = _k_4;
        e.mem[_t_1 + 8] = _k_5;
        e.mem[_t_1 + 9] = _k_6;
        e.mem[_t_1 + 10] = _a_0;
        return term_tsk(FID_VIZ_FILL, _t_1);
      }
      r0 = 8ull;
      r1 = 0ull;
      r2 = 0ull;
      r3 = _k_0;
      r4 = _k_1;
      r5 = _k_2;
      r6 = _k_3;
      r7 = _k_4;
      r8 = _k_5;
      r9 = _k_6;
      r10 = _a_0;
      WL_JMP(FID_VIZ_FILL);
    }
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_STEP)
  {
    u32 _ask_0 = r0;
    u32 _t_0 = r1;
    u32 _bass_0 = r2;
    u32 _mid_0 = r3;
    u32 _hue_0 = r4;
    u32 _beat_0 = r5;
    Term _a_0 = r6;
    WL_OPEN
    Term _a_1 = 13ull;
    u32 _k_0 = f32_rewrap(f32_unbox(1027101164ull) * f32_unbox(f32_rewrap(f32_unbox(1065353216ull) + f32_unbox(f32_rewrap(f32_unbox(_bass_0) * f32_unbox(1056964608ull))))));
    u32 _k_1 = U32_BIN(_ask_0, &, 8191ull);
    u32 _k_2 = U32_BIN((_a_1 >= 32 ? 0 : U32_BIN(_ask_0, >>, _a_1)), &, 8191ull);
    Term _a_2 = 31ull;
    if (seq) {
      WL_ROOM(1);
      STK(0) = FID_VIZ_STEP_K62;
      WL_PUSHN(1);
    } else {
      u64 _t_1 = task_node(e, FID_VIZ_STEP_K62, WL_CONT, WL_IDX, 1);
      WL_CONT = term_tsk(FID_VIZ_STEP_K62, _t_1);
      WL_IDX = 0;
    }
    if (!DEVICE && !seq && fid_nofk(FID_VIZ_DRAW)) {
      u64 _t_2 = task_node(e, FID_VIZ_DRAW, WL_CONT, WL_IDX, 0);
      e.mem[_t_2 + 0] = U32_BIN((_a_2 >= 32 ? 0 : U32_BIN(_ask_0, >>, _a_2)), ==, 1ull);
      e.mem[_t_2 + 1] = _t_0;
      e.mem[_t_2 + 2] = _k_0;
      e.mem[_t_2 + 3] = _mid_0;
      e.mem[_t_2 + 4] = _hue_0;
      e.mem[_t_2 + 5] = _beat_0;
      e.mem[_t_2 + 6] = _k_1;
      e.mem[_t_2 + 7] = _k_2;
      e.mem[_t_2 + 8] = _a_0;
      return term_tsk(FID_VIZ_DRAW, _t_2);
    }
    r0 = U32_BIN((_a_2 >= 32 ? 0 : U32_BIN(_ask_0, >>, _a_2)), ==, 1ull);
    r1 = _t_0;
    r2 = _k_0;
    r3 = _mid_0;
    r4 = _hue_0;
    r5 = _beat_0;
    r6 = _k_1;
    r7 = _k_2;
    r8 = _a_0;
    WL_JMP(FID_VIZ_DRAW);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_STEP_K62)
  {
    Term _h_0 = r0;
    WL_OPEN
    u64 _nd_0 = heap_alloc(e, cls_fit(1));
    e.mem[_nd_0 + 0] = _h_0;
    r0 = term_clo(FID_VIZ_SHOW, _nd_0);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_BIND)
  {
    Term _m_0 = r0;
    Term _f_0 = r1;
    Term _k_0 = r2;
    WL_OPEN
    u64 _nd_0 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_0 + 0] = _f_0;
    e.mem[_nd_0 + 1] = _k_0;
    if (!DEVICE && !seq && fid_nofk(FID_CLO_APPLY)) {
      u64 _t_3 = task_node(e, FID_CLO_APPLY, WL_CONT, WL_IDX, 0);
      e.mem[_t_3 + 0] = _m_0;
      e.mem[_t_3 + 1] = term_clo(FID_IO_BIND_C64, _nd_0);
      return term_tsk(FID_CLO_APPLY, _t_3);
    }
    r0 = _m_0;
    r1 = term_clo(FID_IO_BIND_C64, _nd_0);
    WL_JMP(FID_CLO_APPLY);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_BIND_C64)
  {
    Term _f_1 = r0;
    Term _k_1 = r1;
    Term _x_0 = r2;
    WL_OPEN
    if (seq) {
      WL_ROOM(2);
      STK(0) = _k_1;
      STK(1) = FID_IO_BIND_K65;
      WL_PUSHN(2);
    } else {
      u64 _t_0 = task_node(e, FID_IO_BIND_K65, WL_CONT, WL_IDX, 1);
      e.mem[_t_0 + 0] = _k_1;
      WL_CONT = term_tsk(FID_IO_BIND_K65, _t_0);
      WL_IDX = 1;
    }
    if (!DEVICE && !seq && fid_nofk(FID_CLO_APPLY)) {
      u64 _t_1 = task_node(e, FID_CLO_APPLY, WL_CONT, WL_IDX, 0);
      e.mem[_t_1 + 0] = _f_1;
      e.mem[_t_1 + 1] = _x_0;
      return term_tsk(FID_CLO_APPLY, _t_1);
    }
    r0 = _f_1;
    r1 = _x_0;
    WL_JMP(FID_CLO_APPLY);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_IO_BIND_K65)
  {
    WL_POPN(1);
    Term _k_2 = STK(0);
    Term _h_0 = r0;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_CLO_APPLY)) {
      u64 _t_2 = task_node(e, FID_CLO_APPLY, WL_CONT, WL_IDX, 0);
      e.mem[_t_2 + 0] = _h_0;
      e.mem[_t_2 + 1] = _k_2;
      return term_tsk(FID_CLO_APPLY, _t_2);
    }
    r0 = _h_0;
    r1 = _k_2;
    WL_JMP(FID_CLO_APPLY);
  }}
#endif

  WL_CASE(FID_VIZ_BLANK)
  {
    u32 _z_0 = r0;
    WL_OPEN
    Term _fv_0[1];
    _fv_0[0] = _z_0;
    r0 = blk_new(e, 0, 23ull, 0, 1, _fv_0);
    WL_RETN(1);
  }}

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP)
  {
    Term _a_0 = r0;
    Term _other_0 = r1;
    WL_OPEN
    u64 _nd_0 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_0 + 0] = _a_0;
    e.mem[_nd_0 + 1] = _other_0;
    r0 = term_clo(FID_VIZ_LOOP_C68, _nd_0);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C68)
  {
    Term _a_1 = r0;
    Term _other_1 = r1;
    Term _x_0 = r2;
    WL_OPEN
    u64 _nd_1 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_1 + 0] = _a_1;
    e.mem[_nd_1 + 1] = _other_1;
    if (!DEVICE && !seq && fid_nofk(FID_IO_BIND)) {
      u64 _t_9 = task_node(e, FID_IO_BIND, WL_CONT, WL_IDX, 0);
      e.mem[_t_9 + 0] = term_clo(FID_VIZ_NEXT, 0);
      e.mem[_t_9 + 1] = term_clo(FID_VIZ_LOOP_C69, _nd_1);
      e.mem[_t_9 + 2] = _x_0;
      return term_tsk(FID_IO_BIND, _t_9);
    }
    r0 = term_clo(FID_VIZ_NEXT, 0);
    r1 = term_clo(FID_VIZ_LOOP_C69, _nd_1);
    r2 = _x_0;
    WL_JMP(FID_IO_BIND);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C69)
  {
    Term _a_2 = r0;
    Term _other_2 = r1;
    Term _x_1 = r2;
    WL_OPEN
    u64 _nd_2 = heap_alloc(e, cls_fit(3));
    e.mem[_nd_2 + 0] = _a_2;
    e.mem[_nd_2 + 1] = _other_2;
    e.mem[_nd_2 + 2] = _x_1;
    r0 = term_clo(FID_VIZ_LOOP_C70, _nd_2);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C70)
  {
    Term _a_3 = r0;
    Term _other_3 = r1;
    u32 _x_3 = r2;
    Term _x_2 = r3;
    WL_OPEN
    u64 _nd_3 = heap_alloc(e, cls_fit(1));
    e.mem[_nd_3 + 0] = 0ull;
    u64 _nd_4 = heap_alloc(e, cls_fit(3));
    e.mem[_nd_4 + 0] = _a_3;
    e.mem[_nd_4 + 1] = _other_3;
    e.mem[_nd_4 + 2] = _x_3;
    if (!DEVICE && !seq && fid_nofk(FID_IO_BIND)) {
      u64 _t_8 = task_node(e, FID_IO_BIND, WL_CONT, WL_IDX, 0);
      e.mem[_t_8 + 0] = term_clo(FID_VIZ_PARAM, _nd_3);
      e.mem[_t_8 + 1] = term_clo(FID_VIZ_LOOP_C71, _nd_4);
      e.mem[_t_8 + 2] = _x_2;
      return term_tsk(FID_IO_BIND, _t_8);
    }
    r0 = term_clo(FID_VIZ_PARAM, _nd_3);
    r1 = term_clo(FID_VIZ_LOOP_C71, _nd_4);
    r2 = _x_2;
    WL_JMP(FID_IO_BIND);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C71)
  {
    Term _a_4 = r0;
    Term _other_4 = r1;
    u32 _x_5 = r2;
    Term _x_4 = r3;
    WL_OPEN
    u64 _nd_5 = heap_alloc(e, cls_fit(4));
    e.mem[_nd_5 + 0] = _a_4;
    e.mem[_nd_5 + 1] = _other_4;
    e.mem[_nd_5 + 2] = _x_5;
    e.mem[_nd_5 + 3] = _x_4;
    r0 = term_clo(FID_VIZ_LOOP_C72, _nd_5);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C72)
  {
    Term _a_5 = r0;
    Term _other_5 = r1;
    u32 _x_7 = r2;
    u32 _x_8 = r3;
    Term _x_6 = r4;
    WL_OPEN
    u64 _nd_6 = heap_alloc(e, cls_fit(1));
    e.mem[_nd_6 + 0] = 1ull;
    u64 _nd_7 = heap_alloc(e, cls_fit(4));
    e.mem[_nd_7 + 0] = _a_5;
    e.mem[_nd_7 + 1] = _other_5;
    e.mem[_nd_7 + 2] = _x_7;
    e.mem[_nd_7 + 3] = _x_8;
    if (!DEVICE && !seq && fid_nofk(FID_IO_BIND)) {
      u64 _t_7 = task_node(e, FID_IO_BIND, WL_CONT, WL_IDX, 0);
      e.mem[_t_7 + 0] = term_clo(FID_VIZ_PARAM, _nd_6);
      e.mem[_t_7 + 1] = term_clo(FID_VIZ_LOOP_C73, _nd_7);
      e.mem[_t_7 + 2] = _x_6;
      return term_tsk(FID_IO_BIND, _t_7);
    }
    r0 = term_clo(FID_VIZ_PARAM, _nd_6);
    r1 = term_clo(FID_VIZ_LOOP_C73, _nd_7);
    r2 = _x_6;
    WL_JMP(FID_IO_BIND);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C73)
  {
    Term _a_6 = r0;
    Term _other_6 = r1;
    u32 _x_10 = r2;
    u32 _x_11 = r3;
    Term _x_9 = r4;
    WL_OPEN
    u64 _nd_8 = heap_alloc(e, cls_fit(5));
    e.mem[_nd_8 + 0] = _a_6;
    e.mem[_nd_8 + 1] = _other_6;
    e.mem[_nd_8 + 2] = _x_10;
    e.mem[_nd_8 + 3] = _x_11;
    e.mem[_nd_8 + 4] = _x_9;
    r0 = term_clo(FID_VIZ_LOOP_C74, _nd_8);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C74)
  {
    Term _a_7 = r0;
    Term _other_7 = r1;
    u32 _x_13 = r2;
    u32 _x_14 = r3;
    u32 _x_15 = r4;
    Term _x_12 = r5;
    WL_OPEN
    u64 _nd_9 = heap_alloc(e, cls_fit(1));
    e.mem[_nd_9 + 0] = 2ull;
    u64 _nd_10 = heap_alloc(e, cls_fit(5));
    e.mem[_nd_10 + 0] = _a_7;
    e.mem[_nd_10 + 1] = _other_7;
    e.mem[_nd_10 + 2] = _x_13;
    e.mem[_nd_10 + 3] = _x_14;
    e.mem[_nd_10 + 4] = _x_15;
    if (!DEVICE && !seq && fid_nofk(FID_IO_BIND)) {
      u64 _t_6 = task_node(e, FID_IO_BIND, WL_CONT, WL_IDX, 0);
      e.mem[_t_6 + 0] = term_clo(FID_VIZ_PARAM, _nd_9);
      e.mem[_t_6 + 1] = term_clo(FID_VIZ_LOOP_C75, _nd_10);
      e.mem[_t_6 + 2] = _x_12;
      return term_tsk(FID_IO_BIND, _t_6);
    }
    r0 = term_clo(FID_VIZ_PARAM, _nd_9);
    r1 = term_clo(FID_VIZ_LOOP_C75, _nd_10);
    r2 = _x_12;
    WL_JMP(FID_IO_BIND);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C75)
  {
    Term _a_8 = r0;
    Term _other_8 = r1;
    u32 _x_17 = r2;
    u32 _x_18 = r3;
    u32 _x_19 = r4;
    Term _x_16 = r5;
    WL_OPEN
    u64 _nd_11 = heap_alloc(e, cls_fit(6));
    e.mem[_nd_11 + 0] = _a_8;
    e.mem[_nd_11 + 1] = _other_8;
    e.mem[_nd_11 + 2] = _x_17;
    e.mem[_nd_11 + 3] = _x_18;
    e.mem[_nd_11 + 4] = _x_19;
    e.mem[_nd_11 + 5] = _x_16;
    r0 = term_clo(FID_VIZ_LOOP_C76, _nd_11);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C76)
  {
    Term _a_9 = r0;
    Term _other_9 = r1;
    u32 _x_21 = r2;
    u32 _x_22 = r3;
    u32 _x_23 = r4;
    u32 _x_24 = r5;
    Term _x_20 = r6;
    WL_OPEN
    u64 _nd_12 = heap_alloc(e, cls_fit(1));
    e.mem[_nd_12 + 0] = 3ull;
    u64 _nd_13 = heap_alloc(e, cls_fit(6));
    e.mem[_nd_13 + 0] = _a_9;
    e.mem[_nd_13 + 1] = _other_9;
    e.mem[_nd_13 + 2] = _x_21;
    e.mem[_nd_13 + 3] = _x_22;
    e.mem[_nd_13 + 4] = _x_23;
    e.mem[_nd_13 + 5] = _x_24;
    if (!DEVICE && !seq && fid_nofk(FID_IO_BIND)) {
      u64 _t_5 = task_node(e, FID_IO_BIND, WL_CONT, WL_IDX, 0);
      e.mem[_t_5 + 0] = term_clo(FID_VIZ_PARAM, _nd_12);
      e.mem[_t_5 + 1] = term_clo(FID_VIZ_LOOP_C77, _nd_13);
      e.mem[_t_5 + 2] = _x_20;
      return term_tsk(FID_IO_BIND, _t_5);
    }
    r0 = term_clo(FID_VIZ_PARAM, _nd_12);
    r1 = term_clo(FID_VIZ_LOOP_C77, _nd_13);
    r2 = _x_20;
    WL_JMP(FID_IO_BIND);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C77)
  {
    Term _a_10 = r0;
    Term _other_10 = r1;
    u32 _x_26 = r2;
    u32 _x_27 = r3;
    u32 _x_28 = r4;
    u32 _x_29 = r5;
    Term _x_25 = r6;
    WL_OPEN
    u64 _nd_14 = heap_alloc(e, cls_fit(7));
    e.mem[_nd_14 + 0] = _a_10;
    e.mem[_nd_14 + 1] = _other_10;
    e.mem[_nd_14 + 2] = _x_26;
    e.mem[_nd_14 + 3] = _x_27;
    e.mem[_nd_14 + 4] = _x_28;
    e.mem[_nd_14 + 5] = _x_29;
    e.mem[_nd_14 + 6] = _x_25;
    r0 = term_clo(FID_VIZ_LOOP_C78, _nd_14);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C78)
  {
    Term _a_11 = r0;
    Term _other_11 = r1;
    u32 _x_31 = r2;
    u32 _x_32 = r3;
    u32 _x_33 = r4;
    u32 _x_34 = r5;
    u32 _x_35 = r6;
    Term _x_30 = r7;
    WL_OPEN
    u64 _nd_15 = heap_alloc(e, cls_fit(1));
    e.mem[_nd_15 + 0] = 4ull;
    u64 _nd_16 = heap_alloc(e, cls_fit(7));
    e.mem[_nd_16 + 0] = _a_11;
    e.mem[_nd_16 + 1] = _other_11;
    e.mem[_nd_16 + 2] = _x_31;
    e.mem[_nd_16 + 3] = _x_32;
    e.mem[_nd_16 + 4] = _x_33;
    e.mem[_nd_16 + 5] = _x_34;
    e.mem[_nd_16 + 6] = _x_35;
    if (!DEVICE && !seq && fid_nofk(FID_IO_BIND)) {
      u64 _t_4 = task_node(e, FID_IO_BIND, WL_CONT, WL_IDX, 0);
      e.mem[_t_4 + 0] = term_clo(FID_VIZ_PARAM, _nd_15);
      e.mem[_t_4 + 1] = term_clo(FID_VIZ_LOOP_C79, _nd_16);
      e.mem[_t_4 + 2] = _x_30;
      return term_tsk(FID_IO_BIND, _t_4);
    }
    r0 = term_clo(FID_VIZ_PARAM, _nd_15);
    r1 = term_clo(FID_VIZ_LOOP_C79, _nd_16);
    r2 = _x_30;
    WL_JMP(FID_IO_BIND);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C79)
  {
    Term _a_12 = r0;
    Term _other_12 = r1;
    u32 _x_37 = r2;
    u32 _x_38 = r3;
    u32 _x_39 = r4;
    u32 _x_40 = r5;
    u32 _x_41 = r6;
    Term _x_36 = r7;
    WL_OPEN
    if (seq) {
      WL_ROOM(2);
      STK(0) = _other_12;
      STK(1) = FID_VIZ_LOOP_K80;
      WL_PUSHN(2);
    } else {
      u64 _t_0 = task_node(e, FID_VIZ_LOOP_K80, WL_CONT, WL_IDX, 1);
      e.mem[_t_0 + 0] = _other_12;
      WL_CONT = term_tsk(FID_VIZ_LOOP_K80, _t_0);
      WL_IDX = 1;
    }
    if (!DEVICE && !seq && fid_nofk(FID_VIZ_STEP)) {
      u64 _t_1 = task_node(e, FID_VIZ_STEP, WL_CONT, WL_IDX, 0);
      e.mem[_t_1 + 0] = _x_37;
      e.mem[_t_1 + 1] = _x_38;
      e.mem[_t_1 + 2] = _x_39;
      e.mem[_t_1 + 3] = _x_40;
      e.mem[_t_1 + 4] = _x_41;
      e.mem[_t_1 + 5] = _x_36;
      e.mem[_t_1 + 6] = _a_12;
      return term_tsk(FID_VIZ_STEP, _t_1);
    }
    r0 = _x_37;
    r1 = _x_38;
    r2 = _x_39;
    r3 = _x_40;
    r4 = _x_41;
    r5 = _x_36;
    r6 = _a_12;
    WL_JMP(FID_VIZ_STEP);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_K80)
  {
    WL_POPN(1);
    Term _other_13 = STK(0);
    Term _h_0 = r0;
    WL_OPEN
    u64 _nd_17 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_17 + 0] = _other_13;
    e.mem[_nd_17 + 1] = _h_0;
    r0 = term_clo(FID_VIZ_LOOP_C81, _nd_17);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C81)
  {
    Term _other_14 = r0;
    Term _h_1 = r1;
    Term _x_42 = r2;
    WL_OPEN
    u64 _nd_18 = heap_alloc(e, cls_fit(1));
    e.mem[_nd_18 + 0] = _other_14;
    if (!DEVICE && !seq && fid_nofk(FID_IO_BIND)) {
      u64 _t_3 = task_node(e, FID_IO_BIND, WL_CONT, WL_IDX, 0);
      e.mem[_t_3 + 0] = _h_1;
      e.mem[_t_3 + 1] = term_clo(FID_VIZ_LOOP_C82, _nd_18);
      e.mem[_t_3 + 2] = _x_42;
      return term_tsk(FID_IO_BIND, _t_3);
    }
    r0 = _h_1;
    r1 = term_clo(FID_VIZ_LOOP_C82, _nd_18);
    r2 = _x_42;
    WL_JMP(FID_IO_BIND);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_LOOP_C82)
  {
    Term _other_15 = r0;
    Term _x_43 = r1;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_VIZ_LOOP)) {
      u64 _t_2 = task_node(e, FID_VIZ_LOOP, WL_CONT, WL_IDX, 0);
      e.mem[_t_2 + 0] = _other_15;
      e.mem[_t_2 + 1] = _x_43;
      return term_tsk(FID_VIZ_LOOP, _t_2);
    }
    r0 = _other_15;
    r1 = _x_43;
    WL_JMP(FID_VIZ_LOOP);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_MAIN)
  {
    WL_OPEN
    if (seq) {
      WL_ROOM(1);
      STK(0) = FID_MAIN_K84;
      WL_PUSHN(1);
    } else {
      u64 _t_0 = task_node(e, FID_MAIN_K84, WL_CONT, WL_IDX, 1);
      WL_CONT = term_tsk(FID_MAIN_K84, _t_0);
      WL_IDX = 0;
    }
    if (!seq) {
      u64 _t_1 = task_node(e, FID_VIZ_BLANK, WL_CONT, WL_IDX, 0);
      e.mem[_t_1 + 0] = 0ull;
      return term_tsk(FID_VIZ_BLANK, _t_1);
    }
    r0 = 0ull;
    WL_JMP(FID_VIZ_BLANK);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_MAIN_K84)
  {
    Term _h_0 = r0;
    WL_OPEN
    if (seq) {
      WL_ROOM(2);
      STK(0) = _h_0;
      STK(1) = FID_MAIN_K85;
      WL_PUSHN(2);
    } else {
      u64 _t_2 = task_node(e, FID_MAIN_K85, WL_CONT, WL_IDX, 1);
      e.mem[_t_2 + 0] = _h_0;
      WL_CONT = term_tsk(FID_MAIN_K85, _t_2);
      WL_IDX = 1;
    }
    if (!seq) {
      u64 _t_3 = task_node(e, FID_VIZ_BLANK, WL_CONT, WL_IDX, 0);
      e.mem[_t_3 + 0] = 0ull;
      return term_tsk(FID_VIZ_BLANK, _t_3);
    }
    r0 = 0ull;
    WL_JMP(FID_VIZ_BLANK);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_MAIN_K85)
  {
    WL_POPN(1);
    Term _h_2 = STK(0);
    Term _h_1 = r0;
    WL_OPEN
    if (!DEVICE && !seq && fid_nofk(FID_VIZ_LOOP)) {
      u64 _t_4 = task_node(e, FID_VIZ_LOOP, WL_CONT, WL_IDX, 0);
      e.mem[_t_4 + 0] = _h_2;
      e.mem[_t_4 + 1] = _h_1;
      return term_tsk(FID_VIZ_LOOP, _t_4);
    }
    r0 = _h_2;
    r1 = _h_1;
    WL_JMP(FID_VIZ_LOOP);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_NEXT)
  {
    Term _k_6 = r0;
    WL_OPEN
    u64 _nd_6 = heap_alloc(e, cls_fit(1));
    e.mem[_nd_6 + 0] = _k_6;
    r0 = term_ctr(CID_VIZ_NEXT, _nd_6);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_PARAM)
  {
    Term _i_2 = r0;
    Term _k_7 = r1;
    WL_OPEN
    u64 _nd_7 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_7 + 0] = _i_2;
    e.mem[_nd_7 + 1] = _k_7;
    r0 = term_ctr(CID_VIZ_PARAM, _nd_7);
    WL_RETN(1);
  }}
#endif

#if !DEVICE
  WL_CASE(FID_VIZ_SHOW)
  {
    Term _a_2 = r0;
    Term _k_8 = r1;
    WL_OPEN
    u64 _nd_8 = heap_alloc(e, cls_fit(2));
    e.mem[_nd_8 + 0] = _a_2;
    e.mem[_nd_8 + 1] = _k_8;
    r0 = term_ctr(CID_VIZ_SHOW, _nd_8);
    WL_RETN(1);
  }}
#endif

  WL_CASE(FID_ENTER)
  {
    Term t = r0;
    WL_OPEN
    u32 f   = (u32)term_aux(t);
    u64 a   = term_loc(t);
    u32 war = fid_arity(f);
#if SPRD_USED
    if (term_tag(t) == TAG_SPR) {  // a spread's part (spread_split)
      Term lp = e.mem[a];
      u64  is = e.mem[a + 1];
      Term c  = e.mem[a + 2];
      STK(0) = lp;
      STK(1) = 0;
      STK(2) = FID_EXIT;
      sp += 3 * LANE_STEP;
      seq |= fid_nofk(f) << 1;
      WL_LOAD(term_loc(lp), war)
      r0 = c, r2 = (u32)is, r3 = is >> 32;
      WL_DYN(f);
    }
#endif
    WL_FRAME(t)
    seq |= fid_nofk(f) << 1;
    if (fid_resw(f)) {
      u32 rw = fid_resw(f);
      WL_LOAD(a + war - rw, rw)
      WL_ARGS(a, war - rw + 1)
    } else {
      WL_LOAD(a, war)
    }
    // A bang's root task (continued by the root) was made by the host, which
    // frees it after the bang (corpus_eval): on the device it would leave the
    // host's supply a node short each bang, refilled from the device's.
    if (!DEVICE || e.mem[a + war] != TERM_HOLE) {
      heap_free(e, cls_fit(war + 2), a);
    }
    WL_DYN(f);
  }}

  WL_CASE(FID_IO_EMIT)
  {
    Term x = r0;
    WL_OPEN
    u64 l = heap_alloc(e, 0);
    e.mem[l] = x;
    r0 = term_ctr(CID_EMIT, l);
    WL_RETN(1);
  }}

  WL_CASE(FID_CLO_APPLY)
  {
    Term fun = r0;
    Term arg = r1;
    WL_OPEN
    u32 f    = (u32)term_aux(fun);
    u32 war  = fid_arity(f) - 1;
    u64 a    = term_loc(fun);
    WL_LOAD(a, war)
    spare_free(e, cls_fit(war), a);
    WL_LAST(arg)
    WL_DYN(f);
  }}

  WL_CASE(FID_EXIT)
  {
    u32  n = rn;
    Term rv[WL_RESW];
    WL_SAVE(rv)
    WL_OPEN
    if (err_seen(e.mem)) {
      return 0;
    }
    sp -= 2 * LANE_STEP;
    Term cont = STK(0);
    u32  idx  = (u32)STK(1);
    if (cont != TERM_HOLE && fid_resw((u32)term_aux(cont))) {
      u32 wf = (u32)term_aux(cont);
      u64 wa = term_loc(cont);
      u32 wn = fid_arity(wf);
      WL_FRAME(cont)
      seq = (seq & 1) | fid_nofk(wf) << 1;
      WL_ARGS(wa, wn - n + 1)
      heap_free(e, cls_fit(wn + 2), wa);
      WL_TAKE(rv)
      WL_DYN(wf);
    }
    if (cont != TERM_HOLE && fid_sprd((u32)term_aux(cont))) {
      return spread_give(e, cont, rv[0]);
    }
    return task_deliver(e.mem, cont, idx, rv, n);
  }}

#if DEVICE
  default: {
    err_post(e.mem, ERR_FIDS);
    return 0;
  }
  }
  }
}
#endif

// Monk
// ====

#if DEVICE
// A growing lane that meets a spread's loop with two or more steps left posts
// it for its group (monk_step returns 3 + 4 * its place in the list), and
// after the round's barrier the group splits every loop posted at once
// (spread_split): each loop's lane makes it the join of its parts and takes
// a cell for each, then each lane fills one part's cell (a loop over every
// parts-th step) and queues it on its own ring: three stores and a push, as
// making a task per part (an allocation and a copy of its words, 50 us at
// 32,768 lanes) is not. A part posted in turn becomes a loop of its own. A
// loop of 32,768 steps takes two rounds: one group splits it into 256, then
// each group splits its two into 128 each.
#define SPRD_N    3   // vote: loops posted this round
#define SPRD_STOP 4   // vote: too many to split; none more this pass
#define SPRD_LIST 16  // vote: a posted loop's task, i, c and st, then its cells
#define SPRD_LW   8   // vote: words a posted loop

INLINE u64 spread_steps(DEV u64* H, Term t) {
  return term_tag(t) == TAG_SPR ? H[term_loc(t) + 2]
    : H[term_loc(t) + SPRD_C];
}

INLINE u32 spread_post(DEV u64* H, Term t, TG u32* vote) {
  u32     k   = a32_add(vote + SPRD_N, 1);
  u64     loc = term_loc(t);
  TG u32* p   = vote + SPRD_LIST + SPRD_LW * k;
  p[0] = (u32)t, p[1] = (u32)(t >> 32);
  if (term_tag(t) == TAG_SPR) {
    u64 is = H[loc + 1];
    p[2] = (u32)is, p[3] = (u32)H[loc + 2], p[4] = (u32)(is >> 32);
  } else {
    p[2] = (u32)H[loc + SPRD_I], p[3] = (u32)H[loc + SPRD_C];
    p[4] = (u32)H[loc + SPRD_ST];
  }
  return k;
}

INLINE void spread_split(Env e, TG u32* vote, u32 lane, u32 rg, u32 posted,
  u32 ran) {
  DEV u64* H    = e.mem;
  u32      m    = CUBE_T / posted;  // lanes a loop
  bool     mine = (ran & 3) == 3;
  TG u32*  own  = vote + SPRD_LIST + SPRD_LW * (ran >> 2);
  Term     t    = mine ? own[0] | (u64)own[1] << 32 : 0;
  if (m < 2) {
    if (mine) {
      ring_push(H, rg, t);
    }
    BARD();
    if (lane == 0) {
      a32_store(vote + SPRD_N, 0);
      a32_store(vote + SPRD_STOP, 1);
    }
    return;
  }
  if (mine) {
    u32 f     = (u32)term_aux(t);
    u32 ar    = fid_arity(f);
    u32 parts = own[3] < m ? own[3] : m;
    u64 loc   = term_loc(t);
    if (term_tag(t) == TAG_SPR) {  // a part: a loop of its own now
      Term lp = H[loc];
      u64  n  = task_node(e, f, lp, 0, 0);
      for (u32 w = 0; w < ar; w += 1) {
        H[n + w] = H[term_loc(lp) + w];
      }
      loc = n, t = term_tsk(f, n);
      own[0] = (u32)t, own[1] = (u32)(t >> 32);
    }
    H[loc + SPRD_A] = term_keep(e, H[loc + SPRD_A], parts);
    for (u32 w = SPRD_ST + 1; w < ar; w += 1) {
      if (!term_triv(H[loc + w])) {
        H[loc + w] = term_keep(e, H[loc + w], parts);
      }
    }
    u64 cells = heap_alloc(e, cls_fit(parts * SPRD_CELL));
    own[5] = (u32)cells, own[6] = (u32)(cells >> 32);
    H[loc + SPRD_C]  = 0;
    H[loc + SPRD_I]  = 0;
    H[loc + SPRD_ST] = cells | (u64)parts << 48;
    H[loc + ar + 1]  = (H[loc + ar + 1] & ~0xFFFFFFFFull) | parts;
  }
  BARD();
  u32 k = lane / m, q = lane % m;
  if (k < posted) {
    TG u32* p     = vote + SPRD_LIST + SPRD_LW * k;
    u32     c     = p[3];
    u32     parts = c < m ? c : m;
    if (q < parts) {
      Term pt   = p[0] | (u64)p[1] << 32;
      u64  cell = (p[5] | (u64)p[6] << 32) + q * SPRD_CELL;
      H[cell]     = pt;
      H[cell + 1] = (u32)(p[2] + q * p[4]) | (u64)(u32)(p[4] * parts) << 32;
      H[cell + 2] = (c - q + parts - 1) / parts;
      ring_push(H, rg, term_make(TAG_SPR, term_aux(pt), cell));
    }
  }
  BARD();
  if (lane == 0) {
    a32_store(vote + SPRD_N, 0);
  }
}
#endif

// One turn on a ring: its head task below put0 runs (a growing
// lane skips a fork-free one). The host grows a row ring by
// ring and drains a ring; a device lane does both.
INLINE u32 monk_step(Env e, DEV Term* stk, u32 rg, u32 put0, u32 base, u32 stride,
  TG u32* cur) {
  DEV u64* H   = e.mem;
  bool     seq = stride == 0;
  DEV u32* get = ring_get(H, rg);
  if (*get == put0) {
    return 0;
  }
  DEV u32* lo = (DEV u32*)ring_slot(H, rg, *get);
  u32      hi = a32_load_acq(lo + 1);
  Term     t  = (((u64)hi << 32) | a32_load(lo)) & ~RFC_BIT;
  if ((hi >> 31) != ring_lap(*get)) {
    return 0;
  }
#if DEVICE
  if (!seq && fid_sprd((u32)term_aux(t)) && spread_steps(H, t) >= 2
    && a32_load(cur + SPRD_STOP) == 0) {
    a32_store(get, *get + 1);
    return 3 + 4 * spread_post(H, t, cur);
  }
#endif
  if (!seq && fid_nofk((u32)term_aux(t))) {
    return 0;
  }
  a32_store(get, *get + 1);
  u32 spin = 0;
  for (;;) {
    Term r = work_loop(e, stk, t, seq);
    if (r == 0) {
      return 2;
    }
    if ((u32)H[task_tail(r) + 1] == 0) {
      if (err_spun(H, &spin)) {
        return 2;
      }
      if (stride != 0 && fid_nofk((u32)term_aux(r))) {
        ring_push(H, ring_pick(base, stride, cur), r);
        return 2;
      }
      t      = r;
      seq    = false;
      stride = 0;
      continue;
    }
    task_deal(H, r, base, stride, cur);
    return 1;
  }
}

// Dev
// ===

// One kernel: pass 0 grows the frontier, pass 1 drains each lane's ring,
// pass 2 packs the banks: in one group, each bank's [top, wr) slides onto
// rd, CUBE_T entries a step (loads, barrier, stores: rd <= top), off the
// host's pages. A grow pass ends when its group is full or nothing grew,
// so a spine of forks unrolls whole. TG_HOLD words of threadgroup memory
// hold one group per Apple core (bitonic 1.35x without).

#if DEVICE

INLINE void dev_cut(Env e) {
  if (err_seen(e.mem)) {
    return;
  }
  for (u32 c0 = 0; c0 < NCLS_ALL; c0 += 8) {
    u64 cold[8];  // loaded together, their round trips overlapping
    for (u32 i = 0; i < 8; i += 1) {
      cold[i] = ALC_COLD(e, c0 + i);
    }
    for (u32 i = 0; i < 8; i += 1) {
      if (cold[i]) {
        ALC_COLD(e, c0 + i) = 0;
        bank_push(e.mem, c0 + i, cold[i]);
      }
    }
  }
}

INLINE void bank_pack(DEV u64* H, u32 lane) {
  for (u32 c = 0; c < NCLS_ALL; c += 1) {
    DEV Bank* b  = bank_at(H, c);
    u32       rd = b->rd;
    u32       n  = b->wr - b->top;
    for (u32 i = 0; i < n; i += CUBE_T) {
      Term v = i + lane < n ? H[b->off + b->top + i + lane] : 0;
      BAR();
      if (i + lane < n) {
        H[b->off + rd + i + lane] = v;
      }
    }
    BAR();
    if (lane == 0) {
      b->rd = b->wr = b->top = rd + n;
    }
  }
}

#ifdef __METAL_VERSION__
kernel void bend_dev(DEV u64* H [[buffer(0)]], constant u32& pass [[buffer(1)]],
  TG u32* vote [[threadgroup(0)]],
  u32 grids [[threadgroups_per_grid]],
  u32 row [[threadgroup_position_in_grid]],
  u32 lane [[thread_position_in_threadgroup]]) {
#else
extern "C" __global__ void bend_dev(DEV u64* H, u32 pass) {
  extern __shared__ u32 vote[];
  u32 grids = gridDim.x;
  u32 row   = blockIdx.x;
  u32 lane  = threadIdx.x;
#endif
  if (pass == 2) {
    bank_pack(H, lane);
    return;
  }
  u32  stride = grids == 1 ? CUBE_G : 1;
  u32  me     = row * CUBE_T + stride * lane;
  u32 rg     = pass ? ring_flip(me) : me;
  Env  e      = { H, H + ALC_OFF + me };
  DEV Term*  stk    = (DEV Term*)(H + STAK_OFF + me);
  if (lane == 0) {
    for (u32 i = 0; i < 5; i += 1) {
      a32_store(vote + i, 0);
    }
  }
  BAR();
  u32 put0      = a32_load(ring_put(H, rg));
  u32 seen_has  = 0;
  u32 seen_grew = 0;
  for (;;) {
    if (pass) {
      if (*ring_get(H, rg) == put0 || err_seen(H)) {
        break;
      }
    } else {
      put0 = a32_load(ring_put(H, rg));
      u32 has = put0 != a32_load(ring_get(H, rg));
      if (lane == 0 && (err_seen(H) || root_done(H))) {
        has = CUBE_T;
      }
      a32_add(vote + 2, has);
      BAR();
      has = a32_load(vote + 2);
      if (has - seen_has >= CUBE_T) {
        break;
      }
      seen_has = has;
    }
    u32 ran = monk_step(e, stk, rg, put0, row * CUBE_T, pass ? 0 : stride,
      vote);
    if (!pass) {
      if (ran == 1 || (ran & 3) == 3) {
        a32_add(vote + 1, 1);
      }
      BARD();
      u32 posted = a32_load(vote + SPRD_N);
      if (posted != 0) {
        spread_split(e, vote, lane, rg, posted, ran);
      }
      u32 grew = a32_load(vote + 1);
      if (grew == seen_grew) {
        break;
      }
      seen_grew = grew;
    }
  }
  dev_cut(e);
}

#ifndef __METAL_VERSION__
// Moves whole pages (512 words, a group each) between the corpus and a
// page-locked stage on the host: in, the stage's pages to list[i]; out, back.
extern "C" __global__ void bend_pages(DEV u64* H, const u32* list, u32 n,
  u64* stage, u32 in) {
  DEV u64* h = H + ((u64)list[blockIdx.x] << 9);
  u64*     s = stage + ((u64)blockIdx.x << 9);
  for (u32 i = threadIdx.x; i < 512; i += blockDim.x) {
    if (in) {
      h[i] = s[i];
    } else {
      s[i] = h[i];
    }
  }
}
#endif

#endif

// Window
// ======

// Linux's window fill (the Mac's is window_msl): an Image is a quadtree over
// 2^k x 2^k (Qua splits tl, tr, bl, br; Pix is 0xRRGGBB).
#if defined(__linux__) || defined(BEND_RTC)

INLINE u32 window_pix(DEV u64* H, Term t, u32 k, u32 x, u32 y) {
  for (u32 i = k; term_tag(t) == TAG_CTR;) {
    u32 j = 0;
    if (i > 0) {
      i -= 1;
      j = ((y >> i) & 1) * 2 + ((x >> i) & 1);
    }
    u64 l = term_rfc(t) ? H[term_loc(t)] >> 24 : term_loc(t);
    t = H[l + j];
  }
  return (u32)term_loc(t) & 0xFFFFFF;
}

#ifdef BEND_RTC
extern "C" __global__ void window_dev(DEV u64* H, Term root, u32 w, u32 h,
  u32 k, u32* out) {
  u32 x = blockIdx.x * blockDim.x + threadIdx.x;
  u32 y = blockIdx.y * blockDim.y + threadIdx.y;
  if (x < w && y < h) {
    out[y * w + x] = window_pix(H, root, k, x, y);
  }
}
#endif

#endif

#if !DEVICE

// Row
// ===

static void row_grow(Env e, DEV Term* stk, u32 base, u32 stride, u32 want) {
  u64* H = e.mem;
  u32 cur = 0;
  for (;;) {
    u32 put0[CUBE_T];
    u32 has = 0;
    for (u32 i = 0; i < CUBE_T; i += 1) {
      put0[i] = *ring_put(H, base + i * stride);
      has += put0[i] != *ring_get(H, base + i * stride);
    }
    if (root_done(H) || has >= want) {
      return;
    }
    u32 grew = 0;
    u32 ran  = 0;
    for (u32 i = 0; i < CUBE_T && ran != 2; i += 1) {
      ran   = monk_step(e, stk, base + i * stride, put0[i], base, stride,
        &cur);
      grew += ran == 1;
    }
    if (grew == 0) {
      return;
    }
  }
}

// Pool
// ====

// cpu_count caps the CPU count by the affinity mask and the cgroup quota.

#ifdef _WIN32

// Windows has no lazily backed mapping (MAP_NORESERVE). A span is reserved,
// and its pages are committed the first time they are touched, 64 KiB at a
// time, by a vectored exception handler. A stack's guard is never committed:
// a touch there is ERR_DEEP. Faults outside the spans go on to the host.

#define MAP_FAILED ((void*)-1)

typedef struct { char* lo; char* hi; char* guard; } PoolSpan;

static PoolSpan    pool_spans[1024];
static _Atomic u32 pool_nspans;
static SRWLOCK     pool_span_lock = SRWLOCK_INIT;

static LONG CALLBACK pool_fault(EXCEPTION_POINTERS* x) {
  if (x->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  char* at = (char*)x->ExceptionRecord->ExceptionInformation[1];
  u32   n  = atomic_load_explicit(&pool_nspans, memory_order_acquire);
  for (u32 i = 0; i < n; i += 1) {
    PoolSpan s = pool_spans[i];
    if (at < s.lo || at >= s.hi) {
      continue;
    }
    if (s.guard != NULL && at >= s.guard) {
      err_post(NULL, ERR_DEEP);
    }
    char* end = s.guard != NULL ? s.guard : s.hi;
    char* lo  = (char*)((uintptr_t)at & ~(uintptr_t)0xFFFF);
    lo = lo < s.lo ? s.lo : lo;
    char* hi  = lo + 0x10000 < end ? lo + 0x10000 : end;
    return VirtualAlloc(lo, (SIZE_T)(hi - lo), MEM_COMMIT, PAGE_READWRITE)
      != NULL ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
  }
  return EXCEPTION_CONTINUE_SEARCH;
}

static void* pool_try(void* at, u64 bytes) {
  static _Atomic int armed;
  if (atomic_exchange(&armed, 1) == 0) {
    AddVectoredExceptionHandler(1, pool_fault);
  }
  char* p = VirtualAlloc(at, (SIZE_T)bytes, MEM_RESERVE, PAGE_READWRITE);
  if (p == NULL) {
    return MAP_FAILED;
  }
  AcquireSRWLockExclusive(&pool_span_lock);
  u32 n = atomic_load(&pool_nspans);
  if (n == sizeof pool_spans / sizeof *pool_spans) {
    err_fail("too many reservations");
  }
  pool_spans[n] = (PoolSpan){ p, p + bytes, NULL };
  atomic_store_explicit(&pool_nspans, n + 1, memory_order_release);
  ReleaseSRWLockExclusive(&pool_span_lock);
  return p;
}

// A span is never reused, so a fault can read the table without a lock.
static void pool_free(void* p, u64 bytes) {
  AcquireSRWLockExclusive(&pool_span_lock);
  for (u32 i = 0; i < atomic_load(&pool_nspans); i += 1) {
    if (pool_spans[i].lo == p) {
      pool_spans[i].hi = pool_spans[i].lo;
    }
  }
  ReleaseSRWLockExclusive(&pool_span_lock);
  VirtualFree(p, 0, MEM_RELEASE);
}

#else

static void* pool_try(void* at, u64 bytes) {
  return mmap(at, bytes, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANON | MAP_NORESERVE, -1, 0);
}

static void pool_free(void* p, u64 bytes) {
  munmap(p, bytes);
}

#endif

static void* pool_mmap(u64 bytes) {
  void* p = pool_try(NULL, bytes);
  if (p == MAP_FAILED) {
    err_fail("reservation failed");
  }
  return p;
}

#ifdef _WIN32

static Term* pool_stack(void) {
  u64   len = 1ull << 31;
  char* p   = pool_mmap(len + 16384);
  AcquireSRWLockExclusive(&pool_span_lock);
  for (u32 i = 0; i < atomic_load(&pool_nspans); i += 1) {
    if (pool_spans[i].lo == p) {
      pool_spans[i].guard = p + len;
    }
  }
  ReleaseSRWLockExclusive(&pool_span_lock);
  return (Term*)p;
}

#else

static Term* pool_stack(void) {
  u64   len = 1ull << 31;
  char* p   = pool_mmap(len + 16384 + SIGSTKSZ);
  if (mprotect(p + len, 16384, PROT_NONE) != 0) {
    err_fail("stack guard failed");
  }
  stack_t ss = { .ss_sp = p + len + 16384, .ss_size = SIGSTKSZ };
  sigaltstack(&ss, NULL);
  struct sigaction sa = { .sa_handler = err_trap, .sa_flags = SA_ONSTACK };
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  return (Term*)p;
}

#endif

static void* pool_work(void* arg) {
  Term* stk  = pool_stack();
  u32   seen = 0;
  for (;;) {
    pthread_mutex_lock(&pool_lock);
    while (pool_tick == seen) {
      pthread_cond_wait(&pool_wake, &pool_lock);
    }
    seen = pool_tick;
    pthread_mutex_unlock(&pool_lock);
    Env e = { CORPUS, ALC[1 + (u32)(uintptr_t)arg] };
    for (;;) {
      u32 r = a32_add(&pool_row, 1);
      if (r >= (pool_grow ? CUBE_G : LANES / LINE)) {
        break;
      }
      if (pool_grow) {
        row_grow(e, stk, r * CUBE_T, 1, CUBE_T);
      } else {
        u32  step = CUBE_T / LINE;
        u32 row  = r / step * CUBE_T;
        for (u32 rg = row + r % step; rg < row + CUBE_T; rg += step) {
          u32 put0 = a32_load(ring_put(e.mem, rg));
          while (*ring_get(e.mem, rg) != put0 && !err_seen(e.mem)) {
            monk_step(e, stk, rg, put0, rg, 0, NULL);
          }
        }
      }
    }
    if (a32_sub_rel(&pool_done, 1) == 1) {
      pthread_mutex_lock(&pool_lock);
      pthread_cond_broadcast(&pool_wake);
      pthread_mutex_unlock(&pool_lock);
    }
  }
}

OUTLINE void pool_open(void) {
  static bool up;
  if (up) {
    return;
  }
  up = true;
  for (u32 w = 0; w < pool_size; w += 1) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, pool_work, (void*)(uintptr_t)w)) {
      err_fail("pthread_create");
    }
  }
}

static int cpu_read(const char* path, long* a, long* b) {
  FILE* f = fopen(path, "r");
  int   n = f == NULL ? 0 : fscanf(f, "%ld %ld", a, b);
  if (f != NULL) {
    fclose(f);
  }
  return n;
}

static long cpu_count(void) {
#ifdef _WIN32
  long n = (long)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
#else
  long n = sysconf(_SC_NPROCESSORS_ONLN);
#endif
#ifdef __linux__
  cpu_set_t set;
  if (sched_getaffinity(0, sizeof set, &set) == 0) {
    n = CPU_COUNT(&set);
  }
  long q = 0;
  long p = 0;
  if (cpu_read("/sys/fs/cgroup/cpu.max", &q, &p) != 2) {
    cpu_read("/sys/fs/cgroup/cpu/cpu.cfs_quota_us", &q, &p);
    cpu_read("/sys/fs/cgroup/cpu/cpu.cfs_period_us", &p, &p);
  }
  if (q > 0 && p > 0 && (q + p - 1) / p < n) {
    n = (q + p - 1) / p;
  }
#endif
  return n;
}

OUTLINE void pool_turn(bool grow) {
  pool_grow = grow;
  a32_store(&pool_row, 0);
  a32_store(&pool_done, pool_size);
  pthread_mutex_lock(&pool_lock);
  pool_tick += 1;
  pthread_cond_broadcast(&pool_wake);
  while (a32_load_acq(&pool_done) != 0) {
    pthread_cond_wait(&pool_wake, &pool_lock);
  }
  pthread_mutex_unlock(&pool_lock);
}

// Gpu
// ===

// gpu_make compiles the device program into <binary>.gpu
// (--gpu-build): Metal's binary archive, or CUDA's cubin behind a
// hash of the text. A launch loads it, else notes and compiles. CUDA
// shapes the bag by the device: a group of 128 lanes per 64 KB of
// L2, a power of two in 16..128 (Apple keeps the tuned 128). CUDA
// runs one stream: the default 8 cost about half of the startup.

static const char* gpu_path(void) {
  static char path[4096];
  u32 n = sizeof path - 8;
#ifdef __APPLE__
  _NSGetExecutablePath(path, &n);
#elif defined(_WIN32)
  path[GetModuleFileNameA(NULL, path, n)] = 0;
#else
  path[readlink("/proc/self/exe", path, n)] = 0;
#endif
  return strcat(path, ".gpu");
}

static void gpu_note(const char* path) {
  fprintf(stderr, "bend: compiling the GPU program (%s is missing or"
    " stale)\n", path);
}

#if !BEND_CUDA
#define gpu_map pool_mmap
#endif

#if BEND_METAL || BEND_CUDA

static void gpu_kernel(u32 pass, u32 groups);

static void gpu_run(u32 f) {
  if (f < CUBE_T) {
    gpu_kernel(0, 1);
  }
  if (f < LANES) {
    gpu_kernel(0, CUBE_G);
  }
  gpu_kernel(1, CUBE_G);
  gpu_kernel(2, 1);
}

#endif

#if BEND_CUDA

static u64 gpu_hash(void) {
  u64 key = 14695981039346656037ull ^ CUBE_LOG;
  for (const char* p = BEND_SRC; *p != 0; p += 1) {
    key = (key ^ (u8)*p) * 1099511628211ull;
  }
  return key;
}

#endif

#if BEND_METAL

static void gpu_fail(NSError* err) {
  err_fail([[err localizedDescription] UTF8String]);
}

static bool gpu_probe(void) {
  return (gpu_dev = MTLCreateSystemDefaultDevice()) != nil;
}

static MTLComputePipelineDescriptor* gpu_desc(void) {
  NSError* err = nil;
  MTLCompileOptions* opts = [MTLCompileOptions new];
  opts.mathMode = MTLMathModeSafe;
  opts.preprocessorMacros = @{ @"CUBE_LOG": @(CUBE_LOG) };
  id<MTLLibrary> lib = [gpu_dev newLibraryWithSource:@(BEND_SRC) options:opts
    error:&err];
  if (!lib) {
    gpu_fail(err);
  }
  MTLComputePipelineDescriptor* d = [MTLComputePipelineDescriptor new];
  d.computeFunction = [lib newFunctionWithName:@"bend_dev"];
  return d;
}

static bool gpu_make(const char* path) {
  NSError* err = nil;
  id<MTLBinaryArchive> ar = [gpu_dev
    newBinaryArchiveWithDescriptor:[MTLBinaryArchiveDescriptor new] error:&err];
  if (![ar addComputePipelineFunctionsWithDescriptor:gpu_desc() error:&err]) {
    gpu_fail(err);
  }
  return [ar serializeToURL:[NSURL fileURLWithPath:@(path)] error:&err];
}

static id<MTLComputePipelineState> gpu_pipe(MTLComputePipelineDescriptor* d,
  id<MTLBinaryArchive> ar) {
  NSError* err = nil;
  d.binaryArchives = ar ? @[ar] : @[];
  id<MTLComputePipelineState> pso = [gpu_dev
    newComputePipelineStateWithDescriptor:d
    options:ar ? MTLPipelineOptionFailOnBinaryArchiveMiss : 0 reflection:nil
    error:&err];
  if (!pso && !ar) {
    gpu_fail(err);
  }
  return pso;
}

static u64 gpu_span(void) {
  u64 span = [gpu_dev recommendedMaxWorkingSetSize];
  u64 most = [gpu_dev maxBufferLength];
  span = span < most ? span : most;
  return span < (2ull << 30) ? span : 2ull << 30;
}

static void gpu_load(u64 bytes) {
  gpu_buf = [gpu_dev newBufferWithBytesNoCopy:CORPUS length:bytes
    options:MTLResourceStorageModeShared
      | MTLResourceHazardTrackingModeUntracked deallocator:nil];
  u64 most = [gpu_dev maxBufferLength];
  if (!gpu_buf && bytes > most) {
    char msg[96];
    snprintf(msg, sizeof msg, "--gpu %lluMB is over the device's %lluMB",
      (unsigned long long)(bytes >> 20), (unsigned long long)(most >> 20));
    err_fail(msg);
  }
  if (!gpu_buf) {
    err_fail("the GPU span is more than the device has");
  }
  @autoreleasepool {
    gpu_que = [gpu_dev newCommandQueue];
    const char* path = gpu_path();
    MTLBinaryArchiveDescriptor* ad = [MTLBinaryArchiveDescriptor new];
    ad.url = [NSURL fileURLWithPath:@(path)];
    MTLComputePipelineDescriptor* d = gpu_desc();
    id<MTLBinaryArchive> ar = [gpu_dev newBinaryArchiveWithDescriptor:ad
      error:nil];
    gpu_pso = ar ? gpu_pipe(d, ar) : nil;
    if (!gpu_pso) {
      gpu_note(path);
      gpu_pso = gpu_pipe(d, nil);
    }
  }
}

static void gpu_kernel(u32 pass, u32 groups) {
  [gpu_enc setComputePipelineState:gpu_pso];
  [gpu_enc setBuffer:gpu_buf offset:0 atIndex:0];
  [gpu_enc setBytes:&pass length:sizeof pass atIndex:1];
  [gpu_enc setThreadgroupMemoryLength:TG_HOLD * 8 atIndex:0];
  [gpu_enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
    threadsPerThreadgroup:MTLSizeMake(CUBE_T, 1, 1)];
  [gpu_enc memoryBarrierWithScope:MTLBarrierScopeBuffers];
}

static void gpu_pass(u32 f) {
  @autoreleasepool {
    id<MTLCommandBuffer> cb = [gpu_que commandBuffer];
    gpu_enc = [cb computeCommandEncoder];
    gpu_run(f);
    [gpu_enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if ([cb error]) {
      gpu_fail([cb error]);
    }
  }
}

#elif BEND_CUDA

static void gpu_shape(int units) {
  CUBE_LOG = 31 - CLZ(units < 16 ? 16 : units > 128 ? 128 : units);
}

// A GPU program to run: the sidecar made for this source and device, or NVRTC
// to make one. Without either the bangs run on the CPU (with a note) rather
// than stopping at the first launch.
static bool gpu_ready(void) {
  FILE* in  = fopen(gpu_path(), "rb");
  u64   key = 0;
  bool  ok  = in != NULL && fread(&key, 8, 1, in) == 1 && key == gpu_hash();
  if (in != NULL) {
    fclose(in);
  }
  if (!ok && !gpu_open_rtc()) {
    fprintf(stderr, "bend: no GPU program for this device (%s) and no NVRTC to"
      " make one; running on the CPU\n", gpu_path());
    return false;
  }
  return true;
}

static CUcontext gpu_ctx;

static bool gpu_probe(void) {
  int       managed = 0;
  CUcontext ctx;
  if (!gpu_open_cu()) {
    return false;
  }
#ifdef _WIN32
  if (getenv("CUDA_DEVICE_MAX_CONNECTIONS") == NULL) {
    _putenv("CUDA_DEVICE_MAX_CONNECTIONS=1");
  }
  // Windows has no concurrent managed access. The host never touches the
  // corpus while a kernel runs (it waits in cuCtxSynchronize), so plain
  // managed memory is enough; a host page is slow to share, though (see
  // gpu_map), so only small work should cross.
  CUdevice_attribute need = CU_DEVICE_ATTRIBUTE_MANAGED_MEMORY;
#else
  setenv("CUDA_DEVICE_MAX_CONNECTIONS", "1", 0);
  CUdevice_attribute need = CU_DEVICE_ATTRIBUTE_CONCURRENT_MANAGED_ACCESS;
#endif
  if (cuInit(0) == CUDA_SUCCESS && cuDeviceGet(&gpu_dev, 0) == CUDA_SUCCESS) {
    cuDeviceGetAttribute(&managed, need, gpu_dev);
  }
  // A group per 64 KB of L2, and at least one per multiprocessor (rounded up to a power of two):
  // L2 alone leaves most of a big NVIDIA part idle (an RTX 3090 got 64 groups, 8,192 threads,
  // for 82 multiprocessors of 1,536 threads each), and a compute-bound bang runs twice as fast
  // with 128.
  int l2 = 1 << 23;
  int sms = 0;
  cuDeviceGetAttribute(&l2, CU_DEVICE_ATTRIBUTE_L2_CACHE_SIZE, gpu_dev);
  cuDeviceGetAttribute(&sms, CU_DEVICE_ATTRIBUTE_MULTIPROCESSOR_COUNT, gpu_dev);
  int units = l2 >> 16, per_sm = 1;
  while (per_sm < sms) {
    per_sm *= 2;
  }
  gpu_shape(units > per_sm ? units : per_sm);
  // The host sleeps while it waits for the device, rather than spinning a
  // core (CU_CTX_SCHED_BLOCKING_SYNC; ignored if the context is already open).
  if (managed != 0) {
    cuDevicePrimaryCtxSetFlags(gpu_dev, 0x04);
  }
  return managed != 0
    && cuDevicePrimaryCtxRetain(&ctx, gpu_dev) == CUDA_SUCCESS
    && (gpu_ctx = ctx) != NULL
    && cuCtxSetCurrent(ctx) == CUDA_SUCCESS && gpu_ready();
}

#ifdef _WIN32

// Windows gives the GPU no concurrent access to managed memory: each launch
// takes back every managed page the host has touched, and the host's next
// touch of each costs about 0.4 ms. So on Windows the corpus is device
// memory, and the host works on a copy of it that it fetches a page at a
// time: the first touch of a page after a launch faults, and the handler
// copies the page from the device (tens of microseconds). Before the next
// launch the pages the host wrote go back, and all are given up. The copy is
// one section mapped twice, so the handler fills a page through the second
// view before the host's view of it opens.
//
// A copy of its own costs about 20 microseconds of the device's time, even
// queued, and the host touches much the same few pages every time, so small
// pages cross in a kernel instead (bend_pages), through page-locked memory the
// device reaches over the bus: the written pages go in just before a launch,
// and the pages the host held last time come out just after it, in the same
// wait, ready before the host asks.

static CUdeviceptr gpu_base;  // the corpus on the device
static char*       gpu_fill;  // the host's copy, always writable
static u8*         gpu_held;  // per page: 0 not held, 1 held, 2 held and written, 3 hot
static u32*        gpu_list;  // the pages held
static u32         gpu_nheld;
static u32*        gpu_last;  // the pages held when last given up
static u32         gpu_nlast;
static u64         gpu_pages;
static SRWLOCK     gpu_lock = SRWLOCK_INIT;
static u64 io_tick(void);
static u64         gpu_faults;  // pages fetched on a fault, and the time, so far (for measuring)
static u64         gpu_fault_ns;
static CUfunction  gpu_pages_fn;
static u32*        gpu_in_list;  // page-locked: pages going in and their words
static u64*        gpu_in_stage;
static u32*        gpu_out_list; // and pages coming out
static u64*        gpu_out_stage;
static u32         gpu_nout;

#define GPU_STAGE   256  // pages a kernel moves
#define GPU_RELEARN 64   // launches between relearning the hot pages
#define GPU_RUN   16   // a run this long is one plain copy

static bool gpu_guard(u64 i, u64 n, DWORD prot) {
  DWORD old;
  return VirtualProtect((char*)CORPUS + (i << 12), n << 12, prot, &old) != 0;
}

static LONG CALLBACK gpu_fault(EXCEPTION_POINTERS* x) {
  char* at = (char*)x->ExceptionRecord->ExceptionInformation[1];
  if (x->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION
    || at < (char*)CORPUS || at >= (char*)CORPUS + (gpu_pages << 12)) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  u64  i  = (u64)(at - (char*)CORPUS) >> 12;
  bool ok = true;
  AcquireSRWLockExclusive(&gpu_lock);
  if (gpu_held[i] == 0) {
    u64 t = io_tick();
    cuCtxSetCurrent(gpu_ctx);
    ok = cuMemcpyDtoH(gpu_fill + (i << 12), gpu_base + (i << 12), 4096)
      == CUDA_SUCCESS;
    gpu_faults += 1, gpu_fault_ns += io_tick() - t;
    gpu_held[i]              = 1;
    gpu_list[gpu_nheld++]    = (u32)i;
  }
  if (x->ExceptionRecord->ExceptionInformation[0] == 1) {
    gpu_held[i] = 2;
  }
  ok = ok && gpu_guard(i, 1, gpu_held[i] == 2 ? PAGE_READWRITE : PAGE_READONLY);
  ReleaseSRWLockExclusive(&gpu_lock);
  return ok ? EXCEPTION_CONTINUE_EXECUTION : EXCEPTION_CONTINUE_SEARCH;
}

static int gpu_page_cmp(const void* a, const void* b) {
  u32 x = *(const u32*)a, y = *(const u32*)b;
  return (x > y) - (x < y);
}

static void gpu_pages_run(u32* list, u32 n, u64* stage, u32 in) {
  void* args[] = { &gpu_base, &list, &n, &stage, &in };
  if (cuLaunchKernel(gpu_pages_fn, n, 1, 1, 128, 1, 1, 0, NULL, args, NULL)
    != CUDA_SUCCESS) {
    err_fail("device launch failed");
  }
}

// Sends the pages the host may have written back to the device (the written
// ones and the hot ones). With give_up (before a launch) the held pages are
// given up, all but up to GPU_STAGE of them, which stay hot: writable, and
// fetched again after the launch (gpu_fetch_take). The host touches much the
// same pages every time, so after the first time it touches a page, the page
// costs no protection change at all: at hundreds of frames a second the
// changes ran to a hundred thousand a second. Once a page is hot it would
// stay hot, so every GPU_RELEARN launches all are given up, and the ones the
// host still touches turn hot again (a few hundred changes every so often,
// against moving every page it ever touched at every launch). Without
// give_up the written pages become readable only, and the hot ones stay; and
// only pages lo to hi are sent (the ones a copy on the device is about to
// read).
static void gpu_send(bool give_up, u64 lo, u64 hi) {
  static u32 launches;
  AcquireSRWLockExclusive(&gpu_lock);
  qsort(gpu_list, gpu_nheld, sizeof *gpu_list, gpu_page_cmp);
  u32 m = 0, kept = 0;
  u32 room = give_up && ++launches % GPU_RELEARN == 0 ? 0 : GPU_STAGE;
  for (u32 k = 0, j; k < gpu_nheld; k = j) {
    u64 i  = gpu_list[k];
    u8  st = gpu_held[i];
    for (j = k + 1; j < gpu_nheld && gpu_list[j] == i + (j - k)
      && gpu_held[gpu_list[j]] == st; j += 1) {
    }
    u64 n = j - k;
    if (!give_up) {
      u64 a = i < lo ? lo : i, b = i + n < hi ? i + n : hi;
      if (a >= b) {
        continue;
      }
      i = a, n = b - a;
    }
    if (st >= 2 && n >= GPU_RUN) {
      if (cuMemcpyHtoD(gpu_base + (i << 12), gpu_fill + (i << 12), n << 12)
        != CUDA_SUCCESS) {
        err_fail("device copy failed");
      }
    } else if (st >= 2) {
      for (u64 p = i; p < i + n; p += 1) {
        if (m == GPU_STAGE) {
          gpu_pages_run(gpu_in_list, m, gpu_in_stage, 1);
          cuCtxSynchronize();
          m = 0;
        }
        memcpy(gpu_in_stage + ((u64)m << 9), gpu_fill + (p << 12), 4096);
        gpu_in_list[m++] = (u32)p;
      }
    }
    if (give_up) {
      u64 keep = kept + n <= room ? n : room - kept;
      for (u64 p = i; p < i + keep; p += 1) {
        gpu_last[kept++] = (u32)p;  // (its state until the fetch: see gpu_fetch_take)
      }
      if (keep < n) {
        gpu_guard(i + keep, n - keep, PAGE_NOACCESS);
        memset(gpu_held + i + keep, 0, n - keep);
      }
    } else if (st == 2) {
      gpu_guard(i, n, PAGE_READONLY);
      memset(gpu_held + i, 1, n);
    }
  }
  if (m > 0) {
    // (the next use of the stage is after a wait, which this kernel precedes)
    gpu_pages_run(gpu_in_list, m, gpu_in_stage, 1);
  }
  if (give_up) {
    gpu_nlast = kept, gpu_nheld = 0;
  }
  ReleaseSRWLockExclusive(&gpu_lock);
}

// After a launch, before its wait: has the hot pages copied out.
static void gpu_fetch_queue(void) {
  gpu_nout = gpu_nlast;
  memcpy(gpu_out_list, gpu_last, gpu_nout * sizeof *gpu_out_list);
  if (gpu_nout > 0) {
    gpu_pages_run(gpu_out_list, gpu_nout, gpu_out_stage, 0);
  }
}

// After the wait: the hot pages, fresh, and writable (a page turning hot
// changes protection this once).
static void gpu_fetch_take(void) {
  AcquireSRWLockExclusive(&gpu_lock);
  for (u32 m = 0; m < gpu_nout; m += 1) {
    u64 i = gpu_out_list[m];
    memcpy(gpu_fill + (i << 12), gpu_out_stage + ((u64)m << 9), 4096);
    if (gpu_held[i] != 3) {
      gpu_guard(i, 1, PAGE_READWRITE);
      gpu_held[i] = 3;
    }
    gpu_list[gpu_nheld++] = (u32)i;
  }
  gpu_nout = 0;
  ReleaseSRWLockExclusive(&gpu_lock);
}

static u64* gpu_map(u64 bytes) {
  HANDLE sec = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
    (DWORD)(bytes >> 32), (DWORD)bytes, NULL);
  char*  view = sec ? MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, bytes) : NULL;
  gpu_fill    = sec ? MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, bytes) : NULL;
  gpu_pages   = bytes >> 12;
  gpu_held    = calloc(gpu_pages, 1);
  gpu_list    = malloc(gpu_pages * sizeof *gpu_list);
  gpu_last    = malloc(gpu_pages * sizeof *gpu_last);
  DWORD old;
  if (view == NULL || gpu_fill == NULL || gpu_held == NULL || gpu_list == NULL
    || gpu_last == NULL
    || cuMemAllocHost((void**)&gpu_in_list, GPU_STAGE * 4) != CUDA_SUCCESS
    || cuMemAllocHost((void**)&gpu_out_list, GPU_STAGE * 4) != CUDA_SUCCESS
    || cuMemAllocHost((void**)&gpu_in_stage, GPU_STAGE << 12) != CUDA_SUCCESS
    || cuMemAllocHost((void**)&gpu_out_stage, GPU_STAGE << 12) != CUDA_SUCCESS
    || !VirtualProtect(view, bytes, PAGE_NOACCESS, &old)
    || cuMemAlloc(&gpu_base, bytes) != CUDA_SUCCESS
    || cuMemsetD8(gpu_base, 0, bytes) != CUDA_SUCCESS) {
    err_fail("corpus reservation failed");
  }
  AddVectoredExceptionHandler(1, gpu_fault);
  return (u64*)view;
}

// A host address in the corpus, on the device, once the host's writes to its
// next bytes have gone back.
static CUdeviceptr gpu_at(const void* p, u64 bytes) {
  u64 at = (u64)((const char*)p - (const char*)CORPUS);
  gpu_send(false, at >> 12, (at + bytes + 4095) >> 12);
  return gpu_base + at;
}

#define gpu_dev_base() gpu_base
#define gpu_fault_count() gpu_faults
#define gpu_fault_time()  gpu_fault_ns

#else

#define gpu_send(give_up, lo, hi)
#define gpu_fetch_queue()
#define gpu_fetch_take()
#define gpu_at(p, bytes) ((CUdeviceptr)(uintptr_t)(p))
#define gpu_dev_base() ((CUdeviceptr)(uintptr_t)CORPUS)
#define gpu_fault_count() 0ull
#define gpu_fault_time()  0ull

static u64* gpu_map(u64 bytes) {
  CUdeviceptr p = 0;
  if (cuMemAllocManaged(&p, bytes, CU_MEM_ATTACH_GLOBAL) != CUDA_SUCCESS) {
    err_fail("corpus reservation failed");
  }
#if CUDA_VERSION >= 13000
  cuMemAdvise(p, bytes, CU_MEM_ADVISE_SET_PREFERRED_LOCATION,
    (CUmemLocation){ CU_MEM_LOCATION_TYPE_DEVICE, gpu_dev });
#else
  cuMemAdvise(p, bytes, CU_MEM_ADVISE_SET_PREFERRED_LOCATION, gpu_dev);
#endif
  return (u64*)(uintptr_t)p;
}

#endif

static bool gpu_make(const char* path) {
  int cc[2] = {0, 0};
  cuDeviceGetAttribute(cc,
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, gpu_dev);
  cuDeviceGetAttribute(cc + 1,
    CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, gpu_dev);
  char arch[40];
  char bag[24];
  snprintf(arch, sizeof arch, "--gpu-architecture=sm_%d%d", cc[0], cc[1]);
  snprintf(bag, sizeof bag, "-DCUBE_LOG=%u", CUBE_LOG);
  const char* opts[] = { arch, bag, "--fmad=false", "-default-device" };
  if (!gpu_open_rtc()) {
    err_fail("cannot find NVRTC to compile the GPU program");
  }
  nvrtcProgram prog;
  if (nvrtcCreateProgram(&prog, BEND_SRC, "bend.cu", 0, NULL, NULL)
    != NVRTC_SUCCESS) {
    err_fail("cannot compile the CUDA library");
  }
  if (nvrtcCompileProgram(prog, 4, opts) != NVRTC_SUCCESS) {
    size_t n = 0;
    nvrtcGetProgramLogSize(prog, &n);
    char* log = calloc(n + 1, 1);
    if (log != NULL && nvrtcGetProgramLog(prog, log) == NVRTC_SUCCESS) {
      fprintf(stderr, "%s\n", log);
    }
    err_fail("cannot compile the CUDA library");
  }
  size_t len = 0;
  nvrtcGetCUBINSize(prog, &len);
  char* bin = malloc(len);
  if (bin == NULL || nvrtcGetCUBIN(prog, bin) != NVRTC_SUCCESS) {
    err_fail("cannot load the CUDA library");
  }
  nvrtcDestroyProgram(&prog);
  u64   key = gpu_hash();
  FILE* out = path == NULL ? NULL : fopen(path, "wb");
  bool  ok  = out != NULL && fwrite(&key, 8, 1, out) == 1
    && fwrite(bin, 1, len, out) == len && fclose(out) == 0;
  if (cuModuleLoadData(&gpu_lib, bin) != CUDA_SUCCESS) {
    err_fail("cannot load the CUDA library");
  }
  free(bin);
  return path == NULL || ok;
}

static u64 gpu_span(void) {
  size_t span = 0;
  cuDeviceTotalMem(&span, gpu_dev);
  return span;
}

static void gpu_load(u64 bytes) {
  const char* path = gpu_path();
  FILE*       in   = fopen(path, "rb");
  long        size = in != NULL && fseek(in, 0, SEEK_END) == 0 ? ftell(in) : 0;
  char*       bin  = size > 8 ? malloc((size_t)size) : NULL;
  u64         key  = 0;
  if (bin != NULL && fseek(in, 0, SEEK_SET) == 0
    && fread(bin, 1, (size_t)size, in) == (size_t)size) {
    memcpy(&key, bin, 8);
  }
  if (in != NULL) {
    fclose(in);
  }
  if (key != gpu_hash()
    || cuModuleLoadData(&gpu_lib, bin + 8) != CUDA_SUCCESS) {
    gpu_note(path);
    gpu_make(path);
  }
  free(bin);
  if (cuModuleGetFunction(&gpu_pso, gpu_lib, "bend_dev") != CUDA_SUCCESS) {
    err_fail("cannot load the GPU program");
  }
#ifdef _WIN32
  if (cuModuleGetFunction(&gpu_pages_fn, gpu_lib, "bend_pages") != CUDA_SUCCESS) {
    err_fail("cannot load the GPU program");
  }
#endif
}

static void gpu_kernel(u32 pass, u32 groups) {
  CUdeviceptr base = gpu_dev_base();
  void* args[] = { &base, &pass };
  if (cuLaunchKernel(gpu_pso, groups, 1, 1, CUBE_T, 1, 1, TG_HOLD * 8, NULL,
    args, NULL) != CUDA_SUCCESS) {
    err_fail("device launch failed");
  }
}

static void gpu_pass(u32 f) {
  gpu_send(true, 0, ~0ull);
  gpu_run(f);
  gpu_fetch_queue();
  if (cuCtxSynchronize() != CUDA_SUCCESS) {
    err_fail("device fault");
  }
  gpu_fetch_take();
}

#else

#define gpu_probe() false
#define gpu_make(p) true
#define gpu_span()  0
#define gpu_load(b)
#define gpu_pass(f)

#endif

// Cube
// ====

// Under a unit (CUBE_T / LINE a row) per thread, the host's column grows
// to the rows that give one, no more: each touches a page of every plane.

static void cube_run(u64* H, bool gpu) {
  for (;;) {
    u32 f = a32_exch(a32_at(H, H_CURSOR), 0);
    if (root_done(H)) {
      return;
    }
    if (f == 0) {
      err_fail("frontier drained without a result");
    }
    if (gpu) {
      gpu_pass(f);
    } else {
      if (f * (CUBE_T / LINE) < pool_size) {
        row_grow((Env){ H, ALC[0] }, io_stk, 0, CUBE_G,
          (pool_size + CUBE_T / LINE - 1) / (CUBE_T / LINE));
      }
      if (f < CUBE) {
        pool_turn(true);
      }
      pool_turn(false);
    }
    u32 ec = a32_load(a32_at(H, H_ERROR_CODE));
    if (ec != 0) {
      err_post(H, ec);
    }
  }
}

// Corpus
// ======

// The cores map 8 GiB at a high base and double it in place, so one
// base holds every location; the banks move up past the pages. The GPU maps
// its whole span at once, and never grows it.

static u64 corpus_size;

static void* corpus_map(u64 size) {
  u64   hint = 1ull << 45;
  void* p    = pool_try((void*)hint, size);
  while (p != (void*)hint && hint > size) {
    if (p != MAP_FAILED) {
      pool_free(p, size);
    }
    hint /= 2;
    p     = pool_try((void*)hint, size);
  }
  if (p == MAP_FAILED) {
    err_fail("reservation failed");
  }
  return p;
}

static void corpus_lay(u64* H, u64 size) {
  u64 span = size / 8;
  u64 cap  = span > HEAP_OFF ? (span - HEAP_OFF) / (PAGE_LEN + 10) : 0;
  if (cap <= CUBE) {
    err_fail("the GPU span is under the rings, stacks and a page per lane");
  }
  cap = cap < ~0u ? cap : ~0u - 1;
  u64 at = HEAP_OFF + (cap << PAGE_BITS);
  for (u32 c = 0; c < NCLS_ALL; c += 1) {
    Bank* b = bank_at(H, c);
    memcpy(H + at, H + b->off, b->wr * sizeof(u64));
    b->off  = at;
    at     += 2 * (cap >> ((c < NCLS ? NCLS : c) - PAGE_BITS));
  }
  corpus_size = size;
  a32_store_rel(a32_at(H, H_CAP), (u32)cap);
}

static bool corpus_grow(u64* H, u64 need) {
  bool ok = true;
  LOCK(bank_lock);
  while (ok && need > a32_load(a32_at(H, H_CAP))) {
    u64   more = corpus_size;
    char* at   = (char*)H + more;
    void* got  = io_gpu || more >= 1ull << 43 ? MAP_FAILED
      : pool_try(at, more);
    ok = got == at;
    if (ok) {
      corpus_lay(H, more * 2);
    } else if (got != MAP_FAILED) {
      pool_free(got, more);
    }
  }
  UNLOCK(bank_lock);
  return ok;
}

static u64* corpus_setup(bool gpu, long threads, u64 bytes) {
  io_gpu     = gpu;
  KEEP_WORDS = gpu ? CHUNK : CAP_WORDS;
  u64 dflt   = gpu ? gpu_span() : 1ull << 33;
  u64 size   = (gpu && bytes != 0 ? bytes : dflt) & ~16383ull;
  CORPUS     = gpu ? gpu_map(size) : corpus_map(size);
  u64* H     = CORPUS;
#if BEND_CUDA
  if (gpu) {
    cuMemsetD8(gpu_at(H, STAK_OFF * 8), 0, STAK_OFF * 8);
    cuCtxSynchronize();
  }
#endif
  corpus_lay(H, size);
  memcpy(H + STAT_OFF, STAT_IMG, STAT_LEN * sizeof(u64));
  a32_store(a32_at(H, H_BUMP), 1);
  if (gpu) {
    gpu_load(size);
  }
  pool_size = threads < 1 ? 1 : threads < CUBE_T ? threads : CUBE_T;
  return H;
}

OUTLINE Term corpus_eval(u64* H, Term t) {
  Env  e = { H, ALC[0] };
  Term rv[WL_RESW];
  for (;;) {
    Term r = work_loop(e, io_stk, t, !BANGS && pool_size == 1);
    if (r == 0) {
      if (root_done(H)) {
        break;
      }
      err_fail("solo delivery lost");
    }
    if ((u32)H[task_tail(r) + 1] == 0) {
      t = r;
      if (io_gpu && fid_bangs((u32)term_aux(t))) {
        u64  tl   = task_tail(t);
        Term cont = H[tl];
        u32  idx  = (u32)(H[tl + 1] >> 32) & 0xFFFF;
        H[tl]     = TERM_HOLE;
        a32_store(a32_at(H, H_CURSOR), 1);
        // Ring 0 is empty between bangs, so it starts from its first slot
        // again: the host then writes one slot, on one page, not the next of
        // its 1,024 (a page apart), which on Windows is a fetch every bang.
        if (*ring_get(H, 0) == *ring_put(H, 0)) {
          *ring_get(H, 0) = *ring_put(H, 0) = 0;
        }
        ring_push(H, 0, t);
        cube_run(H, true);
        heap_free(e, cls_fit(fid_arity((u32)term_aux(t)) + 2), term_loc(t));
        Term p = task_deliver(H, cont, idx, rv, root_take(H, rv));
        if (root_done(H)) {
          break;
        }
        if (p == 0) {
          err_fail("seam delivery lost");
        }
        t = p;
      }
      continue;
    }
    task_deal(H, r, 0, 0, NULL);
    pool_open();
    cube_run(H, false);
    break;
  }
  root_take(H, rv);
  return rv[0];
}

// Io
// ==

// Base's opaque, linear handles pack host fds or pointers into aux and loc:
// no forging, copying, reuse or host wrapper. A request's cont applied to
// its item is the next request. A parked request keeps its fd, deadline and
// readiness in word, time and evts; the loop then calls pack: a value
// resumes, IO_PARK parks again. The edge is UTF-8, decoded as WHATWG does: a
// broken sequence yields one U+FFFD and its breaking byte is read again as a
// lead. inet_aton reads a leading zero as octal, so io_sys_addr refuses it.
// macOS poll misses FIFO EOF, so io_wait selects, its sets sized to the
// highest fd (_DARWIN_UNLIMITED_SELECT allows fds past FD_SETSIZE).

#include <errno.h>
#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#define IO_READ 1
#define IO_TIME 2
#define IO_PARK TERM_HOLE

#define io_hand(v)   term_make(TAG_PAK, (u64)(v) >> 40, (u64)(v) & LOC_MASK)
#define io_hand_v(t) (((u64)term_aux(t) << 40) | term_loc(t))

struct IoWork;
typedef void (*IoCall)(struct IoWork* w);
typedef Term (*IoPack)(Env e, struct IoWork* w);

typedef struct IoWork {
  intptr_t       hand;
  intptr_t       made;
  u32            word;
  u64            size;
  char*          data;
  char*          text;
  u32            code;
  IoCall         call;
  IoPack         pack;
  Term           cont;
  Term           item;
  u64            time;
  short          evts;
  struct IoWork* next;
} IoWork;

typedef Term (*Effect)(Env e, Term* f, IoWork* w);

typedef struct {
  Effect run;
  u32    ask;
} IoEff;

static IoEff io_eff_rows[1 << 16];
static u32   io_live;

static u64 io_tick(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (u64)ts.tv_sec * 1000000000ull + (u64)ts.tv_nsec;
}

OUTLINE void* io_mem(void* mem) {
  if (mem == NULL) {
    err_fail("host allocation failed");
  }
  return mem;
}

#ifndef _WIN32
static int io_sys_addr(const char* host, u32 port, struct sockaddr_in* at) {
  memset(at, 0, sizeof(*at));
  at->sin_family = AF_INET;
  at->sin_port   = htons((uint16_t)port);
  for (const char* p = host; *p != 0; p += 1) {
    if ((p == host || p[-1] == '.') && *p == '0'
      && p[1] >= '0' && p[1] <= '9') {
      return -1;
    }
  }
  return port > 65535 || inet_pton(AF_INET, host, &at->sin_addr) != 1
    ? -1 : 0;
}
#endif

static int    io_argc;
static char** io_argv;

static void io_eff(u32 cid, Effect run, u32 need) {
  if (io_eff_rows[cid].run != NULL) {
    err_fail("two effects register one request");
  }
  io_eff_rows[cid] = (IoEff){ run, need };
}

static u64 io_sys_end(IoWork* w, ssize_t n) {
  w->code = n < 0 ? (u32)errno : 0;
  return n < 0 ? 0 : (u64)n;
}

static IoWork* io_runs;
static IoWork* io_park;
static IoWork* io_jobs;

static void io_push(IoWork** q, IoWork* a) {
  IoWork* l = *q != NULL ? *q : a;
  a->next = l->next;
  l->next = a;
  *q      = a;
}

static IoWork* io_pop(IoWork** q) {
  IoWork* a  = (*q)->next;
  (*q)->next = a->next;
  *q         = a != *q ? *q : NULL;
  return a;
}

static void io_spawn(Term m) {
  IoWork* a = io_mem(calloc(1, sizeof(IoWork)));
  a->cont  = m;
  a->item  = term_clo(FID_IO_EMIT, 0);
  io_push(&io_runs, a);
  io_live += 1;
}

static Term io_wait_on(IoWork* w, int fd, short evts, u64 time, IoPack more) {
  w->word = (u32)fd;
  w->pack = more;
  w->time = time;
  w->evts = evts;
  io_push(&io_park, w);
  return IO_PARK;
}

OUTLINE void io_out(FILE* h, const char* data, u64 len) {
  if (fwrite(data, 1, len, h) != len) {
    err_fail("a short write on a standard stream");
  }
}

OUTLINE void io_sync(void) {
  if (fflush(stdout) != 0) {
    err_fail("a short write on a standard stream");
  }
}

static u64 io_utf8(char* buf, u64 c) {
  u64 k = c < 0x80 ? 1 : c < 0x800 ? 2 : c < 0x10000 ? 3 : 4;
  for (u64 i = k; i > 1; i -= 1) {
    buf[i - 1] = (char)(0x80 | (c & 0x3F));
    c >>= 6;
  }
  buf[0] = (char)(k == 1 ? c : (0xF00 >> k) | c);
  return k;
}

OUTLINE char* io_cstr(Env e, Term s, u64* len) {
  u64   cap = 64;
  u64   n   = 0;
  char* buf = io_mem(malloc(cap));
  while (term_aux(s) == CID_SCON) {
    Term fb[2];
    spare_free(e, cls_fit(2), ctr_take(e, s, 2, fb));
    if (n + 5 > cap) {
      cap *= 2;
      buf = io_mem(realloc(buf, cap));
    }
    n += io_utf8(buf + n, fb[0]);
    s = fb[1];
  }
  buf[n] = 0;
  *len = n;
  return buf;
}

OUTLINE void io_errs(Env e, Term s) {
  u64   n    = 0;
  char* text = io_cstr(e, s, &n);
  io_sync();
  io_out(stderr, text, n);
  io_out(stderr, "\n", 1);
  free(text);
}

#define io_nul(s, n) (strlen(s) != (n))

#define io_seal(e, t, cid) (cid_hot(cid) ? rfc_seal(e, t) : (t))

static Term io_node(Env e, u64 cid, Term a, Term b) {
  u64 l = heap_alloc(e, 1);
  e.mem[l]     = io_seal(e, a, cid);
  e.mem[l + 1] = io_seal(e, b, cid);
  return term_ctr(cid, l);
}

static Term io_str(Env e, const char* p, u64 n) {
  Term s    = term_pak(CID_SNIL, 0);
  u64  hole = 0;
  u64  c = 0, need = 0, lo = 0x80, hi = 0xBF;
  for (u64 i = 0; i < n || need > 0; i += 1) {
    u64 b = i < n ? (uint8_t)p[i] : 0x100;
    if (need > 0 && (b < lo || b > hi)) {
      need = 0;
      c    = 0xFFFD;
      i   -= 1;
    } else if (need > 0) {
      lo = 0x80;
      hi = 0xBF;
      c  = (c << 6) | (b & 0x3F);
      if (--need > 0) {
        continue;
      }
    } else if (b < 0x80) {
      c = b;
    } else if (b < 0xC2 || b > 0xF4) {
      c = 0xFFFD;
    } else {
      need = b < 0xE0 ? 1 : b < 0xF0 ? 2 : 3;
      lo   = b == 0xE0 ? 0xA0 : b == 0xF0 ? 0x90 : 0x80;
      hi   = b == 0xED ? 0x9F : b == 0xF4 ? 0x8F : 0xBF;
      c    = b & (0x3F >> need);
      continue;
    }
    u64  l = heap_alloc(e, 1);
    Term t = term_ctr(CID_SCON, l);
    e.mem[l] = c;
    if (hole == 0) {
      s = t;
    } else {
      e.mem[hole] = io_seal(e, t, CID_SCON);
    }
    hole = l + 1;
  }
  if (hole != 0) {
    e.mem[hole] = io_seal(e, term_pak(CID_SNIL, 0), CID_SCON);
  }
  return s;
}

#define io_tup(e, a, b) io_node(e, CID_TUPLE, a, b)
#define io_done(e, v)   io_box(e, CID_DONE, v)

static Term io_box(Env e, u64 cid, Term v) {
  u64 l = heap_alloc(e, 0);
  e.mem[l] = io_seal(e, v, cid);
  return term_ctr(cid, l);
}

static Term io_fail(Env e, u32 code, const char* text) {
  const char* s = text != NULL ? text : strerror((int)code);
  Term t = io_tup(e, code, io_str(e, s, strlen(s)));
  return io_box(e, CID_FAIL, t);
}

static pthread_mutex_t io_gate = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  io_bell = PTHREAD_COND_INITIALIZER;
static u32             io_busy;
static u32             io_size;
#ifdef _WIN32

// Windows select() takes sockets only, so a finished work item goes on a
// list under io_gate and rings io_done_bell, which io_wait sleeps on.
static IoWork*        io_done;
static pthread_cond_t io_done_bell = PTHREAD_COND_INITIALIZER;

static void io_take(Env e) {
  pthread_mutex_lock(&io_gate);
  IoWork* acts = io_done;
  io_done      = NULL;
  pthread_mutex_unlock(&io_gate);
  while (acts != NULL) {
    IoWork* a = io_pop(&acts);
    a->item   = a->pack(e, a);
    io_push(&io_runs, a);
    io_busy -= 1;
  }
}

static void io_ring(IoWork* a) {
  pthread_mutex_lock(&io_gate);
  io_push(&io_done, a);
  pthread_cond_signal(&io_done_bell);
  pthread_mutex_unlock(&io_gate);
}

#else

static int io_wake_fd[2];

static void io_take(Env e) {
  IoWork* acts[64];
  ssize_t n;
  while ((n = read(io_wake_fd[0], acts, sizeof acts)) > 0) {
    for (u32 i = 0; i < (u32)n / sizeof(IoWork*); i += 1) {
      IoWork* a = acts[i];
      a->item   = a->pack(e, a);
      io_push(&io_runs, a);
      io_busy -= 1;
    }
  }
}

static void io_ring(IoWork* a) {
  while (write(io_wake_fd[1], &a, sizeof a) != sizeof a) {
  }
}

#endif

static void* io_help(void* arg) {
  for (;;) {
    pthread_mutex_lock(&io_gate);
    while (io_jobs == NULL) {
      pthread_cond_wait(&io_bell, &io_gate);
    }
    IoWork* a = io_pop(&io_jobs);
    pthread_mutex_unlock(&io_gate);
    a->call(a);
    io_ring(a);
  }
}

static Term io_work(IoWork* w, IoCall call, IoPack pack) {
  w->call  = call;
  w->pack  = pack;
  io_busy += 1;
  if (io_busy > io_size && io_size < IO_HELP) {
    pthread_t tid;
    if (pthread_create(&tid, NULL, io_help, NULL)) {
      err_fail("pthread_create");
    }
    pthread_detach(tid);
    io_size += 1;
  }
  pthread_mutex_lock(&io_gate);
  io_push(&io_jobs, w);
  pthread_cond_signal(&io_bell);
  pthread_mutex_unlock(&io_gate);
  return IO_PARK;
}

static Term io_exec(Env e, IoWork* w) {
  Term fs[256];
  u32  c = (u32)term_aux(w->cont);
  u32  n = cid_arity(c);
  spare_free(e, cls_fit(n), ctr_take(e, w->cont, n, fs));
  w->cont = fs[n - 1];
  return io_eff_rows[c].run(e, fs, w);
}

static bool io_bit(u8* set, int fd, bool put) {
  u8* at = set + fd / 8;
  *at |= put << fd % 8;
  return *at >> fd % 8 & 1;
}

#ifdef _WIN32

// Sleeps until a work item finishes or the soonest timer is due. Waiting on
// a handle (a socket or a process) is not supported on Windows yet.
static void io_wait(Env e) {
  u64 soon = 0;
  for (IoWork* a = io_park; a != NULL;
    a = a->next != io_park ? a->next : NULL) {
    if (a->evts != 0) {
      err_fail("waiting on a socket or process is not supported on Windows");
    }
    if (a->time != 0 && (soon == 0 || a->time < soon)) {
      soon = a->time;
    }
  }
  io_sync();
  pthread_mutex_lock(&io_gate);
  if (io_done == NULL) {
    if (soon == 0) {
      pthread_cond_wait(&io_done_bell, &io_gate);
    } else {
      u64 tick = io_tick();
      if (soon > tick) {
        struct timespec at;
        clock_gettime(CLOCK_REALTIME, &at);
        u64 ns = (u64)at.tv_nsec + (soon - tick);
        at.tv_sec  += (time_t)(ns / 1000000000ull);
        at.tv_nsec  = (long)(ns % 1000000000ull);
        pthread_cond_timedwait(&io_done_bell, &io_gate, &at);
      }
    }
  }
  pthread_mutex_unlock(&io_gate);
  io_take(e);
  u64     now  = io_tick();
  IoWork* todo = io_park;
  io_park = NULL;
  while (todo != NULL) {
    IoWork* a = io_pop(&todo);
    if (a->time == 0 || a->time > now) {
      io_push(&io_park, a);
      continue;
    }
    Term x = a->pack(e, a);
    if (x != IO_PARK) {
      a->item = x;
      io_push(&io_runs, a);
    }
  }
}

#else

static void io_wait(Env e) {
  int top  = io_wake_fd[0];
  u64 soon = 0;
  for (IoWork* a = io_park; a != NULL;
    a = a->next != io_park ? a->next : NULL) {
    if (a->time != 0 && (soon == 0 || a->time < soon)) {
      soon = a->time;
    }
    if (a->evts != 0 && (int)a->word > top) {
      top = (int)a->word;
    }
  }
  u64 len = (u64)top / 64 * 8 + 8;
  u8* set[2] = { io_mem(calloc(2, len)), NULL };
  set[1] = set[0] + len;
  io_bit(set[0], io_wake_fd[0], true);
  for (IoWork* a = io_park; a != NULL;
    a = a->next != io_park ? a->next : NULL) {
    if (a->evts != 0) {
      io_bit(set[a->evts == POLLOUT], (int)a->word, true);
    }
  }
  u64 tick = io_tick();
  u64 ms = soon > tick ? (soon - tick) / 1000000 + 1 : 0;
  struct timeval tv = { ms / 1000, ms % 1000 * 1000 };
  io_sync();
  if (select(top + 1, (fd_set*)set[0], (fd_set*)set[1], NULL,
    soon == 0 ? NULL : &tv) < 0) {
    if (errno != EINTR) {
      err_fail("the poller failed");
    }
    memset(set[0], 0, 2 * len);
  }
  if (io_bit(set[0], io_wake_fd[0], false)) {
    io_take(e);
  }
  u64     now  = io_tick();
  IoWork* todo = io_park;
  io_park = NULL;
  while (todo != NULL) {
    IoWork* a   = io_pop(&todo);
    bool    due = (a->evts != 0
        && io_bit(set[a->evts == POLLOUT], (int)a->word, false))
      || (a->time != 0 && a->time <= now);
    if (!due) {
      io_push(&io_park, a);
      continue;
    }
    Term x = a->pack(e, a);
    if (x != IO_PARK) {
      a->item = x;
      io_push(&io_runs, a);
    }
  }
  free(set[0]);
}

#endif

static int f32_text(char* buf, f32 v) {
  int n = 0;
  int p = 0;
  if (v != v) {
    return sprintf(buf, "nan");
  }
  for (; p < 9; p += 1) {
    n = snprintf(buf, 40, "%.*e", p, (double)v);
    if (strtof(buf, NULL) == v) {
      break;
    }
  }
  char* ep = strchr(buf, 'e');
  if (ep == NULL) {
    return n;
  }
  int ex = atoi(ep + 1);
  if (ex >= 21 || ex <= -7) {
    n = (int)(ep - buf) + sprintf(ep, "e%c%d", ex < 0 ? '-' : '+', abs(ex));
  } else if (ex <= p) {
    n = snprintf(buf, 40, "%.*f", p - ex, (double)v);
  } else {
    int s = *buf == '-';
    memmove(buf + s + 1, buf + s + 2, p);
    memset(buf + s + 1 + p, '0', ex - p);
    n = s + 1 + ex;
  }
  return n;
}

static Term f32_show(Env e, Term x) {
  char buf[40];
  return io_str(e, buf, f32_text(buf, f32_unbox(x)));
}

static Term f32_read(Env e, Term s) {
  u64 n = 0;
  char* text = io_cstr(e, s, &n);
  char* end;
  f32 v = strtof(text, &end);
  Term out = n > 0 && (u64)(end - text) == n && strpbrk(text, "xX(") == NULL
    ? io_box(e, CID_SOME, f32_rewrap(v)) : term_pak(CID_NONE, 0);
  free(text);
  return out;
}


// Show
// ====

// show_val prints a pure main's value as term_show spells it: d
// is a SHOW_DESC node (see show_main), w its words, and chain the
// bracket of the [a, b] or (a, b) the value continues, or 0. Con
// or Nil spell a list, Tuple a tuple, and their tails continue
// it. show_chr escapes as char_show does; show_f32 prints the
// shortest text that reads back, with a point before an e.

#if MAIN_PURE

static void show_val(Env e, u32 d, const Term* w, char chain);

static void show_chr(u64 c, char q) {
  char b[4];
  int  k = c == 10 ? 'n' : c == 9 ? 't' : c == 13 ? 'r' : c == 0 ? '0'
    : c == 92 || c == (u64)q ? (int)c : 0;
  if (k != 0) {
    printf("\\%c", k);
  } else if (c < 32 || c == 127 || (c >= 0xD800 && c <= 0xDFFF)
    || c > 0x10FFFF) {
    printf("\\u{%llx}", (unsigned long long)c);
  } else {
    fwrite(b, 1, io_utf8(b, c), stdout);
  }
}

static void show_f32(u32 x) {
  char  buf[40];
  int   n  = f32_text(buf, f32_unbox(x));
  char* ep = memchr(buf, 'e', n);
  int   m  = ep == NULL ? n : (int)(ep - buf);
  buf[n] = 0;
  if (strpbrk(buf, ".ni") == NULL) {
    printf("%.*s.0%s", m, buf, buf + m);
  } else {
    fputs(buf, stdout);
  }
}

static void show_val(Env e, u32 d, const Term* w, char chain) {
  const u32* D = SHOW_DESC;
  Term one;
  char zs[4];
  u32  zn = 0;
  for (bool tail = true; tail;) switch (tail = false, D[d]) {
    case 0:
      printf("%u", (u32)w[0]);
      break;
    case 1:
      show_f32((u32)w[0]);
      break;
    case 2:
      printf("%llun", (unsigned long long)w[0]);
      break;
    case 3:
      putchar('\'');
      show_chr(D[d + 1] != 0 ? term_loc(w[0]) : w[0], '\'');
      putchar('\'');
      break;
    case 4:
      putchar('"');
      for (Term s = w[0]; term_aux(s) == CID_SCON;) {
        u64 l = term_peek(e, s);
        show_chr(e.mem[l], '"');
        s = e.mem[l + 1];
      }
      putchar('"');
      break;
    case 5:
      fputs("{==}", stdout);
      break;
    case 6:
      putchar('[');
      for (u32 i = 0, g = D[d + 2]; i < 1u << (blk_cls(w[0]) - g); i += 1) {
        Term v[1u << g];
        for (u32 j = 0; j < 1u << g; j += 1) {
          v[j] = blk_read(e.mem, term_tag(w[0]) == TAG_ARR,
            term_peek(e, w[0]), (i << g) + j);
        }
        fputs(i > 0 ? ", " : "", stdout);
        show_val(e, D[d + 1], v, 0);
      }
      putchar(']');
      break;
    default: {
      Term t   = w[0];
      bool box = D[d + 1] != 0;
      u32  key = box ? (u32)term_aux(t) : D[d + 2] > 1 ? (u32)t : 0;
      u32  a   = d + 3;
      for (u32 i = 0; box ? D[a + 1] != key : i != key; i += 1) {
        a += 4 + 2 * D[a + 2];
      }
      if (box) {
        one = term_loc(t);
        w   = term_tag(t) == TAG_PAK ? &one : e.mem + term_peek(e, t);
      }
      char o = "{[("[D[a + 3]];
      if (o == '{') {
        printf("%s{", SHOW_NAMES[D[a]]);
      } else if (chain != o) {
        putchar(o);
      }
      if (o == '{' || chain != o) {
        zs[zn++] = "}])"[D[a + 3]];
      }
      for (u32 j = 0; j < D[a + 2]; j += 1) {
        if (o == '[' ? j == 0 && chain == o : j > 0) {
          fputs(", ", stdout);
        }
        if (j == 1 && o != '{') {
          tail  = true;
          chain = o;
          d     = D[a + 5 + 2 * j];
          w     = w + D[a + 4 + 2 * j];
        } else {
          show_val(e, D[a + 5 + 2 * j], w + D[a + 4 + 2 * j], 0);
        }
      }
    }
  }
  while (zn > 0) {
    putchar(zs[--zn]);
  }
}

#endif

// Run
// ===

static void io_step(Env e, IoWork* a) {
  for (;;) {
    u64  ap  = task_node(e, FID_CLO_APPLY, TERM_HOLE, 0, 0);
    e.mem[ap]     = a->cont;
    e.mem[ap + 1] = a->item;
    Term req = corpus_eval(e.mem, term_tsk(FID_CLO_APPLY, ap));
    u32  c   = (u32)term_aux(req);
    u64  at  = term_peek(e, req);
    if (c == CID_EMIT) {
      term_drop(e, req);
      free(a);
      io_live -= 1;
      return;
    }
    if (c == CID_HALT) {
      io_errs(e, e.mem[at + 1]);
      exit((int)(u32)e.mem[at]);
    }
    if (io_eff_rows[c].run == NULL) {
      err_fail("an alien request");
    }
    u32 need = io_eff_rows[c].ask;
    u32 word = (u32)(need & IO_READ ? io_hand_v(e.mem[at]) : e.mem[at]);
    a->cont  = req;
    if (need != 0) {
      io_wait_on(a, (int)word, need & IO_READ ? POLLIN : 0,
        need & IO_TIME ? io_tick() + (u64)word * 1000000ull : 0, io_exec);
      return;
    }
    Term x = io_exec(e, a);
    if (x == IO_PARK) {
      return;
    }
    a->item = x;
  }
}

OUTLINE void io_loop(u64* H) {
  Env e = { H, ALC[0] };
  io_stk = pool_stack();
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);
  if (pipe(io_wake_fd) | fcntl(io_wake_fd[0], F_SETFL, O_NONBLOCK)) {
    err_fail("the event loop failed to open");
  }
#endif
  Term m = corpus_eval(H, term_tsk(MAIN_FID, task_node(e, MAIN_FID,
    TERM_HOLE, 0, 0)));
#if MAIN_PURE
  show_val(e, 0, H + H_ROOT_WORD, 0);
  putchar('\n');
  return;
#endif
  io_spawn(m);
  for (u32 n = 0;; n += 1) {
    if (io_runs == NULL) {
      if (io_live == 0) {
        return;
      }
      if (io_park == NULL && io_busy == 0) {
        io_sync();
        err_fail("deadlock: every computation waits on a channel");
      }
      io_wait(e);
      continue;
    }
    if ((n & 63) == 0 && io_busy != 0) {
      io_take(e);
    }
    io_step(e, io_pop(&io_runs));
  }
}

// Requests
// ========

// The player's side of viz.bend, and the effects that program imports. The Bend program runs on a
// thread of its own (its main renamed bendviz_main): each frame it blocks in Viz.next until the
// player asks, reads the request with Viz.param, draws, and hands the pixels over with Viz.show.
// The player never waits: it asks for a frame and takes the newest finished one when it is ready.
// Included in the Bend program's C (the runtime's types and helpers are in scope), and compiled
// into the player; bendviz.h declares the player's side.

#ifndef BENDVIZ_BRIDGE
#define BENDVIZ_BRIDGE

#define BENDVIZ_MAX 4096
#define BENDVIZ_PIXELS (1L << 23)  // the most pixels: the program's buffers (as in bendviz.h)
#define BENDVIZ_TEXTURES 4          // textures shared with Direct3D (as in bendviz.h)

static pthread_mutex_t bv_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  bv_asked = PTHREAD_COND_INITIALIZER;
static pthread_cond_t  bv_shown = PTHREAD_COND_INITIALIZER;  // a new frame is ready (bendviz_wait)
static bool            bv_want;                       // a request waiting for Bend
static float           bv_ask[5], bv_now[5];          // the request, and the one being drawn
static u32             bv_ask_word, bv_now_word;       // gpu flag and size, as Viz.next returns them
static u32*            bv_front;                       // the newest finished frame, rows packed
static u32*            bv_back;                        // where the next one is being put
static size_t          bv_cap;                         // pixels each of those holds
static bool            bv_pinned;                      // they are page-locked (for the GPU's copies)
static bool            bv_lent;                        // the player is reading the front buffer
// Frames the player takes on the device (graphics interop): they never visit the host.
static bool            bv_device_ok;                   // the player can take them
static unsigned long long bv_dev_front;                // the newest frame, in Bend's heap (CUdeviceptr)
static bool            bv_front_on_device;             // the newest frame is bv_dev_front
static bool            bv_front_shared;                // or in a shared texture (bendviz_d3d11_take)
// Counts for the stats: frames Bend finished and dropped, and the time from a request (the first
// not yet served) to the helper taking it and to Bend starting on it, summed over the frames.
static u64             bv_asked_at, bv_took_at;
static u64             bv_n_drawn, bv_n_dropped;
static double          bv_took_ms, bv_began_ms;

// With BENDVIZ_TRACE set to a file name, 3,000 events from 3 s after the start (requests, Bend's
// steps, the player's takes) are written there with their times, to find where frames go.
static FILE*           bv_trace_out;
static u64             bv_trace_t0;
static int             bv_trace_n;
static pthread_mutex_t bv_trace_lock = PTHREAD_MUTEX_INITIALIZER;
static u64 io_tick(void);
static void bv_trace(const char* what, long a, long b) {
  if (bv_trace_out == NULL) return;
  u64 t = io_tick();
  if (t < bv_trace_t0 + 3000000000ull) return;
  pthread_mutex_lock(&bv_trace_lock);
  if (bv_trace_out != NULL) {
    fprintf(bv_trace_out, "%.3f %s %ld %ld\n", (double)(t - bv_trace_t0) / 1e6, what, a, b);
    if (++bv_trace_n == 3000) fclose(bv_trace_out), bv_trace_out = NULL;
  }
  pthread_mutex_unlock(&bv_trace_lock);
}
#if BEND_CUDA && defined(_WIN32)
// The player's copy out of a frame into its texture is queued, and finishes on the GPU after the
// player has moved on. An event marks it done, which drawing into that buffer again waits for, on
// the GPU.
typedef struct CUevent_st* BvReadEvent;
static BvReadEvent bv_read_front;  // the player's copy out of the frame shown last
static CUresult (CUDAAPI* bv_ev_wait)(CUstream, BvReadEvent, unsigned);
static CUresult (CUDAAPI* bv_ev_mark)(BvReadEvent, CUstream);
// A frame's copy queued behind its kernels (bv_early_copy).
static struct {
  unsigned long long shown[2];  // the buffers of the last two frames shown, newest first
  pthread_t thread;             // Bend's thread
  bool drawing;                 // from the frame's start (viz_next_pack) to its show
  u32 waits;                    // waits for the GPU since the frame started
  unsigned long long at;        // the buffer copied early, or 0
} bv_early;
#endif
static int             bv_done_w, bv_done_h;
static bool            bv_fresh, bv_done_gpu;
static double          bv_ms;                          // how long the last frame took, all told
static double          bv_draw_ms, bv_copy_ms;          // of which drawing, and bringing it to the host
static u64             bv_began, bv_drawn;

// ---- The effects ----

static void viz_next_call(IoWork* w) {  // an IO helper thread: no Bend heap here
  pthread_mutex_lock(&bv_lock);
  while (!bv_want) {
    pthread_cond_wait(&bv_asked, &bv_lock);
  }
  bv_want = false;
  bv_took_at = io_tick();
  bv_trace("helper-took", 0, 0);
  memcpy(bv_now, bv_ask, sizeof bv_now);
  bv_now_word = bv_ask_word;
  pthread_mutex_unlock(&bv_lock);
}

#ifdef BENDVIZ_EMBED
static void bv_count_launches(void);
static double bv_wait_total(void);
static double bv_launch_total(void);
static void bv_edges_of(u64 drawn, double* before, double* after);
#else
static double bv_wait_total(void) { return 0; }
static double bv_launch_total(void) { return 0; }
static void bv_edges_of(u64 drawn, double* before, double* after) { (void)drawn, *before = *after = 0; }
#endif
static double bv_wait_began, bv_wait_frame;  // waiting for the GPU: by the frame's start, and in the last frame
static double bv_launch_began, bv_launch_frame;  // likewise in launch calls
static double bv_before_frame, bv_after_frame;   // the last frame's host time before its first launch and after its last wait

static Term viz_next_pack(Env e, IoWork* w) {
#ifdef BENDVIZ_EMBED
  bv_count_launches();
#endif
#if BEND_CUDA && defined(_WIN32)
  bv_early.thread = pthread_self(), bv_early.drawing = true, bv_early.waits = 0, bv_early.at = 0;
#endif
  bv_began = io_tick();
  bv_trace("began", 0, 0);
  pthread_mutex_lock(&bv_lock);
  if (bv_asked_at) {
    bv_took_ms += (double)(bv_took_at - bv_asked_at) / 1e6, bv_began_ms += (double)(bv_began - bv_took_at) / 1e6;
  }
  pthread_mutex_unlock(&bv_lock);
  bv_wait_began = bv_wait_total();
  bv_launch_began = bv_launch_total();
  return (Term)bv_now_word;
}

Term viz_next_run(Env e, Term* f, IoWork* w) {
  return io_work(w, viz_next_call, viz_next_pack);
}

Term viz_param_run(Env e, Term* f, IoWork* w) {
  u32 i = (u32)f[0];
  return f32_rewrap(i < 5 ? bv_now[i] : 0.0f);
}

// The frame is the buffer's first w * h pixels. A frame the player takes on the device stays in
// Bend's buffer. Otherwise it goes to a host back buffer, which then swaps with the front one for
// the player, so no frame is copied on the host twice; with the GPU these are page-locked and the
// copy is one bulk transfer from the device (the host would otherwise read the heap a page at a
// time). A frame the CPU drew is copied on the host.
static bool bv_room(size_t n) {
  if (n <= bv_cap) {
    return true;
  }
  bool gpu = io_gpu;
#if BEND_CUDA
  if (bv_pinned) {
    // Page-locked memory is freed with the driver's own call, which the runtime does not load;
    // the buffers only grow, rarely, so the old ones are left.
  } else
#endif
  {
    free(bv_back);
    if (!bv_lent) free(bv_front);
  }
  bv_back = bv_front = NULL, bv_cap = 0, bv_pinned = false;
#if BEND_CUDA
  if (gpu && cuMemAllocHost((void**)&bv_back, n * 4) == CUDA_SUCCESS
    && cuMemAllocHost((void**)&bv_front, n * 4) == CUDA_SUCCESS) {
    bv_pinned = true;
  }
#endif
  if (!bv_pinned) {
    bv_back = malloc(n * 4), bv_front = malloc(n * 4);
  }
  bv_cap = bv_back != NULL && bv_front != NULL ? n : 0;
  return bv_cap >= n;
}

#if BEND_CUDA && defined(_WIN32)
// ---- Frames into Direct3D textures shared with CUDA ----

// The player's side (gtube's viz.c) makes four textures and two fences, shared as NT handles
// (bendviz_d3d11_share). Bend imports them into CUDA once, and after each frame copies it into a
// texture neither on screen nor waiting to be taken, on its own stream, after waiting (on the GPU)
// for Direct3D to be done with that texture, then signals its fence. The player makes Direct3D wait
// (on the GPU) for that signal before drawing the texture, and signals its own fence as it goes. No
// texture is mapped each frame, so neither thread holds up the other's calls into the driver (the
// map cost Bend's launches 0.4 ms a frame at 4K), and the player takes a frame in no time.
typedef struct {
  int type;
  union { int fd; struct { void* handle; const void* name; } win32; const void* nvSciBufObject; } handle;
  unsigned long long size;
  unsigned int flags;
  unsigned int reserved[16];
} BvExtMemDesc;  // CUDA_EXTERNAL_MEMORY_HANDLE_DESC
typedef struct {
  unsigned long long offset;
  struct { size_t Width, Height, Depth; int Format; unsigned int NumChannels, Flags; } arrayDesc;
  unsigned int numLevels;
  unsigned int reserved[16];
} BvExtArrayDesc;  // CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC
typedef struct {
  int type;
  union { int fd; struct { void* handle; const void* name; } win32; const void* nvSciSyncObj; } handle;
  unsigned int flags;
  unsigned int reserved[16];
} BvExtSemDesc;  // CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC
typedef struct {
  struct {
    struct { unsigned long long value; } fence;
    union { void* fence; unsigned long long reserved; } nvSciSync;
    struct { unsigned long long key; } keyedMutex;
    unsigned int reserved[12];
  } params;
  unsigned int flags;
  unsigned int reserved[16];
} BvSemSignal;  // CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS
typedef struct {
  struct {
    struct { unsigned long long value; } fence;
    union { void* fence; unsigned long long reserved; } nvSciSync;
    struct { unsigned long long key; unsigned int timeoutMs; } keyedMutex;
    unsigned int reserved[10];
  } params;
  unsigned int flags;
  unsigned int reserved[16];
} BvSemWait;  // CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS
typedef struct {
  size_t srcXInBytes, srcY;
  int srcMemoryType;
  const void* srcHost;
  CUdeviceptr srcDevice;
  void* srcArray;
  size_t srcPitch;
  size_t dstXInBytes, dstY;
  int dstMemoryType;
  void* dstHost;
  CUdeviceptr dstDevice;
  void* dstArray;
  size_t dstPitch;
  size_t WidthInBytes, Height;
} BvShareCopy;  // CUDA_MEMCPY2D

static struct {
  void* tex[BENDVIZ_TEXTURES];      // from the player: NT handles, the size, and a generation
  void* fence_cuda;                 // (CUDA signals it, Direct3D waits)
  void* fence_d3d;                  // (Direct3D signals it, CUDA waits)
  int   w, h;
  u32   gen;
  // Bend's thread: the imports, of generation imported
  u32   imported;
  bool  failed;
  void* mem[BENDVIZ_TEXTURES];
  void* mip[BENDVIZ_TEXTURES];
  void* arr[BENDVIZ_TEXTURES];
  void* sem_cuda;
  void* sem_d3d;
  unsigned long long cuda_value;
  // Under bv_lock: the texture on screen, the one waiting to be taken (-1 none), the Direct3D fence
  // value after which each texture was last read, and the value CUDA signals for the waiting one.
  int   shown, published;
  unsigned long long read_done[BENDVIZ_TEXTURES], pub_value;
  unsigned long long completed;     // a value Direct3D's fence has reached (as of the last take)
  int   pending;                    // written this frame, published at the swap (-1 none)
  unsigned long long pending_value;
  bool  busy;                       // Bend's thread is using the imports (under bv_lock)
} bv_share = { .shown = -1, .published = -1, .pending = -1 };

static CUresult (CUDAAPI* bv_ext_import)(void**, const BvExtMemDesc*);
static CUresult (CUDAAPI* bv_ext_array)(void**, void*, const BvExtArrayDesc*);
static CUresult (CUDAAPI* bv_ext_level)(void**, void*, unsigned);
static CUresult (CUDAAPI* bv_ext_sem)(void**, const BvExtSemDesc*);
static CUresult (CUDAAPI* bv_ext_signal)(void* const*, const BvSemSignal*, unsigned, CUstream);
static CUresult (CUDAAPI* bv_ext_wait)(void* const*, const BvSemWait*, unsigned, CUstream);
static CUresult (CUDAAPI* bv_ext_free_mem)(void*);
static CUresult (CUDAAPI* bv_ext_free_sem)(void*);
static CUresult (CUDAAPI* bv_ext_free_mip)(void*);
static CUresult (CUDAAPI* bv_ext_copy)(const BvShareCopy*, CUstream);

static bool bv_share_load(void) {
  static int loaded;
  if (loaded == 0) {
    static const char* const names[] = { "nvcuda.dll", NULL };
    void* lib = gpu_lib_open(names);
    bv_ext_import   = (__typeof__(bv_ext_import))gpu_sym(lib, "cuImportExternalMemory");
    bv_ext_array    = (__typeof__(bv_ext_array))gpu_sym(lib, "cuExternalMemoryGetMappedMipmappedArray");
    bv_ext_level    = (__typeof__(bv_ext_level))gpu_sym(lib, "cuMipmappedArrayGetLevel");
    bv_ext_sem      = (__typeof__(bv_ext_sem))gpu_sym(lib, "cuImportExternalSemaphore");
    bv_ext_signal   = (__typeof__(bv_ext_signal))gpu_sym(lib, "cuSignalExternalSemaphoresAsync");
    bv_ext_wait     = (__typeof__(bv_ext_wait))gpu_sym(lib, "cuWaitExternalSemaphoresAsync");
    bv_ext_free_mem = (__typeof__(bv_ext_free_mem))gpu_sym(lib, "cuDestroyExternalMemory");
    bv_ext_free_sem = (__typeof__(bv_ext_free_sem))gpu_sym(lib, "cuDestroyExternalSemaphore");
    bv_ext_free_mip = (__typeof__(bv_ext_free_mip))gpu_sym(lib, "cuMipmappedArrayDestroy");
    bv_ext_copy     = (__typeof__(bv_ext_copy))gpu_sym(lib, "cuMemcpy2DAsync_v2");
    loaded = bv_ext_import && bv_ext_array && bv_ext_level && bv_ext_sem && bv_ext_signal && bv_ext_wait
      && bv_ext_free_mem && bv_ext_free_sem && bv_ext_free_mip && bv_ext_copy ? 1 : -1;
  }
  return loaded == 1;
}

// Drops the imports (Bend's context must be current).
static void bv_share_drop(void) {
  for (int i = 0; i < BENDVIZ_TEXTURES; i += 1) {
    if (bv_share.mip[i]) bv_ext_free_mip(bv_share.mip[i]);
    if (bv_share.mem[i]) bv_ext_free_mem(bv_share.mem[i]);
    bv_share.mip[i] = bv_share.mem[i] = bv_share.arr[i] = NULL;
  }
  if (bv_share.sem_cuda) bv_ext_free_sem(bv_share.sem_cuda);
  if (bv_share.sem_d3d) bv_ext_free_sem(bv_share.sem_d3d);
  bv_share.sem_cuda = bv_share.sem_d3d = NULL;
  bv_share.imported = 0;
}

static bool bv_share_go(u32 gen, void* const tex[BENDVIZ_TEXTURES], void* fc, void* fd, unsigned long long at, int fw, int fh, bool idle);

// On Bend's thread, the frame at `at` just drawn (fw x fh): into a shared texture, if the player
// has shared some of this size (and, if idle, only into one Direct3D is known to be done with).
// Whether it went; the swap then publishes it.
static bool bv_share_frame(unsigned long long at, int fw, int fh, bool idle) {
  pthread_mutex_lock(&bv_lock);
  u32 gen = bv_share.gen;
  bool fits = gen != 0 && !bv_share.failed && bv_share.w == fw && bv_share.h == fh;
  void* tex[BENDVIZ_TEXTURES];
  memcpy(tex, bv_share.tex, sizeof tex);
  void* fc = bv_share.fence_cuda;
  void* fd = bv_share.fence_d3d;
  bv_share.busy = fits;
  pthread_mutex_unlock(&bv_lock);
  if (!fits) {
    return false;
  }
  bool went = bv_share_go(gen, tex, fc, fd, at, fw, fh, idle);
  pthread_mutex_lock(&bv_lock);
  bv_share.busy = false;
  pthread_mutex_unlock(&bv_lock);
  return went;
}

// A frame's copy queued behind its kernels. The program draws into two buffers in turn, so a
// frame's buffer is the one shown two frames before, and at the frame's wait for its kernels
// (bv_timed_sync, on Bend's thread) its copy into a texture is queued before the wait. The GPU
// then goes on from the kernels to the copy, rather than turning to Direct3D's work while Bend's
// thread makes those calls after the wait (four driver calls, 100 us), and back (each turn leaves
// the GPU idle 50 us). The show checks the guess (the same buffer, done in that one wait) and
// else copies as before. Only into a texture Direct3D is known to be done with: the wait for the
// kernels waits for everything queued, and waiting there for Direct3D (a vertical blank, with
// vsync) held Bend to 35 frames a second.
// (bv_early, above)

static void bv_early_copy(void) {
  if (!bv_early.drawing || !pthread_equal(pthread_self(), bv_early.thread)) {
    return;
  }
  bv_early.waits += 1;
  unsigned long long at = bv_early.shown[1];
  pthread_mutex_lock(&bv_lock);
  bool device = bv_device_ok;
  pthread_mutex_unlock(&bv_lock);
  int fw = (int)(bv_now_word & 8191), fh = (int)(bv_now_word >> 13 & 8191);
  if (bv_early.waits == 1 && at != 0 && device && bv_now_word >> 31 != 0 && bv_share_frame(at, fw, fh, true)) {
    bv_early.at = at;
  }
}

static bool bv_share_go(u32 gen, void* const tex[BENDVIZ_TEXTURES], void* fc, void* fd, unsigned long long at, int fw, int fh, bool idle) {
  if (!bv_share_load()) {
    return false;
  }
  if (bv_share.imported != gen) {
    bv_share_drop();
    bool ok = true;
    for (int i = 0; ok && i < BENDVIZ_TEXTURES; i += 1) {
      BvExtMemDesc md = { 0 };
      md.type = 6, md.handle.win32.handle = tex[i];  // CU_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_RESOURCE
      md.size = (unsigned long long)fw * fh * 4, md.flags = 1;  // CUDA_EXTERNAL_MEMORY_DEDICATED
      BvExtArrayDesc ad = { 0 };
      ad.arrayDesc.Width = (size_t)fw, ad.arrayDesc.Height = (size_t)fh;
      ad.arrayDesc.Format = 1, ad.arrayDesc.NumChannels = 4, ad.numLevels = 1;  // CU_AD_FORMAT_UNSIGNED_INT8
      ok = bv_ext_import(&bv_share.mem[i], &md) == CUDA_SUCCESS
        && bv_ext_array(&bv_share.mip[i], bv_share.mem[i], &ad) == CUDA_SUCCESS
        && bv_ext_level(&bv_share.arr[i], bv_share.mip[i], 0) == CUDA_SUCCESS;
    }
    BvExtSemDesc sc = { 0 }, sd = { 0 };
    sc.type = sd.type = 5;  // CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D11_FENCE
    sc.handle.win32.handle = fc, sd.handle.win32.handle = fd;
    ok = ok && bv_ext_sem(&bv_share.sem_cuda, &sc) == CUDA_SUCCESS && bv_ext_sem(&bv_share.sem_d3d, &sd) == CUDA_SUCCESS;
    if (!ok) {
      bv_share_drop();
      pthread_mutex_lock(&bv_lock);
      bv_share.failed = true;  // (the player falls back to mapping the texture)
      pthread_mutex_unlock(&bv_lock);
      return false;
    }
    bv_share.imported = gen;
  }
  pthread_mutex_lock(&bv_lock);
  int slot = -1;  // of those neither on screen nor waiting, the one Direct3D last read longest ago
  for (int i = 0; i < BENDVIZ_TEXTURES; i += 1) {
    if (i != bv_share.shown && i != bv_share.published && (slot < 0 || bv_share.read_done[i] < bv_share.read_done[slot])) slot = i;
  }
  unsigned long long read = bv_share.read_done[slot];
  bool busy = read > bv_share.completed;
  pthread_mutex_unlock(&bv_lock);
  if (idle && busy) {
    return false;
  }
  if (read) {  // Direct3D is done drawing it (on the GPU)
    BvSemWait wp = { 0 };
    wp.params.fence.value = read;
    bv_ext_wait(&bv_share.sem_d3d, &wp, 1, NULL);
  }
  BvShareCopy c = { 0 };
  c.srcMemoryType = 2, c.srcDevice = at, c.srcPitch = (size_t)fw * 4;  // device
  c.dstMemoryType = 3, c.dstArray = bv_share.arr[slot];                // array
  c.WidthInBytes = (size_t)fw * 4, c.Height = (size_t)fh;
  BvSemSignal sp = { 0 };
  sp.params.fence.value = bv_share.cuda_value + 1;
  if (bv_ext_copy(&c, NULL) != CUDA_SUCCESS || bv_ext_signal(&bv_share.sem_cuda, &sp, 1, NULL) != CUDA_SUCCESS) {
    return false;
  }
  bv_share.cuda_value += 1;
  bv_share.pending = slot, bv_share.pending_value = bv_share.cuda_value;
  return true;
}
#endif

Term viz_show_run(Env e, Term* f, IoWork* w) {
  Term   a  = f[0];
  u32*   px = (u32*)blk_ptr(e.mem, blk_loc(e.mem, a), 0);
  int    fw = (int)(bv_now_word & 8191), fh = (int)(bv_now_word >> 13 & 8191);
  size_t n  = (size_t)fw * (size_t)fh;
  u64    drawn = io_tick();  // the bang (or the CPU's work) is done
  bv_trace("drawn", 0, 0);
  double drawn_wait = bv_wait_total(), drawn_launch = bv_launch_total();
  double before, after;
  bv_edges_of(drawn, &before, &after);
  bv_drawn = drawn;
  if (n > ((size_t)1 << blk_cls(a))) {
    bv_n_dropped += 1;
    bv_trace("drop-size", 0, 0);
    return a;
  }
  bool on_device = false, copied = false;
  bool gpu_drew = bv_now_word >> 31 != 0 && io_gpu;  // else the frame is in host memory
  bool shared = false;                                 // it went into a shared texture
  unsigned long long at = 0;
#if BEND_CUDA
  // On the device the frame stays where Bend drew it: the program draws into two buffers in turn,
  // so the player copies this one out while Bend draws the next into the other.
  pthread_mutex_lock(&bv_lock);
  bool device = gpu_drew && bv_device_ok;
  pthread_mutex_unlock(&bv_lock);
  if (device) {
    on_device = copied = true;
#ifdef _WIN32
    unsigned long long mine = gpu_base + (unsigned long long)((char*)px - (char*)CORPUS);
    if (bv_early.at != 0 && bv_early.at == mine && bv_early.waits == 1) {
      at = mine, shared = true;  // (copied already, behind the kernels)
    } else {
      at = gpu_at(px, n * 4);
      shared = bv_share_frame(at, fw, fh, false);
    }
    bv_early.shown[1] = bv_early.shown[0], bv_early.shown[0] = at, bv_early.drawing = false;
#else
    at = gpu_at(px, n * 4);
#endif
  }
#endif
  if (!on_device) {
    pthread_mutex_lock(&bv_lock);
    bool room = n <= bv_cap || (!bv_lent && bv_room(n));
    pthread_mutex_unlock(&bv_lock);
    if (!room) {
      bv_n_dropped += 1;
      bv_trace("drop-room", 0, 0);
      return a;
    }
#if BEND_CUDA
    if (gpu_drew && bv_pinned) {
      copied = cuMemcpyDtoH(bv_back, gpu_at(px, n * 4), n * 4) == CUDA_SUCCESS;
    }
#endif
    if (!copied) {
      memcpy(bv_back, px, n * 4);
    }
  }
  u64 now = io_tick();
  pthread_mutex_lock(&bv_lock);
  while (bv_lent) {  // the player is reading the front buffer: a moment, then swap
    pthread_mutex_unlock(&bv_lock);
    sched_yield();
    pthread_mutex_lock(&bv_lock);
  }
  if (shared) {
#if BEND_CUDA && defined(_WIN32)
    if (bv_share.imported == bv_share.gen) {  // (not a set shared since)
      bv_share.published = bv_share.pending, bv_share.pub_value = bv_share.pending_value;
    } else {
      shared = false;
    }
    bv_share.pending = -1;
#endif
    on_device = false;
  } else if (on_device) {
    bv_dev_front = at;
#if BEND_CUDA && defined(_WIN32)
    // The frame shown before this one is in the buffer drawn into next: that drawing waits, on the
    // GPU, for the player's copy out of it, if it took it (the player takes only the newest frame).
    if (bv_read_front) bv_ev_wait(NULL, bv_read_front, 0);
    bv_read_front = NULL;
#endif
  } else {
    u32* t = bv_front;
    bv_front = bv_back, bv_back = t;
  }
  bv_front_on_device = on_device;
  bv_front_shared = shared;
  bv_draw_ms = (double)(drawn - bv_began) / 1e6, bv_copy_ms = (double)(now - drawn) / 1e6;
  bv_wait_frame = drawn_wait - bv_wait_began;
  bv_launch_frame = drawn_launch - bv_launch_began;
  bv_before_frame = before, bv_after_frame = after;
  bv_ms       = (double)(now - bv_began) / 1e6;
  bv_done_w   = fw, bv_done_h = fh;
  bv_done_gpu = gpu_drew;  // asked for, and there to use
  bv_fresh    = true;
  bv_n_drawn += 1;
  pthread_cond_broadcast(&bv_shown);
  bv_trace("swap", on_device, 0);
  pthread_mutex_unlock(&bv_lock);
  return a;
}

static void __attribute__((constructor)) viz_use(void) {
#ifdef CID_VIZ_NEXT
  io_eff(CID_VIZ_NEXT, viz_next_run, 0);
#endif
#ifdef CID_VIZ_PARAM
  io_eff(CID_VIZ_PARAM, viz_param_run, 0);
#endif
#ifdef CID_VIZ_SHOW
  io_eff(CID_VIZ_SHOW, viz_show_run, 0);
#endif
}

// ---- The player's side ----

// Only when the program is built into the player (its main renamed to bendviz_main); a standalone
// build of the same C (to make its GPU program) has none of this.
#ifdef BENDVIZ_EMBED

int bendviz_main(int argc, char** argv);

static void* bv_thread(void* arg) {
  char* argv[] = { "gtube", "--gpu", (char*)arg, "--threads", "4", NULL };
  bendviz_main(5, argv);
  return NULL;
}

// Starts the Bend program, once. `gpu_heap` is a size such as "512MB", or "off" for the CPU only.
bool bendviz_start(const char* gpu_heap) {
  static bool started;
  if (started) {
    return true;
  }
  // Bend reports a fatal error on stderr and exits at once; unbuffered, the report survives.
  setvbuf(stderr, NULL, _IONBF, 0);
  const char* trace = getenv("BENDVIZ_TRACE");
  if (trace != NULL && (bv_trace_out = fopen(trace, "w")) != NULL) bv_trace_t0 = io_tick();
  pthread_t tid;
  if (pthread_create(&tid, NULL, bv_thread, (void*)gpu_heap)) {
    return false;
  }
  pthread_detach(tid);
  started = true;
  return true;
}

// Asks for a frame of w x h (each at most BENDVIZ_MAX), replacing any request not yet started.
// params: time, bass, mids, hue, beat.
void bendviz_request(const float params[5], int w, int h, bool gpu) {
  w = w < 1 ? 1 : w > BENDVIZ_MAX ? BENDVIZ_MAX : w;
  h = h < 1 ? 1 : h > BENDVIZ_MAX ? BENDVIZ_MAX : h;
  if ((long)w * h > BENDVIZ_PIXELS) {  // the program's buffers hold 2^23 pixels: fewer rows
    h = (int)(BENDVIZ_PIXELS / w);
  }
  pthread_mutex_lock(&bv_lock);
  memcpy(bv_ask, params, sizeof bv_ask);
  bv_ask_word = (u32)w | (u32)h << 13 | (gpu ? 1u << 31 : 0);
  bv_trace("request", bv_want, 0);
  if (!bv_want) bv_asked_at = io_tick();  // the first request since Bend last took one
  bv_want     = true;
  pthread_cond_signal(&bv_asked);
  pthread_mutex_unlock(&bv_lock);
}

// Waits up to ms for a frame newer than the last one taken; whether one is ready.
bool bendviz_wait(double ms) {
  struct timespec at;
  clock_gettime(CLOCK_REALTIME, &at);
  long long ns = at.tv_nsec + (long long)(ms * 1e6);
  at.tv_sec += (time_t)(ns / 1000000000LL), at.tv_nsec = (long)(ns % 1000000000LL);
  pthread_mutex_lock(&bv_lock);
  while (!bv_fresh && pthread_cond_timedwait(&bv_shown, &bv_lock, &at) == 0) {
  }
  bool fresh = bv_fresh;
  pthread_mutex_unlock(&bv_lock);
  return fresh;
}

// Marks the newest frame taken without taking it (for measuring the cost of taking frames).
void bendviz_discard(void) {
  pthread_mutex_lock(&bv_lock);
  bv_fresh = false;
  pthread_mutex_unlock(&bv_lock);
}

// Lends the newest finished frame (rows packed, w x h), if one arrived since the last call and is
// on the host, until bendviz_return; NULL otherwise. gpu is where it was drawn and ms how long it
// took, all told.
const u32* bendviz_borrow(int* w, int* h, bool* gpu, double* ms) {
  pthread_mutex_lock(&bv_lock);
  const u32* frame = NULL;
  bv_trace("borrow-try", bv_fresh, bv_front_on_device);
  if (bv_fresh && !bv_front_on_device && !bv_front_shared && bv_front != NULL) {
    frame = bv_front, bv_lent = true, bv_fresh = false;
    *w = bv_done_w, *h = bv_done_h, *gpu = bv_done_gpu, *ms = bv_ms;
  }
  pthread_mutex_unlock(&bv_lock);
  return frame;
}

// Whether the player takes frames drawn on the GPU on the device (bendviz_to_d3d11), from the next
// one on; otherwise they come to the host.
void bendviz_device_frames(bool on) {
  pthread_mutex_lock(&bv_lock);
  bv_device_ok = on;
  pthread_mutex_unlock(&bv_lock);
}

#if BEND_CUDA && defined(_WIN32)

// Graphics interop, loaded from the driver the runtime opened.
typedef struct CUgraphicsResource_st* CUgraphicsResource;
typedef struct CUarray_st*            CUarray;
typedef struct {
  size_t srcXInBytes, srcY;
  int srcMemoryType;
  const void* srcHost;
  CUdeviceptr srcDevice;
  CUarray srcArray;
  size_t srcPitch;
  size_t dstXInBytes, dstY;
  int dstMemoryType;
  void* dstHost;
  CUdeviceptr dstDevice;
  CUarray dstArray;
  size_t dstPitch;
  size_t WidthInBytes, Height;
} BvCopy2D;  // CUDA_MEMCPY2D

static CUresult (CUDAAPI* bv_register)(CUgraphicsResource*, void*, unsigned);
static CUresult (CUDAAPI* bv_unregister)(CUgraphicsResource);
static CUresult (CUDAAPI* bv_map_flags)(CUgraphicsResource, unsigned);
static CUresult (CUDAAPI* bv_map)(unsigned, CUgraphicsResource*, CUstream);
static CUresult (CUDAAPI* bv_unmap)(unsigned, CUgraphicsResource*, CUstream);
static CUresult (CUDAAPI* bv_array)(CUarray*, CUgraphicsResource, unsigned, unsigned);
static CUresult (CUDAAPI* bv_copy2d)(const BvCopy2D*, CUstream);
static CUresult (CUDAAPI* bv_stream_create)(CUstream*, unsigned);
static CUresult (CUDAAPI* bv_ev_create)(BvReadEvent*, unsigned);
static CUresult (CUDAAPI* bv_ctx_create)(CUcontext*, unsigned, CUdevice);
static CUresult (CUDAAPI* bv_peer_copy)(CUdeviceptr, CUcontext, CUdeviceptr, CUcontext, size_t, CUstream);

// The player's copies go on a stream of their own and aren't waited for: mapping the texture waits
// for Direct3D's queued work, which with vsync sits behind the last present until the next vertical
// blank, and waiting on the player's thread would stall the player a whole refresh.
//
// With vsync they also go in a CUDA context of their own (BV_APART): on Windows a context's streams
// share one queue on the GPU, so in Bend's context the map's wait for the blank would hold up the
// kernels drawing the next frame as well, and every other frame would come late. That context first
// copies the frame out of Bend's buffer into one of its own, which is all Bend then waits for, and
// from there into the texture. Without vsync nothing waits for a blank, and the copy goes straight
// from Bend's buffer in Bend's context (BV_SHARED), a millisecond a frame cheaper, and far less of
// Bend's time. Each side has its stream, its registration of the texture, and two events used in
// turn, one guarding each of Bend's buffers.
enum { BV_SHARED, BV_APART };
typedef struct {
  CUcontext          ctx;
  CUstream           stream;
  BvReadEvent        ev[2];
  u32                next_ev;
  void*              registered;  // the texture registered in this context, and its handle
  CUgraphicsResource res;
  CUdeviceptr        buf;         // BV_APART: the frame, copied out of Bend's buffer
  size_t             buf_cap;
  int                ready;       // 0 not tried, 1 ready, -1 failed
} BvSide;
static BvSide bv_side[2];
static bool   bv_apart;           // presents wait for the vertical blank (bendviz_interop_apart)

static bool bv_interop_load(void) {
  static int loaded;
  if (loaded == 0) {
    static const char* const names[] = { "nvcuda.dll", NULL };
    void* lib = gpu_lib_open(names);
    bv_register   = (__typeof__(bv_register))gpu_sym(lib, "cuGraphicsD3D11RegisterResource");
    bv_unregister = (__typeof__(bv_unregister))gpu_sym(lib, "cuGraphicsUnregisterResource");
    bv_map_flags  = (__typeof__(bv_map_flags))gpu_sym(lib, "cuGraphicsResourceSetMapFlags_v2");
    bv_map        = (__typeof__(bv_map))gpu_sym(lib, "cuGraphicsMapResources");
    bv_unmap      = (__typeof__(bv_unmap))gpu_sym(lib, "cuGraphicsUnmapResources");
    bv_array      = (__typeof__(bv_array))gpu_sym(lib, "cuGraphicsSubResourceGetMappedArray");
    bv_copy2d     = (__typeof__(bv_copy2d))gpu_sym(lib, "cuMemcpy2DAsync_v2");
    bv_stream_create = (__typeof__(bv_stream_create))gpu_sym(lib, "cuStreamCreate");
    bv_ev_create     = (__typeof__(bv_ev_create))gpu_sym(lib, "cuEventCreate");
    bv_ev_mark       = (__typeof__(bv_ev_mark))gpu_sym(lib, "cuEventRecord");
    bv_ev_wait       = (__typeof__(bv_ev_wait))gpu_sym(lib, "cuStreamWaitEvent");
    bv_ctx_create    = (__typeof__(bv_ctx_create))gpu_sym(lib, "cuCtxCreate_v2");
    bv_peer_copy     = (__typeof__(bv_peer_copy))gpu_sym(lib, "cuMemcpyPeerAsync");
    loaded = bv_register && bv_unregister && bv_map_flags && bv_map && bv_unmap && bv_array && bv_copy2d
      && bv_stream_create && bv_ev_create && bv_ev_mark && bv_ev_wait && bv_ctx_create && bv_peer_copy ? 1 : -1;
  }
  return loaded == 1;
}

// The side, set up the first time and made current on this (the player's) thread; NULL if it can't be.
static BvSide* bv_side_ready(int which) {
  BvSide* s = &bv_side[which];
  if (s->ready == 0) {
    bool ok = bv_interop_load()
      && (which == BV_APART ? bv_ctx_create(&s->ctx, 0, gpu_dev) == CUDA_SUCCESS : (s->ctx = gpu_ctx) != NULL)
      && cuCtxSetCurrent(s->ctx) == CUDA_SUCCESS
      && bv_stream_create(&s->stream, 1) == CUDA_SUCCESS  // CU_STREAM_NON_BLOCKING
      && bv_ev_create(&s->ev[0], 2) == CUDA_SUCCESS && bv_ev_create(&s->ev[1], 2) == CUDA_SUCCESS;  // no timing
    s->ready = ok ? 1 : -1;
  }
  return s->ready == 1 && cuCtxSetCurrent(s->ctx) == CUDA_SUCCESS ? s : NULL;
}

static void bv_side_release(BvSide* s) {
  if (s->registered && cuCtxSetCurrent(s->ctx) == CUDA_SUCCESS) bv_unregister(s->res);
  s->registered = NULL;
}

// Whether the player's presents wait for the vertical blank (vsync): see BvSide.
void bendviz_interop_apart(bool apart) {
  bv_apart = apart;
}

// Copies the newest finished frame, if it is on the device, into a Direct3D 11 texture of its size
// (ID3D11Texture2D*, B8G8R8A8), without the host: returns 1, with w and h the frame's size and gpu
// and ms as for bendviz_borrow. Returns 0 when there is no such frame, with w and h the size of a
// waiting one (so the player can make a texture that size and call again), and -1 when interop
// fails: the player should then stop asking for device frames.
int bendviz_to_d3d11(void* texture, int tw, int th, int* w, int* h, bool* gpu, double* ms) {
  pthread_mutex_lock(&bv_lock);
  bool waiting = bv_fresh && bv_front_on_device;
  *w = bv_done_w, *h = bv_done_h;
  bool fits = waiting && bv_done_w == tw && bv_done_h == th;
  if (fits) {
    bv_lent = true;
  }
  bv_trace("take-try", bv_fresh, bv_front_on_device);
  pthread_mutex_unlock(&bv_lock);
  if (!fits) {
    return 0;
  }
  int result = -1;
  BvSide* s = bv_side_ready(bv_apart ? BV_APART : BV_SHARED);
  if (s != NULL) {
    if (s->registered != texture) {
      bv_side_release(s);
      if (bv_register(&s->res, texture, 0) == CUDA_SUCCESS) {
        s->registered = texture;
        bv_map_flags(s->res, 2);  // write-discard: the old contents are not needed
      }
    }
    size_t      bytes = (size_t)tw * th * 4;
    BvReadEvent ev    = s->ev[s->next_ev ^= 1];
    CUdeviceptr src   = bv_dev_front;
    bool        ok    = s->registered != NULL;
    if (ok && s == &bv_side[BV_APART]) {
      if (s->buf_cap < bytes) {
        if (s->buf) cuMemFree(s->buf);
        s->buf_cap = cuMemAlloc(&s->buf, bytes) == CUDA_SUCCESS ? bytes : 0;
      }
      ok = s->buf_cap >= bytes && bv_peer_copy(s->buf, s->ctx, src, gpu_ctx, bytes, s->stream) == CUDA_SUCCESS
        && bv_ev_mark(ev, s->stream) == CUDA_SUCCESS;
      src = s->buf;
    }
    CUarray arr;
    if (ok && bv_map(1, &s->res, s->stream) == CUDA_SUCCESS) {
      if (bv_array(&arr, s->res, 0, 0) == CUDA_SUCCESS) {
        BvCopy2D c = { 0 };
        c.srcMemoryType = 2, c.srcDevice = src, c.srcPitch = (size_t)tw * 4;  // device
        c.dstMemoryType = 3, c.dstArray = arr;                                // array
        c.WidthInBytes = (size_t)tw * 4, c.Height = (size_t)th;
        result = bv_copy2d(&c, s->stream) == CUDA_SUCCESS ? 1 : -1;
      }
      if (bv_unmap(1, &s->res, s->stream) != CUDA_SUCCESS) result = -1;
      if (s == &bv_side[BV_SHARED] && bv_ev_mark(ev, s->stream) != CUDA_SUCCESS) result = -1;  // read Bend's buffer till here
    }
    if (result == 1) {
      bv_read_front = ev;  // (the buffers don't swap while the frame is lent)
    }
  }
  bv_trace("take-done", result, bv_apart);
  pthread_mutex_lock(&bv_lock);
  if (result == 1) {
    bv_fresh = false;
    *gpu = bv_done_gpu, *ms = bv_ms;
  }
  bv_lent = false;
  pthread_mutex_unlock(&bv_lock);
  return result;
}

// Lets go of the texture bendviz_to_d3d11 last wrote, before the player destroys it (or Direct3D):
// left registered, the driver would reach into Direct3D after it is gone.
void bendviz_release_d3d11(void) {
  bv_side_release(&bv_side[BV_SHARED]);
  bv_side_release(&bv_side[BV_APART]);
}

// Shares textures (BENDVIZ_TEXTURES, w x h, B8G8R8A8, as NT handles) and two fences (NT handles: one CUDA
// signals and Direct3D waits for, one the other way) for Bend to copy frames into; see bv_share.
// Replaces any set shared before (bendviz_d3d11_unshare that first).
void bendviz_d3d11_share(void* const tex[BENDVIZ_TEXTURES], void* fence_cuda, void* fence_d3d, int w, int h) {
  pthread_mutex_lock(&bv_lock);
  for (int i = 0; i < BENDVIZ_TEXTURES; i += 1) bv_share.tex[i] = tex[i], bv_share.read_done[i] = 0;
  bv_share.fence_cuda = fence_cuda, bv_share.fence_d3d = fence_d3d;
  bv_share.w = w, bv_share.h = h, bv_share.failed = false;
  bv_share.gen = bv_share.gen + 1 ? bv_share.gen + 1 : 1;
  bv_share.shown = bv_share.published = -1;
  pthread_mutex_unlock(&bv_lock);
}

// Stops sharing: waits until Bend isn't copying, and lets go of the imports (before the player lets
// go of the textures and fences, or Direct3D).
void bendviz_d3d11_unshare(void) {
  pthread_mutex_lock(&bv_lock);
  bv_share.gen = 0, bv_share.shown = bv_share.published = -1;
  while (bv_share.busy) {
    pthread_mutex_unlock(&bv_lock);
    sched_yield();
    pthread_mutex_lock(&bv_lock);
  }
  pthread_mutex_unlock(&bv_lock);
  if (bv_share.imported && cuCtxSetCurrent(gpu_ctx) == CUDA_SUCCESS) {
    cuCtxSynchronize();  // (copies into the textures may be queued)
    bv_share_drop();
  }
}

// Whether sharing failed (the player then takes frames by mapping its texture: bendviz_to_d3d11).
bool bendviz_d3d11_failed(void) {
  pthread_mutex_lock(&bv_lock);
  bool failed = bv_share.failed;
  pthread_mutex_unlock(&bv_lock);
  return failed;
}

// Each frame: Direct3D will reach d3d_done (the player signalled it) once done with what it drew so
// far, which covers the texture on screen. Returns the texture (0 to 2) of a new frame, after which
// the player makes Direct3D wait for CUDA's fence to reach *wait_value before drawing it; or -1
// when no new frame is waiting.
int bendviz_d3d11_take(unsigned long long d3d_done, unsigned long long d3d_completed, unsigned long long* wait_value, int* w, int* h,
  bool* gpu, double* ms) {
  pthread_mutex_lock(&bv_lock);
  bv_share.completed = d3d_completed;
  if (bv_share.shown >= 0) {
    bv_share.read_done[bv_share.shown] = d3d_done;
  }
  int slot = -1;
  if (bv_fresh && bv_front_shared && bv_share.published >= 0) {
    slot = bv_share.shown = bv_share.published, bv_share.published = -1;
    *wait_value = bv_share.pub_value, bv_fresh = false;
    *w = bv_done_w, *h = bv_done_h, *gpu = bv_done_gpu, *ms = bv_ms;
  }
  bv_trace("share-take", slot, 0);
  pthread_mutex_unlock(&bv_lock);
  return slot;
}

#endif

void bendviz_return(void) {
  pthread_mutex_lock(&bv_lock);
  bv_lent = false;
  pthread_mutex_unlock(&bv_lock);
}

// How the last frame's time divides: drawing it (of which waiting for the GPU), and bringing it to
// the host.
// Totals so far: frames Bend finished and dropped, the time from a request to the helper taking it
// and from there to Bend starting on it, summed over the frames, and heap pages fetched on a fault
// and the time fetching them.
void bendviz_cycle(unsigned long long* drawn, unsigned long long* dropped, double* took_ms, double* began_ms,
  unsigned long long* faults, double* fault_ms) {
  pthread_mutex_lock(&bv_lock);
  *drawn = bv_n_drawn, *dropped = bv_n_dropped, *took_ms = bv_took_ms, *began_ms = bv_began_ms;
#if BEND_CUDA
  *faults = gpu_fault_count();  // (Windows: heap pages the host fetched on a fault, and the time)
  *fault_ms = (double)gpu_fault_time() / 1e6;
#else
  *faults = 0, *fault_ms = 0;
#endif
  pthread_mutex_unlock(&bv_lock);
}

// The host's part of the last frame's drawing: before its first launch, in launch calls, and after
// its last wait.
void bendviz_host_parts(double* before_ms, double* launch_ms, double* after_ms) {
  pthread_mutex_lock(&bv_lock);
  *before_ms = bv_before_frame, *launch_ms = bv_launch_frame, *after_ms = bv_after_frame;
  pthread_mutex_unlock(&bv_lock);
}

void bendviz_times(double* draw_ms, double* wait_ms, double* copy_ms) {
  pthread_mutex_lock(&bv_lock);
  *draw_ms = bv_draw_ms, *wait_ms = bv_wait_frame, *copy_ms = bv_copy_ms;
  pthread_mutex_unlock(&bv_lock);
}

// ---- Diagnostics ----

// The heap's span, and on the GPU a count of kernel launches and of the time spent waiting for
// them, through wrappers around the runtime's driver calls (installed on the first frame, after
// the runtime has loaded the driver). For measuring what a frame costs; the player doesn't use them.
// With BENDVIZ_PAGES set to a file name, one frame's heap pages resident on the host side (in the
// process's working set) are written there at each launch and wait, and at the start of the next
// frame, with the heap's layout: which parts of the heap the host touches around a bang. Windows only.
#if defined(_WIN32) && defined(BENDVIZ_EMBED)
#include <psapi.h>
static FILE* bv_pages_out;
static u32 bv_frame;
static bool bv_pages_on;  // during the frame being recorded
static void bv_pages(const char* label) {
  if (!bv_pages_on) {
    return;
  }
  size_t n = corpus_size / 4096;
  PSAPI_WORKING_SET_EX_INFORMATION* info = calloc(n, sizeof *info);
  for (size_t i = 0; i < n; i++) info[i].VirtualAddress = (char*)CORPUS + i * 4096;
  if (info != NULL && K32QueryWorkingSetEx(GetCurrentProcess(), info, (DWORD)(n * sizeof *info))) {
    fprintf(bv_pages_out, "%s:", label);
    for (size_t i = 0; i < n; i++) {
      if (info[i].VirtualAttributes.Valid) fprintf(bv_pages_out, " %zu", i);
    }
    fprintf(bv_pages_out, "\n");
  }
  free(info);
}
static void bv_pages_frame_start(void) {
  bv_frame += 1;
  if (bv_frame == 1) {
    const char* path = getenv("BENDVIZ_PAGES");
    bv_pages_out = path != NULL ? fopen(path, "w") : NULL;
  }
  if (bv_pages_out == NULL) {
    return;
  }
  if (bv_frame == 6) {  // the fifth frame, from the end of the fourth to the start of the sixth
    bv_pages_on = true;
    bv_pages("frame start");
  } else if (bv_pages_on) {
    bv_pages("next frame");
    bv_pages_on = false;
    fprintf(bv_pages_out, "layout: alc %llu ring %llu stak %llu stat %llu heap %llu size %llu\n",
      (unsigned long long)ALC_OFF, (unsigned long long)RING_OFF, (unsigned long long)STAK_OFF,
      (unsigned long long)STAT_OFF, (unsigned long long)HEAP_OFF, (unsigned long long)(corpus_size / 8));
    for (u32 c = 0; c < NCLS_ALL; c += 1) {
      Bank* b = bank_at(CORPUS, c);
      fprintf(bv_pages_out, "bank %u: off %llu rd %u wr %u top %u\n", c, (unsigned long long)b->off, b->rd, b->wr, b->top);
    }
    fclose(bv_pages_out), bv_pages_out = NULL;
  }
}
// With BENDVIZ_TOUCH set, the time the host takes to read a word of three heap pages it uses (the
// header, the start of the heap and one further in), twice each, after one frame's wait.
static void bv_touch(void) {
  if (bv_frame != 6 || getenv("BENDVIZ_TOUCH") == NULL) {
    return;
  }
  static const size_t page[3] = { 0, 100416, 116801 };
  volatile u64* H = CORPUS;
  for (int k = 0; k < 3; k += 1) {
    u64 t0 = io_tick();
    (void)H[page[k] * 512];
    u64 t1 = io_tick();
    (void)H[page[k] * 512 + 1];
    u64 t2 = io_tick();
    fprintf(stderr, "  page %zu: first read %.1f us, second %.1f us\n", page[k], (double)(t1 - t0) / 1e3, (double)(t2 - t1) / 1e3);
  }
}
#else
static void bv_pages(const char* label) { (void)label; }
static void bv_pages_frame_start(void) {}
static void bv_touch(void) {}
#endif

static u64 bv_launches;
static double bv_wait_ms, bv_launch_ms;
static u64 bv_first_launch, bv_last_sync;  // this frame's first launch, and the end of its last wait
#if BEND_CUDA
static __typeof__(gpu_fn_cuLaunchKernel) bv_real_launch;
static __typeof__(gpu_fn_cuCtxSynchronize) bv_real_sync;
// With BENDVIZ_KERNELS set, each launch is waited for, and its time kept by its place in the frame;
// set to "events", the device's own clock times each kernel and the gap before it, without waiting.
static int bv_ktime = -1;  // 0 off, 1 waiting, 2 events
static double bv_kms[8], bv_kgap[8];
static u64 bv_kn[8];
static u32 bv_kidx, bv_kgrid[8], bv_kprev;
typedef struct CUevent_st* BvEvent;
static CUresult (CUDAAPI* bv_ev_create)(BvEvent*, unsigned);
static CUresult (CUDAAPI* bv_ev_record)(BvEvent, CUstream);
static CUresult (CUDAAPI* bv_ev_sync)(BvEvent);
static CUresult (CUDAAPI* bv_ev_elapsed)(float*, BvEvent, BvEvent);
static BvEvent bv_ev[8][2];
// The last frame's event times, added up once they are done.
static void bv_events_take(void) {
  if (bv_ktime != 2 || bv_kprev == 0) return;
  u32 n = bv_kprev < 8 ? bv_kprev : 8;
  bv_ev_sync(bv_ev[n - 1][1]);
  for (u32 i = 0; i < n; i += 1) {
    float k = 0, g = 0;
    bv_ev_elapsed(&k, bv_ev[i][0], bv_ev[i][1]);
    if (i > 0) bv_ev_elapsed(&g, bv_ev[i - 1][1], bv_ev[i][0]);
    bv_kms[i] += k, bv_kgap[i] += g, bv_kn[i] += 1;
  }
  bv_kprev = 0;
}
static CUresult CUDAAPI bv_counted_launch(CUfunction f, unsigned gx, unsigned gy, unsigned gz, unsigned bx,
  unsigned by, unsigned bz, unsigned shared, CUstream st, void** params, void** extra) {
  bv_pages("before launch");
  u64 t = io_tick();
  if (bv_first_launch < bv_began) bv_first_launch = t;
  if (bv_ktime == 2 && bv_kidx < 8) bv_ev_record(bv_ev[bv_kidx][0], st), bv_kgrid[bv_kidx] = gx;
  CUresult r = bv_real_launch(f, gx, gy, gz, bx, by, bz, shared, st, params, extra);
  if (bv_ktime == 2 && bv_kidx < 8) bv_ev_record(bv_ev[bv_kidx][1], st), bv_kprev = bv_kidx + 1;
  bv_launches += 1, bv_launch_ms += (double)(io_tick() - t) / 1e6;
  if (bv_ktime == 1 && bv_kidx < 8) {
    bv_real_sync();
    bv_kms[bv_kidx] += (double)(io_tick() - t) / 1e6, bv_kn[bv_kidx] += 1, bv_kgrid[bv_kidx] = gx;
  }
  bv_kidx += 1;
  bv_pages("after launch");
  return r;
}
static CUresult CUDAAPI bv_timed_sync(void) {
#ifdef _WIN32
  bv_early_copy();
#endif
  u64 t = io_tick();
  CUresult r = bv_real_sync();
  bv_last_sync = io_tick();
  bv_pages("after wait");
  bv_touch();
  bv_wait_ms += (double)(io_tick() - t) / 1e6;
  return r;
}
#endif

static void bv_count_launches(void) {
#if BEND_CUDA
  bv_pages_frame_start();
  bv_events_take();
  bv_kidx = 0;
  if (io_gpu && bv_real_launch == NULL) {
    const char* how = getenv("BENDVIZ_KERNELS");
    bv_ktime = how == NULL ? 0 : strcmp(how, "events") ? 1 : 2;
    if (bv_ktime == 2) {
      static const char* const names[] = { "nvcuda.dll", "libcuda.so.1", "libcuda.so", NULL };
      void* lib = gpu_lib_open(names);
      bv_ev_create  = (__typeof__(bv_ev_create))gpu_sym(lib, "cuEventCreate");
      bv_ev_record  = (__typeof__(bv_ev_record))gpu_sym(lib, "cuEventRecord");
      bv_ev_sync    = (__typeof__(bv_ev_sync))gpu_sym(lib, "cuEventSynchronize");
      bv_ev_elapsed = (__typeof__(bv_ev_elapsed))gpu_sym(lib, "cuEventElapsedTime_v2");
      if (!bv_ev_elapsed) bv_ev_elapsed = (__typeof__(bv_ev_elapsed))gpu_sym(lib, "cuEventElapsedTime");
      for (u32 i = 0; i < 8; i += 1) bv_ev_create(&bv_ev[i][0], 0), bv_ev_create(&bv_ev[i][1], 0);
    }
    bv_real_launch = gpu_fn_cuLaunchKernel, gpu_fn_cuLaunchKernel = bv_counted_launch;
    memset(bv_kms, 0, sizeof bv_kms), memset(bv_kn, 0, sizeof bv_kn), memset(bv_kgap, 0, sizeof bv_kgap);
    bv_real_sync = gpu_fn_cuCtxSynchronize, gpu_fn_cuCtxSynchronize = bv_timed_sync;
  }
#endif
}

static double bv_wait_total(void) {
  return bv_wait_ms;
}

static double bv_launch_total(void) {
  return bv_launch_ms;
}

// The host's time in this frame before its first launch and after its last wait (0 if it made none).
static void bv_edges_of(u64 drawn, double* before, double* after) {
  bool launched = bv_first_launch >= bv_began && bv_last_sync >= bv_first_launch;
  *before = launched ? (double)(bv_first_launch - bv_began) / 1e6 : 0;
  *after  = launched && drawn >= bv_last_sync ? (double)(drawn - bv_last_sync) / 1e6 : 0;
}

// With BENDVIZ_KERNELS set: the kernels' mean times by their place in a frame since the last call,
// and their groups.
int bendviz_kernels(double* ms, double* gap_ms, unsigned* groups, int most) {
  int n = 0;
  for (; n < most && n < 8 && bv_kn[n] > 0; n += 1) {
    ms[n] = bv_kms[n] / (double)bv_kn[n], gap_ms[n] = bv_kgap[n] / (double)bv_kn[n], groups[n] = bv_kgrid[n];
  }
  memset(bv_kms, 0, sizeof bv_kms), memset(bv_kn, 0, sizeof bv_kn), memset(bv_kgap, 0, sizeof bv_kgap);
  return n;
}

void bendviz_heap(void** base, size_t* bytes) {
  *base = CORPUS, *bytes = corpus_size;
}

// The last frame's time before its first launch, and after its last wait until the frame came back.
void bendviz_edges(double* before_ms, double* after_ms) {
  *before_ms = (double)(bv_first_launch - bv_began) / 1e6;
  *after_ms = (double)(bv_drawn - bv_last_sync) / 1e6;
}

void bendviz_launches(unsigned long long* launches, double* launch_ms, double* wait_ms) {
  *launches = bv_launches, *launch_ms = bv_launch_ms, *wait_ms = bv_wait_ms;
}

// Whether the GPU is in use (false until the program has started and probed it).
bool bendviz_gpu(void) {
  return io_gpu;
}

#endif
#endif


// Main
// ====

int main(int argc, char** argv) {
  long thr = 0;
  int  gpu = -1;
  u64  mem = 0;
  io_argv = argv + 1;
  for (int i = 1; i < argc; i += 1) {
    const char* a = argv[i];
    const char* v = i + 1 < argc ? argv[i + 1] : NULL;
    if (strcmp(a, "--") == 0) {
      while (i + 1 < argc) {
        io_argv[io_argc++] = argv[++i];
      }
    } else if (strcmp(a, "--bend-help") == 0) {
      printf(CLI_HELP, argv[0]);
      return 0;
    } else if (strcmp(a, "--gpu-build") == 0) {
      if (gpu_probe() && !gpu_make(gpu_path())) {
        fprintf(stderr, "bend: cannot write %s\n", gpu_path());
        return 1;
      }
      return 0;
    } else if (strcmp(a, "--threads") == 0) {
      char* end = NULL;
      thr = v != NULL ? strtol(v, &end, 10) : 0;
      if (thr < 1 || end == NULL || *end != '\0') {
        err_fail("expected a thread count of 1 or more after --threads");
      }
      i += 1;
    } else if (strcmp(a, "--gpu") == 0) {
      char*  end = NULL;
      double n   = v != NULL ? strtod(v, &end) : 0;
      u64    mul = end == NULL ? 0 : strcmp(end, "GB") == 0 ? 1ull << 30
        : strcmp(end, "MB") == 0 ? 1ull << 20 : 0;
      if (v != NULL && strcmp(v, "off") == 0) {
        gpu = 0;
      } else if (v != NULL && (strcmp(v, "on") == 0 || (mul != 0 && n > 0))) {
        gpu = 1;
        mem = (u64)(n * (double)mul);
      } else {
        err_fail("expected on, off or a size like 4GB after --gpu");
      }
      i += 1;
    } else {
      io_argv[io_argc++] = argv[i];
    }
  }
  bool dev = gpu != 0 && BANGS != 0 && gpu_probe();
  if (gpu == 1 && BANGS != 0 && !dev) {
    err_fail("--gpu on, but this binary found no GPU device");
  }
  io_loop(corpus_setup(dev, thr > 0 ? thr : cpu_count(), mem));
  io_sync();
  return 0;
}

#endif
