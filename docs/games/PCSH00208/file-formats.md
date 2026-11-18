# PCSH00208 file-format notes

This document records only structural metadata needed for interoperability and research. It does
not include extracted payloads, strings, or other copyrighted game content.

## `GDAT` archives

The game's Orochi archives begin with ASCII `GDAT`. Header integers and the entry table are
little-endian. The header includes an entry count, followed by fixed-size 16-byte entries:

| Entry field | Size | Meaning |
|---|---:|---|
| Resource hash | 4 bytes | Identifier used to locate a resource |
| Type hash | 4 bytes | Identifies the payload family |
| Payload offset | 4 bytes | Absolute byte offset from the start of the archive |
| Payload length | 4 bytes | Stored payload size |

Offsets are absolute, not relative to the entry table. Validate both `offset` and
`offset + length` against the archive size before reading a payload.

Known type hashes from the tested data are:

| Type hash | Extension |
|---:|---|
| `FC397BB3` | `tex` |
| `2C1CF0E2` | `dt2` |
| `FA61C1D7` | `mdl` |
| `5E12DE0D` | `mdc` |
| `637A3623` | `mse` |
| `DDE6C1B6` | `msq2` |
| `598A8375` | `mtsq` |
| `2DCB5FBB` | `mss` |

The hash algorithm and collision policy have not been established. Tools should retain the numeric
hash as the primary identity rather than assuming an extension is unique.

## `BILZ` payloads

Many `GDAT` payloads contain a `BILZ` stream, frequently beginning at payload offset `0x80`. That
offset is an observation, not a universal rule; locate and validate the signature for each payload.

Unlike the surrounding `GDAT` table, identified `BILZ` numeric fields are big-endian.

Observed compression methods:

| Method | Meaning |
|---:|---|
| 0 | Stored, no compression |
| 7 | zlib-compressed |

A safe decoder should:

1. verify the `BILZ` signature and that all declared fields fit in the payload;
2. parse its numeric fields as big-endian;
3. reject unknown methods explicitly;
4. bound decompression to the declared output size; and
5. verify that the decoder produced exactly that size.

The complete field-offset map is still under investigation and should not be inferred from the
common `0x80` placement.

## `DT2` resources

`DT2` is the primary localization-heavy resource family observed in the game data. A scan found
27,078 `DT2` resources across the supplied base game and update. The executable also references
English UI paths and multilingual fonts.

The internal `DT2` schema is not yet documented well enough for a writer or lossless round trip.
Current findings establish resource presence and language behavior, not a stable text-extraction
API. Avoid committing extracted records while the format is being studied.

## Other payload families

The known extensions suggest these broad roles, based on file requests and usage:

- `tex`: textures;
- `mdl` and `mdc`: model-related data;
- `mse`, `msq2`, `mtsq`, and `mss`: effect, sequence, or sound-related resources.

Those descriptions are intentionally conservative. Binary layouts for these formats have not been
confirmed and should remain separate follow-up work.

## Research workflow

Keep all source archives and decoded output outside the repository. A useful metadata-only scan
can emit:

- archive-relative file name;
- archive size;
- entry count;
- entry index;
- resource and type hashes;
- payload offset and length;
- `BILZ` signature offset and method; and
- a cryptographic digest of a payload when exact identity is needed.

Do not emit payload bytes or official text into logs intended for a pull request.

