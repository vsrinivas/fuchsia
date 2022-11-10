// Copyright 2020 Google LLC
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

//! # ICU unicode character properties support
//!
//! NOTE: Only very partially supported.  However, it is easy to add new
//! functionality, so if you want you can do that yourself, or you can report
//! missing functionality at <https://github.com/google/rust_icu/issues>.
//!
//! Since 1.0.2

use {
    rust_icu_common as common,
    rust_icu_sys as sys,
    rust_icu_sys::versioned_function,
    rust_icu_sys::*,
    std::ffi,
};

/// Implements `u_charFromName`.
///
/// Since 1.0.2
pub fn from_name(name_choice: sys::UCharNameChoice, name: &str) 
    -> Result<sys::UChar32, common::Error> {
    let mut status = common::Error::OK_CODE;
    let asciiz = ffi::CString::new(name)?;
    let result = unsafe {
        assert!(common::Error::is_ok(status));
        versioned_function!(u_charFromName)(name_choice, asciiz.as_ptr(), &mut status) 
    };
    common::Error::ok_or_warning(status)?;
    Ok(result)
}

/// Implements `u_charType`.
///
/// Since 1.0.2
pub fn char_type(c: sys::UChar32) -> sys::UCharCategory {
    let result = unsafe { versioned_function!(u_charType)(c) };
    result.into()
}

/// See <http://www.unicode.org/reports/tr44/#Canonical_Combining_Class_Values> for
/// the list of combining class values.
///
/// Implements `u_getCombiningClass`
///
/// Since 1.0.2
pub fn get_combining_class(c: sys::UChar32) -> u8 {
    unsafe { versioned_function!(u_getCombiningClass)(c) }
}

#[cfg(test)]
mod tests {

    use super::*;

    #[test]
    fn test_from_name() {
        struct Test {
            name_choice: sys::UCharNameChoice,
            name: &'static str,
            expected: sys::UChar32,

        }
        let tests = vec![
            Test {
                name_choice: sys::UCharNameChoice::U_UNICODE_CHAR_NAME,
                name: "LATIN CAPITAL LETTER A",
                expected: 'A' as sys::UChar32,
            },
            Test {
                name_choice: sys::UCharNameChoice::U_UNICODE_CHAR_NAME,
                name: "LATIN CAPITAL LETTER B",
                expected: 'B' as sys::UChar32,
            },
            Test {
                name_choice: sys::UCharNameChoice::U_UNICODE_CHAR_NAME,
                name: "LATIN SMALL LETTER C",
                expected: 'c' as sys::UChar32,
            },
            Test {
                name_choice: sys::UCharNameChoice::U_UNICODE_CHAR_NAME,
                name: "CJK RADICAL BOX",
                expected: '⺆' as sys::UChar32,
            },
        ];
        for test in tests {
            let result = from_name(test.name_choice, test.name).unwrap();
            assert_eq!(result, test.expected);
        }
    }

    #[test]
    fn test_char_type() {
        struct Test {
            ch: sys::UChar32,
            cat: sys::UCharCategory,
        }
        let tests = vec![
            Test {
                ch: 'A' as sys::UChar32,
                cat: sys::UCharCategory::U_UPPERCASE_LETTER,
            },
            Test {
                ch: 0x300, // A combining character.
                cat: sys::UCharCategory::U_NON_SPACING_MARK,
            },
        ];
        for test in tests {
            let result = char_type(test.ch);
            assert_eq!(result, test.cat);
        }
    }

    #[test]
    fn test_combining_class() {
        #[derive(Debug)]
        struct Test {
            ch: sys::UChar32,
            class: u8,
        }
        let tests = vec![
            Test {
                ch: 'A' as sys::UChar32,
                class: 0,
            },
            Test {
                ch: 0x300 as sys::UChar32,
                class: 230,
            },
            Test {
                ch: 0x301 as sys::UChar32,
                class: 230,
            },
        ];
        for test in tests {
            let result = get_combining_class(test.ch);
            assert_eq!(result, test.class, "test: {:?}", test);
        }
    }
}
