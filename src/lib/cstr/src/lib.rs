// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate contains the [`cstr`] macro for making `&'static CStr` types out of string literals.

#![deny(missing_docs)]

// Re-export libc to be used from the c_char macro
#[doc(hidden)]
pub mod __libc_reexport {
    pub use libc::*;
}

/// Creates a `&'static CStr` from a string literal.
#[macro_export]
macro_rules! cstr {
    ($s:expr) => {
        // `concat` macro always produces a static string literal.
        // It is always safe to create a CStr from a null-terminated string.
        // If there are interior null bytes, the string will just end early.
        unsafe {
            ::std::ffi::CStr::from_ptr::<'static>(
                concat!($s, "\0").as_ptr() as *const $crate::__libc_reexport::c_char
            )
        }
    };
}

#[cfg(test)]
mod tests {
    use {super::cstr, std::ffi};

    #[test]
    fn cstr() {
        let cstring = ffi::CString::new("test string").expect("CString::new failed");
        let cstring_cstr = cstring.as_c_str();
        let cstr = cstr!("test string");

        assert_eq!(cstring_cstr, cstr);
    }

    #[test]
    fn cstr_not_equal() {
        let cstring = ffi::CString::new("test string").expect("CString::new failed");
        let cstring_cstr = cstring.as_c_str();
        let cstr = cstr!("different test string");

        assert_ne!(cstring_cstr, cstr);
    }

    #[test]
    fn cstr_early_null() {
        let cstring = ffi::CString::new("test").expect("CString::new failed");
        let cstring_cstr = cstring.as_c_str();
        let cstr = cstr!("test\0 string");

        assert_eq!(cstring_cstr, cstr);
    }
}
