# Font Server integration tests

Updated 2019-12-12

---

## Test groups

There are three groups of tests:
* [`reviewed_api`](reviewed_api)\
  Tests for the API-reviewed `fuchsia.fonts.Provider` protocol in the SDK.

* [`old_api`](old_api.rs)\
  Tests for the initial version of that protocol. This is deprecated and will be
  removed as soon as we're confident we've removed all remaining clients.

* [`experimental_api`](experimental_api)\
  Tests for not-yet-reviewed experimental APIs, which are not in the SDK.

## Where are the manifests coming from?

You'll notice that this directory contains only two `.json` files, containing
hard-coded manifests. These are installed into the font server's namespace
(under `/testdata`) by the tests that launch the font provider.

The rest of the test manifests are generated in the calls to `font_collection()`
in [`//src/fonts/BUILD.gn`](../BUILD.gn), using real font data pulled down from
CIPD (just like production font manifests). These are added to the font server's
namespace under `/config/data/`.

## Omitted fields
Any omitted fields in the manifests are assumed to have the following values:

- Fallback: false
- Generic family: None
- Index: 0
- Styles (Slant, Weight, Width)
    - Upright, 400, Normal
- Languages: []