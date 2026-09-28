// The player's side of viz.bend, and the effects that program imports. The Bend program runs on a
// thread of its own (its main renamed bendviz_main): each frame it blocks in Viz.next until the
// player asks, reads the request with Viz.param, draws, and hands the pixels over with Viz.show.
// The player never waits: it asks for a frame and takes the newest finished one when it is ready.
// Included in the Bend program's C (the runtime's types and helpers are in scope), and compiled
// into the player; bendviz.h declares the player's side.

#ifndef BENDVIZ_BRIDGE
#define BENDVIZ_BRIDGE

#define BENDVIZ_MAX 4096

static pthread_mutex_t bv_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  bv_asked = PTHREAD_COND_INITIALIZER;
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
static unsigned long long bv_dev_front, bv_dev_back;   // two device buffers (CUdeviceptr)
static size_t          bv_dev_cap;
static bool            bv_front_on_device;             // the newest frame is bv_dev_front
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
  bv_began = io_tick();
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

// The frame is the buffer's first w * h pixels. It goes to the back buffer, which then swaps with
// the front one for the player, so no frame is copied on the host. On the GPU the buffers are
// page-locked and the copy is one bulk transfer from the device (the host would otherwise read the
// heap a page at a time). Copying through plain device memory first made no difference, on Linux or
// on Windows. A frame the CPU drew is copied on the host.
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

#if BEND_CUDA
// Two device buffers of n pixels, for frames the player takes on the device.
static bool bv_dev_room(size_t n) {
  if (n <= bv_dev_cap) {
    return true;
  }
  if (bv_dev_cap) {
    cuMemFree(bv_dev_back);
    if (!bv_lent) cuMemFree(bv_dev_front);  // (a lent one is lost rather than pulled away)
  }
  bv_dev_cap = 0;
  if (cuMemAlloc(&bv_dev_back, n * 4) != CUDA_SUCCESS || cuMemAlloc(&bv_dev_front, n * 4) != CUDA_SUCCESS) {
    return false;
  }
  bv_dev_cap = n;
  return true;
}
#endif

