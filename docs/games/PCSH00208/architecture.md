# PCSH00208 architecture notes

These notes describe the parts of update 1.30 that are relevant to its Nativ pack. Names and counts
are observations from analysis and runtime traces; unknown behavior is called out rather than
inferred.

## Program and module set

The main program is `eboot.bin.elf`. Recompilation of the tested update produced 44,279 translated
functions covering 3,478,303 guest instructions.

The observed route requires:

| Module | Role in this pack |
|---|---|
| `libc` | Sony C/C++ runtime used by the game |
| `libgxm_es4` | GXM graphics API and shader-patcher path |
| `libgpu_es4` | GPU service support used by GXM |
| `libfios2` | Sony FIOS file access used by the resource layer |
| `libSceJson` | JSON support imported by the executable |

`libface`, `libsmart`, and `libult` are present in `sce_module`, but the tested route did not load
them. They should only be added to `MODULES` if a later trace reaches their imports.

The base game is version 1.00 and the layered update is version 1.30. Import and pack generation
must use the update-composed `app0_patched` view. Treating the base and patch as unrelated trees
selects the wrong executable and produces a pack for 1.00.

## Native execution layers

The resulting program has four relevant layers:

1. The main ARM executable and required Sony modules are translated to native C.
2. Nativ's core runtime supplies the Vita kernel, memory, AppUtil, input, save-data, and file APIs.
3. Recompiled `libgxm_es4` forwards graphics work into the runtime's GXM boundary.
4. Decoded USSE shaders run either as translated SPIR-V on the host GPU or through the software
   interpreter when a shader operation is outside translator coverage.

No game executable or module is stored in the pack. The user's decrypted copy is analyzed and
recompiled locally.

## Resource layout

Observed file requests use both `masterPSP2` and `finalizedPSP2` trees under `app0`. Paths cover
models, textures, UI, sound, effects, and unit data. Some requests deliberately probe a preferred
tree and then fall back, so an individual `not found` trace is not enough to establish a missing
asset.

The resource system uses Orochi-style `GDAT` archives. Entries identify payloads by a resource hash
and a type hash. Many payloads contain a `BILZ` stream, commonly after a per-resource header.
Localization-heavy data is carried in `DT2` resources. See [file-formats.md](file-formats.md).

## Boot and state flow

The deterministic route exposes these coarse states:

```text
save initialization
  -> New Start
  -> network notice
  -> agreement
  -> previous-game import choice
  -> opening movie
  -> network notice
  -> opening dialogue
  -> first mission
```

The route is frame-driven for regression testing, not a claim that state transitions are fixed to
those frame numbers on every host. The test clock and headless runner make the checked route
repeatable on the current toolchain.

## CPU translation finding

The opening sequence executes:

```text
vrshrn.i16 d16, q8, #2
```

followed by two more `VRSHRN.I16` instructions. `VRSHRN` performs a rounded right shift followed by
narrowing. The translator copies source lanes before writing the destination so overlapping NEON
registers remain correct. Unsigned 64-bit rounded-shift logic avoids overflow in the usual
`value + rounding-bias` expression.

### CRI hangar movie decoder

The rear hangar displays are not static textures. The game opens
`finalizedPSP2/GP/movie/hangar.usm`, a CRI USM container whose video stream is MPEG-1, 640x72 at
30 FPS in YUV420P. The source movie decodes cleanly with FFmpeg. Recompiled game middleware writes
one 640x72 Y plane and two 320x36 chroma planes, which fragment program `0x12339f90` samples as
`gMovieY`, `gMovieU`, and `gMovieV`.

The decoder's inverse transform at guest function `0x8175fe3c` uses widening by-scalar operations,
including:

```text
vmull.s16 q14, d22, d0[0]
vmull.s16 q14, d27, d0[3]
vmlsl.s16 q13, d24, d0[1]
```

The translator previously treated these as vector-vector operations. It multiplied source element
`k` by `d0[k]`, even when the instruction selected one scalar lane. That preserved block placement
and approximate DC values but corrupted the IDCT residuals, producing the displays' severe 8x8
high-frequency noise. Widening `VMULL`, `VMLAL`, and `VMLSL` now broadcast the encoded `dM[x]`
lane; their vector-vector forms retain pairwise lane behavior.

