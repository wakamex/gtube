# The Bend effect in gtube's visualizer: a plasma drawing every pixel of the visualizer (up to
# 4096 x 4096) into a flat buffer, rows packed at the frame's width, on the GPU or on the CPU's
# threads, as the player asks each frame. Both run the same pixel function (Viz.px); each chip gets
# the schedule that suits it: square tiles for the CPU, whose threads each keep to one patch of
# memory, and a stride for the GPU, whose neighbouring lanes then write neighbouring pixels.
#
# The program runs on a thread of the player for as long as the player does: it waits for the
# player to ask for a frame (Viz.next), reads that frame's inputs (Viz.param), draws, and hands
# the buffer back (Viz.show), which returns it for the next frame. bridge.c holds those effects
# and the player's side of them.
import Base

# What the player asks for: bit 31 set for the GPU, the width in bits 0-12 and the height in
# bits 13-25 (each at most 4096).
def Viz.next() -> IO(U32):
  import "./bridge.c"

# 0 time in seconds, 1 bass, 2 mids, 3 hue, 4 beat (1 on a beat, fading).
def Viz.param(i: U32) -> IO(F32):
  import "./bridge.c"

def Viz.show(a: Array<U32>) -> IO(Array<U32>):
  import "./bridge.c"

type Viz.K is Data:
  K{t: F32, zoom: F32, mid: F32, hue: F32, beat: F32, w: U32, h: U32}

# A colour from a palette position f, the classic cosine palette, brighter on the beat.
def Viz.color(+f: F32, +t: F32, +hue: F32, +beat: F32) -> U32:
  +v = F32.min(1.0, (0.6 + 0.25 * F32.sin((f * 12.566 + t : F32)) + beat * 0.25 : F32))
  +r = F32.to_u32((255.0 * v * (0.5 + 0.5 * F32.cos((6.2832 * (f * 0.6 + hue) : F32))) : F32))
  +g = F32.to_u32((255.0 * v * (0.5 + 0.5 * F32.cos((6.2832 * (f * 0.6 + hue + 0.33) : F32))) : F32))
  +b = F32.to_u32((255.0 * v * (0.5 + 0.5 * F32.cos((6.2832 * (f * 0.6 + hue + 0.67) : F32))) : F32))
  (r << 16n .|. g << 8n .|. b : U32)

# One pixel: four sine waves, zoomed by the bass, the last one rippling out with the mids.
def Viz.px(+x: U32, +y: U32, +k: Viz.K) -> U32:
  K{t, zoom, mid, hue, beat, w, h} = k
  +unit = (200.0 / U32.to_f32(h) * zoom : F32)
  +dx = ((U32.to_f32(x) - U32.to_f32(w) * 0.5) * unit : F32)
  +dy = ((U32.to_f32(y) - U32.to_f32(h) * 0.5) * unit : F32)
  +w1 = F32.sin((dx + t : F32))
  +w2 = F32.sin((dy * 1.3 + t * 1.1 : F32))
  +w3 = F32.sin(((dx + dy) * 0.7 + t * 0.7 : F32))
  +w4 = F32.sin((F32.hypot(dx, dy) * 1.5 - t * 2.0 - mid * 3.0 : F32))
  Viz.color(((w1 + w2 + w3 + w4 + 4.0) * 0.125 + t * 0.15 : F32), t, hue, beat)

def Viz.put.at(+x: U32, +y: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h} = k
  Array.set(U32, a, (y * w + x : U32), Viz.px(x, y, k))

