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
# voxels).
def Viz.next() -> IO(U32):
  import "./bridge.c"

# 0 time in seconds, 1 bass, 2 mids, 3 hue, 4 beat (1 on a beat, fading), 5 the seconds between
# the frames the player shows (averaged).
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
      K{t, zoom, mid, hue, beat, w, h} = k
      +c = Bool.to_u32((x + d >= w : U32))
      Viz.lin(r, (i + 32768 : U32), (x + d - c * w : U32), (y + e + c : U32), d, e, k, Viz.lput((i < w * h : U32), i, x, y, k, a))

# A leaf: pixels j, j + 32768, ... It never forks, so the runtime's growing passes leave it queued,
# and every lane starts its leaf together when the queues drain (a leaf run during growth would
# hold its whole group at the next step).
def Viz.leaf(+j: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h} = k
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
      K{t, zoom, mid, hue, beat, w, h} = k
      Viz.zlin(r, (i + 32768 : U32), k, Viz.zput((i < w * h : U32), i, a))

def Viz.zleaf(+j: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h} = k
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
  K{t, zoom, mid, hue, beat, w, h} = k
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
  K{t, zoom, mid, hue, beat, w, h} = k
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
  K{t, zoom, mid, hue, beat, w, h} = k
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
  K{t, zoom, mid, hue, beat, w, h} = k
  Viz.branch(Viz.depth(), (U32.to_f32(w) * 0.5 : F32), U32.to_f32(h), 1.5708, (U32.to_f32(h) * 0.24 : F32), k,
    Array.spread(~U32, ~Viz.K, ~Viz.zleaf, a, 32768, k))

# The voxels: a flight over land made of blocks, ray traced. Each pixel's ray steps from cell to
# cell of the grid (the cell it is in, then whichever cell boundary it crosses next) until it
# enters a solid block, up to Viz.reach() cells: no pixel depends on another, and every one runs
# the same short loop, the shape the GPU's lanes run best. A block is solid below the land's height
# at its column, waves the bass lifts and the mids ripple; it is coloured by its height and lit by
# the face the ray entered, its edges drawn dark, and fades into the sky with distance.

def Viz.reach() -> Nat:
  120n

# The land's height at column (X, Z), in whole blocks.
def Viz.land(+X: F32, +Z: F32, +k: Viz.K) -> F32:
  K{t, zoom, mid, hue, beat, w, h} = k
  +a = F32.sin((X * 0.19 + t * 0.35 : F32))
  +b = F32.cos((Z * 0.13 - t * 0.15 : F32))
  +c = F32.sin(((X + Z) * 0.09 + mid * 3.0 : F32))
  +lift = ((zoom - 0.045) * 100.0 : F32)
  F32.floor((6.0 + 2.5 * a + 2.5 * b + 2.0 * c + lift : F32))

def Viz.sgn(neg: Bool) -> F32:
  match neg:
    case True{}:
      (0.0 - 1.0 : F32)
    case False{}:
      1.0

def Viz.abs(+x: F32) -> F32:
  F32.max(x, (0.0 - x : F32))

# A colour's channels scaled by s, then mixed toward the sky colour c2 by f.
def Viz.mix(+c: U32, +s: F32, +c2: U32, +f: F32) -> U32:
  +r = (U32.to_f32((c >> 16n .&. 255 : U32)) * s * (1.0 - f) + U32.to_f32((c2 >> 16n .&. 255 : U32)) * f : F32)
  +g = (U32.to_f32((c >> 8n .&. 255 : U32)) * s * (1.0 - f) + U32.to_f32((c2 >> 8n .&. 255 : U32)) * f : F32)
  +b = (U32.to_f32((c .&. 255 : U32)) * s * (1.0 - f) + U32.to_f32((c2 .&. 255 : U32)) * f : F32)
  (F32.to_u32(F32.min(255.0, r)) << 16n .|. F32.to_u32(F32.min(255.0, g)) << 8n .|. F32.to_u32(F32.min(255.0, b)) : U32)

# The sky toward the ray's height dy: the palette, darkened toward the zenith.
def Viz.sky(+dy: F32, +k: Viz.K) -> U32:
  K{t, zoom, mid, hue, beat, w, h} = k
  Viz.mix(Viz.color((0.55 + dy * 0.3 : F32), t, hue, (beat * 0.3 : F32)), F32.clamp((0.75 - dy * 0.6 : F32), 0.15, 1.0), 0, 0.0)

# How far a point at p is from the nearest whole number: the closer to a block's edge, the smaller.
def Viz.edge(+p: F32) -> F32:
  +f = (p - F32.floor(p) : F32)
  F32.min(f, (1.0 - f : F32))