Term viz_show_run(Env e, Term* f, IoWork* w) {
  Term   a  = f[0];
  u32*   px = (u32*)blk_ptr(e.mem, blk_loc(e.mem, a), 0);
  int    fw = (int)(bv_now_word & 8191), fh = (int)(bv_now_word >> 13 & 8191);
  size_t n  = (size_t)fw * (size_t)fh;
  u64    drawn = io_tick();  // the bang (or the CPU's work) is done
  double drawn_wait = bv_wait_total(), drawn_launch = bv_launch_total();
  double before, after;
  bv_edges_of(drawn, &before, &after);
  bv_drawn = drawn;
  if (n > ((size_t)1 << blk_cls(a))) {
    return a;
  }
  bool on_device = false, copied = false;
  bool gpu_drew = bv_now_word >> 31 != 0 && io_gpu;  // else the frame is in host memory
#if BEND_CUDA
  // On the device: one copy between device buffers, finished before the player may read it.
  pthread_mutex_lock(&bv_lock);
  // (while the player reads the front buffer, the back one is written, but neither is reallocated)
  bool device = gpu_drew && bv_device_ok && (n <= bv_dev_cap || (!bv_lent && bv_dev_room(n)));
  pthread_mutex_unlock(&bv_lock);
  if (device && cuMemcpyDtoD(bv_dev_back, gpu_at(px), n * 4) == CUDA_SUCCESS
    && cuCtxSynchronize() == CUDA_SUCCESS) {
    on_device = copied = true;
  }
#endif
  if (!on_device) {
    pthread_mutex_lock(&bv_lock);
    bool room = n <= bv_cap || (!bv_lent && bv_room(n));
    pthread_mutex_unlock(&bv_lock);
    if (!room) {
      return a;
    }
#if BEND_CUDA
    if (gpu_drew && bv_pinned) {
      copied = cuMemcpyDtoH(bv_back, gpu_at(px), n * 4) == CUDA_SUCCESS;
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
  if (on_device) {
    unsigned long long t = bv_dev_front;
    bv_dev_front = bv_dev_back, bv_dev_back = t;
  } else {
    u32* t = bv_front;
    bv_front = bv_back, bv_back = t;
  }
  bv_front_on_device = on_device;
  bv_draw_ms = (double)(drawn - bv_began) / 1e6, bv_copy_ms = (double)(now - drawn) / 1e6;
  bv_wait_frame = drawn_wait - bv_wait_began;
  bv_launch_frame = drawn_launch - bv_launch_began;
  bv_before_frame = before, bv_after_frame = after;
  bv_ms       = (double)(now - bv_began) / 1e6;
  bv_done_w   = fw, bv_done_h = fh;
  bv_done_gpu = gpu_drew;  // asked for, and there to use
  bv_fresh    = true;
  pthread_mutex_unlock(&bv_lock);
  return a;
}

static void __attribute__((constructor)) viz_use(void) {
#ifdef CID(Viz.next)
  io_eff(CID(Viz.next), viz_next_run, 0);
#endif
#ifdef CID(Viz.param)
  io_eff(CID(Viz.param), viz_param_run, 0);
#endif
#ifdef CID(Viz.show)
  io_eff(CID(Viz.show), viz_show_run, 0);
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
  pthread_mutex_lock(&bv_lock);
  memcpy(bv_ask, params, sizeof bv_ask);
  bv_ask_word = (u32)w | (u32)h << 13 | (gpu ? 1u << 31 : 0);
  bv_want     = true;
  pthread_cond_signal(&bv_asked);
  pthread_mutex_unlock(&bv_lock);
}

// Lends the newest finished frame (rows packed, w x h), if one arrived since the last call and is
// on the host, until bendviz_return; NULL otherwise. gpu is where it was drawn and ms how long it
// took, all told.
const u32* bendviz_borrow(int* w, int* h, bool* gpu, double* ms) {
  pthread_mutex_lock(&bv_lock);
  const u32* frame = NULL;
  if (bv_fresh && !bv_front_on_device && bv_front != NULL) {
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
static CUresult (CUDAAPI* bv_stream_sync)(CUstream);
// The player's copies go on a stream of their own, which doesn't wait for the kernels drawing the
// next frame (the default stream would, holding the front buffer lent all that time).
static CUstream bv_stream;

static bool bv_interop_ready(void) {
  static int ready = -1;
  if (ready < 0) {
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
    bv_stream_sync   = (__typeof__(bv_stream_sync))gpu_sym(lib, "cuStreamSynchronize");
    CUcontext ctx;  // this thread works in the runtime's context
    ready = bv_register && bv_unregister && bv_map_flags && bv_map && bv_unmap && bv_array && bv_copy2d
      && bv_stream_create && bv_stream_sync
      && cuDevicePrimaryCtxRetain(&ctx, gpu_dev) == CUDA_SUCCESS && cuCtxSetCurrent(ctx) == CUDA_SUCCESS
      && bv_stream_create(&bv_stream, 1) == CUDA_SUCCESS;  // CU_STREAM_NON_BLOCKING
  }
  return ready == 1;
}

// Copies the newest finished frame, if it is on the device, into a Direct3D 11 texture of its size
// (ID3D11Texture2D*, B8G8R8A8), without the host: returns 1, with w and h the frame's size and gpu
// and ms as for bendviz_borrow. Returns 0 when there is no such frame, with w and h the size of a
// waiting one (so the player can make a texture that size and call again), and -1 when interop
// fails: the player should then stop asking for device frames.
static void*              bv_registered;  // the texture CUDA has registered, and its handle
static CUgraphicsResource bv_res;
void bendviz_release_d3d11(void);

int bendviz_to_d3d11(void* texture, int tw, int th, int* w, int* h, bool* gpu, double* ms) {
  pthread_mutex_lock(&bv_lock);
  bool waiting = bv_fresh && bv_front_on_device;
  *w = bv_done_w, *h = bv_done_h;
  bool fits = waiting && bv_done_w == tw && bv_done_h == th;
  if (fits) {
    bv_lent = true;
  }
  pthread_mutex_unlock(&bv_lock);
  if (!fits) {
    return 0;
  }
  int result = -1;
  if (bv_interop_ready()) {
    if (bv_registered != texture) {
      bendviz_release_d3d11();
      if (bv_register(&bv_res, texture, 0) == CUDA_SUCCESS) {
        bv_registered = texture;
        bv_map_flags(bv_res, 2);  // write-discard: the old contents are not needed
      }
    }
    CUarray arr;
    if (bv_registered && bv_map(1, &bv_res, bv_stream) == CUDA_SUCCESS) {
      if (bv_array(&arr, bv_res, 0, 0) == CUDA_SUCCESS) {
        BvCopy2D c = { 0 };
        c.srcMemoryType = 2, c.srcDevice = bv_dev_front, c.srcPitch = (size_t)tw * 4;  // device
        c.dstMemoryType = 3, c.dstArray = arr;                                          // array
        c.WidthInBytes = (size_t)tw * 4, c.Height = (size_t)th;
        result = bv_copy2d(&c, bv_stream) == CUDA_SUCCESS ? 1 : -1;
      }
      if (bv_unmap(1, &bv_res, bv_stream) != CUDA_SUCCESS || bv_stream_sync(bv_stream) != CUDA_SUCCESS) result = -1;
    }
  }
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
  if (bv_registered) bv_unregister(bv_res);
  bv_registered = NULL;
}

#endif

void bendviz_return(void) {
  pthread_mutex_lock(&bv_lock);
  bv_lent = false;
  pthread_mutex_unlock(&bv_lock);
}

// How the last frame's time divides: drawing it (of which waiting for the GPU), and bringing it to
// the host.
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
