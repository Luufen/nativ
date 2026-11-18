# Gundam Breaker 3 (PCSH00208)

This directory records findings for the Asian Vita release of **Gundam Breaker 3**, title ID
`PCSH00208`, with update 1.30. Findings are tied to the following decrypted update executable:

```text
SHA-256  4b6146d3b13ba7e52ab685d0042aa3c2bec7277a5ea68dc6d51c12d013c53386
Content  HP0700-PCSH00208_00-GUNDAMBREAKER3VM
```

The pack does not contain game code or data. A contributor must supply a legally obtained game,
update, and firmware. Do not add extracted strings, screenshots, captures, generated recompilation
output, or other game assets to Git.

## Documents

- [Architecture](architecture.md) describes the executable, modules, resource layers, and graphics
  paths observed during recompilation.
- [File formats](file-formats.md) records the current `GDAT`, `BILZ`, and `DT2` findings.
- [Localization](localization.md) explains the existing English resources and the copyright-safe
  runtime selection used by the pack.

## Tested result

Update 1.30 has been tested from a fresh profile through:

1. initial save creation;
2. the network notice and agreement;
3. the Gundam Breaker 2 data-import choice;
4. the opening movie and English story sequence;
5. the first playable mission; and
6. movement, jump, normal attack, and strong attack.

At frame 11600 of the development route, gameplay had zero missed CPU functions, unsupported CPU
instructions, and unknown NIDs. The player could move into the first enemy group and score Ace
Points. This is a functional checkpoint, not a claim that the complete game has been tested.

The current fresh-profile pack test reaches active gameplay at frame 12000 after the shader
changes. It has zero software-renderer draws, missed CPU functions, unsupported CPU instructions,
and unknown NIDs.

A separate persistent-save route reaches the hangar and its animated rear displays. The displays'
CRI USM movie now decodes correctly; a live-captured luma plane matched the corresponding source
movie frame with 0.999985 correlation and 0.0214 mean absolute byte error.

Four eight-frame captures reproduced Gunpla, character, and paint-screen flashes on a three-frame
cycle. The affected material draws used a 4x4 no-mip light-probe cube. No-mip cube faces are
tightly packed swizzled images; applying the linear texture row stride doubled each face from 64
to 128 bytes and made faces 3-5 sample adjacent allocations. Replaying the captures through both
renderers after correcting the shared face offset keeps all three display buffers stable.

The remaining grey Gunpla was traced to the off-screen paint-map generator rather than the final
material draw. Its fragment shader received the edited palette and coherent source textures, but
container-15 texture-buffer loads and copied dependent samplers all resolved to texture unit 0.
Nativ now represents texture descriptors as synthetic unit indices in both the interpreter and
SPIR-V translator. The GXP's second dependent-sampler table also permutes those descriptors inside
container 15; honoring that table fixes the remaining duplicated unit-2 sample and restores the
omitted unit-1 armor masks. In an all-yellow capture, software replay increased loose-yellow
coverage from 7,185 pixels (10.96%) to 35,736 (54.53%), while the previously neutral shoulder
pixel changed from `(101, 96, 91)` to `(241, 189, 68)`. Vulkan produced 35,732 loose-yellow pixels
and differed from software by at most four 8-bit values. A fresh release run then regenerated all
part atlases and displayed every intended armor region in the selected yellow.

## Deterministic fresh-profile route

`packs/PCSH00208/pack.toml` contains the reference route. `TEST_INPUT` entries are comma-separated
inclusive frame ranges in the form `from-to:hex-mask`.

| Frames | Input | Purpose |
|---:|---|---|
| 500-510 | Square (`8000`) | Select New Start |
| 900-910 | Circle (`2000`) | Dismiss the network notice |
| 2400-2410 | Cross (`4000`) | Accept the agreement |
| 3300-3310 | Cross (`4000`) | Do not import Gundam Breaker 2 data |
| 4000-4010 | Circle (`2000`) | Confirm the choice |
| 5500-5510 | Circle (`2000`) | Dismiss the post-movie network notice |
| 7600-10670 | Repeated Circle taps | Advance the opening story dialogue |
| 11100-12000 | Left stick forward | Move into the first enemy group |
| 11200-11320 | Square (`8000`) | Normal attack |
| 11400-11420 | Triangle (`1000`) | Strong attack |
| 11500-11520 | Cross (`4000`) | Jump |

