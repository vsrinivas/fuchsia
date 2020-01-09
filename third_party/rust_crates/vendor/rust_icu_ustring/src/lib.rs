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

//! # Implementation of the functions in the ICU4C `ustring.h` header.
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
#[derive(Debug)]
pub struct UChar {
    rep: Vec<rust_icu_sys::UChar>,
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
                0 as *mut sys::UChar,
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
                0 as *mut raw::c_char,
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
            Err(_) => Err(common::Error::wrapper("could not convert to utf8")),
            Ok(x) => {
                trace!("result UChar*->utf8: {:?}", x);
                Ok(x)
            }
        }
    }
}

impl crate::UChar {
    /// Allocates a new UChar with given capacity.
    ///
    /// Capacity and size must always be the same with `UChar` when used for interacting with
    /// low-level code.
    pub fn new_with_capacity(capacity: usize) -> crate::UChar {
        let rep: Vec<sys::UChar> = vec![0; capacity];
        crate::UChar { rep: rep }
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