# Pixels past the frame the player asked for are left alone.
def Viz.put(inside: Bool, +x: U32, +y: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match inside:
    case True{}:
      Viz.put.at(x, y, k, a)
    case False{}:
      a

# Loops count down a Nat, which is how the checker sees that they end.
def Viz.row(n: Nat, +x: U32, +y: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+r:
      K{t, zoom, mid, hue, beat, w, h} = k
      Viz.row(r, (x + 1 : U32), y, k, Viz.put(((x < w) && (y < h) : U32), x, y, k, a))

def Viz.tile(n: Nat, +y: U32, +x: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+r:
      Viz.tile(r, (y + 1 : U32), x, k, Viz.row(16n, x, y, k, a))

# A square of 16 << d pixels at (x, y): four quarters in parallel, down to 16x16 tiles. A square
# wholly outside the frame is skipped before it forks. The buffer is forked into aliases, one per
# quarter (Array.fork is @unsafe: the quarters write disjoint pixels), each taken apart by its own
# def, since destructuring a computed pair is a match.
@unsafe
def Viz.fill(+d: Nat, +x: U32, +y: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h} = k
  Viz.fill.in(((x < w) && (y < h) : U32), d, x, y, k, a)

@unsafe
def Viz.fill.in(inside: Bool, +d: Nat, +x: U32, +y: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match inside:
    case False{}:
      a
    case True{}:
      Viz.fill.go(d, x, y, k, a)

@unsafe
def Viz.fill.go(+d: Nat, +x: U32, +y: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match d:
    case 0n:
      Viz.tile(16n, y, x, k, a)
    case 1n+e:
      Viz.fork1(e, x, y, k, Array.fork(U32, a))

@unsafe
def Viz.fork1(+e: Nat, +x: U32, +y: U32, +k: Viz.K, p: Array<U32> & Array<U32>) -> Array<U32>:
  (a1, r) = p
  Viz.fork2(e, x, y, k, a1, Array.fork(U32, r))

@unsafe
def Viz.fork2(+e: Nat, +x: U32, +y: U32, +k: Viz.K, a1: Array<U32>, p: Array<U32> & Array<U32>) -> Array<U32>:
  (a2, r) = p
  Viz.fork3(e, x, y, k, a1, a2, Array.fork(U32, r))

@unsafe
def Viz.fork3(+e: Nat, +x: U32, +y: U32, +k: Viz.K, a1: Array<U32>, a2: Array<U32>, p: Array<U32> & Array<U32>) -> Array<U32>:
  (a3, a4) = p
  +h = U32.shln(16, e)
  +x1 = U32.add(x, h)
  +y1 = U32.add(y, h)
  q w r z = Viz.fill(e, x, y, k, a1) Viz.fill(e, x1, y, k, a2)
    Viz.fill(e, x, y1, k, a3) Viz.fill(e, x1, y1, k, a4)
  Array.join(U32, Array.join(U32, q, w), Array.join(U32, r, z))

# The whole frame on the CPU: one 4096 square (16 << 8), of which the frame's part is drawn.
@unsafe
def Viz.frame(+k: Viz.K, a: Array<U32>) -> Array<U32>:
  Viz.fill(8n, 0, 0, k, a)

# Strided: 16,384 leaves (4^7, one per GPU lane), leaf j drawing pixels j, j + 16384, ... so that
# neighbouring lanes write neighbouring pixels, as CUDA's threads do.
def Viz.lput(inside: Bool, +i: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match inside:
    case True{}:
      K{t, zoom, mid, hue, beat, w, h} = k
      Array.set(U32, a, i, Viz.px((i % w : U32), (i / w : U32), k))
    case False{}:
      a

def Viz.lin(n: Nat, +i: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+r:
      K{t, zoom, mid, hue, beat, w, h} = k
      Viz.lin(r, (i + 16384 : U32), k, Viz.lput((i < w * h : U32), i, k, a))

@unsafe
def Viz.lfill(+d: Nat, +j: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match d:
    case 0n:
      K{t, zoom, mid, hue, beat, w, h} = k
      Viz.lin(U32.to_nat(((w * h + 16383) / 16384 : U32)), j, k, a)
    case 1n+e:
      Viz.lfork1(e, j, k, Array.fork(U32, a))

@unsafe
def Viz.lfork1(+e: Nat, +j: U32, +k: Viz.K, p: Array<U32> & Array<U32>) -> Array<U32>:
  (a1, r) = p
  Viz.lfork2(e, j, k, a1, Array.fork(U32, r))

@unsafe
def Viz.lfork2(+e: Nat, +j: U32, +k: Viz.K, a1: Array<U32>, p: Array<U32> & Array<U32>) -> Array<U32>:
  (a2, r) = p
  Viz.lfork3(e, j, k, a1, a2, Array.fork(U32, r))

@unsafe
def Viz.lfork3(+e: Nat, +j: U32, +k: Viz.K, a1: Array<U32>, a2: Array<U32>, p: Array<U32> & Array<U32>) -> Array<U32>:
  (a3, a4) = p
  +s = U32.shln(1, (e + e : Nat))
  q w r z = Viz.lfill(e, j, k, a1) Viz.lfill(e, (j + s : U32), k, a2)
    Viz.lfill(e, (j + s + s : U32), k, a3) Viz.lfill(e, (j + s + s + s : U32), k, a4)
  Array.join(U32, Array.join(U32, q, w), Array.join(U32, r, z))

# The whole frame on the GPU.
@unsafe
def Viz.frame.gpu(+k: Viz.K, a: Array<U32>) -> Array<U32>:
  Viz.lfill(7n, 0, k, a)

# The same pixels, on the GPU or on the CPU's threads.
@unsafe
def Viz.draw(gpu: Bool, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match gpu:
    case True{}:
      Viz.frame.gpu!(k, a)
    case False{}:
      Viz.frame(k, a)

# One frame as asked (a value bound in a do block is used once, so the request is taken apart here).
@unsafe
def Viz.step(+ask: U32, t: F32, bass: F32, mid: F32, hue: F32, beat: F32, a: Array<U32>) -> IO(Array<U32>):
  +k = {K{t, (0.045 * (1.0 + bass * 0.5) : F32), mid, hue, beat, (ask .&. 8191 : U32), (ask >> 13n .&. 8191 : U32)} : Viz.K}
  Viz.show(Viz.draw(U32.is_eq((ask >> 31n : U32), 1), k, a))

@unsafe
def Viz.loop(a: Array<U32>) -> IO(Unit):
  do IO<Unit>:
    ask : U32 <- Viz.next()
    t : F32 <- Viz.param(0)
    bass : F32 <- Viz.param(1)
    mid : F32 <- Viz.param(2)
    hue : F32 <- Viz.param(3)
    beat : F32 <- Viz.param(4)
    b : Array<U32> <- Viz.step(ask, t, bass, mid, hue, beat, a)
    Viz.loop(b)

@unsafe
def main() -> IO(Unit):
  Viz.loop([0 : U32^24n])
