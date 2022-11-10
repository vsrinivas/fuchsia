// Copyright 2021 Google LLC
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

//! Contains implementations of functions from ICU's `unorm2.h`.

use {
    rust_icu_common as common,
    rust_icu_sys as sys,
    rust_icu_sys::versioned_function,
    rust_icu_sys::*, 
    rust_icu_ustring as ustring,
    rust_icu_ustring::buffered_uchar_method_with_retry,
};
use std::convert::{TryFrom, TryInto};

#[derive(Debug)]
pub struct UNormalizer {
    rep: std::ptr::NonNull<sys::UNormalizer2>,
    owned: bool,
}

impl Drop for UNormalizer {
    /// Implements `unorm2_close`
    fn drop(&mut self) {
        // Close the normalizer only if we own it.
        if !self.owned {
            return
        }
        unsafe {
            versioned_function!(unorm2_close)(self.rep.as_ptr())
        }
    }
}

impl UNormalizer {
    /// Implements `unorm2_getNFCInstance`.
    pub fn new_nfc() -> Result<Self, common::Error> {
        unsafe { UNormalizer::new_normalizer_unowned(versioned_function!(unorm2_getNFCInstance)) }
    }

    /// Implements `unorm2_getNFDInstance`.
    pub fn new_nfd() -> Result<Self, common::Error> {
        unsafe { UNormalizer::new_normalizer_unowned(versioned_function!(unorm2_getNFDInstance)) }
    }

    /// Implements `unorm2_getNFKCInstance`.
    pub fn new_nfkc() -> Result<Self, common::Error> {
        unsafe { UNormalizer::new_normalizer_unowned(versioned_function!(unorm2_getNFKCInstance)) }
    }

    /// Implements `unorm2_getNFKDInstance`.
    pub fn new_nfkd() -> Result<Self, common::Error> {
        unsafe { UNormalizer::new_normalizer_unowned(versioned_function!(unorm2_getNFKDInstance)) }
    }

    /// Implements `unorm2_getNFKCCasefoldInstance`.
    pub fn new_nfkc_casefold() -> Result<Self, common::Error> {
        unsafe { UNormalizer::new_normalizer_unowned(versioned_function!(unorm2_getNFKCCasefoldInstance)) }
    }

    unsafe fn new_normalizer_unowned(
        constrfn: unsafe extern "C" fn(*mut sys::UErrorCode) -> *const sys::UNormalizer2) -> Result<Self, common::Error> {
        let mut status = common::Error::OK_CODE;
        let rep = {
            assert!(common::Error::is_ok(status));
            let ptr = constrfn(&mut status) as *mut sys::UNormalizer2;
            std::ptr::NonNull::new_unchecked(ptr)
        };
        common::Error::ok_or_warning(status)?;
        Ok(UNormalizer{ rep, owned: false })
    }

    /// Implements `unorm2_normalize`.
    pub fn normalize(&self, norm: &str) -> Result<String, common::Error> {
        let norm = ustring::UChar::try_from(norm)?;
        let result = self.normalize_ustring(&norm)?;
        String::try_from(&result)
    }

    /// Implements `unorm2_normalize`.
    pub fn normalize_ustring(
        &self,
        norm: &ustring::UChar
        ) -> Result<ustring::UChar, common::Error> {
        const CAPACITY: usize = 200;
        buffered_uchar_method_with_retry!(
            norm_uchar,
            CAPACITY,
            [ptr: *const sys::UNormalizer2, s: *const sys::UChar, l: i32,],
            []
        );
        let result = norm_uchar(
            versioned_function!(unorm2_normalize),
            self.rep.as_ptr(),
            norm.as_c_ptr(),
            norm.len() as i32,
            )?;
        Ok(result)
    }

    /// Implements `unorm2_composePair`.
    pub fn compose_pair(&self, point1: sys::UChar32, point2: sys::UChar32) -> sys::UChar32 {
        let result: sys::UChar32 = unsafe {
            versioned_function!(unorm2_composePair)(
                self.rep.as_ptr(), point1, point2)
        };
        result
    }

}

#[cfg(test)]
mod tests {
    use super::*;
    use rust_icu_ustring::UChar;

    #[test]
    fn test_compose_pair_nfkc() -> Result<(), common::Error> {
        struct Test {
            p1: sys::UChar32,
            p2: sys::UChar32,
            ex: sys::UChar32,
        }
        let tests = vec![
            Test {p1: 1, p2: 0, ex: -1, },
            // See the article: https://en.wikipedia.org/wiki/Combining_character
            // LATIN CAPITAL LETTER A WITH GRAVE
            Test {p2: 0x300, p1: 'A' as sys::UChar32, ex: 'À' as sys::UChar32 },
            // LATIN CAPITAL LETTER A WITH ACUTE
            Test {p2: 0x301, p1: 'A' as sys::UChar32, ex: 'Á' as sys::UChar32 },
            // LATIN CAPITAL LETTER A WITH CIRCUMFLEX
            Test {p2: 0x302, p1: 'A' as sys::UChar32, ex: 'Â' as sys::UChar32 },
            // LATIN CAPITAL LETTER A WITH TILDE
            Test {p2: 0x303, p1: 'A' as sys::UChar32, ex: 'Ã' as sys::UChar32 },
        ];

        for t in tests {
            let n = UNormalizer::new_nfkc()?;
            let result = n.compose_pair(t.p1, t.p2);
            assert_eq!(result, t.ex);
        }
        Ok(())
    }

    // https://github.com/google/rust_icu/issues/244
    #[test]
    fn test_long_input_string() -> Result<(), common::Error> {
        let s = (0..67).map(|_| "탐").collect::<String>();
        let u = UChar::try_from(&s[..]).unwrap();
        let normalizer = UNormalizer::new_nfd().unwrap();
        normalizer.normalize_ustring(&u).unwrap();

        Ok(())
    }
}
