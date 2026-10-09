# The Bend effect in gtube's visualizer: a plasma drawing every pixel of the visualizer (up to
# 2^23 pixels, as 3840x2160) into a flat buffer, rows packed at the frame's width, on the GPU or on the CPU's
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
# bits 13-25 (each at most 4096), and the effect in bits 26-29 (0 the plasma, 1 the tree, 2 the
# voxels, 3 the flames).
def Viz.next() -> IO(U32):
  import "./bridge.c"

# 0 time in seconds, 1 bass, 2 mids, 3 hue, 4 beat (1 on a beat, fading), 5 the seconds between
# the frames the player shows (averaged).
def Viz.param(i: U32) -> IO(F32):
  import "./bridge.c"

def Viz.show(a: Array<U32>) -> IO(Array<U32>):
  import "./bridge.c"

type Viz.K is Data:
  K{t: F32, zoom: F32, mid: F32, hue: F32, beat: F32, w: U32, h: U32, fx: U32}

# A colour from a palette position f, the classic cosine palette, brighter on the beat.
def Viz.color(+f: F32, +t: F32, +hue: F32, +beat: F32) -> U32:
  +v = F32.min(1.0, (0.6 + 0.25 * F32.sin((f * 12.566 + t : F32)) + beat * 0.25 : F32))
  +r = F32.to_u32((255.0 * v * (0.5 + 0.5 * F32.cos((6.2832 * (f * 0.6 + hue) : F32))) : F32))
  +g = F32.to_u32((255.0 * v * (0.5 + 0.5 * F32.cos((6.2832 * (f * 0.6 + hue + 0.33) : F32))) : F32))
  +b = F32.to_u32((255.0 * v * (0.5 + 0.5 * F32.cos((6.2832 * (f * 0.6 + hue + 0.67) : F32))) : F32))
  (r << 16n .|. g << 8n .|. b : U32)

# One pixel: four sine waves, zoomed by the bass, the last one rippling out with the mids.
def Viz.px(+x: U32, +y: U32, +k: Viz.K) -> U32:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  +unit = (200.0 / U32.to_f32(h) * zoom : F32)
  +dx = ((U32.to_f32(x) - U32.to_f32(w) * 0.5) * unit : F32)
  +dy = ((U32.to_f32(y) - U32.to_f32(h) * 0.5) * unit : F32)
  +w1 = F32.sin((dx + t : F32))
  +w2 = F32.sin((dy * 1.3 + t * 1.1 : F32))
  +w3 = F32.sin(((dx + dy) * 0.7 + t * 0.7 : F32))
  +w4 = F32.sin((F32.hypot(dx, dy) * 1.5 - t * 2.0 - mid * 3.0 : F32))
  Viz.color(((w1 + w2 + w3 + w4 + 4.0) * 0.125 + t * 0.15 : F32), t, hue, beat)

