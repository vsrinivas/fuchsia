Updated 06/25/2019

This doc is intended to answer the question "Which fonts are being loaded when
this test runs?"

# Omitted fields
Any ommitted fields are assumed to have the following values:

- Fallback: false
- Generic family: None
- Index: 0
- Styles (Slant, Weight, Width)
    - Upright, 400, Normal
- Languages: []

# Default fonts
Fonts loaded by `start_provider_with_default_fonts()`.

Count: 7

- Material Icons (MaterialIcons)
- Roboto
    - Fallback: true
    - Generic family: SansSerif
    - Styles
        - 300
        - 400
        - 500
- Roboto Mono
    - Fallback: true
    - Generic family: Monospace
    - Styles
        - 300
        - 400
        - 500

# Test fonts
Fonts loaded by `start_provider_with_test_fonts()` in addition to the default
fonts.

Count: 8

- Noto Serif CJK
    - Fallback: true
    - Generic family: Serif
    - Index: 0
    - Languages
        - ja
- Noto Serif CJK
    - Fallback: true
    - Generic family: Serif
    - Index: 1
    - Languages
        - ko
- Noto Serif CJK
    - Fallback: true
    - Generic family: Serif
    - Index: 2
    - Languages
        - zh-hans
- Noto Serif CJK
    - Fallback: true
    - Generic family: Serif
    - Index: 3
    - Languages
        - zh-Hant
        - zh-Bopo
- Noto Sans CJK
    - Fallback: true
    - Generic family: SansSerif
    - Index: 0
    - Languages
        - ja
- Noto Sans CJK
    - Fallback: true
    - Generic family: SansSerif
    - Index: 1
    - Languages
        - ko
- Noto Sans CJK
    - Fallback: true
    - Generic family: SansSerif
    - Index: 2
    - Languages
        - zh-hans
- Noto Sans CJK
    - Fallback: true
    - Generic family: SansSerif
    - Index: 3
    - Languages
        - zh-Hant
        - zh-Bopo