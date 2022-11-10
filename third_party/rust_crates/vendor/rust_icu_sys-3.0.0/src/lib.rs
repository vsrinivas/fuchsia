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

// Notes:
// * deref_nullptr: since rustc 1.53, bindgen causes UB warnings -- see
// https://github.com/rust-lang/rust-bindgen/issues/1651 remove this once bindgen has fixed the
// issue (currently at version 1.59.1)
#![allow(
    dead_code,
    non_snake_case,
    non_camel_case_types,
    non_upper_case_globals,
    unused_imports,
    rustdoc::bare_urls,
    deref_nullptr,
)]

#[cfg(all(feature = "icu_version_in_env", feature = "icu_config"))]
compile_error!(
    "Features `icu_version_in_env` and `icu_config` are not compatible."
        + " Choose at most one of them."
);

#[cfg(all(feature = "icu_config", not(feature = "use-bindgen")))]
compile_error!("Feature `icu_config` is useless without the feature `use-bindgen`");

// This feature combination is not inherrently a problem; we had no use case that
// required it just yet.
#[cfg(all(not(feature = "renaming"), not(feature = "use-bindgen")))]
compile_error!("You must use `renaming` when not using `use-bindgen`");

#[cfg(feature = "use-bindgen")]
include!(concat!(env!("OUT_DIR"), "/macros.rs"));
#[cfg(all(
    feature = "use-bindgen",
    feature = "icu_config",
    not(feature = "icu_version_in_env")
))]
include!(concat!(env!("OUT_DIR"), "/lib.rs"));

#[cfg(not(feature = "use-bindgen"))]
include!("../bindgen/macros.rs");

#[cfg(all(
    not(feature = "use-bindgen"),
    not(feature = "icu_version_in_env"),
    not(feature = "icu_config")
))]
include!("../bindgen/lib.rs");

#[cfg(all(
    not(feature = "use-bindgen"),
    feature = "icu_version_in_env",
    not(feature = "icu_config")
))]
include!(concat!(
    "../bindgen/lib_",
    env!("RUST_ICU_MAJOR_VERSION_NUMBER"),
    ".rs"
));

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

impl From<i8> for UCharCategory {
    fn from(value: i8) -> Self {
        match value {
            0 => UCharCategory::U_UNASSIGNED,
            1 => UCharCategory::U_UPPERCASE_LETTER,
            2 => UCharCategory::U_LOWERCASE_LETTER,
            3 => UCharCategory::U_TITLECASE_LETTER,
            4 => UCharCategory::U_MODIFIER_LETTER,
            5 => UCharCategory::U_OTHER_LETTER,
            6 => UCharCategory::U_NON_SPACING_MARK,
            7 => UCharCategory::U_ENCLOSING_MARK,
            8 => UCharCategory::U_COMBINING_SPACING_MARK,
            9 => UCharCategory::U_DECIMAL_DIGIT_NUMBER,
            10 => UCharCategory::U_LETTER_NUMBER,
            11 => UCharCategory::U_OTHER_NUMBER,
            12 => UCharCategory::U_SPACE_SEPARATOR,
            13 => UCharCategory::U_LINE_SEPARATOR,
            14 => UCharCategory::U_PARAGRAPH_SEPARATOR,
            15 => UCharCategory::U_CONTROL_CHAR,
            16 => UCharCategory::U_FORMAT_CHAR,
            17 => UCharCategory::U_PRIVATE_USE_CHAR,
            18 => UCharCategory::U_SURROGATE,
            19 => UCharCategory::U_DASH_PUNCTUATION,
            20 => UCharCategory::U_START_PUNCTUATION,
            21 => UCharCategory::U_END_PUNCTUATION,
            22 => UCharCategory::U_CONNECTOR_PUNCTUATION,
            23 => UCharCategory::U_OTHER_PUNCTUATION,
            24 => UCharCategory::U_MATH_SYMBOL,
            25 => UCharCategory::U_CURRENCY_SYMBOL,
            26 => UCharCategory::U_MODIFIER_SYMBOL,
            27 => UCharCategory::U_OTHER_SYMBOL,
            28 => UCharCategory::U_INITIAL_PUNCTUATION,
            29 => UCharCategory::U_FINAL_PUNCTUATION,
            30 => UCharCategory::U_CHAR_CATEGORY_COUNT,
            _ => { 
                panic!("could not convert: {}", value);
            }
        }
    }
}

// Items used by the `versioned_function!` macro. Unstable private API; do not use.
#[doc(hidden)]
pub mod __private_do_not_use {
    pub extern crate paste;
}
