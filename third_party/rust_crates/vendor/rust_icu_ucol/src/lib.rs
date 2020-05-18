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

//! # ICU collation support for rust
//!
//! This crate provides [collation](https://en.wikipedia.org/wiki/Unicode_collation_algorithm)
//! (locale-sensitive string ordering), based on the collation as implemented by the ICU library.
//! Specifically the functionality exposed through its C API, as available in the [header
//! `ucol.h`](https://unicode-org.github.io/icu-docs/apidoc/released/icu4c/ucol_8h.html).
//!
//! The main type is [UCollator], which can be created using `UCollator::try_from` from a `&str`.
//!
//! A detailed discussion of collation is out of scope of source code documentation.  An interested
//! reader can check out the [collation documentation on the ICU user
//! guide](http://userguide.icu-project.org/collation).
//!
//! Are you missing some features from this crate?  Consider [reporting an
//! issue](https://github.com/google/rust_icu/issues) or even [contributing the
//! functionality](https://github.com/google/rust_icu/pulls).
//!
//! ## Examples
//!
//! Some example code for the use of collation is given below.
//!
//! First off, the more low-level API, which uses [ustring::UChar] is the following, which requires
//! a conversion to [ustring::UChar] prior to use.  This function is mostly used in algorithms that
//! compose Unicode functionality.
//!
//! ```
//! use rust_icu_ustring as ustring;
//! use rust_icu_ucol as ucol;
//! use std::convert::TryFrom;
//! let collator = ucol::UCollator::try_from("sr-Latn").expect("collator");
//! let mut mixed_up = vec!["d", "dž", "đ", "a", "b", "c", "č", "ć"];
//! mixed_up.sort_by(|a, b| {
//!    let first = ustring::UChar::try_from(*a).expect("first");
//!    let second = ustring::UChar::try_from(*b).expect("second");
//!    collator.strcoll(&first, &second)
//! });
//! let alphabet = vec!["a", "b", "c", "č", "ć", "d", "dž", "đ"];
//! assert_eq!(alphabet, mixed_up);
//! ```
//! A more rustful API is [UCollator::strcoll_utf8] which can operate on rust `AsRef<str>` and can
//! be used without converting the input data ahead of time.
//!
//! ```
//! use rust_icu_ustring as ustring;
//! use rust_icu_ucol as ucol;
//! use std::convert::TryFrom;
//! let collator = ucol::UCollator::try_from("sr-Latn").expect("collator");
//! let mut mixed_up = vec!["d", "dž", "đ", "a", "b", "c", "č", "ć"];
//! mixed_up.sort_by(|a, b| collator.strcoll_utf8(a, b).expect("strcoll_utf8"));
//! let alphabet = vec!["a", "b", "c", "č", "ć", "d", "dž", "đ"];
//! assert_eq!(alphabet, mixed_up);
//! ```
use {
    rust_icu_common as common, rust_icu_sys as sys,
    rust_icu_sys::versioned_function,
    rust_icu_sys::*,
    rust_icu_ustring as ustring,
    std::{cmp::Ordering, convert::TryFrom, ffi, ptr},
};

#[derive(Debug)]
pub struct UCollator {
    rep: ptr::NonNull<sys::UCollator>,
}

impl Drop for UCollator {
    /// Releases the resources taken up by a single collator.
    ///
    /// Implements `ucol_close`
    fn drop(&mut self) {
        unsafe { versioned_function!(ucol_close)(self.rep.as_ptr()) };
    }
}

impl TryFrom<&str> for UCollator {
    type Error = common::Error;
    /// Makes a new collator from the supplied locale, e.g. `en-US`, or
    /// `de@collation=phonebook`.
    ///
    /// Other examples:
    ///
    /// * `el-u-kf-upper`
    /// * `el@colCaseFirst=upper`
    ///
    /// Implements ucol_open
    fn try_from(locale: &str) -> Result<UCollator, Self::Error> {
        let locale_cstr = ffi::CString::new(locale)?;
        let mut status = common::Error::OK_CODE;
        // Unsafety note: this is the way to create the collator.  We expect all
        // the passed-in values to be well-formed.
        let rep = unsafe {
            assert!(common::Error::is_ok(status));
            versioned_function!(ucol_open)(locale_cstr.as_ptr(), &mut status) as *mut sys::UCollator
        };
        common::Error::ok_or_warning(status)?;
        Ok(UCollator {
            rep: ptr::NonNull::new(rep).unwrap(),
        })
    }
}

