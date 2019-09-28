# ICU timezone data

The ICU timezone data in Fuchsia is provided dynamically through the ICU data
files (`icudtl.dat`).  These are loaded on demand by programs, provided that
the program's package is configured to make the file available to the program
at runtime.

The APIs made available are different per language, so please refer to the
pages below for specific details:

- [C++ library for ICU data loading](/src/lib/icu_data/cpp)
- [Rust library for ICU data loading](/src/lib/icu_data/rust)
