// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Commonly used functionality adapters.
//!
//! At the moment, this crate contains the declaration of various errors

use {rust_icu_sys as sys, std::ffi, std::os, thiserror::Error};

/// Represents a Unicode error, resulting from operations of low-level ICU libraries.
///
/// This is modeled after absl::Status in the Abseil library, which provides ways
/// for users to avoid dealing with all the numerous error codes directly.
#[derive(Error, Debug)]
pub enum Error {
    /// The error originating in the underlying sys library.
    ///
    /// At the moment it is possible to produce an Error which has a zero error code (i.e. no
    /// error), because it makes it unnecessary for users to deal with error codes directly.  It
    /// does make for a bit weird API, so we may turn it around a bit.  Ideally, it should not be
    /// possible to have an Error that isn't really an error.
    #[error("ICU error code: {}", _0)]
    Sys(sys::UErrorCode),

    /// Errors originating from the wrapper code.  For example when pre-converting input into
    /// UTF8 for input that happens to be malformed.
    #[error("wrapper error: {}", _0)]
    Wrapper(&'static str),
}

impl Error {
    /// The error code denoting no error has happened.
    pub const OK_CODE: sys::UErrorCode = sys::UErrorCode::U_ZERO_ERROR;

    /// Returns true if this error code corresponds to no error.
    pub fn is_ok(code: sys::UErrorCode) -> bool {
        code == Self::OK_CODE
    }

    /// Creates a new error from the supplied status.  Ok is returned if the error code does not
    /// correspond to an error code (as opposed to OK or a warning code).
    pub fn ok_or_warning(status: sys::UErrorCode) -> Result<(), Self> {
        if Self::is_ok(status) || status < Self::OK_CODE {
            Ok(())
        } else {
            Err(Error::Sys(status))
        }
    }

    /// Creates a new error from the supplied status.  Ok is returned if the
    /// error code does not constitute an error in preflight mode.
    ///
    /// This error check explicitly ignores the buffer overflow error when reporting whether it
    /// contains an error condition.
    ///
    /// Preflight calls to ICU libraries do a dummy scan of the input to determine the buffer sizes
    /// required on the output in case of conversion calls such as `ucal_strFromUTF8`.  The way
    /// this call is made is to offer a zero-capacity buffer (which could be pointed to by a `NULL`
    /// pointer), and then call the respective function.  The function will compute the buffer
    /// size, but will also return a bogus buffer overflow error.
    pub fn ok_preflight(status: sys::UErrorCode) -> Result<(), Self> {
        if status > Self::OK_CODE && status != sys::UErrorCode::U_BUFFER_OVERFLOW_ERROR {
            Err(Error::Sys(status))
        } else {
            Ok(())
        }
    }

    /// An error occurring when a string with interior NUL byte is converted to C string.
    // TODO(fmil): rework common::Error to be more rustful.
    pub fn string_with_interior_nul() -> Self {
        Error::Wrapper("attempted to convert a string with interior NUL byte")
    }

    /// Returns true if this error has the supplied `code`.
    pub fn is_code(&self, code: sys::UErrorCode) -> bool {
        if let Error::Sys(c) = self {
            return *c == code;
        }
        false
    }

    /// Returns true if the error is an error, not a warning.
    ///
    /// The ICU4C library has error codes for errors and warnings.
    pub fn is_err(&self) -> bool {
        match self {
            Error::Sys(code) => *code > sys::UErrorCode::U_ZERO_ERROR,
            Error::Wrapper(_) => true,
        }
    }

    /// Return true if there was an error in a preflight call.
    ///
    /// This error check explicitly ignores the buffer overflow error when reporting whether it
    /// contains an error condition.
    ///
    /// Preflight calls to ICU libraries do a dummy scan of the input to determine the buffer sizes
    /// required on the output in case of conversion calls such as `ucal_strFromUTF8`.  The way
    /// this call is made is to offer a zero-capacity buffer (which could be pointed to by a `NULL`
    /// pointer), and then call the respective function.  The function will compute the buffer
    /// size, but will also return a bogus buffer overflow error.
    pub fn is_preflight_err(&self) -> bool {
        // We may expand the set of error codes that are exempt from error checks in preflight.
        self.is_err() && !self.is_code(sys::UErrorCode::U_BUFFER_OVERFLOW_ERROR)
    }

    /// Returns true if the error is, in fact, a warning (nonfatal).
    pub fn is_warn(&self) -> bool {
        match self {
            Error::Sys(c) => *c < sys::UErrorCode::U_ZERO_ERROR,
            _ => false,
        }
    }
}

/// Used to simulate an array of C-style strings.
#[derive(Debug)]
pub struct CStringVec {
    // The internal representation of the vector of C strings.
    rep: Vec<ffi::CString>,
    // Same as rep, but converted into C pointers.
    c_rep: Vec<*const os::raw::c_char>,
}

impl CStringVec {
    /// Creates a new C string vector from the provided rust strings.
    ///
    /// C strings are continuous byte regions that end in `\0` and do not
    /// contain `\0` anywhere else.
    ///
    /// Use `as_c_array` to get an unowned raw pointer to the array, to pass
    /// into FFI C code.
    pub fn new(strings: &[&str]) -> Result<Self, Error> {
        let mut rep = Vec::with_capacity(strings.len());
        // Convert all to asciiz strings and insert into the vector.
        for elem in strings {
            let asciiz = ffi::CString::new(*elem).map_err(|_| Error::string_with_interior_nul())?;
            rep.push(asciiz);
        }
        let c_rep = rep.iter().map(|e| e.as_ptr()).collect();
        Ok(CStringVec { rep, c_rep })
    }

    /// Returns the underlying array of C strings as a C array pointer.  The
    /// array must not change after construction to ensure that this pointer
    /// remains valid.
    pub fn as_c_array(&self) -> *const *const os::raw::c_char {
        self.c_rep.as_ptr() as *const *const os::raw::c_char
    }

    /// Returns the number of elements in the vector.
    pub fn len(&self) -> usize {
        self.rep.len()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_code() {
        let error = Error::ok_or_warning(sys::UErrorCode::U_BUFFER_OVERFLOW_ERROR).err().unwrap();
        assert!(error.is_code(sys::UErrorCode::U_BUFFER_OVERFLOW_ERROR));
        assert!(!error.is_preflight_err());
        assert!(!error.is_code(sys::UErrorCode::U_ZERO_ERROR));
    }

    #[test]
    fn test_into_char_array() {
        let values = vec!["eenie", "meenie", "minie", "moe"];
        let c_array = CStringVec::new(&values).expect("success");
        assert_eq!(c_array.len(), 4);
    }

    #[test]
    fn test_with_embedded_nul_byte() {
        let values = vec!["hell\0x00o"];
        let _c_array = CStringVec::new(&values).expect_err("should fail");
    }
}
