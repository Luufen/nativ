# PCSH00208 localization and English behavior

## Finding

The Asian 1.30 release already contains substantial official English content. On a host locale of
`en_US`, Nativ reported Vita system language ID 1 and the tested route displayed English save-data
prompts, notices, agreements, story dialogue, HUD text, and mission UI.

No replacement strings are needed for the tested release. The upstreamable English behavior is a
runtime language selection, not a redistribution patch.

## Runtime selection

Games read the Vita language through `sceAppUtilSystemParamGetInt`. Nativ normally resolves that
value from the host locale or the per-game `game_lang` setting. `RECOMP_GAME_LANG` takes precedence.

The PCSH00208 pack sets:

```text
RECOMP_GAME_LANG=en_us
```

for both normal runs and the regression route. This forces Vita language ID 1 on every host locale
and makes the game's own English resources deterministic.

This setting changes only the language value exposed by the runtime. It does not modify archives,
inject text, replace fonts, or bypass ownership checks.

## Verified English surfaces

The following surfaces were observed in English:

- save-data initialization;
- network and agreement notices;
- the previous-game data-import choice;
- opening story dialogue;
- first-mission HUD and objective UI; and
- target and score labels during combat.

Later missions, add-on content, multiplayer, and uncommon error dialogs have not been exhaustively
checked. A missing English surface should be documented with its state and resource identity, not
copied into the repository.

## Copyright-safe policy

Allowed in this repository:

- language IDs and selection behavior;
- archive and record structure;
- hashes, offsets, sizes, and cryptographic digests;
- original code that selects or loads resources; and
- original documentation and test metadata.

Not allowed:

- official strings or string tables;
- extracted fonts, images, audio, video, models, or textures;
- repacked game archives;
- executable or firmware bytes; and
- captures or screenshots containing game content.

## Future overlay design

An overlay remains useful only for original community-authored text or for a regional build that
lacks usable English resources. If implemented later, it should:

1. live outside the repository as a user-supplied package;
2. declare the exact title ID, executable digest, and update version it targets;
3. map stable resource identities to replacement records without embedding original records;
4. validate encoding, record sizes, and bounds before activation;
5. fall back to the untouched game resource when an overlay entry is absent;
6. report malformed or version-mismatched overlays explicitly; and
7. never download or bundle copyrighted source material.

That loader is not required for PCSH00208 update 1.30 because its existing English resources cover
the tested path.
