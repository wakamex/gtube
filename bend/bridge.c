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
#define BENDVIZ_TEXTURES 4          // images the player draws frames from (as in bendviz.h)

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
// Frames the player takes on the device: they never visit the host.
static bool            bv_device_ok;                   // the player can take them
static bool            bv_front_shared;                // the newest frame is in an image the player draws (bendviz_vk_take)
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
static int             bv_done_w, bv_done_h;
static bool            bv_fresh, bv_done_gpu;
static double          bv_ms;                          // how long the last frame took, all told
static double          bv_draw_ms, bv_copy_ms;          // of which drawing, and bringing it to the host
static u64             bv_began, bv_drawn;

// ---- The effects ----

#if BEND_VULKAN
static bool bv_vk_holding(void);
static void bv_vk_start(void);
static pthread_cond_t bv_vk_taken;
#endif

static void viz_next_call(IoWork* w) {  // an IO helper thread: no Bend heap here
  pthread_mutex_lock(&bv_lock);
  while (!bv_want) {
    pthread_cond_wait(&bv_asked, &bv_lock);
  }
#if BEND_VULKAN
  while (bv_vk_holding()) {  // (the next frame would draw into the buffer on screen)
    pthread_cond_wait(&bv_vk_taken, &bv_lock);
  }
#endif
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
#if BEND_VULKAN
  bv_vk_start();
  // With BEND_VK_STAMPS: the GPU's time for each kind of command, every 500 frames, on stderr.
  static u32 frames;
  if (gpu_stamp_pool != 0 && ++frames % 500 == 0) {
    u32 kind[GPU_STAMPS];
    double ms[GPU_STAMPS];
    u64 n[GPU_STAMPS];
    int k = gpu_stamps_take(kind, ms, n, GPU_STAMPS);
    for (int i = 0; i < k; i += 1) fprintf(stderr, "stamps: kind %u: %.3f ms x %.2f a frame\n", kind[i], ms[i], n[i] / 500.0);
  }
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


#if BEND_VULKAN
// ---- Frames into Vulkan images the player draws ----

// The player (gtube, on SDL's Vulkan renderer) draws on Bend's device (bendviz_vk_open), on the
// family's first queue, and shows Bend's frames in images, each an SDL texture. The images are
// linear and stay in the GENERAL layout. Where it can, an image is made over the buffer in Bend's
// heap the frame was drawn in (the program draws into two in turn), so the player draws the frame
// where Bend drew it and nothing is copied. Else (a row pitch the driver pads, say) the image has
// memory of its own and a kernel of Bend's (bend_blit) copies the frame in, on Bend's queue, once
// the player's draws of that image are done: after each present the player signals a timeline
// semaphore (bendviz_vk_mark), and the copy waits, on the GPU, for the value marked after the image
// was last drawn. (vkCmdCopyBufferToImage into an optimally tiled image took 0.7 ms at 4K on RTX
// 30s; bend_blit takes a sixth of that.) A frame is published once it is done on the GPU.
//
// Bend never draws into the buffer on screen: while a frame over one of its buffers waits to be
// taken, Bend starts no other (the next would draw into the buffer shown), and the frame after a
// take waits, on the GPU, for the mark after the last draw of the buffer shown before. With only
// one queue both share it, it runs everything in order, and the player holds bendviz_vk_lock
// whenever it may submit.
typedef struct {
  int sType; const void* pNext; VkFlags flags; int type; int format; uint32_t w, h, d;
  uint32_t mips, layers; VkFlags samples; int tiling; VkFlags usage; int sharing;
  uint32_t nfams; const uint32_t* fams; int layout;
} BvImageInfo;  // VkImageCreateInfo
typedef struct {
  int sType; const void* pNext; VkFlags src, dst; int old_layout, new_layout;
  uint32_t src_fam, dst_fam; uint64_t image;
  VkFlags aspect; uint32_t mip, nmips, layer, nlayers;
} BvImageBarrier;  // VkImageMemoryBarrier
typedef struct { VkFlags aspect; uint32_t mip, layer; } BvSubresource;  // VkImageSubresource
typedef struct { VkDeviceSize off, size, row, array, depth; } BvLayout;   // VkSubresourceLayout
typedef struct { VkFlags linear, optimal, buffer; } BvFormat;             // VkFormatProperties
typedef struct {
  int sType; const void* pNext; VkFlags flags; uint32_t n; const VkSemaphore* sems; const uint64_t* values;
} BvSemWait;  // VkSemaphoreWaitInfo

static VkResult (VKAPI* bv_vk_create_image)(VkDevice, const BvImageInfo*, const void*, uint64_t*);
static void (VKAPI* bv_vk_image_needs)(VkDevice, uint64_t, VkMemoryRequirements*);
static VkResult (VKAPI* bv_vk_bind_image)(VkDevice, uint64_t, VkDeviceMemory, VkDeviceSize);
static void (VKAPI* bv_vk_destroy_image)(VkDevice, uint64_t, const void*);
static void (VKAPI* bv_vk_free)(VkDevice, VkDeviceMemory, const void*);
static VkResult (VKAPI* bv_vk_queue_idle)(VkQueue);
static VkResult (VKAPI* bv_vk_submit)(VkQueue, uint32_t, const VkSubmitInfo*, VkFence);
static VkResult (VKAPI* bv_vk_sem_wait)(VkDevice, const BvSemWait*, uint64_t);
static void (VKAPI* bv_vk_sub_layout)(VkDevice, uint64_t, const BvSubresource*, BvLayout*);
static void (VKAPI* bv_vk_format)(VkPhysicalDevice, int, BvFormat*);
static void (VKAPI* bv_vk_destroy_buf)(VkDevice, VkBuffer, const void*);

#define BV_OWN 4  // images with memory of their own, at most (the rest of BENDVIZ_TEXTURES are over Bend's buffers)
typedef struct {
  uint64_t       image;
  u64            over;       // over Bend's heap: 1 + the frame's byte in it; else 0, and the image's
  VkDeviceMemory mem;        // memory, a buffer over it and its address
  VkBuffer       buf;
  u64            at;
  bool           laid;       // in the GENERAL layout (else still preinitialized)
  u64            read_done;  // the mark after the player last drew it
} BvSlot;

static struct {
  BvSlot         slot[BENDVIZ_TEXTURES];
  int            w, h;            // the frames' size; the rows of own images are pitch bytes apart
  u32            pitch;
  bool           no_over;         // no images over Bend's buffers (the player asked, or they failed)
  u32            gen;             // of the set of images (0: none), and of the pending frame's set
  u32            pending_gen;
  // Under bv_lock: the image on screen, the one waiting to be taken, and the one written this frame
  // (published at the swap), -1 for none; and whether Bend's thread is making or writing one.
  int            shown, published, pending;
  bool           busy;
  bool           held;            // the player holds bendviz_vk_lock (its thread only)
  VkSemaphore    sem;             // the player's marks, and the last
  u64            mark;
  u64            start_after;     // the next frame's work waits for this mark
  VkQueue        queue;           // the player's (the family's first)
  bool           share_q;         // Bend submits to the player's queue too (see bendviz_vk_pump)
} bv_vk = { .shown = -1, .published = -1, .pending = -1 };
static pthread_mutex_t bv_q1_lock = PTHREAD_MUTEX_INITIALIZER;  // submits to Bend's own queue
static pthread_cond_t bv_vk_taken = PTHREAD_COND_INITIALIZER;  // a frame was taken (or given up)

static bool bv_vk_load(void) {
  static int loaded;
  if (loaded == 0) {
#define BV_VK(f, name) f = (__typeof__(f))vkGetInstanceProcAddr(gpu_inst, name)
    BV_VK(bv_vk_create_image, "vkCreateImage");
    BV_VK(bv_vk_image_needs, "vkGetImageMemoryRequirements");
    BV_VK(bv_vk_bind_image, "vkBindImageMemory");
    BV_VK(bv_vk_destroy_image, "vkDestroyImage");
    BV_VK(bv_vk_free, "vkFreeMemory");
    BV_VK(bv_vk_queue_idle, "vkQueueWaitIdle");
    BV_VK(bv_vk_submit, "vkQueueSubmit");
    BV_VK(bv_vk_sem_wait, "vkWaitSemaphores");
    BV_VK(bv_vk_sub_layout, "vkGetImageSubresourceLayout");
    BV_VK(bv_vk_format, "vkGetPhysicalDeviceFormatProperties");
    BV_VK(bv_vk_destroy_buf, "vkDestroyBuffer");
    loaded = bv_vk_create_image && bv_vk_image_needs && bv_vk_bind_image && bv_vk_destroy_image && bv_vk_free
      && bv_vk_queue_idle && bv_vk_submit && bv_vk_sem_wait && bv_vk_sub_layout && bv_vk_format && bv_vk_destroy_buf ? 1 : -1;
  }
  return loaded == 1;
}

// A barrier moving an image between layouts, after everything before it on the queue.
static void bv_vk_layout(uint64_t image, int from, int to, VkFlags src, VkFlags dst) {
  BvImageBarrier b = { 45, NULL, src, dst, from, to, ~0u, ~0u, image, 1, 0, 1, 0, 1 };
  vkCmdPipelineBarrier(gpu_cb, 0x10000, 0x10000, 0, 0, NULL, 0, NULL, 1, &b);
}

static void bv_vk_drop(BvSlot* s) {
  if (s->image != 0) bv_vk_destroy_image(gpu_dev, s->image, NULL);
  if (s->buf != 0) bv_vk_destroy_buf(gpu_dev, s->buf, NULL);
  if (s->mem != 0) bv_vk_free(gpu_dev, s->mem, NULL);
  *s = (BvSlot){ 0 };
}

// A linear image of w x h: over Bend's heap at byte over - 1, or with memory of its own. False if the
// device can't (the image must be sampled; over the heap its rows must be packed, as the frame's
// are, and its place aligned).
static bool bv_vk_make(BvSlot* s, int w, int h, u64 over) {
  BvImageInfo ii = { 14, NULL, 0, 1, 44, (uint32_t)w, (uint32_t)h, 1, 1, 1, 1, 1, 0x4, 0, 0, NULL, 8 };
  BvSubresource sr = { 1, 0, 0 };
  BvLayout lay;
  BvFormat fp;
  VkMemoryRequirements mi, mb;
  bv_vk_format(gpu_phys, 44, &fp);
  if ((fp.linear & 1) == 0 || bv_vk_create_image(gpu_dev, &ii, NULL, &s->image) != 0) {
    s->image = 0;
    return false;
  }
  bv_vk_image_needs(gpu_dev, s->image, &mi);
  bv_vk_sub_layout(gpu_dev, s->image, &sr, &lay);
  bool ok = lay.off == 0;
  if (over != 0) {
    u64 at = over - 1;
    ok = ok && lay.row == (u64)w * 4 && (mi.types >> gpu_heap.type & 1) != 0 && at % mi.align == 0
      && at + mi.size <= gpu_heap.size && bv_vk_bind_image(gpu_dev, s->image, gpu_heap.mem, at) == 0;
    s->over = over;
  } else {
    VkBufferCreateInfo bi = { 12, NULL, 0, mi.size, 0x20020, 0, 0, NULL };  // (storage, addressed)
    ok = ok && vkCreateBuffer(gpu_dev, &bi, NULL, &s->buf) == 0;
    if (ok) {
      vkGetBufferMemoryRequirements(gpu_dev, s->buf, &mb);
      VkMemoryAllocateFlagsInfo fl = { 1000060000, NULL, 2, 0 };
      VkMemoryAllocateInfo ai = { 5, &fl, mi.size > mb.size ? mi.size : mb.size, gpu_mem_pick(mi.types & mb.types, 1, 0) };
      VkBufferDeviceAddressInfo di = { 1000244001, NULL, s->buf };
      ok = ai.type != ~0u && vkAllocateMemory(gpu_dev, &ai, NULL, &s->mem) == 0
        && bv_vk_bind_image(gpu_dev, s->image, s->mem, 0) == 0 && vkBindBufferMemory(gpu_dev, s->buf, s->mem, 0) == 0;
      if (ok) s->at = vkGetBufferDeviceAddress(gpu_dev, &di), bv_vk.pitch = (u32)lay.row;
    }
  }
  if (!ok) bv_vk_drop(s);
  return ok;
}

// On Bend's thread, the frame at byte `at` of the heap just drawn (fw x fh): into an image, if the
// player takes frames of this size. Whether it went; the swap then publishes it.
static bool bv_vk_frame(unsigned long long at, int fw, int fh) {
  pthread_mutex_lock(&bv_lock);
  u32 gen = bv_vk.gen;
  bool fits = gen != 0 && bv_vk.w == fw && bv_vk.h == fh;
  int over = -1, own = -1, blank = -1, nown = 0;  // (the slot over this buffer; a free own one; an unused one)
  for (int i = 0; fits && i < BENDVIZ_TEXTURES; i += 1) {
    BvSlot* s = &bv_vk.slot[i];
    bool free = i != bv_vk.shown && i != bv_vk.published;
    if (s->image == 0) {
      if (blank < 0) blank = i;
    } else if (s->over != 0) {
      if (s->over == at + 1 && free) over = i;
    } else {
      nown += 1;
      if (free && (own < 0 || s->read_done < bv_vk.slot[own].read_done)) own = i;
    }
  }
  bool try_over = fits && over < 0 && !bv_vk.no_over && blank >= 0;
  bool make_own = fits && over < 0 && own < 0 && nown < BV_OWN && blank >= 0;
  bv_vk.busy = fits;
  pthread_mutex_unlock(&bv_lock);
  if (!fits) {
    return false;
  }
  if (try_over) {  // (a slot from the pool of unused ones: Bend's thread alone makes them)
    if (bv_vk_make(&bv_vk.slot[blank], fw, fh, at + 1)) {
      over = blank;
    } else {
      bv_vk.no_over = true;
      make_own = own < 0 && nown < BV_OWN;
    }
  }
  if (over < 0 && make_own && bv_vk_make(&bv_vk.slot[blank], fw, fh, 0)) {
    own = blank;
  }
  int slot = over >= 0 ? over : own;
  if (slot >= 0) {
    BvSlot* s = &bv_vk.slot[slot];
    gpu_lock_on();
    if (!s->laid) {  // preinitialized to GENERAL, once, keeping the memory as it is
      gpu_cmd();
      bv_vk_layout(s->image, 8, 1, 0, 0x20);
    }
    if (s->over == 0) {
      gpu_blit(at, s->at, (u32)fw, (u32)fh, bv_vk.pitch);
      gpu_wait_sem = bv_vk.sem, gpu_wait_at = s->read_done;  // (0: nothing to wait for)
    }
    gpu_flush();
    gpu_lock_off();
    s->laid = true;
  }
  pthread_mutex_lock(&bv_lock);
  bv_vk.busy = false;
  if (slot >= 0) bv_vk.pending = slot, bv_vk.pending_gen = gen;
  pthread_mutex_unlock(&bv_lock);
  return slot >= 0;
}

// On Bend's thread as a frame starts: its work waits, on the GPU, for the player's last draw of the
// buffer it draws into.
static void bv_vk_start(void) {
  pthread_mutex_lock(&bv_lock);
  if (bv_vk.start_after != 0) gpu_wait_sem = bv_vk.sem, gpu_wait_at = bv_vk.start_after;
  pthread_mutex_unlock(&bv_lock);
}

// Under bv_lock: a frame over one of Bend's buffers waits to be taken (and Bend starts no other).
static bool bv_vk_holding(void) {
  return bv_fresh && bv_front_shared && bv_vk.published >= 0 && bv_vk.slot[bv_vk.published].over != 0;
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
  bool copied = false;
  bool gpu_drew = bv_now_word >> 31 != 0 && io_gpu;  // else the frame is in host memory
  bool shared = false;                                 // it went into an image the player draws
#if BEND_VULKAN
  // A frame drawn on the GPU goes into an image the player draws, never crossing to the host.
  pthread_mutex_lock(&bv_lock);
  bool device = gpu_drew && bv_device_ok;
  pthread_mutex_unlock(&bv_lock);
  if (device) {  // (a frame no image fits, the size just changed, comes through the host)
    shared = copied = bv_vk_frame((unsigned long long)((char*)px - (char*)CORPUS), fw, fh);
  }
#endif
  if (!shared) {
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
#elif BEND_VULKAN
    if (gpu_drew) {
      gpu_fetch(bv_back, (u64)((char*)px - (char*)CORPUS), n * 4);
      copied = true;
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
#if BEND_VULKAN
    bool current = bv_vk.gen == bv_vk.pending_gen;  // (else the player made a new set since)
    if (current) bv_vk.published = bv_vk.pending;
    bv_vk.pending = -1;
    if (!current) {
      bv_n_dropped += 1;
      pthread_mutex_unlock(&bv_lock);
      return a;
    }
#endif
  } else {
    u32* t = bv_front;
    bv_front = bv_back, bv_back = t;
  }
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
  bv_trace("swap", shared, 0);
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
#if BEND_VULKAN
  pthread_cond_broadcast(&bv_vk_taken);
#endif
  pthread_mutex_unlock(&bv_lock);
}

// Lends the newest finished frame (rows packed, w x h), if one arrived since the last call and is
// on the host, until bendviz_return; NULL otherwise. gpu is where it was drawn and ms how long it
// took, all told.
const u32* bendviz_borrow(int* w, int* h, bool* gpu, double* ms) {
  pthread_mutex_lock(&bv_lock);
  const u32* frame = NULL;
  bv_trace("borrow-try", bv_fresh, bv_front_shared);
  if (bv_fresh && !bv_front_shared && bv_front != NULL) {
    frame = bv_front, bv_lent = true, bv_fresh = false;
    *w = bv_done_w, *h = bv_done_h, *gpu = bv_done_gpu, *ms = bv_ms;
  }
  pthread_mutex_unlock(&bv_lock);
  return frame;
}

// Whether the player takes frames drawn on the GPU on the device (bendviz_vk_take), from the next
// one on; otherwise they come to the host.
void bendviz_device_frames(bool on) {
  pthread_mutex_lock(&bv_lock);
  bv_device_ok = on;
  pthread_mutex_unlock(&bv_lock);
}


#if BEND_VULKAN

static VkResult bv_vk_submit_any(const VkSubmitInfo* si);

// Opens Bend's Vulkan device for the player to draw on too, with the extensions its drawing needs
// (an instance's to reach the window, a device's to present): the instance, physical device, device
// and queue family (a VkInstance, VkPhysicalDevice, VkDevice and index), or false without one (Bend
// then runs on the CPU). Before bendviz_start.
bool bendviz_vk_open(const char* const* iexts, int niexts, const char* const* dexts, int ndexts, void** inst,
  void** phys, void** dev, unsigned* family) {
  gpu_iexts = iexts, gpu_niexts = (u32)niexts, gpu_dexts = dexts, gpu_ndexts = (u32)ndexts;
  VkSemaphoreTypeCreateInfo ti = { 1000207002, NULL, 1, 0 };  // a timeline, from 0
  VkSemaphoreCreateInfo     si = { 9, &ti, 0 };
  if (!gpu_probe() || !bv_vk_load() || vkCreateSemaphore(gpu_dev, &si, NULL, &bv_vk.sem) != 0) {
    return false;
  }
  gpu_submit = bv_vk_submit_any;
  vkGetDeviceQueue(gpu_dev, gpu_family, 0, &bv_vk.queue);
  *inst = gpu_inst, *phys = gpu_phys, *dev = gpu_dev, *family = gpu_family;
  return true;
}

// After each present, on the player's thread: marks the draws submitted so far, which cover the
// image on screen (a signal on the player's queue, after everything submitted before it).
void bendviz_vk_mark(void) {
  if (gpu_qshared || bv_vk.share_q) {
    return;  // (one queue runs everything in order)
  }
  u64 value = bv_vk.mark + 1;
  VkTimelineSemaphoreSubmitInfo ti = { 1000207003, NULL, 0, NULL, 1, &value };
  VkSubmitInfo si = { 4, &ti, 0, NULL, NULL, 0, NULL, 1, &bv_vk.sem };
  if (bv_vk_submit(bv_vk.queue, 1, &si, 0) != 0) {
    return;
  }
  pthread_mutex_lock(&bv_lock);
  bv_vk.mark = value;
  if (bv_vk.shown >= 0) bv_vk.slot[bv_vk.shown].read_done = value;
  pthread_mutex_unlock(&bv_lock);
}

// With vsync, on the player's thread after bendviz_vk_mark: waits until the frame before this one
// is done on the GPU. Its queue otherwise runs frames behind (each waits for a swapchain image to
// come free), and the marks with it, so an image Bend could copy into would come free late.
void bendviz_vk_settle(void) {
  u64 prev = bv_vk.mark - 1;
  BvSemWait w = { 1000207004, NULL, 0, 1, &bv_vk.sem, &prev };
  if (!gpu_qshared && !bv_vk.share_q && bv_vk.mark > 1) bv_vk_sem_wait(gpu_dev, &w, 100000000ull);  // (0.1 s at most)
}

// Without vsync (frames drawn from Bend's buffers), Bend submits to the player's queue: their work
// then runs one after the other with nothing between them, where on a queue each the GPU changes
// context between them (about 120 us, twice a frame, on an RTX 3080 under Windows). Bend submits
// itself while the player lets go of the queue; while the player holds it, Bend posts its submit and
// the player makes it at its next bendviz_vk_pump (before its present) or when it lets go. With
// vsync the player holds the queue through each present's wait for the vertical blank, so Bend uses
// its own queue (a family with only one shares it always).
static pthread_mutex_t     bv_rq_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t      bv_rq_done = PTHREAD_COND_INITIALIZER;  // (or the queue let go)
static const VkSubmitInfo* bv_rq;  // a submit posted, not yet made
static VkResult            bv_rq_result;

static VkResult bv_vk_submit_shared(const VkSubmitInfo* si) {
  pthread_mutex_lock(&bv_rq_lock);
  bv_rq = si;
  while (bv_rq != NULL) {
    if (pthread_mutex_trylock(&gpu_qlock) == 0) {
      bv_rq = NULL;
      bv_rq_result = bv_vk_submit(bv_vk.queue, 1, si, gpu_fence);
      pthread_mutex_unlock(&gpu_qlock);
      break;
    }
    pthread_cond_wait(&bv_rq_done, &bv_rq_lock);
  }
  VkResult r = bv_rq_result;
  pthread_mutex_unlock(&bv_rq_lock);
  return r;
}

static VkResult bv_vk_submit_any(const VkSubmitInfo* si) {
  if (bv_vk.share_q || gpu_qshared) {
    return bv_vk_submit_shared(si);
  }
  pthread_mutex_lock(&bv_q1_lock);
  VkResult r = bv_vk_submit(gpu_queue, 1, si, gpu_fence);
  pthread_mutex_unlock(&bv_q1_lock);
  return r;
}

// On the player's thread, holding the queue: makes a submit Bend posted.
void bendviz_vk_pump(void) {
  pthread_mutex_lock(&bv_rq_lock);
  if (bv_rq != NULL) {
    bv_rq_result = bv_vk_submit(bv_vk.queue, 1, bv_rq, gpu_fence);
    bv_rq = NULL;
    pthread_cond_broadcast(&bv_rq_done);
  }
  pthread_mutex_unlock(&bv_rq_lock);
}

// Held by the player around its drawing (from its first draw to its present): Bend's submits wait.
// The player lets a waiting submit go first, or at a thousand frames a second it would take the
// lock back each time before Bend's thread woke.
void bendviz_vk_lock(void) {
  while (atomic_load(&gpu_qwant) != 0) {
    sched_yield();
  }
  pthread_mutex_lock(&gpu_qlock);
  bv_vk.held = true;
}

void bendviz_vk_unlock(void) {
  bendviz_vk_pump();
  bv_vk.held = false;
  pthread_mutex_unlock(&gpu_qlock);
  pthread_mutex_lock(&bv_rq_lock);  // (a submit posted meanwhile goes itself)
  pthread_cond_broadcast(&bv_rq_done);
  pthread_mutex_unlock(&bv_rq_lock);
}

// Lets go of the images (the player's textures of them gone), once the queue is done with them. A
// frame being written finishes first (with the player's lock let go meanwhile: it waits for the queue).
void bendviz_vk_unshare(void) {
  bool held = bv_vk.held;
  pthread_mutex_lock(&bv_lock);
  bv_vk.gen = 0, bv_vk.shown = bv_vk.published = -1;
  pthread_cond_broadcast(&bv_vk_taken);  // (a frame waiting to be taken never will be)
  while (bv_vk.busy) {
    pthread_mutex_unlock(&bv_lock);
    if (held) bendviz_vk_unlock();
    sched_yield();
    if (held) bendviz_vk_lock();
    pthread_mutex_lock(&bv_lock);
  }
  pthread_mutex_unlock(&bv_lock);
  bool any = false;
  for (int i = 0; i < BENDVIZ_TEXTURES; i += 1) any = any || bv_vk.slot[i].image != 0;
  if (any) {  // (both queues: Bend's work may be on either)
    if (!held) pthread_mutex_lock(&gpu_qlock);
    bv_vk_queue_idle(bv_vk.queue);
    if (!held) pthread_mutex_unlock(&gpu_qlock);
    pthread_mutex_lock(&bv_q1_lock);
    bv_vk_queue_idle(gpu_queue);
    pthread_mutex_unlock(&bv_q1_lock);
  }
  for (int i = 0; i < BENDVIZ_TEXTURES; i += 1) bv_vk_drop(&bv_vk.slot[i]);
}

// Frames of w x h go into images from here on (made as they are needed; see bendviz_vk_take),
// replacing any set made before (the player's textures of those gone first); false if the device
// can't sample linear images. With over, where it can, the player draws a frame from the buffer Bend
// drew it in, and Bend draws the next only once that one is taken and the draws of the buffer before
// are done: without vsync the fastest, as nothing is copied, but with vsync those draws run a frame
// late, so the player then asks for copies into images of their own.
bool bendviz_vk_frames(int w, int h, bool over) {
  bendviz_vk_unshare();
  BvFormat fp;
  if (!bv_vk_load()) {
    return false;
  }
  bv_vk_format(gpu_phys, 44, &fp);
  if ((fp.linear & 1) == 0) {
    return false;
  }
  pthread_mutex_lock(&bv_lock);
  static u32 gens;
  gens = gens + 1 ? gens + 1 : 1;
  bv_vk.gen = gens, bv_vk.w = w, bv_vk.h = h, bv_vk.no_over = !over, bv_vk.share_q = over;
  pthread_mutex_unlock(&bv_lock);
  return true;
}

// Each frame: the slot (0 to BENDVIZ_TEXTURES - 1) of a new frame to draw, with its VkImage (the same
// for a slot until the next bendviz_vk_frames: a texture made of it lasts until then), or -1 when
// none is waiting. The image drawn before is Bend's to write again once the draws of it are done.
int bendviz_vk_take(int* w, int* h, bool* gpu, double* ms, unsigned long long* image) {
  pthread_mutex_lock(&bv_lock);
  int slot = -1;
  if (bv_fresh && bv_front_shared && bv_vk.published >= 0) {
    slot = bv_vk.shown = bv_vk.published, bv_vk.published = -1, bv_fresh = false;
    *w = bv_done_w, *h = bv_done_h, *gpu = bv_done_gpu, *ms = bv_ms, *image = bv_vk.slot[slot].image;
    // (the last mark covers the draws of the image shown before, which, over Bend's buffer, the next
    // frame may draw into)
    bv_vk.start_after = gpu_qshared || bv_vk.share_q || bv_vk.slot[slot].over == 0 ? 0 : bv_vk.mark;
    pthread_cond_broadcast(&bv_vk_taken);
  }
  bv_trace("vk-take", slot, 0);
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
#if BEND_CUDA || BEND_VULKAN
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
static double bv_kms[8], bv_kgap[8];  // (BENDVIZ_KERNELS, below)
static u64 bv_kn[8];
static u32 bv_kgrid[8];
#if BEND_CUDA
static __typeof__(gpu_fn_cuLaunchKernel) bv_real_launch;
static __typeof__(gpu_fn_cuCtxSynchronize) bv_real_sync;
// With BENDVIZ_KERNELS set, each launch is waited for, and its time kept by its place in the frame;
// set to "events", the device's own clock times each kernel and the gap before it, without waiting.
static int bv_ktime = -1;  // 0 off, 1 waiting, 2 events
static u32 bv_kidx, bv_kprev;
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
