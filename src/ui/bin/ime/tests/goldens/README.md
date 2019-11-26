IME and keyboard layout golden test cases
===

unicode-iso-keymap.json
---
Mapping from Unicode's positional layout (1) to Fuchsia Key (2). It's not currently
used in code or tests and serves as a human-readable reference for manual conversion
Unicode CLDRs to Fuchsia keyboard layouts.

1. <https://www.unicode.org/reports/tr35/tr35-keyboards.html#Definitions>
2. <https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/sdk/fidl/fuchsia.ui.input2/keys.fidl>

en-us.json
---
Contains U.S. keyboard layout test cases.

Symbolic keys tests are modeled after Unicode CLDR from
<https://www.unicode.org/cldr/charts/36/keyboards/layouts/en.html#en-t-k0-osx>

Following modifier states are supported:

- `None`
- `Shift`
- `CapsLock` + `Shift`
- `CapsLock`

Non-symbolic key tests are based on xkeyboard-config (https://github.com/freedesktop/xkeyboard-config) symbol map, more specifically "`symbols/pc(pc105)`".

The test set consists of key presses that should match semantic meanings.
Symbolic key codes (e.g. `<ESC>`, `<RTRN>`) which could be mapped directly to Key values are used.
Keysyms that could be mapped directly into Semantic Keys are used as well.
Symbolic key codes, modifiers, and keysyms which were not included are listed below as well as reasons for that.

Modifiers supported:

- `None`
- `NumLock`

Keys omitted:

- covered by Unicode CLDR:
  - `BKSL` aka `Key.BACKSLASH`
  - `LSGT` aka `Key.NON_US_BACKSLASH`
  - `SPCE` aka `Key.SPACE`
- no semantic meaning defined in `SemanticKeyAction` yet:
  - `PRSC`
  - `PAUS`
  - `KPPT`
  - `MDSW`
  - `SUPR`
  - `HYPR`
  - `OUTP`
  - `KITG`
  - `KIDN`
  - `KIUP`
- Symbolic key `<Alt>` not supported (only `Key.LEFT_ALT` and `Key.RIGHT_ALT`)
- Symbolic key `<Meta>` not supported (only `Key.LEFT_META` and `Key.RIGHT_META`)

Modifiers omitted:

- `Mod3`
- `Mod5`

Modifier interpretations:

  - `Lock` (virtual modifier) is `Modifiers.CAPS_LOCK`
  - `Shift` (virtual modifier) is `Modifiers.SHIFT`
  - `Control` (virtual modifier) is `Modifiers.CONTROL`
  - `Mod1` (virtual modifier) is `Modifiers.ALT`
  - `Mod2` (virtual modifier) is `Modifiers.NUM_LOCK`
  - `Mod4` (virtual modifier) is `Modifiers.META`

Key interpretations:

  - `LVL3` is `Key.RIGHT_ALT`
  - `Shift_L` and `LFSH` are `Key.LEFT_SHIFT`
  - `Shift_R` and `RTSH` are `Key.RIGHT_SHIFT`
  - `Control_L` and `LCTL` are `Key.LEFT_CTRL`
  - `Control_R` and `RCTL` are `Key.RIGHT_CTRL`
  - `Super_L` and `LWIN` are `Key.LEFT_META`
  - `Super_R` and `RWIN` are `Key.RIGHT_META`
  - `Alt_L` and `LALT` are `Key.LEFT_ALT`
  - `Alt_R` and `RALT` are `Key.RIGHT_ALT`
