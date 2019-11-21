IME and keyboard layout golden test cases
===

en-us.json
---
Keyboard layout is modeled after Unicode CLDR from
<https://www.unicode.org/cldr/charts/36/keyboards/layouts/en.html#en-t-k0-osx>

First 3 pages are used, for following modifier states:

- None
- Shift
- CapsLock + Shift
- CapsLock

unicode-iso-keymap.json
---
Mapping from Unicode's positional layout (1) to Fuchsia Key (2). It's not currently
used in code or tests and serves as a human-readable reference for manual conversion
Unicode CLDRs to Fuchsia keyboard layouts.

1. <https://www.unicode.org/reports/tr35/tr35-keyboards.html#Definitions>
2. <https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/sdk/fidl/fuchsia.ui.input2/keys.fidl>
