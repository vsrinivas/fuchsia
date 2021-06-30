# WLAN Common

This library encapsulates WLAN functions that are very general or are shared between multiple layers of the WLAN stack.

## Major Responsibilities

### Field and Element Parsing

The ['ie parser'](./rust/src/ie/mod.rs) contains many definitions and parsing functions for parsing binary fields and information elements from WLAN frames into internal representations.

### MAC Frame parsing

The ['mac frame parser'](./rust/src/mac/mod.rs) parses binary frames to determine their types. A user of WLAN Common can use this information to determine which field and element parsing should be performed.

### Test Utilities

Utilities for genering a set of ['fake frames'](./rust/src/test_utils/fake_frames.rs) as well as entire ['fake stations'](./rust/src/test_utils/fake_stas.rs) for unit testing throughout the WLAN stack.

Also provides an 'assert_variant' macro that is used extensively in testing.

### Frame writing

Various utilities for writing fields and frames into binary formats.

## Rust Transition

This library currently contains both Rust and C++ implementions for many different functions. The C++ functions are mostly deprecated; our MLME implemention is transitioning from C++ to Rust, and we will remove this C++ functionality when the transition is complete.