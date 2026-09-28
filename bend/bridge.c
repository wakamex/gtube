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