A new live capture after rebuilding matched its decoded Y plane to the closest direct source frame
with 0.999985 correlation and 0.0214 mean absolute byte error. The pre-fix plane reached only
0.5656 correlation and 26.52 byte error. Captures and decoded movie data remain private and are not
part of the pack.

## Graphics path

### Shared-exponent HDR format

The first mission uses texture format `0x19000000` over color targets with base format
`0x31000000`. Both represent `SE5M9M9M9`, a four-byte shared-exponent RGB format:

```text
bits  0-8   red mantissa
bits  9-17  green mantissa
bits 18-26  blue mantissa
bits 27-31  shared exponent

component = mantissa * 2^(exponent - 24)
```

The maximum finite component is 65408. Treating this format as eight bytes caused incorrect texture
addressing. Treating a target download as RGBA8 turned black into `0xff000000`, which decoded as a
large HDR value and overexposed the mission.

The software renderer now packs and unpacks the target exactly. The GPU renderer creates a direct
view when a `0x19` texture aliases a `0x31` target, avoiding a target download, packed conversion,
decode, and upload each frame.

### No-mip cube-map faces

The material shaders bind a 4x4 RGBA8 light-probe cube. Its six no-mip faces are swizzled 4x4
images packed directly after one another, so each face occupies `4 * 4 * 4 = 64` bytes and the
whole cube occupies 384 bytes.

The shared cube helper previously used the row stride for a linear texture. Linear widths are
rounded to eight texels, so the helper incorrectly advanced 128 bytes per face. Faces 0-2 happened
to read valid even-numbered data, while faces 3-5 read beyond the cube into whatever followed its
allocation. Three rotating cube allocations therefore produced a repeatable colored, black, and
grey material cycle even though the first 384 bytes were identical.

Both the software sampler and GPU uploader use the same face-offset helper. Using the tightly
packed swizzled size removes the cycle in GPU and software replays of the title, hangar, character,
and paint-screen captures. A synthetic six-face 4x4 regression covers the no-mip layout.

### Off-screen paint-map generation

Gunpla paint is baked into 256x256 material maps by a fullscreen off-screen pass. The observed pass
uses vertex program `0x11e1ce50` and fragment program `0x138cc0d0`; its default-uniform block
contains 18 material colors plus camouflage, glow, weathering, and background parameters. Captures
proved that vivid palette edits reached those uniforms and that all source textures were coherent,
yet the generated map remained almost flat green.

The fragment GXP declares a 64-word texture buffer. Container 19 places its container-15 base in
`SA122` with a `-4` byte bias, and dependent-sampler entries place descriptor words in nearby SA
registers. The shader both copies a dependent sampler's word 0 through a temporary and loads later
descriptors with `LDST` before `SMP`. Nativ previously left those numeric SA words at zero and
ignored resident buffer 15, so every dynamic sample selected unit 0.

For programs with a texture buffer, Nativ now uses a synthetic descriptor layout
`{ texture_unit, 0, 0, 0 }`. Register setup writes the signed container-15 bias and dependent
sampler indices. Interpreter `LDST` recognizes the texture-buffer base instead of dereferencing
guest address zero, and the SPIR-V translator emits the same descriptor-word result without
declaring a guest-memory window. Programs without a texture buffer retain zero numeric descriptor
words so shaders that inspect real control bits do not regress.

On the vivid-yellow diagnostic capture, the old output had mean RGBA
`(14.07, 69.92, 39.00, 255)` and no yellow pixels. The corrected software result has mean RGBA
`(85.22, 88.99, 69.44, 255)` with 4,994 yellow pixels; Vulkan produces the same count and differs
by at most three 8-bit values. Synthetic tests cover copied dependent samplers, descriptor word 0,
zero-valued words 1–3, the no-texture-buffer compatibility case, and both execution backends.

An all-yellow capture disproved the initial interpretation that the remaining neutral panels were
separate UI sections. All six primary colors, `gMaterialColor[0,3,6,9,12,15]`, reached the shader
as `(1.0, 0.7843137, 0.2823529, 1.0)`, while the other entries were the unused black secondary and
tertiary colors. Nevertheless, 54,970 atlas pixels (83.88%) remained neutral.