impl UCollator {
    /// Compares strings `first` and `second` according to the collation rules in this collator.
    ///
    /// Returns [Ordering::Less] if `first` compares as less than `second`, and for other return
    /// codes respectively.
    ///
    /// Implements `ucol_strcoll`
    pub fn strcoll(&self, first: &ustring::UChar, second: &ustring::UChar) -> Ordering {
        let result = unsafe {
            assert!(first.len() <= std::i32::MAX as usize);
            assert!(second.len() <= std::i32::MAX as usize);
            versioned_function!(ucol_strcoll)(
                self.rep.as_ptr(),
                first.as_c_ptr(),
                first.len() as i32,
                second.as_c_ptr(),
                second.len() as i32,
            )
        };
        UCollator::to_rust_ordering(result)
    }

    /// Compares strings `first` and `second` according to the collation rules in this collator.
    ///
    /// Returns [Ordering::Less] if `first` compares as less than `second`, and for other return
    /// codes respectively.
    ///
    /// In contrast to [UCollator::strcoll], this function requires no string conversions to
    /// compare two rust strings.
    ///
    /// Implements `ucol_strcoll`
    pub fn strcoll_utf8(
        &self,
        first: impl AsRef<str>,
        second: impl AsRef<str>,
    ) -> Result<Ordering, common::Error> {
        let mut status = common::Error::OK_CODE;
        // Unsafety note:
        // - AsRef is always well formed UTF-8 in rust.
        let result = unsafe {
            assert!(first.as_ref().len() <= std::i32::MAX as usize);
            assert!(second.as_ref().len() <= std::i32::MAX as usize);
            versioned_function!(ucol_strcollUTF8)(
                self.rep.as_ptr(),
                first.as_ref().as_ptr() as *const i8,
                first.as_ref().len() as i32,
                second.as_ref().as_ptr() as *const i8,
                second.as_ref().len() as i32,
                &mut status,
            )
        };
        common::Error::ok_or_warning(status)?;
        Ok(UCollator::to_rust_ordering(result))
    }

    // Converts ICU ordering result type to a Rust ordering result type.
    fn to_rust_ordering(result: sys::UCollationResult) -> Ordering {
        match result {
            sys::UCollationResult::UCOL_LESS => Ordering::Less,
            sys::UCollationResult::UCOL_GREATER => Ordering::Greater,
            sys::UCollationResult::UCOL_EQUAL => Ordering::Equal,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn basic() {
        let _ = crate::UCollator::try_from("de@collation=phonebook").expect("collator created");
    }

    #[test]
    fn strcoll_utf8_test() -> Result<(), common::Error> {
        let collator = crate::UCollator::try_from("sr-Latn")?;
        let mut mixed_up = vec!["d", "dž", "đ", "a", "b", "c", "č", "ć"];
        mixed_up.sort_by(|a, b| collator.strcoll_utf8(a, b).expect("strcoll_utf8"));

        let alphabet = vec!["a", "b", "c", "č", "ć", "d", "dž", "đ"];
        assert_eq!(alphabet, mixed_up);
        Ok(())
    }

    #[test]
    fn strcoll_test() -> Result<(), common::Error> {
        let collator = crate::UCollator::try_from("sr-Latn")?;
        let mut mixed_up = vec!["d", "dž", "đ", "a", "b", "c", "č", "ć"];
        mixed_up.sort_by(|a, b| {
            let first = ustring::UChar::try_from(*a).expect("first");
            let second = ustring::UChar::try_from(*b).expect("second");
            collator.strcoll(&first, &second)
        });

        let alphabet = vec!["a", "b", "c", "č", "ć", "d", "dž", "đ"];
        assert_eq!(alphabet, mixed_up);
        Ok(())
    }
}
