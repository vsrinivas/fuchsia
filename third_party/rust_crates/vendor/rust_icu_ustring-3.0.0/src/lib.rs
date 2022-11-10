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

//! # Implemuntation of the functions in the ICU4C `ustring.h` header.
//!
//! This is where the UTF-8 strings get converted back and forth to the UChar
//! representation.
//!

use {
    log::trace, rust_icu_common as common, rust_icu_sys as sys, rust_icu_sys::*,
    std::convert::TryFrom, std::os::raw,
};

/// The implementation of the ICU `UChar*`.
///
/// While the original type is defined in `umachine.h`, most useful functions for manipulating
/// `UChar*` are in fact here.
///
/// The first thing you probably want to do is to start from a UTF-8 rust string, produce a UChar.
/// This is necessarily done with a conversion.  See the `TryFrom` implementations in this crate
/// for that.
///
/// Implements `UChar*` from ICU.
#[derive(Debug, Clone)]
pub struct UChar {
    rep: Vec<rust_icu_sys::UChar>,
}

/// Same as `rust_icu_common::buffered_string_method_with_retry`, but for unicode strings.
///
/// Example use:
///
/// Declares an internal function `select_impl` with a templatized type signature, which is then
/// called in subsequent code.
///
/// ```rust ignore
/// pub fn select_ustring(&self, number: f64) -> Result<ustring::UChar, common::Error> {
///     const BUFFER_CAPACITY: usize = 20;
///     buffered_uchar_method_with_retry!(
///         select_impl,
///         BUFFER_CAPACITY,
///         [rep: *const sys::UPluralRules, number: f64,],
///         []
///     );
///
///     select_impl(
///         versioned_function!(uplrules_select),
///         self.rep.as_ptr(),
///         number,
///     )
/// }
/// ```
#[macro_export]
macro_rules! buffered_uchar_method_with_retry {

    ($method_name:ident, $buffer_capacity:expr,
     [$($before_arg:ident: $before_arg_type:ty,)*],
     [$($after_arg:ident: $after_arg_type:ty,)*]) => {
        fn $method_name(
            method_to_call: unsafe extern "C" fn(
                $($before_arg_type,)*
                *mut sys::UChar,
                i32,
                $($after_arg_type,)*
                *mut sys::UErrorCode,
            ) -> i32,
            $($before_arg: $before_arg_type,)*
            $($after_arg: $after_arg_type,)*
        ) -> Result<ustring::UChar, common::Error> {
            let mut status = common::Error::OK_CODE;
            let mut buf: Vec<sys::UChar> = vec![0; $buffer_capacity];

            // Requires that any pointers that are passed in are valid.
            let full_len: i32 = unsafe {
                assert!(common::Error::is_ok(status), "status: {:?}", status);
                method_to_call(
                    $($before_arg,)*
                    buf.as_mut_ptr() as *mut sys::UChar,
                    $buffer_capacity as i32,
                    $($after_arg,)*
                    &mut status,
                )
            };

            // ICU methods are inconsistent in whether they silently truncate the output or treat
            // the overflow as an error, so we need to check both cases.
            if status == sys::UErrorCode::U_BUFFER_OVERFLOW_ERROR ||
               (common::Error::is_ok(status) &&
                    full_len > $buffer_capacity
                        .try_into()
                        .map_err(|e| common::Error::wrapper(e))?) {
                
                status = common::Error::OK_CODE;
                assert!(full_len > 0);
                let full_len: usize = full_len
                    .try_into()
                    .map_err(|e| common::Error::wrapper(e))?;
                buf.resize(full_len, 0);

                // Same unsafe requirements as above, plus full_len must be exactly the output
                // buffer size.
                unsafe {
                    assert!(common::Error::is_ok(status), "status: {:?}", status);
                    method_to_call(
                        $($before_arg,)*
                        buf.as_mut_ptr() as *mut sys::UChar,
                        full_len as i32,
                        $($after_arg,)*
                        &mut status,
                    )
                };
            }

            common::Error::ok_or_warning(status)?;

            // Adjust the size of the buffer here.
            if (full_len >= 0) {
                let full_len: usize = full_len
                    .try_into()
                    .map_err(|e| common::Error::wrapper(e))?;
                buf.resize(full_len, 0);
            }
            Ok(ustring::UChar::from(buf))
        }
    }
}