The GXP header's second dependent-sampler table, at offsets 136 and 140, contains 60 entries that
map each `(texture unit, descriptor word)` pair to a container-15 word. The generator deliberately
permutes this layout: unit 1 occupies words 8–11 and unit 2 occupies words 4–7. Nativ's first
container-15 implementation assumed a linear `word / 4` unit, so the word at offset 8 selected unit
2 a second time and unit 1 was never sampled. The full pre-fix sample sequence was
`0,2,2,3,4,5,6,7,8,9,10,11,12,13,14,15`; applying the table changes it to
`0,2,1,3,5,6,4,8,15,14,13,12,11,10,7,9`.

Program decoding now builds one shared container-15 word map from that table. Interpreter `LDST`
and SPIR-V translation both use it; programs without a table keep the linear fallback. A synthetic
regression deliberately maps descriptor slot 5 to texture unit 3 so an implementation that
reintroduces `word / 4` fails.

Replaying the all-yellow generator draw changed software mean RGB from
`(86.7866, 85.7813, 66.7448)` to `(140.8229, 121.7992, 58.0088)`. Loose-yellow coverage rose from
7,185 pixels (10.96%) to 35,736 (54.53%), neutral coverage fell from 54,970 pixels (83.88%) to
25,367 (38.71%), and pixel `(30,30)` changed from `(101,96,91)` to `(241,189,68)`. Vulkan produced
35,732 loose-yellow pixels; its maximum channel difference from software was four and its mean
absolute channel difference was 0.274.

The focused capture records the generator draw for one 256x256 part atlas. Later material draw 25
samples that corrected target, but the other seven part atlases are capture memory records produced
by the old live run. `RECOMP_REPLAY_KEEP_TARGETS=1` correctly preserves the rendered target but
cannot reconstruct updates that occurred before capture began, so the recorded final Gunpla is not
an all-parts validation. A fresh release run regenerated all part atlases and visually confirmed
that every intended armor region followed the all-yellow selection.

### Shader translation

The first mission renders correctly, including terrain, Earth, enemies, HUD, and target markers.
Two dominant material programs use texture-buffer indexing in `SMP`:

| Fragment program | Primary instruction | Dynamic source |
|---:|---:|---|
| `0x127e2290` | 77 | `PA4` |
| `0x1284b1d0` | 38 | `TEMP0` |

The low byte of the source register selects the Vita texture unit at run time. SDL_GPU exposes
units as separate bindings rather than an indexable descriptor array, so the translator declares
the compatible 2D or cube units and emits an `OpSwitch` with one statically named sampler per case.
Only the selected case samples. Implicit-LOD fragment operations compute derivatives before the
non-uniform switch and use explicit gradients inside each case.

Those programs also use fragment `LDST` to read ordinary uniform-buffer memory. Fragment loads now
share the persistent 64 KiB guest-memory window ring used by vertex shaders while retaining the
sampled-image fast path for pointers inside F16 or RGBA8 GPU targets. A module that already declares
16 samplers omits the target images and uses storage-only windows, avoiding a driver failure from
the larger mixed resource layout.

Two programs used earlier in the live route, `0x138d3790` and `0x138d6ad0`, each declare an 81920
byte container-18 thread buffer. The GXP buffer table places its pointer in `SA8`; the hardware
allocation divides to 1280 words (5 KiB) of private memory per invocation. Both programs write 64
sampled words and later load selected words from that scratch area. The interpreter now provides a
zeroed per-thread buffer, while SPIR-V lowers those accesses to a `Private` array. No writable SDL
graphics storage resource or guest-memory writeback is involved.

The remaining captured fallback was map-scan program `0x12470f60`. Its branch graph is a three-way
ladder: nested and outer selections share one unconditional closing jump. The translator now
closes the inner merge before using that same guest branch to enter the outer else block.

In the private frame-10970 capture:

- all 216 draws execute on the GPU;
- all 93 generated SPIR-V modules pass `spirv-val`;
- the final replay image is pixel-identical to the earlier replay that used 42 software draws.

The fresh-profile frame-12000 route also has zero software-renderer draws. Guest-visible fragment
`LDST` stores and arbitrary crossing branch graphs remain unsupported in general, but neither is
present in the validated routes; the observed stores target per-invocation container-18 memory.