The pack captures frame 12000 in the first mission. The stick tuple in `TEST_ENV` is left X/Y
followed by right X/Y:

```text
RECOMP_INPUT_STICKS=11100-12000:128,0,128,128
RECOMP_INPUT=...,11200-11320:8000,11400-11420:1000,11500-11520:4000
```

The route holds forward, then exercises Square, Triangle, and Cross.

## Reproducing the pack test

After importing a legally obtained copy and building it:

```text
python pipeline/vitarecomp.py test PCSH00208
```

For renderer work, record a private capture near the first gameplay frame and replay it instead of
running the complete route for every shader change:

```text
python pipeline/vitarecomp.py capture PCSH00208 10970
python pipeline/vitarecomp.py replay PCSH00208 10970
```

Captures contain game data and must remain outside Git.

## Runtime fixes exercised by this game

- Update-aware pack generation must select the layered `app0_patched` tree so the 1.30 executable
  is hashed and imported rather than the base 1.00 executable.
- The CPU recompiler needs the NEON `VRSHRN` narrowing family. The opening route reaches
  `VRSHRN.I16` around frame 4154.
- The bundled MPEG-1 decoder uses widening NEON multiply-by-scalar forms in its IDCT. `VMULL`,
  `VMLAL`, and `VMLSL` must broadcast the selected `dM[x]` lane instead of pairing source lane
  `k` with `dM[k]`; the latter corrupts the hangar movie's YUV planes before rendering.
- The renderer needs the Vita `SE5M9M9M9` shared-exponent texture and color-target formats. Its
  texture base is `0x19`; its color-target base is `0x31`.
- A render target written as `SE5M9M9M9` can be sampled through a texture view. The GPU path uses a
  direct view of the internal RGBA16F target; the software path implements exact packed
  conversion.
- Gameplay material shaders select 2D and cube texture units from `TEMP0` or `PA4` at run time.
  The SPIR-V path preserves that choice with structured sampler switches, including explicit
  gradients for implicit-LOD fragment samples.
- The paint-map generator loads sampler indices through the container-15 texture buffer and copies
  dependent-sampler word 0 through temporary registers. The interpreter and SPIR-V translator
  synthesize unit-index descriptor words instead of reading guest address zero. They honor the
  GXP texture-buffer dependent-sampler table because its descriptor order is not linear.
- No-mip cube-map faces are tightly packed swizzled images. They must not inherit the rounded row
  stride used by linear textures; a 4x4 RGBA8 face occupies 64 bytes.
- Fragment `LDST` loads can read either a GPU color target or a 64 KiB guest-memory window. Large
  dynamic-sampler modules use storage-only LDST variants to stay within the sampled-resource
  budget.
- Fragment programs `0x138d3790` and `0x138d6ad0` use container 18 as a 5 KiB per-invocation
  thread buffer. Their `LDST` stores and loads use SPIR-V `Private` memory rather than a writable
  SDL graphics storage resource.
- The map-scan fragment shader uses a three-way branch ladder whose nested and outer selections
  share a closing jump. That compiler shape is lowered to nested structured SPIR-V selections.

## Known gaps

- The private frame-10970 replay now runs all 216 recorded draws on the GPU with no software
  fallback and is pixel-identical to the earlier software-backed replay. A coverage-disabled live
  route completed 12000 presented frames in 203.2 seconds (59.1 presented frames per second on the
  tested RTX 5070), including boot, movies, loading, and the opening mission. This is not a
  sustained gameplay frame-time benchmark.
- Guest-visible fragment `LDST` stores and arbitrary crossing branch graphs remain outside general
  translator coverage. Container-18 stores are supported because their memory is private to each
  invocation.
- The direct GPU view of an `SE5M9M9M9` target preserves the internal RGBA16F value instead of
  explicitly quantizing it through the packed shared-exponent representation. The visual result is
  correct on the tested mission, but the small precision difference has not been measured.
- Candidate 60 FPS constants and frame-delta sites have not been enabled. Widescreen behavior has
  not been characterized.
- A later interactive mission also reported missing terrain with intact collision and flickering
  enemy effect overlays. Separate failing mission captures are still required.
- Only the opening and first mission have been exercised. Save progression, later missions,
  add-ons, multiplayer, and completion are unverified.