def Viz.vpick(a: Bool, +x: F32, +y: F32) -> F32:
  match a:
    case True{}:
      x
    case False{}:
      y

def Viz.vlight.side(side: Bool) -> F32:
  match side:
    case True{}:
      0.72
    case False{}:
      0.5

# Light by face: tops brightest, and on the beat, sides facing x half lit, those facing z darker.
def Viz.vlight(top: Bool, side: Bool, +flash: F32) -> F32:
  match top:
    case True{}:
      (1.0 + flash : F32)
    case False{}:
      Viz.vlight.side(side)

# A hit: the block at height Y, entered through face 0 (a side facing x), 1 (the top or bottom)
# or 2 (a side facing z) at distance d along the ray from (ox, oy, oz) in direction (dx, dy, dz).
def Viz.vshade(+face: U32, +Y: F32, +d: F32, +ox: F32, +oy: F32, +oz: F32, +dx: F32, +dy: F32, +dz: F32, +k: Viz.K) -> U32:
  K{t, zoom, mid, hue, beat, w, h} = k
  +px = (ox + dx * d : F32)
  +py = (oy + dy * d : F32)
  +pz = (oz + dz * d : F32)
  +e1 = Viz.edge(Viz.vpick(U32.is_eq(face, 0), pz, px))
  +e2 = Viz.edge(Viz.vpick(U32.is_eq(face, 1), pz, py))
  +line = F32.clamp((F32.min(e1, e2) * 14.0 : F32), 0.35, 1.0)
  +lit = Viz.vlight(U32.is_eq(face, 1), U32.is_eq(face, 0), (beat * 0.35 : F32))
  +fog = F32.clamp(((d - 18.0) / 70.0 : F32), 0.0, 1.0)
  Viz.mix(Viz.color((Y * 0.07 + t * 0.03 : F32), t, hue, beat), (lit * line : F32), Viz.sky(0.0, k), fog)

# 1 where a < b, else 0, with no branch. (With the land's height and Bool helpers called from the
# march's body instead, the GPU ran out of its lanes' stacks at 120 steps, though not at 24.)
def Viz.below(+a: F32, +b: F32) -> F32:
  F32.min(1.0, F32.max(0.0, ((b - a) * 1000000000.0 : F32)))

# The ray in cell (X, Y, Z), solid or not: mx, my and mz are the distances along it to the next
# boundary on each axis, ex, ey and ez the distance between boundaries, and sx, sy and sz the step on
# each (1 or -1). It is one def calling only itself, matched on the steps left and the cell first,
# so it runs as a loop; the next cell, across whichever boundary is nearest, is picked with
# arithmetic (bx, by and bz: 1 for the axis crossed, else 0), and the land's height (as Viz.land)
# is worked out in place.
def Viz.march(n: Nat, solid: Bool, +X: F32, +Y: F32, +Z: F32, +mx: F32, +my: F32, +mz: F32, +ex: F32, +ey: F32, +ez: F32,
              +sx: F32, +sy: F32, +sz: F32, +face: U32, +d: F32, +ox: F32, +oy: F32, +oz: F32, +dx: F32, +dy: F32, +dz: F32, +k: Viz.K) -> U32:
  match n solid:
    case 0n False{}:
      Viz.sky(dy, k)
    case 0n True{}:
      Viz.vshade(face, Y, d, ox, oy, oz, dx, dy, dz, k)
    case 1n++r True{}:
      Viz.vshade(face, Y, d, ox, oy, oz, dx, dy, dz, k)
    case 1n++r False{}:
      K{t, zoom, mid, hue, beat, w, h} = k
      +bx = F32.min(Viz.below(mx, my), Viz.below(mx, mz))
      +by = ((1.0 - bx) * Viz.below(my, mz) : F32)
      +bz = (1.0 - bx - by : F32)
      +X2 = (X + sx * bx : F32)
      +Y2 = (Y + sy * by : F32)
      +Z2 = (Z + sz * bz : F32)
      +land = F32.floor((6.0 + 2.5 * F32.sin((X2 * 0.19 + t * 0.35 : F32)) + 2.5 * F32.cos((Z2 * 0.13 - t * 0.15 : F32))
        + 2.0 * F32.sin(((X2 + Z2) * 0.09 + mid * 3.0 : F32)) + (zoom - 0.045) * 100.0 : F32))
      Viz.march(r, F32.is_lt(Y2, land), X2, Y2, Z2, (mx + ex * bx : F32), (my + ey * by : F32), (mz + ez * bz : F32),
        ex, ey, ez, sx, sy, sz, F32.to_u32((by + 2.0 * bz : F32)), (mx * bx + my * by + mz * bz : F32), ox, oy, oz, dx, dy, dz, k)