def Viz.put.at(+x: U32, +y: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
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
      K{t, zoom, mid, hue, beat, w, h, fx} = k
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
  K{t, zoom, mid, hue, beat, w, h, fx} = k
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

# Strided: 32,768 leaves (one per GPU lane: 128 groups of 256), leaf j drawing pixels j, j + 32768,
# ... so that neighbouring lanes write neighbouring pixels, as CUDA's threads do.
def Viz.lput(inside: Bool, +i: U32, +x: U32, +y: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match inside:
    case True{}:
      Array.set(U32, a, i, Viz.px(x, y, k))
    case False{}:
      a

# Pixel i is at column x and row y; the next, 32,768 on, is d columns and e rows further (d = 32768
# % w, e = 32768 / w), wrapping into the next row at most once, so no pixel divides.
def Viz.lin(n: Nat, +i: U32, +x: U32, +y: U32, +d: U32, +e: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+r:
      K{t, zoom, mid, hue, beat, w, h, fx} = k
      +c = Bool.to_u32((x + d >= w : U32))
      Viz.lin(r, (i + 32768 : U32), (x + d - c * w : U32), (y + e + c : U32), d, e, k, Viz.lput((i < w * h : U32), i, x, y, k, a))

# A leaf: pixels j, j + 32768, ... It never forks, so the runtime's growing passes leave it queued,
# and every lane starts its leaf together when the queues drain (a leaf run during growth would
# hold its whole group at the next step).
def Viz.leaf(+j: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  Viz.lin(U32.to_nat(((w * h + 32767) / 32768 : U32)), j, (j % w : U32), (j / w : U32), (32768 % w : U32), (32768 / w : U32), k, a)

# The whole frame on the GPU: a leaf on every lane. The runtime's growing passes split the spread
# across the lanes, each making its own leaf at once (one group splits it 256 ways, then each group
# its two parts 128 ways), and the leaves come out numbered so that neighbouring lanes draw
# neighbouring pixels.
@unsafe
def Viz.frame.gpu(+k: Viz.K, a: Array<U32>) -> Array<U32>:
  Array.spread(~U32, ~Viz.K, ~Viz.leaf, a, 32768, k)

# The tree: a trunk from the bottom of the frame, each branch splitting into two shorter ones,
# Viz.depth levels deep, their angles and lengths moved by the music. Every branch forks exactly
# two, so the work is balanced, the case Bend's scheduler is built for, and the same recursion runs
# on the CPU's threads or the GPU.

# Black first: a leaf clears pixels j, j + 32768, ... as the plasma's leaves draw them.
def Viz.zput(inside: Bool, +i: U32, a: Array<U32>) -> Array<U32>:
  match inside:
    case True{}:
      Array.set(U32, a, i, 0)
    case False{}:
      a

def Viz.zlin(n: Nat, +i: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+r:
      K{t, zoom, mid, hue, beat, w, h, fx} = k
      Viz.zlin(r, (i + 32768 : U32), k, Viz.zput((i < w * h : U32), i, a))

def Viz.zleaf(+j: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  Viz.zlin(U32.to_nat(((w * h + 32767) / 32768 : U32)), j, k, a)

# One pixel of a branch, if it lies in the frame (a coordinate one past, so that one left of or
# above the frame comes out 0).
def Viz.dot.at(inside: Bool, +x: U32, +y: U32, +c: U32, +w: U32, a: Array<U32>) -> Array<U32>:
  match inside:
    case True{}:
      Array.set(U32, a, (y * w + x : U32), c)
    case False{}:
      a

def Viz.dot(+x: F32, +y: F32, +c: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  +xi = F32.to_u32((x + 1.0 : F32))
  +yi = F32.to_u32((y + 1.0 : F32))
  Viz.dot.at(((xi >= 1) && (yi >= 1) && (xi <= w) && (yi <= h) : U32), (xi - 1 : U32), (yi - 1 : U32), c, w, a)

# A branch's line: n pixels from (x, y), each (sx, sy) on from the last.
def Viz.seg(n: Nat, +x: F32, +y: F32, +sx: F32, +sy: F32, +c: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+r:
      Viz.seg(r, (x + sx : F32), (y + sy : F32), sx, sy, c, k, Viz.dot(x, y, c, k, a))

# A branch d levels above the tips, from (x, y) at angle ang (radians, 0 pointing right, up
# positive), len pixels long: its line, then its two children at once.
@unsafe
def Viz.branch(+d: Nat, +x: F32, +y: F32, +ang: F32, +len: F32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  +dx = (len * F32.cos(ang) : F32)
  +dy = (0.0 - len * F32.sin(ang) : F32)
  +n = F32.to_u32((F32.max(F32.max(dx, (0.0 - dx : F32)), F32.max(dy, (0.0 - dy : F32))) + 1.0 : F32))
  +c = Viz.color((U32.to_f32(U32.from_nat(d)) * 0.06 + t * 0.05 : F32), t, hue, beat)
  Viz.branch.on(d, (x + dx : F32), (y + dy : F32), ang, len, k,
    Viz.seg(U32.to_nat(n), x, y, (dx / U32.to_f32(n) : F32), (dy / U32.to_f32(n) : F32), c, k, a))

@unsafe
def Viz.branch.on(+d: Nat, +x: F32, +y: F32, +ang: F32, +len: F32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match d:
    case 0n:
      a
    case 1n+e:
      Viz.branch2(e, x, y, ang, len, k, Array.fork(U32, a))

@unsafe
def Viz.branch2(+e: Nat, +x: F32, +y: F32, +ang: F32, +len: F32, +k: Viz.K, p: Array<U32> & Array<U32>) -> Array<U32>:
  (l, r) = p
  Viz.branch3(e, x, y, ang, len, k, l, r)

@unsafe
def Viz.branch3(+e: Nat, +x: F32, +y: F32, +ang: F32, +len: F32, +k: Viz.K, l: Array<U32>, r: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  +lvl = U32.to_f32(U32.from_nat(e))
  +spread = (0.3 + mid * 0.4 + beat * 0.15 + 0.1 * F32.sin((t * 1.3 + lvl * 0.7 : F32)) : F32)
  +sway = (0.08 * F32.sin((t * 0.9 + lvl * 0.4 : F32)) : F32)
  +len2 = (len * (0.7 + zoom * 1.2 : F32) : F32)
  q z = Viz.branch(e, x, y, (ang + spread + sway : F32), len2, k, l) Viz.branch(e, x, y, (ang - spread + sway : F32), len2, k, r)
  Array.join(U32, q, z)

# Levels of the tree: 2^17 - 1 branches.
def Viz.depth() -> Nat:
  16n

@unsafe
def Viz.tree(+k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  Viz.branch(Viz.depth(), (U32.to_f32(w) * 0.5 : F32), U32.to_f32(h), 1.5708, (U32.to_f32(h) * 0.24 : F32), k,
    Array.spread(~U32, ~Viz.K, ~Viz.zleaf, a, 32768, k))

def Viz.glow(+r: F32, +g: F32, +b: F32) -> U32:
  (F32.to_u32(F32.min(255.0, (r * 255.0 : F32))) << 16n .|. F32.to_u32(F32.min(255.0, (g * 255.0 : F32))) << 8n .|. F32.to_u32(F32.min(255.0, (b * 255.0 : F32))) : U32)

# The voxels: a flight over land made of voxels, drawn the way NovaLogic's Comanche (1992) drew its
# land, that the music reshapes column by column. The land is a heightmap of 1 by 1 columns, each
# one height and one colour, as Comanche's voxels were; the bass heaves its hills, the mids roll ripples
# across it, and each beat sends a ring outward from under the jet that lifts the columns it
# passes. Each pixel's ray runs out over the columns, its height rising in a straight line with
# distance (the projection sheared, not tilted, as in 1992), in steps that lengthen as it goes,
# then finds the column it met by halving the last step. The pixels are big, as 1992's were (the
# screen about 270 of them tall, at least 2 screen pixels square), ordered-dithered to VGA's 64
# levels a channel. Up close each column shows, lighter or darker than its neighbours and its sides
# darker than its top; from 40 to 100 away that fades, where columns grow too small for the pixels
# to draw cleanly and would beat against them.
# No pixel depends on another, and every one runs the same short loop.

# The land's height at (x, z), before it is cut into columns: warped hills and ridges, with valleys
# below the water's level (0); the bass heaves the hills, the mids roll ripples across them, and a
# beat's ring, centred under the jet at (ox, oz), widens as the beat fades.
def Viz.ground(+x: F32, +z: F32, +ox: F32, +oz: F32, +k: Viz.K) -> F32:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  +bass = F32.min(1.0, F32.max(0.0, ((zoom - 0.045) * 44.4 : F32)))
  +wx = (x + 22.0 * F32.sin((z * 0.017 : F32)) : F32)
  +wz = (z + 22.0 * F32.sin((x * 0.015 + 1.3 : F32)) : F32)
  +ridge = (1.0 - F32.abs(F32.sin((wx * 0.031 + wz * 0.019 + 0.7 : F32))) : F32)
  +hills = (5.0 + 10.0 * F32.sin((wx * 0.019 + wz * 0.013 : F32)) + 6.0 * F32.sin((wz * 0.031 - wx * 0.023 + 2.1 : F32))
    + 9.0 * ridge * ridge : F32)
  +heave = (bass * (5.0 + 4.0 * F32.sin((x * 0.05 + z * 0.04 + t * 0.8 : F32))) : F32)
  +ripple = (mid * 2.5 * F32.sin((x * 0.12 + z * 0.09 - t * 3.0 : F32)) : F32)
  +ex = (x - ox : F32)
  +ez = (z - oz : F32)
  +ring = (F32.sqrt((ex * ex + ez * ez : F32)) - (1.0 - beat) * 90.0 : F32)
  (hills + heave + ripple + beat * 5.0 * F32.exp((0.0 - ring * ring / 40.0 : F32)) : F32)

# The column at (x, z): the land's height at the column's middle.
def Viz.column(+x: F32, +z: F32, +ox: F32, +oz: F32, +k: Viz.K) -> F32:
  Viz.ground((F32.floor(x) + 0.5 : F32), (F32.floor(z) + 0.5 : F32), ox, oz, k)

# The ray from (ox, oy, oz) along (dx, dz) a unit of depth, rising slope a unit: at depth d, gap
# above the land (or the water) there. It returns the depth of the first step below it, or 100000
# for the sky. One def calling only itself, so it runs as a loop.
def Viz.scan(n: Nat, under: Bool, +d: F32, +gap: F32, +ox: F32, +oy: F32, +oz: F32, +dx: F32, +dz: F32, +slope: F32, +k: Viz.K) -> F32:
  match n under:
    case 0n False{}:
      100000.0
    case 0n True{}:
      d
    case 1n++m True{}:
      d
    case 1n++m False{}:
      +d2 = (d + 0.4 + d * 0.02 : F32)
      +gap2 = (oy + slope * d2 - F32.max(Viz.column((ox + dx * d2 : F32), (oz + dz * d2 : F32), ox, oz, k), 0.0) : F32)
      Viz.scan(m, F32.is_lt(gap2, 0.0), d2, gap2, ox, oy, oz, dx, dz, slope, k)

# Where between depths lo (above the land) and hi (below it) the ray meets the land, halving the
# gap n times, so a column's edge lands in the right place instead of between two steps. It
# returns the near side of the land (hi), so the point is in the column the ray met.
def Viz.refine(n: Nat, +lo: F32, +hi: F32, +ox: F32, +oy: F32, +oz: F32, +dx: F32, +dz: F32, +slope: F32, +k: Viz.K) -> F32:
  match n:
    case 0n:
      hi
    case 1n+r:
      +mid = ((lo + hi) * 0.5 : F32)
      +gap = (oy + slope * mid - F32.max(Viz.column((ox + dx * mid : F32), (oz + dz * mid : F32), ox, oz, k), 0.0) : F32)
      +below = F32.min(1.0, F32.max(0.0, ((0.0 - gap) * 1000000000.0 : F32)))
      Viz.refine(r, (lo + (mid - lo) * (1.0 - below) : F32), (hi - (hi - mid) * below : F32), ox, oy, oz, dx, dz, slope, k)

# One channel quantized to VGA's 64 levels, dithered by thr (0 to 1).
def Viz.vga(+c: F32, +thr: F32) -> F32:
  (F32.floor((F32.min(1.0, F32.max(0.0, c)) * 63.0 + thr : F32)) / 63.0 : F32)

# A 4x4 Bayer threshold for pixel block (bx, by): 0 to 15.
def Viz.bayer(+bx: U32, +by: U32) -> U32:
  +a = (bx .&. 1 : U32)
  +b = (by .&. 1 : U32)
  +c = (bx >> 1n .&. 1 : U32)
  +e = (by >> 1n .&. 1 : U32)
  (4 * (2 * ((a + b) % 2) + b) + 2 * ((c + e) % 2) + e : U32)

# One pixel of the voxels. The jet flies forward at 30 a second, weaving, banked into its turns,
# high enough to look out over the land and to clear what rises ahead; each beat jolts it up. What
# the ray meets is lit by a low sun, each column one colour by its height (sand, grass, forest,
# rock, snow) and a little lighter or darker than its neighbours, its top lit more than its sides;
# water is deep blue, paler in the shallows, mirroring the sky further off, swelling into waves
# with the bass and glittering with the mids. All of it hazes toward the horizon; past the land, the sky, pale and warm low down.
def Viz.vox(+x: U32, +y: U32, +k: Viz.K) -> U32:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  +blk = U32.max(2, (h / 270 : U32))
  +bx = (x / blk : U32)
  +by = (y / blk : U32)
  +u = ((U32.to_f32((bx * blk + blk / 2 : U32)) - U32.to_f32(w) * 0.5) / U32.to_f32(h) : F32)
  +v = ((U32.to_f32(h) * 0.5 - U32.to_f32((by * blk + blk / 2 : U32))) / U32.to_f32(h) : F32)
  +bass = F32.min(1.0, F32.max(0.0, ((zoom - 0.045) * 44.4 : F32)))
  +oz = (t * 30.0 : F32)
  +ox = (40.0 * F32.sin((t * 0.05 : F32)) + 15.0 * F32.sin((t * 0.13 : F32)) : F32)
  +yaw = F32.atan(((2.0 * F32.cos((t * 0.05 : F32)) + 1.95 * F32.cos((t * 0.13 : F32))) / 30.0 : F32))
  +bank = ((0.0 - 1.2) * (0.1 * F32.sin((t * 0.05 : F32)) + 0.2535 * F32.sin((t * 0.13 : F32))) : F32)
  +oy = (F32.max(40.0, (F32.max(Viz.ground(ox, oz, ox, oz, k), Viz.ground(ox, (oz + 25.0 : F32), ox, oz, k)) + 22.0 : F32)) + beat * 1.5 : F32)
  +cb = F32.cos(bank)
  +sb = F32.sin(bank)
  +ur = (u * cb + v * sb : F32)
  +vr = (v * cb - u * sb - 0.24 : F32)
  +fx = F32.sin(yaw)
  +fz = F32.cos(yaw)
  +dx = (fx + ur * 1.1 * fz : F32)
  +dz = (fz - ur * 1.1 * fx : F32)
  +slope = (vr * 1.1 : F32)
  +hit = Viz.scan(220n, False{}, 0.5, 1.0, ox, oy, oz, dx, dz, slope, k)
  +sky = F32.min(1.0, F32.max(0.0, ((hit - 99999.0) * 1000000000.0 : F32)))
  +d = (sky * hit + (1.0 - sky) * Viz.refine(7n, ((hit - 0.4) / 1.02 : F32), hit, ox, oy, oz, dx, dz, slope, k) : F32)
  +px = (ox + dx * d : F32)
  +pz = (oz + dz * d : F32)
  +py = (oy + slope * d : F32)
  +cx = (F32.floor(px) + 0.5 : F32)
  +cz = (F32.floor(pz) + 0.5 : F32)
  +raw = Viz.ground(cx, cz, ox, oz, k)
  +land = raw
  +wet = F32.min(1.0, F32.max(0.0, ((0.0 - land) * 1000000000.0 : F32)))
  +hx = (Viz.ground((cx + 0.6 : F32), cz, ox, oz, k) - Viz.ground((cx - 0.6 : F32), cz, ox, oz, k) : F32)
  +hz = (Viz.ground(cx, (cz + 0.6 : F32), ox, oz, k) - Viz.ground(cx, (cz - 0.6 : F32), ox, oz, k) : F32)
  +nl = F32.sqrt((hx * hx + 1.44 + hz * hz : F32))
  +sun = F32.max(0.0, (((0.0 - hx) * 0.55 + 1.2 * 0.62 + (0.0 - hz) * 0.56) / nl : F32))
  +detail = F32.min(1.0, F32.max(0.0, ((100.0 - d) / 60.0 : F32)))
  +face = F32.min(1.0, F32.max(0.0, (1.0 - (land - py) * 8.0 : F32)))
  +lit = ((0.32 + 0.85 * sun) * (1.0 - 0.38 * (1.0 - face) * detail) : F32)
  +jh = (F32.sin((cx * 12.9898 + cz * 78.233 : F32)) * 43758.5 : F32)
  +jit = (1.0 + 0.24 * (jh - F32.floor(jh) - 0.5) * detail : F32)
  +g1 = F32.min(1.0, F32.max(0.0, ((land - 1.5) / 1.5 : F32)))
  +g2 = F32.min(1.0, F32.max(0.0, ((land - 9.0) / 4.0 : F32)))
  +g3 = F32.min(1.0, F32.max(0.0, ((land - 16.0) / 4.0 : F32)))
  +g4 = F32.min(1.0, F32.max(0.0, ((land - 23.0) / 3.0 : F32)))
  +cr = ((((0.8 * (1.0 - g1) + 0.36 * g1) * (1.0 - g2) + 0.2 * g2) * (1.0 - g3) + 0.47 * g3) * (1.0 - g4) + 0.92 * g4 : F32)
  +cg = ((((0.72 * (1.0 - g1) + 0.56 * g1) * (1.0 - g2) + 0.4 * g2) * (1.0 - g3) + 0.4 * g3) * (1.0 - g4) + 0.93 * g4 : F32)
  +cbl = ((((0.5 * (1.0 - g1) + 0.22 * g1) * (1.0 - g2) + 0.16 * g2) * (1.0 - g3) + 0.32 * g3) * (1.0 - g4) + 0.95 * g4 : F32)
  +qx = (px + 2.5 * F32.sin((pz * 0.21 : F32)) : F32)
  +qz = (pz + 2.5 * F32.sin((px * 0.17 + 0.8 : F32)) : F32)
  +swell = (0.25 + 1.1 * bass : F32)
  +wave = (0.8 + swell * 0.12 * (F32.cos((qx * 0.35 + qz * 0.12 + t * 2.2 : F32)) + 0.8 * F32.cos((qz * 0.45 - qx * 0.2 - t * 2.9 : F32))
    + 0.5 * F32.cos((qx * 0.9 - qz * 0.7 + t * 4.1 : F32))) : F32)
  +ripple = (F32.sin((qx * 1.3 + qz * 0.4 + t * 4.0 : F32)) + F32.sin((qz * 1.9 - qx * 0.7 - t * 3.1 : F32))
    + F32.sin((qx * 2.9 - qz * 2.3 + t * 5.3 : F32)) : F32)
  +near = F32.min(1.0, F32.max(0.0, (1.0 - d / 120.0 : F32)))
  +glint = (F32.max(0.0, (ripple - 2.55 + 0.6 * bass : F32)) * (1.6 + mid * 3.0) * near : F32)
  +shallow = F32.min(1.0, F32.max(0.0, (1.0 + raw / 3.0 : F32)))
  +sheen = F32.min(0.5, (d / 300.0 : F32))
  +wr = (((0.06 + 0.3 * shallow) * wave * (1.0 - sheen) + 0.5 * sheen) + glint : F32)
  +wg = (((0.22 + 0.26 * shallow) * wave * (1.0 - sheen) + 0.62 * sheen) + glint : F32)
  +wb = (((0.34 + 0.08 * shallow) * wave * (1.0 - sheen) + 0.8 * sheen) + glint : F32)
  +gr = (wet * wr + (1.0 - wet) * cr * jit * lit : F32)
  +gg = (wet * wg + (1.0 - wet) * cg * jit * lit : F32)
  +gb = (wet * wb + (1.0 - wet) * cbl * jit * lit : F32)
  +haze = F32.min(1.0, F32.max(0.0, ((d - 120.0) / 480.0 : F32)))
  +up = F32.min(1.0, F32.max(0.0, ((vr + 0.25) * 2.2 : F32)))
  +sr = (0.93 - 0.68 * up : F32)
  +sg = (0.74 - 0.3 * up : F32)
  +sbl = (0.56 + 0.2 * up : F32)
  +r = (sky * sr + (1.0 - sky) * (gr * (1.0 - haze) + 0.9 * haze) : F32)
  +g = (sky * sg + (1.0 - sky) * (gg * (1.0 - haze) + 0.74 * haze) : F32)
  +b = (sky * sbl + (1.0 - sky) * (gb * (1.0 - haze) + 0.62 * haze) : F32)
  +thr = ((U32.to_f32(Viz.bayer(bx, by)) + 0.5) / 16.0 : F32)
  Viz.glow(Viz.vga(r, thr), Viz.vga(g, thr), Viz.vga(b, thr))

# The flames: a campfire, ray marched as a glowing volume, with the camera circling it. Each
# pixel's ray steps through the fire, and at each step its heat there (a bed of coals, a short
# mass of flame on them, and nine tongues that grow, sway and break into licks apart from each
# other) adds light coloured as a hot body glows, red through orange to a yellow-white core, and
# hides a share of what lies behind, so tongues further back show through; the ray stops once
# little would show. Sparks rise through it and a faint smoke above it. Behind lies the ground
# where the ray meets it, lit by the fire and fading into the dark with distance, with logs, ash
# under the coals and a ring of stones, or else the night sky. A ray is marched only where it
# crosses a cylinder around the fire. The firelight rises and falls with the tongues' heights; the
# bass stretches the inner tongues a little.

# How tall tongue i of the fire stands at time t: it grows and dies down over a lifetime of its own
# (1.1 to 2.4 s); the inner tongues stand tallest, and lift (from the bass) stretches them.
def Viz.tip(+t: F32, +i: F32, +lift: F32) -> F32:
  +hr = (i * 0.618034 + 0.31 - F32.floor((i * 0.618034 + 0.31 : F32)) : F32)
  +hp = (i * 0.381966 + 0.17 - F32.floor((i * 0.381966 + 0.17 : F32)) : F32)
  +ph = (t / (1.1 + 1.3 * hp) + hr : F32)
  +grow = (0.5 - 0.5 * F32.cos(((ph - F32.floor(ph)) * 6.2832 : F32)) : F32)
  ((0.5 + 1.0 * (1.0 - hr)) * (0.4 + 0.6 * grow) * (1.0 + lift * (1.0 - hr)) : F32)

# Tongue i of the fire at (x, y, z): its base sits on the coals at a spot of its own, it stands as
# tall as Viz.tip says, and it sways at its own pace, so the tongues
# change apart from each other. turb, the rising noise, eats its edges, most near the tip, where
# pieces break off, and gaps rising through its upper half split the top into licks: about twice
# a second, as a buoyant fire half a metre to a metre across puffs, faster as they rise. Its density is highest at the base; lift (from the bass) stretches the inner ones.
def Viz.tongue(+x: F32, +y: F32, +z: F32, +t: F32, +i: F32, +turb: F32, +lift: F32) -> F32:
  +ha = (i * 2.39996 : F32)
  +hr = (i * 0.618034 + 0.31 - F32.floor((i * 0.618034 + 0.31 : F32)) : F32)
  +hp = (i * 0.381966 + 0.17 - F32.floor((i * 0.381966 + 0.17 : F32)) : F32)
  +br = (0.04 + 0.3 * hr : F32)
  +bx = (F32.cos(ha) * br : F32)
  +bz = (F32.sin(ha) * br : F32)
  +tip = Viz.tip(t, i, lift)
  +q = F32.max(0.0, (1.0 - y / tip : F32))
  +sway = (y * y * 0.1 : F32)
  +ox = (bx * (1.0 - 0.5 * F32.min(1.0, y)) + sway * F32.sin((t * (0.8 + hp) + i * 1.7 + y * 0.6 : F32)) : F32)
  +oz = (bz * (1.0 - 0.5 * F32.min(1.0, y)) + sway * F32.cos((t * (0.7 + hr) + i * 2.9 + y * 0.5 : F32)) : F32)
  +ex = (x - ox : F32)
  +ez = (z - oz : F32)
  +wide = ((0.15 + 0.07 * hr) * F32.pow(q, 0.4) + 0.001 : F32)
  +body = (((wide - F32.sqrt((ex * ex + ez * ez : F32))) / wide * 1.4 + turb * (0.3 + 0.5 * (1.0 - q)) - 0.15) : F32)
  +gap = (F32.max(0.0, (0.0 - F32.sin((F32.sqrt(y) * 14.8 - t * 6.2832 * (1.8 + 0.4 * hp) + i * 1.3 : F32)) : F32)) * F32.min(1.0, ((1.0 - q) * 1.8 : F32)) : F32)
  (F32.min(1.0, F32.max(0.0, body)) * F32.min(1.0, (q * 40.0 : F32)) * (1.0 - gap) * (0.55 + 0.45 * q) : F32)

# The ray at (x, y, z), stepping (sx, sy, sz); r, g and b the light gathered so far, tr the share
# of what lies behind that still shows, and (br, bg, bb) what lies behind: the lit ground or the sky.
def Viz.burn(n: Nat, done: Bool, +x: F32, +y: F32, +z: F32, +sx: F32, +sy: F32, +sz: F32, +r: F32, +g: F32, +b: F32, +tr: F32,
             +br: F32, +bg: F32, +bb: F32, +k: Viz.K) -> U32:
  match n done:
    case 0n False{}:
      Viz.glow((r + tr * br : F32), (g + tr * bg : F32), (b + tr * bb : F32))
    case 0n True{}:
      Viz.glow((r + tr * br : F32), (g + tr * bg : F32), (b + tr * bb : F32))
    case 1n++m True{}:
      Viz.glow((r + tr * br : F32), (g + tr * bg : F32), (b + tr * bb : F32))
    case 1n++m False{}:
      K{t, zoom, mid, hue, beat, w, h, fx} = k
      +bass = F32.min(1.0, ((zoom - 0.045) * 44.4 : F32))
      +tall = (1.1 * (1.0 + bass * 0.08) : F32)
      +up = (y * 2.4 + y * y * 1.6 - t * 1.1 : F32)
      +n1 = (F32.sin((x * 7.0 + up : F32)) * F32.sin((z * 6.5 - up * 0.7 + 1.7 : F32)) : F32)
      +n2 = (F32.sin((x * 12.0 - z * 8.0 + up * 1.8 : F32)) * F32.cos((z * 11.0 + x * 4.0 - up * 1.3 : F32)) : F32)
      +n3 = F32.sin((x * 15.0 + z * 13.0 + up * 2.9 : F32))
      +turb = (n1 * 0.5 + n2 * 0.33 + n3 * 0.17 : F32)
      +far = F32.sqrt((x * x + z * z : F32))
      +ground = F32.min(1.0, F32.max(0.0, ((y + 0.02) * 9.0 : F32)))
      +lift = (bass * 0.25 : F32)
      +f0 = Viz.tongue(x, y, z, t, 0.0, turb, lift)
      +f1 = Viz.tongue(x, y, z, t, 1.0, turb, lift)
      +f2 = Viz.tongue(x, y, z, t, 2.0, turb, lift)
      +f3 = Viz.tongue(x, y, z, t, 3.0, turb, lift)
      +f4 = Viz.tongue(x, y, z, t, 4.0, turb, lift)
      +f5 = Viz.tongue(x, y, z, t, 5.0, turb, lift)
      +f6 = Viz.tongue(x, y, z, t, 6.0, turb, lift)
      +f7 = Viz.tongue(x, y, z, t, 7.0, turb, lift)
      +f8 = Viz.tongue(x, y, z, t, 8.0, turb, lift)
      +hb = (0.45 * (1.0 + bass * 0.1) : F32)
      +qb = F32.max(0.0, (1.0 - y / hb : F32))
      +rb = (0.4 * F32.sqrt(qb) + 0.001 : F32)
      +mass = (F32.min(1.0, F32.max(0.0, ((rb - far) / rb * 1.2 + turb * 0.4 - 0.1 : F32))) * F32.min(1.0, (qb * 40.0 : F32)) * (0.5 + 0.5 * qb) : F32)
      +flame = (ground * F32.max(mass, F32.max(F32.max(F32.max(f0, f1), F32.max(f2, f3)), F32.max(F32.max(f4, f5), F32.max(f6, F32.max(f7, f8))))) : F32)
      +coal = (F32.min(1.0, F32.max(0.0, ((0.8 - F32.sqrt((x * x + z * z : F32))) * 3.0 : F32)))
        * F32.min(1.0, F32.max(0.0, ((0.12 - y) * 12.0 : F32))) * ground
        * (0.62 + 0.22 * F32.sin((x * 17.0 + t * 1.3 : F32)) * F32.sin((z * 19.0 - t * 1.1 : F32))) : F32)
      +heat = F32.max(flame, coal)
      +shimmer = (0.9 + 0.1 * F32.sin((t * 9.0 + x * 6.0 + z * 5.0 + y * 4.0 : F32)) : F32)
      +edge = F32.min(1.0, F32.max(0.0, ((heat - 0.22) * 5.0 : F32)))
      +a = F32.min(1.0, (edge * heat * 0.6 : F32))
      # Heat 0 to 1 is 1000 to 2000 K. The colour is a blackbody's at that temperature, seen by
      # an eye adapted to the hottest (2000 K) gas, so that reads white and cooler gas orange and
      # red; it dims steeply as it cools (a compressed Planck falloff).
      +glow = (shimmer * 3.0 * F32.exp((2.5 * (heat - 1.0) : F32)) : F32)
      +er = glow
      +eg = (glow * (0.158 + 0.842 * heat) : F32)
      +eb = (glow * F32.min(1.0, F32.max(0.0, ((heat - 0.9) * 10.0 : F32))) : F32)
      +cu = (x * 6.0 : F32)
      +cv = ((y - t * 0.45) * 6.0 : F32)
      +cw = (z * 6.0 : F32)
      +iu = F32.floor(cu)
      +iv = F32.floor(cv)
      +iw = F32.floor(cw)
      +hs = (F32.sin((iu * 127.1 + iv * 311.7 + iw * 74.7 : F32)) * 43758.5 : F32)
      +hc = (hs - F32.floor(hs) : F32)
      +h2 = (hc * 13.7 - F32.floor((hc * 13.7 : F32)) : F32)
      +h3 = (hc * 71.3 - F32.floor((hc * 71.3 : F32)) : F32)
      +fu = (cu - iu - 0.25 - 0.5 * h2 : F32)
      +fv = ((cv - iv - 0.5 : F32) * 0.35 : F32)
      +fw = (cw - iw - 0.25 - 0.5 * h3 : F32)
      +dot = F32.max(0.0, (1.0 - (fu * fu + fv * fv + fw * fw) * 45.0 : F32))
      +life = (F32.min(1.0, F32.max(0.0, ((y - 0.15) * 3.0 : F32))) * F32.min(1.0, F32.max(0.0, (1.0 - y / 2.3 : F32)))
        * F32.min(1.0, F32.max(0.0, ((0.75 - far) * 2.5 : F32))) : F32)
      +alive = F32.min(1.0, F32.max(0.0, ((hc - 0.955) * 1000000000.0 : F32)))
      +spark = (tr * alive * dot * life * (0.6 + 0.4 * F32.sin((t * 23.0 + hc * 60.0 : F32))) * 3.0 : F32)
      +sp = ((y - t * 0.3) * 4.0 : F32)
      +wisp = (0.5 + 0.5 * F32.sin((x * 5.0 + sp : F32)) * F32.sin((z * 5.0 - sp * 0.8 + 1.0 : F32)) : F32)
      +smoke = (F32.min(1.0, F32.max(0.0, ((y - tall * 0.75) * 1.5 : F32))) * F32.min(1.0, F32.max(0.0, (2.5 - y : F32)))
        * F32.min(1.0, F32.max(0.0, ((0.3 + 0.2 * y - far) * 4.0 : F32))) * wisp : F32)
      +tr2 = (tr * (1.0 - a * 0.55) * (1.0 - smoke * 0.08) : F32)
      +lit = (tr * smoke * 0.006 * F32.max(0.0, (2.6 - y : F32)) : F32)
      +near = F32.max(0.0, (1.0 - (far * far + (y - 0.4) * (y - 0.4) * 0.5) / 1.44 : F32))
      +haze = (tr * 0.012 * shimmer * near * near : F32)
      Viz.burn(m, F32.is_lt(tr2, 0.02), (x + sx : F32), (y + sy : F32), (z + sz : F32), sx, sy, sz,
        (r + tr * a * er + haze + spark + lit : F32), (g + tr * a * eg + haze * 0.45 + spark * 0.55 + lit * 0.7 : F32),
        (b + tr * a * eb + haze * 0.15 + spark * 0.15 + lit * 0.55 : F32), tr2,
        br, bg, bb, k)

# 1 where x > 0, else 0, with no branch.
def Viz.pos(+x: F32) -> F32:
  F32.min(1.0, F32.max(0.0, (x * 1000000000.0 : F32)))

# The distance along the ray from (ox, oy, oz) in direction (dx, dy, dz) to a log: a cylinder of
# radius 0.085 lying on the ground from (ax, az) to (bx, bz), or 1000 if the ray misses it.
def Viz.log(+ox: F32, +oy: F32, +oz: F32, +dx: F32, +dy: F32, +dz: F32, +ax: F32, +az: F32, +bx: F32, +bz: F32) -> F32:
  +bax = (bx - ax : F32)
  +baz = (bz - az : F32)
  +oax = (ox - ax : F32)
  +oay = (oy - 0.085 : F32)
  +oaz = (oz - az : F32)
  +baba = (bax * bax + baz * baz : F32)
  +bard = (bax * dx + baz * dz : F32)
  +baoa = (bax * oax + baz * oaz : F32)
  +rdoa = (dx * oax + dy * oay + dz * oaz : F32)
  +oaoa = (oax * oax + oay * oay + oaz * oaz : F32)
  +qa = F32.max(0.000001, (baba - bard * bard : F32))
  +qb = (baba * rdoa - baoa * bard : F32)
  +qc = (baba * oaoa - baoa * baoa - 0.007225 * baba : F32)
  +hh = (qb * qb - qa * qc : F32)
  +tt = (((0.0 - qb) - F32.sqrt(F32.max(0.0, hh))) / qa : F32)
  +yy = (baoa + tt * bard : F32)
  +ok = (Viz.pos(hh) * Viz.pos(yy) * Viz.pos((baba - yy : F32)) * Viz.pos(tt) : F32)
  (ok * tt + (1.0 - ok) * 1000.0 : F32)

# The nearest of five logs lying in a star, their inner ends in the fire: a loop, so the GPU
# program holds the log's test once.
def Viz.logs(n: Nat, +i: F32, +ox: F32, +oy: F32, +oz: F32, +dx: F32, +dy: F32, +dz: F32, +best: F32) -> F32:
  match n:
    case 0n:
      best
    case 1n+r:
      +a = (0.3 + i * 1.2566 : F32)
      +c = F32.cos(a)
      +s = F32.sin(a)
      Viz.logs(r, (i + 1.0 : F32), ox, oy, oz, dx, dy, dz,
        F32.min(best, Viz.log(ox, oy, oz, dx, dy, dz, (c * 0.08 : F32), (s * 0.08 : F32), (c * 0.95 : F32), (s * 0.95 : F32))))

# The nearest stone a ray hits: its distance, or 1000, and its shade there.
type Viz.S is Data:
  S{t: F32, shade: F32}

def Viz.S.t(s: Viz.S) -> F32:
  match s:
    case S{t, shade}:
      t

def Viz.S.shade(s: Viz.S) -> F32:
  match s:
    case S{t, shade}:
      shade

# Twelve stones in a ring around the fire, each a sphere half sunk in the ground, of a size and a
# place a little different from its neighbours'; lit by the fire on the side facing it. A loop of
# arithmetic only, keeping the nearest hit.
def Viz.stones(n: Nat, +i: F32, +ox: F32, +oy: F32, +oz: F32, +dx: F32, +dy: F32, +dz: F32, +flick: F32, +best: F32, +shade: F32) -> Viz.S:
  match n:
    case 0n:
      S{best, shade}
    case 1n+r:
      +a = (i * 0.5236 + 0.18 * F32.sin((i * 7.1 : F32)) : F32)
      +rad = (0.11 + 0.05 * F32.sin((i * 3.7 + 1.0 : F32)) * F32.sin((i * 3.7 + 1.0 : F32)) : F32)
      +ring = (1.0 + 0.06 * F32.sin((i * 5.3 : F32)) : F32)
      +cx = (F32.cos(a) * ring : F32)
      +cy = (rad * 0.35 : F32)
      +cz = (F32.sin(a) * ring : F32)
      +px = (ox - cx : F32)
      +py = (oy - cy : F32)
      +pz = (oz - cz : F32)
      +b = (px * dx + py * dy + pz * dz : F32)
      +hh = (b * b - (px * px + py * py + pz * pz - rad * rad) : F32)
      +tt = ((0.0 - b) - F32.sqrt(F32.max(0.0, hh)) : F32)
      +near = (Viz.pos(hh) * Viz.pos(tt) * Viz.pos((best - tt : F32)) : F32)
      +hx = (ox + dx * tt : F32)
      +hy = (oy + dy * tt : F32)
      +hz = (oz + dz * tt : F32)
      +nx = ((hx - cx) / rad : F32)
      +ny = ((hy - cy) / rad : F32)
      +nz = ((hz - cz) / rad : F32)
      +lx = (0.0 - hx : F32)
      +ly = (0.35 - hy : F32)
      +lz = (0.0 - hz : F32)
      +ll = F32.sqrt((lx * lx + ly * ly + lz * lz : F32))
      +lam = F32.max(0.0, ((nx * lx + ny * ly + nz * lz) / ll : F32))
      +lit = ((lam * 1.4 * flick / (1.0 + (hx * hx + hz * hz) * 2.0) + 0.015) * (0.38 + 0.08 * F32.sin((i * 9.1 : F32))) : F32)
      Viz.stones(r, (i + 1.0 : F32), ox, oy, oz, dx, dy, dz, flick, (near * tt + (1.0 - near) * best : F32), (near * lit + (1.0 - near) * shade : F32))

# One pixel of the flames: the camera circles the fire about every 80 s, a little above its coals
# and looking slightly down. The ray is marched only across the cylinder of radius 1.25 and height
# 2.6 around the fire, in steps of 0.05; behind it lies the ground or the sky.
def Viz.flame(+x: U32, +y: U32, +k: Viz.K) -> U32:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  +u = ((U32.to_f32(x) - U32.to_f32(w) * 0.5) / U32.to_f32(h) : F32)
  +v = ((U32.to_f32(h) * 0.5 - U32.to_f32(y)) / U32.to_f32(h) : F32)
  +ang = (t * 0.08 : F32)
  +sa = F32.sin(ang)
  +ca = F32.cos(ang)
  +fy = (v - 0.2 : F32)
  +rx = (u * ca - 1.15 * sa : F32)
  +rz = (u * sa + 1.15 * ca : F32)
  +len = F32.sqrt((rx * rx + fy * fy + rz * rz : F32))
  +dx = (rx / len : F32)
  +dy = (fy / len : F32)
  +dz = (rz / len : F32)
  +cx = (2.7 * sa : F32)
  +cy = (1.0 + 0.05 : F32)
  +cz = (0.0 - 2.7 * ca : F32)
  # Where the ray meets the ground (y = 0), if it points down: the firelight there falls off with
  # distance from the coals and flickers on its own; ash darkens the ground under the coals, and
  # stones ring the fire, a little paler.
  +down = F32.min(1.0, F32.max(0.0, ((0.0 - dy) * 1000000000.0 : F32)))
  +reach = (cy / F32.max(0.0001, (0.0 - dy : F32)) : F32)
  +gx = (cx + dx * reach : F32)
  +gz = (cz + dz * reach : F32)
  +gd = (gx * gx + gz * gz : F32)
  # The firelight rises and falls with the fire: the tongues' heights against their mean (6.3).
  +lift = (F32.min(1.0, ((zoom - 0.045) * 44.4 : F32)) * 0.25 : F32)
  +flick = ((Viz.tip(t, 0.0, lift) + Viz.tip(t, 1.0, lift) + Viz.tip(t, 2.0, lift) + Viz.tip(t, 3.0, lift) + Viz.tip(t, 4.0, lift)
    + Viz.tip(t, 5.0, lift) + Viz.tip(t, 6.0, lift) + Viz.tip(t, 7.0, lift) + Viz.tip(t, 8.0, lift)) / 6.3 : F32)
  +light = (1.2 * flick / (1.0 + gd * 3.0) : F32)
  +ash = (0.45 + 0.55 * F32.min(1.0, F32.max(0.0, ((F32.sqrt(gd) - 0.45) * 1.4 : F32))) : F32)
  +grain = (0.85 + 0.15 * F32.sin((gx * 23.0 + gz * 7.0 : F32)) * F32.sin((gz * 19.0 - gx * 5.0 : F32)) : F32)
  +alb = (0.3 * grain * ash : F32)
  # Logs and stones: the nearest of each the ray meets. A log is charred bark lit by the fire, its
  # inner end glowing in cracks; a stone is lit on the side facing the fire.
  +tl = Viz.logs(5n, 0.0, cx, cy, cz, dx, dy, dz, 1000.0)
  +hit2 = Viz.stones(12n, 0.0, cx, cy, cz, dx, dy, dz, flick, 1000.0, 0.0)
  +ts = Viz.S.t(hit2)
  +stone = Viz.S.shade(hit2)
  +lx = (cx + dx * tl : F32)
  +lz = (cz + dz * tl : F32)
  +ld = F32.sqrt((lx * lx + lz * lz : F32))
  +ember = (F32.min(1.0, F32.max(0.0, ((0.5 - ld) * 2.5 : F32))) * F32.max(0.0, (0.55 + 0.45 * F32.sin((lx * 31.0 + t * 0.4 : F32)) * F32.sin((lz * 29.0 - t * 0.3 : F32)) : F32)) : F32)
  +bark = (0.06 * (0.8 + 0.2 * F32.sin((lx * 47.0 + lz * 41.0 : F32))) * 1.3 * flick / (1.0 + ld * ld * 3.0) : F32)
  +lr = (bark + ember * 1.3 : F32)
  +lg = (bark * 0.6 + ember * 0.38 : F32)
  +lb = (bark * 0.4 + ember * 0.06 : F32)
  # What lies nearest behind the fire: a log, a stone, the ground or the sky.
  +tg = (down * reach + (1.0 - down) * 1000.0 : F32)
  +ilog = (Viz.pos((ts - tl : F32)) * Viz.pos((tg - tl : F32)) * Viz.pos((999.0 - tl : F32)) : F32)
  +istone = ((1.0 - ilog) * Viz.pos((tg - ts : F32)) * Viz.pos((999.0 - ts : F32)) : F32)
  +rest = (1.0 - ilog - istone : F32)
  +tsolid = F32.min(F32.min(tl, ts), tg)
  # The cylinder around the fire: where the ray enters and leaves it (none, if it passes by).
  +qa = F32.max(0.000001, (dx * dx + dz * dz : F32))
  +qb = (cx * dx + cz * dz : F32)
  +disc = (qb * qb - qa * (cx * cx + cz * cz - 1.5625) : F32)
  +hit = F32.min(1.0, F32.max(0.0, (disc * 1000000000.0 : F32)))
  +root = F32.sqrt(F32.max(0.0, disc))
  # Each pixel starts its march a fraction of a step further in, so thin flame edges grain
  # instead of banding between steps.
  +jh = (F32.sin((u * 12.9898 + v * 78.233 : F32)) * 43758.5 : F32)
  +t0 = (F32.max(0.0, (((0.0 - qb) - root) / qa : F32)) + 0.025 * (jh - F32.floor(jh)) : F32)
  +roof = ((2.6 - cy) / F32.max(0.0001, dy) : F32)
  +t1 = F32.min(F32.min(F32.min((((0.0 - qb) + root) / qa : F32), (down * reach + (1.0 - down) * roof : F32)), (t0 + 3.2 : F32)), tsolid)
  +steps = F32.to_u32((hit * F32.max(0.0, ((t1 - t0) / 0.05 : F32)) : F32))
  Viz.burn(U32.to_nat(steps), False{}, (cx + dx * t0 : F32), (cy + dy * t0 : F32), (cz + dz * t0 : F32),
    (dx * 0.05 : F32), (dy * 0.05 : F32), (dz * 0.05 : F32), 0.0, 0.0, 0.0, 1.0,
    (ilog * lr + istone * stone * 1.0 + rest * (down * alb * light + (1.0 - down) * 0.005) : F32),
    (ilog * lg + istone * stone * 0.62 + rest * (down * alb * light * 0.55 + (1.0 - down) * 0.008) : F32),
    (ilog * lb + istone * stone * 0.4 + rest * (down * alb * light * 0.28 + (1.0 - down) * 0.015) : F32), k)

def Viz.traced.at(flames: Bool, +x: U32, +y: U32, +k: Viz.K) -> U32:
  match flames:
    case True{}:
      Viz.flame(x, y, k)
    case False{}:
      Viz.vox(x, y, k)

# A pixel of the ray-traced effects: the flames, or the voxels.
def Viz.traced(+x: U32, +y: U32, +k: Viz.K) -> U32:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  Viz.traced.at(U32.is_eq(fx, 3), x, y, k)

# The ray-traced effects' frames, strided over the lanes as the plasma's is.
def Viz.vput(inside: Bool, +i: U32, +x: U32, +y: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match inside:
    case True{}:
      Array.set(U32, a, i, Viz.traced(x, y, k))
    case False{}:
      a

def Viz.vlin(n: Nat, +i: U32, +x: U32, +y: U32, +d: U32, +e: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+r:
      K{t, zoom, mid, hue, beat, w, h, fx} = k
      +c = Bool.to_u32((x + d >= w : U32))
      Viz.vlin(r, (i + 32768 : U32), (x + d - c * w : U32), (y + e + c : U32), d, e, k, Viz.vput((i < w * h : U32), i, x, y, k, a))

def Viz.vleaf(+j: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h, fx} = k
  Viz.vlin(U32.to_nat(((w * h + 32767) / 32768 : U32)), j, (j % w : U32), (j / w : U32), (32768 % w : U32), (32768 / w : U32), k, a)

@unsafe
def Viz.rays(+k: Viz.K, a: Array<U32>) -> Array<U32>:
  Array.spread(~U32, ~Viz.K, ~Viz.vleaf, a, 32768, k)

# One effect's frame: the tree, a ray-traced one (the voxels or the flames), or the plasma (on the
# CPU in tiles, on the GPU strided).
@unsafe
def Viz.pick(+fx: U32, gpu: Bool, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  Viz.pick.tree(U32.is_eq(fx, 1), fx, gpu, k, a)

@unsafe
def Viz.pick.tree(tree: Bool, +fx: U32, gpu: Bool, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match tree:
    case True{}:
      Viz.tree(k, a)
    case False{}:
      Viz.pick.rays((fx >= 2 : U32), gpu, k, a)

@unsafe
def Viz.pick.rays(rays: Bool, gpu: Bool, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match rays:
    case True{}:
      Viz.rays(k, a)
    case False{}:
      Viz.pick.plasma(gpu, k, a)

@unsafe
def Viz.pick.plasma(gpu: Bool, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match gpu:
    case True{}:
      Viz.frame.gpu(k, a)
    case False{}:
      Viz.frame(k, a)

# Two frames in one launch: the GPU's growing and packing passes, and the round trip to it, are
# paid once for both.
@unsafe
def Viz.frames.gpu(+fx: U32, +k: Viz.K, +k2: Viz.K, a: Array<U32>, b: Array<U32>) -> Array<U32> & Array<U32>:
  x y = Viz.pick(fx, True{}, k, a) Viz.pick(fx, True{}, k2, b)
  (x, y)

# The same pixels, on the GPU or on the CPU's threads.
@unsafe
def Viz.draw(gpu: Bool, +fx: U32, +k: Viz.K, +k2: Viz.K, a: Array<U32>, b: Array<U32>) -> Array<U32> & Array<U32>:
  match gpu:
    case True{}:
      Viz.frames.gpu!(fx, k, k2, a, b)
    case False{}:
      (Viz.pick(fx, False{}, k, a), Viz.pick(fx, False{}, k2, b))

# The frame asked for and the one after it, dt seconds on (a value bound in a do block is used once,
# so the request is taken apart here), shown in turn.
@unsafe
def Viz.step(+ask: U32, +t: F32, bass: F32, +mid: F32, +hue: F32, +beat: F32, dt: F32, a: Array<U32>, b: Array<U32>) -> IO(Array<U32> & Array<U32>):
  +z = (0.045 * (1.0 + bass * 0.5) : F32)
  +w = (ask .&. 8191 : U32)
  +h = (ask >> 13n .&. 8191 : U32)
  +fx = (ask >> 26n .&. 15 : U32)
  +k = {K{t, z, mid, hue, beat, w, h, fx} : Viz.K}
  +k2 = {K{(t + dt : F32), z, mid, hue, beat, w, h, fx} : Viz.K}
  Viz.show2(Viz.draw(U32.is_eq((ask >> 31n : U32), 1), fx, k, k2, a, b))

@unsafe
def Viz.show2(p: Array<U32> & Array<U32>) -> IO(Array<U32> & Array<U32>):
  (x, y) = p
  IO.bind(Array<U32>, Array<U32> & Array<U32>, Viz.show(x), x2 => IO.bind(Array<U32>, Array<U32> & Array<U32>, Viz.show(y), y2 => IO.pure(Array<U32> & Array<U32>, (x2, y2))))

@unsafe
# Six frame buffers, drawn into two at a time, in turn: the player draws the last two frames from
# two while the next two are drawn into two others, and the third pair frees up meanwhile, so no
# frame is copied aside for it and neither waits for the other (bridge.c keeps Bend from drawing
# into a buffer still on screen).
def Viz.loop(a: Array<U32>, b: Array<U32>, c: Array<U32>, d: Array<U32>, e: Array<U32>, f: Array<U32>) -> IO(Unit):
  do IO<Unit>:
    ask : U32 <- Viz.next()
    t : F32 <- Viz.param(0)
    bass : F32 <- Viz.param(1)
    mid : F32 <- Viz.param(2)
    hue : F32 <- Viz.param(3)
    beat : F32 <- Viz.param(4)
    dt : F32 <- Viz.param(5)
    p : Array<U32> & Array<U32> <- Viz.step(ask, t, bass, mid, hue, beat, dt, a, b)
    Viz.loop.at(c, d, e, f, p)

@unsafe
def Viz.loop.at(c: Array<U32>, d: Array<U32>, e: Array<U32>, f: Array<U32>, p: Array<U32> & Array<U32>) -> IO(Unit):
  (x, y) = p
  Viz.loop(c, d, e, f, x, y)

# A frame buffer of 2^23 pixels (32 MB; 3840x2160 fits, and bridge.c scales down a bigger frame),
# made on the GPU when there is one, so the host never writes it, which would then have to cross to
# the GPU.
def Viz.blank(z: U32) -> Array<U32>:
  [z : U32^23n]

@unsafe
def main() -> IO(Unit):
  Viz.loop(Viz.blank!(0), Viz.blank!(0), Viz.blank!(0), Viz.blank!(0), Viz.blank!(0), Viz.blank!(0))
