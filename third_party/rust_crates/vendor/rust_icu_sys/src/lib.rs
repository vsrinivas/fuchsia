// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#![doc(test(ignore))]
#![allow(
    dead_code,
    non_snake_case,
    non_camel_case_types,
    non_upper_case_globals,
    unused_imports
)]

#[cfg(all(feature="icu_version_in_env", feature="icu_config"))]
compile_error!(
    "Features `icu_version_in_env` and `icu_config` are not compatible." +
    " Choose at most one of them.");

#[cfg(all(feature="icu_config", not(feature="use-bindgen")))]
compile_error!("Feature `icu_config` is useless without the feature `use-bindgen`");

// This feature combination is not inherrently a problem; we had no use case that
// required it just yet.
#[cfg(all(not(feature="renaming"), not(feature="use-bindgen")))]
compile_error!("You must use `renaming` when not using `use-bindgen`");

#[cfg(feature="use-bindgen")]
include!(concat!(env!("OUT_DIR"), "/macros.rs"));
#[cfg(all(feature="use-bindgen",feature="icu_config",not(feature="icu_version_in_env")))]
include!(concat!(env!("OUT_DIR"), "/lib.rs"));

#[cfg(not(feature="use-bindgen"))]
include!("../bindgen/macros.rs");

#[cfg(all(not(feature="use-bindgen"),not(feature="icu_version_in_env"),not(feature="icu_config")))]
include!("../bindgen/lib.rs");

#[cfg(all(not(feature="use-bindgen"),feature="icu_version_in_env",not(feature="icu_config")))]
include!(concat!("../bindgen/lib_", env!("RUST_ICU_MAJOR_VERSION_NUMBER"), ".rs"));

// Add the ability to print the error code, so that it can be reported in
// aggregated errors.
impl std::fmt::Display for UErrorCode {
    fn fmt(&self, f: &mut std::fmt::Formatter) -> std::fmt::Result {
        write!(f, "{:?}", self)
    }
}

extern crate libc;

// A "fake" extern used to express link preferences.  The libraries mentioned
// below will be converted to "-l" flags to the linker.
#[link(name = "icui18n", kind = "dylib")]
#[link(name = "icuuc", kind = "dylib")]
extern "C" {}