# The distance along the ray to the first boundary past o on an axis, stepping s, at e per cell.
def Viz.first(+c: F32, +o: F32, +s: F32, +e: F32) -> F32:
  (((c - o) * s + (s + 1.0) * 0.5) * e : F32)

def Viz.ray(+ox: F32, +oy: F32, +oz: F32, +dx: F32, +dy: F32, +dz: F32, +k: Viz.K) -> U32:
  +X = F32.floor(ox)
  +Y = F32.floor(oy)
  +Z = F32.floor(oz)
  +sx = Viz.sgn(F32.is_lt(dx, 0.0))
  +sy = Viz.sgn(F32.is_lt(dy, 0.0))
  +sz = Viz.sgn(F32.is_lt(dz, 0.0))
  +ex = (1.0 / F32.max(Viz.abs(dx), 0.0001) : F32)
  +ey = (1.0 / F32.max(Viz.abs(dy), 0.0001) : F32)
  +ez = (1.0 / F32.max(Viz.abs(dz), 0.0001) : F32)
  Viz.march(Viz.reach(), F32.is_lt(Y, Viz.land(X, Z, k)), X, Y, Z, Viz.first(X, ox, sx, ex), Viz.first(Y, oy, sy, ey), Viz.first(Z, oz, sz, ez),
    ex, ey, ez, sx, sy, sz, 1, 0.0, ox, oy, oz, dx, dy, dz, k)

# One pixel of the voxels: the camera flies forward over the land, swaying, looking a little down.
def Viz.vox(+x: U32, +y: U32, +k: Viz.K) -> U32:
  K{t, zoom, mid, hue, beat, w, h} = k
  +u = ((U32.to_f32(x) - U32.to_f32(w) * 0.5) / U32.to_f32(h) : F32)
  +v = ((U32.to_f32(h) * 0.5 - U32.to_f32(y)) / U32.to_f32(h) : F32)
  +yaw = (0.35 * F32.sin((t * 0.11 : F32)) : F32)
  +fx = F32.sin(yaw)
  +fz = F32.cos(yaw)
  +rx = (fx + u * fz : F32)
  +ry = (v - 0.32 : F32)
  +rz = (fz - u * fx : F32)
  +len = F32.sqrt((rx * rx + ry * ry + rz * rz : F32))
  Viz.ray((7.0 * F32.sin((t * 0.17 : F32)) : F32), 17.5, (t * 5.0 : F32), (rx / len : F32), (ry / len : F32), (rz / len : F32), k)

# The voxels' frame, strided over the lanes as the plasma's is.
def Viz.vput(inside: Bool, +i: U32, +x: U32, +y: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match inside:
    case True{}:
      Array.set(U32, a, i, Viz.vox(x, y, k))
    case False{}:
      a

def Viz.vlin(n: Nat, +i: U32, +x: U32, +y: U32, +d: U32, +e: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match n:
    case 0n:
      a
    case 1n+r:
      K{t, zoom, mid, hue, beat, w, h} = k
      +c = Bool.to_u32((x + d >= w : U32))
      Viz.vlin(r, (i + 32768 : U32), (x + d - c * w : U32), (y + e + c : U32), d, e, k, Viz.vput((i < w * h : U32), i, x, y, k, a))

def Viz.vleaf(+j: U32, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  K{t, zoom, mid, hue, beat, w, h} = k
  Viz.vlin(U32.to_nat(((w * h + 32767) / 32768 : U32)), j, (j % w : U32), (j / w : U32), (32768 % w : U32), (32768 / w : U32), k, a)

@unsafe
def Viz.voxels(+k: Viz.K, a: Array<U32>) -> Array<U32>:
  Array.spread(~U32, ~Viz.K, ~Viz.vleaf, a, 32768, k)

# One effect's frame: the tree, the voxels, or the plasma (on the CPU in tiles, on the GPU strided).
@unsafe
def Viz.pick(+fx: U32, gpu: Bool, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  Viz.pick.tree(U32.is_eq(fx, 1), fx, gpu, k, a)

@unsafe
def Viz.pick.tree(tree: Bool, +fx: U32, gpu: Bool, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match tree:
    case True{}:
      Viz.tree(k, a)
    case False{}:
      Viz.pick.vox(U32.is_eq(fx, 2), gpu, k, a)

@unsafe
def Viz.pick.vox(vox: Bool, gpu: Bool, +k: Viz.K, a: Array<U32>) -> Array<U32>:
  match vox:
    case True{}:
      Viz.voxels(k, a)
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
  +k = {K{t, z, mid, hue, beat, w, h} : Viz.K}
  +k2 = {K{(t + dt : F32), z, mid, hue, beat, w, h} : Viz.K}
  +fx = (ask >> 26n .&. 15 : U32)
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
