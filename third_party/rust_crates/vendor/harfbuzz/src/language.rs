// Copyright 2018 The Servo Project Developers. See the COPYRIGHT
// file at the top-level directory of this distribution.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![allow(missing_docs)]

use std;
use sys;

#[derive(Copy, Clone, PartialEq, PartialOrd)]
pub struct Language {
    /// The underlying `hb_language_t` from the `harfbuzz-sys` crate.
    ///
    /// This isn't commonly needed unless interfacing directly with
    /// functions from the `harfbuzz-sys` crate that haven't been
    /// safely exposed.
    raw: sys::hb_language_t,
}

impl Language {
    pub fn from_string(lang: &str) -> Self {
        Language {
            raw: unsafe {
                sys::hb_language_from_string(
                    lang.as_ptr() as *const std::os::raw::c_char,
                    lang.len() as std::os::raw::c_int,
                )
            },
        }
    }

    pub fn to_string(&self) -> &str {
        unsafe { std::ffi::CStr::from_ptr(sys::hb_language_to_string(self.raw)) }
            .to_str()
            .unwrap()
    }

    pub unsafe fn from_raw(raw: sys::hb_language_t) -> Self {
        Language { raw }
    }

    pub fn as_raw(self) -> sys::hb_language_t {
        self.raw
    }

    pub fn get_process_default() -> Self {
        Language {
            raw: unsafe { sys::hb_language_get_default() },
        }
    }

    pub fn is_valid(self) -> bool {
        !self.raw.is_null()
    }
}

impl std::fmt::Debug for Language {
    fn fmt(&self, fmt: &mut std::fmt::Formatter) -> std::fmt::Result {
        fmt.write_str(self.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::Language;

    #[test]
    fn test_lookup() {
        let en = Language::from_string("en_US");
        assert!(en.is_valid());
    }
}