impl TryFrom<&str> for crate::UChar {
    type Error = common::Error;

    /// Tries to produce a string from the UTF-8 encoded thing.
    ///
    /// This conversion ignores warnings (e.g. warnings about unterminated buffers), since for rust
    /// they are not relevant.
    ///
    /// Implements `u_strFromUTF8`.
    fn try_from(rust_string: &str) -> Result<Self, Self::Error> {
        let mut status = common::Error::OK_CODE;
        let mut dest_length: i32 = 0;
        // Preflight to see how long the buffer should be. See second call below
        // for safety notes.
        //
        // TODO(fmil): Consider having a try_from variant which allocates a buffer
        // of sufficient size instead of running the algorithm twice.
        trace!("utf8->UChar*: {}, {:?}", rust_string.len(), rust_string);
        // Requires that rust_string be a valid C string.
        unsafe {
            assert!(common::Error::is_ok(status));
            versioned_function!(u_strFromUTF8)(
                std::ptr::null_mut(),
                0,
                &mut dest_length,
                rust_string.as_ptr() as *const raw::c_char,
                rust_string.len() as i32,
                &mut status,
            );
        }
        trace!("before error check");
        // We expect buffer overflow error here.  The API is weird, but there you go.
        common::Error::ok_preflight(status)?;
        trace!("input  utf8->UChar*: {:?}", rust_string);
        let mut rep: Vec<sys::UChar> = vec![0; dest_length as usize];
        let mut status = common::Error::OK_CODE;
        // Assumes that rust_string contains a valid rust string.  It is OK for the string to have
        // embedded zero bytes.  Assumes that 'rep' is large enough to hold the entire result.
        unsafe {
            assert!(common::Error::is_ok(status));
            versioned_function!(u_strFromUTF8)(
                rep.as_mut_ptr(),
                rep.len() as i32,
                &mut dest_length,
                rust_string.as_ptr() as *const raw::c_char,
                rust_string.len() as i32,
                &mut status,
            );
        }
        common::Error::ok_or_warning(status)?;
        trace!("result utf8->uchar*[{}]: {:?}", dest_length, rep);
        Ok(crate::UChar { rep })
    }
}

impl TryFrom<&UChar> for String {
    type Error = common::Error;

    /// Tries to produce a UTF-8 encoded rust string from a UChar.
    ///
    /// This conversion ignores warnings and only reports actual ICU errors when
    /// they happen.
    ///
    /// Implements `u_strToUTF8`.
    fn try_from(u: &UChar) -> Result<String, Self::Error> {
        let mut status = common::Error::OK_CODE;
        let mut dest_length: i32 = 0;
        // First probe for required destination length.
        unsafe {
            assert!(common::Error::is_ok(status));
            versioned_function!(u_strToUTF8)(
                std::ptr::null_mut(),
                0,
                &mut dest_length,
                u.rep.as_ptr(),
                u.rep.len() as i32,
                &mut status,
            );
        }
        trace!("preflight UChar*->utf8 buf[{}]", dest_length);

        // The API doesn't really document this well, but the preflight code will report buffer
        // overflow error even when we are explicitly just trying to check for the size of the
        // resulting buffer.
        common::Error::ok_preflight(status)?;

        // Buffer to store the converted string.
        let mut buf: Vec<u8> = vec![0; dest_length as usize];
        trace!("pre:  result UChar*->utf8 buf[{}]: {:?}", buf.len(), buf);
        let mut status = common::Error::OK_CODE;

        // Requires that buf is a buffer with enough capacity to store the
        // resulting string.
        unsafe {
            assert!(common::Error::is_ok(status));
            versioned_function!(u_strToUTF8)(
                buf.as_mut_ptr() as *mut raw::c_char,
                buf.len() as i32,
                &mut dest_length,
                u.rep.as_ptr(),
                u.rep.len() as i32,
                &mut status,
            );
        }
        trace!("post: result UChar*->utf8 buf[{}]: {:?}", buf.len(), buf);
        common::Error::ok_or_warning(status)?;
        let s = String::from_utf8(buf);
        match s {
            Err(e) => Err(e.into()),
            Ok(x) => {
                trace!("result UChar*->utf8: {:?}", x);
                Ok(x)
            }
        }
    }
}

