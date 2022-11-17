#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![no_std]
//! Low-level bindings to the [zstd] library.
//!
//! [zstd]: https://facebook.github.io/zstd/

#[cfg(feature = "std")]
extern crate std;

// The bindings used depend on a few feature flags.

// Std-based (no libc)
#[cfg(all(
    feature = "std",
    not(feature = "experimental"),
    not(feature = "bindgen")
))]
include!("bindings_zstd_std.rs");

// Std-based (no libc)
#[cfg(all(
    feature = "std",
    not(feature = "experimental"),
    feature = "zdict_builder",
    not(feature = "bindgen")
))]
include!("bindings_zdict_std.rs");