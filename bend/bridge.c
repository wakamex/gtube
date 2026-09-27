// The player's side of viz.bend, and the effects that program imports. The Bend program runs on a
// thread of its own (its main renamed bendviz_main): each frame it blocks in Viz.next until the
// player asks, reads the request with Viz.param, draws, and hands the pixels over with Viz.show.
// The player never waits: it asks for a frame and takes the newest finished one when it is ready.
// Included in the Bend program's C (the runtime's types and helpers are in scope), and compiled
// into the player; bendviz.h declares the player's side.

#ifndef BENDVIZ_BRIDGE
#define BENDVIZ_BRIDGE

#define BENDVIZ_W 1024
#define BENDVIZ_H 512

static pthread_mutex_t bv_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  bv_asked = PTHREAD_COND_INITIALIZER;
static bool            bv_want;                       // a request waiting for Bend
static float           bv_ask[5], bv_now[5];          // the request, and the one being drawn
static u32             bv_ask_word, bv_now_word;       // gpu flag and size, as Viz.next returns them
static u32*            bv_pixels;                      // the newest finished frame, BENDVIZ_W wide
static u32*            bv_copy;                        // a staging copy for the GPU's bulk transfer
static int             bv_done_w, bv_done_h;
static bool            bv_fresh, bv_done_gpu;
static double          bv_ms;                          // how long the last frame took to draw
static u64             bv_began;

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

static Term viz_next_pack(Env e, IoWork* w) {
  bv_began = io_tick();
  return (Term)bv_now_word;
}

Term viz_next_run(Env e, Term* f, IoWork* w) {
  return io_work(w, viz_next_call, viz_next_pack);
}

Term viz_param_run(Env e, Term* f, IoWork* w) {
  u32 i = (u32)f[0];
  return f32_rewrap(i < 5 ? bv_now[i] : 0.0f);
}

// On the GPU the heap is managed memory, which the host reads a page at a time (slowly, on
// Windows); one bulk copy brings the frame over instead.
Term viz_show_run(Env e, Term* f, IoWork* w) {
  Term a  = f[0];
  u32* px = (u32*)blk_ptr(e.mem, blk_loc(e.mem, a), 0);
  u32  n  = 1u << blk_cls(a);
  const u32* from = px;
#if BEND_CUDA
  if (io_gpu && bv_copy != NULL
    && cuMemcpyDtoH(bv_copy, (CUdeviceptr)(uintptr_t)px, (size_t)n * 4) == CUDA_SUCCESS) {
    from = bv_copy;
  }
#endif
  double ms = (double)(io_tick() - bv_began) / 1e6;
  pthread_mutex_lock(&bv_lock);
  if (bv_pixels != NULL && n == BENDVIZ_W * BENDVIZ_H) {
    memcpy(bv_pixels, from, (size_t)n * 4);
    bv_done_w   = (int)(bv_now_word & 2047);
    bv_done_h   = (int)(bv_now_word >> 11 & 1023);
    bv_done_gpu = bv_now_word >> 31 != 0;
    bv_ms       = ms;
    bv_fresh    = true;
  }
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
  bv_pixels = calloc((size_t)BENDVIZ_W * BENDVIZ_H, 4);
  bv_copy   = calloc((size_t)BENDVIZ_W * BENDVIZ_H, 4);
  pthread_t tid;
  if (bv_pixels == NULL || bv_copy == NULL || pthread_create(&tid, NULL, bv_thread, (void*)gpu_heap)) {
    return false;
  }
  pthread_detach(tid);
  started = true;
  return true;
}

// Asks for a frame of w x h (at most BENDVIZ_W x BENDVIZ_H), replacing any request not yet
// started. params: time, bass, mids, hue, beat.
void bendviz_request(const float params[5], int w, int h, bool gpu) {
  w = w < 1 ? 1 : w > BENDVIZ_W ? BENDVIZ_W : w;
  h = h < 1 ? 1 : h > BENDVIZ_H ? BENDVIZ_H : h;
  pthread_mutex_lock(&bv_lock);
  memcpy(bv_ask, params, sizeof bv_ask);
  bv_ask_word = (u32)w | (u32)h << 11 | (gpu ? 1u << 31 : 0);
  bv_want     = true;
  pthread_cond_signal(&bv_asked);
  pthread_mutex_unlock(&bv_lock);
}

// Copies the newest finished frame (rows BENDVIZ_W apart) into out, if one arrived since the last
// call; w and h are its size, gpu where it was drawn and ms how long drawing it took.
bool bendviz_take(u32* out, int* w, int* h, bool* gpu, double* ms) {
  pthread_mutex_lock(&bv_lock);
  bool fresh = bv_fresh;
  if (fresh) {
    memcpy(out, bv_pixels, (size_t)BENDVIZ_W * BENDVIZ_H * 4);
    *w = bv_done_w, *h = bv_done_h, *gpu = bv_done_gpu, *ms = bv_ms;
    bv_fresh = false;
  }
  pthread_mutex_unlock(&bv_lock);
  return fresh;
}

// Whether the GPU is in use (false until the program has started and probed it).
bool bendviz_gpu(void) {
  return io_gpu;
}

#endif
#endif