impl From<Vec<sys::UChar>> for crate::UChar {
    /// Adopts a vector of [sys::UChar] into a string.
    fn from(rep: Vec<sys::UChar>) -> crate::UChar {
        crate::UChar { rep }
    }
}

impl crate::UChar {
    /// Allocates a new UChar with given capacity.
    ///
    /// Capacity and size must always be the same with `UChar` when used for interacting with
    /// low-level code.
    pub fn new_with_capacity(capacity: usize) -> crate::UChar {
        let rep: Vec<sys::UChar> = vec![0; capacity];
        crate::UChar::from(rep)
    }

    /// Creates a new [crate::UChar] from its low-level representation, a buffer
    /// pointer and a buffer size.
    ///
    /// Does *not* take ownership of the buffer that was passed in.
    ///
    /// **DO NOT USE UNLESS YOU HAVE NO OTHER CHOICE.**
    ///
    /// # Safety
    ///
    /// `rep` must point to an initialized sequence of at least `len` `UChar`s.
    pub unsafe fn clone_from_raw_parts(rep: *mut sys::UChar, len: i32) -> crate::UChar {
        assert!(len >= 0);
        // Always works for len: i32 >= 0.
        let cap = len as usize;

        // View the deconstructed buffer as a vector of UChars.  Then make a
        // copy of it to return.  This is not efficient, but is always safe.
        let original = Vec::from_raw_parts(rep, cap, cap);
        let copy = original.clone();
        // Don't free the buffer we don't own.
        std::mem::forget(original);
        crate::UChar::from(copy)
    }

    /// Converts into a zeroed-out string.
    ///
    /// This is a very weird ICU API thing, where there apparently exists a zero-terminated
    /// `UChar*`.
    pub fn make_z(&mut self) {
        self.rep.push(0);
    }

    /// Returns the constant pointer to the underlying C representation.
    /// Intended for use in low-level code.
    pub fn as_c_ptr(&self) -> *const rust_icu_sys::UChar {
        self.rep.as_ptr()
    }

    /// Returns the length of the string, in code points.
    pub fn len(&self) -> usize {
        self.rep.len()
    }

    /// Returns whether the string is empty.
    pub fn is_empty(&self) -> bool {
        self.rep.is_empty()
    }

    /// Returns the underlying representation as a mutable C representation.  Caller MUST ensure
    /// that the representation won't be reallocated as result of adding anything to it, and that
    /// it is correctly sized, or bad things will happen.
    pub fn as_mut_c_ptr(&mut self) -> *mut sys::UChar {
        self.rep.as_mut_ptr()
    }

    /// Resizes this string to match new_size.
    ///
    /// If the string is made longer, the new space is filled with zeroes.
    pub fn resize(&mut self, new_size: usize) {
        self.rep.resize(new_size, 0);
    }

    /// Returns the equivalent UTF-8 string, useful for debugging.
    pub fn as_string_debug(&self) -> String {
        String::try_from(self).unwrap()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip_conversion() {
        let samples = vec!["", "Hello world!", "❤  Hello world  ❤"];
        for s in samples.iter() {
            let uchar =
                crate::UChar::try_from(*s).expect(&format!("forward conversion succeeds: {}", s));
            let res =
                String::try_from(&uchar).expect(&format!("back conversion succeeds: {:?}", uchar));
            assert_eq!(*s, res);
        }
    }
}
