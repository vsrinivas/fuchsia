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

//! # Commonly used functionality adapters.
//!
//! At the moment, this crate contains the declaration of various errors

use {
    anyhow::anyhow,
    rust_icu_sys as sys,
    std::{ffi, os, convert::TryInto},
    thiserror::Error,
};

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
    #[error(transparent)]
    Wrapper(#[from] anyhow::Error),
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
    /// Preflight calls to ICU libraries do a read-only scan of the input to determine the buffer
    /// sizes required on the output in case of conversion calls such as `ucal_strFromUTF8`.  The
    /// way this call is made is to offer a zero-capacity buffer (which could be pointed to by a
    /// `NULL` pointer), and then call the respective function.  The function will compute the
    /// buffer size, but will also return a bogus buffer overflow error.
    pub fn ok_preflight(status: sys::UErrorCode) -> Result<(), Self> {
        if status > Self::OK_CODE && status != sys::UErrorCode::U_BUFFER_OVERFLOW_ERROR {
            Err(Error::Sys(status))
        } else {
            Ok(())
        }
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
    /// Preflight calls to ICU libraries do a read-only scan of the input to determine the buffer
    /// sizes required on the output in case of conversion calls such as `ucal_strFromUTF8`.  The
    /// way this call is made is to offer a zero-capacity buffer (which could be pointed to by a
    /// `NULL` pointer), and then call the respective function.  The function will compute the
    /// buffer size, but will also return a bogus buffer overflow error.
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

    pub fn wrapper(source: impl Into<anyhow::Error>) -> Self {
        Self::Wrapper(source.into())
    }
}

impl From<ffi::NulError> for Error {
    fn from(e: ffi::NulError) -> Self {
        Self::wrapper(e)
    }
}

impl From<std::str::Utf8Error> for Error {
    fn from(e: std::str::Utf8Error) -> Self {
        Self::wrapper(e)
    }
}

impl From<std::string::FromUtf8Error> for Error {
    fn from(e: std::string::FromUtf8Error) -> Self {
        Self::wrapper(e)
    }
}

impl Into<std::fmt::Error> for Error {
    fn into(self) -> std::fmt::Error {
        // It is not possible to transfer any info into std::fmt::Error, so we log instead.
        eprintln!("error while formatting: {:?}", &self);
        std::fmt::Error {}
    }
}

/// `type_name` is the type to implement drop for.
/// `impl_function_name` is the name of the function that implements
/// memory deallocation.  It is assumed that the type has an internal
/// representation wrapped in a [std::ptr::NonNull].
///
/// Example:
///
/// ```rust ignore
/// pub struct UNumberFormatter {
///   rep: std::ptr::NonNull<Foo>,
/// }
/// //...
/// simple_drop_impl!(UNumberFormatter, unumf_close);
/// ```
#[macro_export]
macro_rules! simple_drop_impl {
    ($type_name:ty, $impl_function_name:ident) => {
        impl $crate::__private_do_not_use::Drop for $type_name {
            #[doc = concat!("Implements `", stringify!($impl_function_name), "`.")]
            fn drop(&mut self) {
                unsafe {
                    $crate::__private_do_not_use::versioned_function!($impl_function_name)
                        (self.rep.as_ptr());
                }
            }
        }
    };
}

/// Helper for calling ICU4C `uloc` methods that require a resizable output string buffer.
pub fn buffered_string_method_with_retry<F>(
    mut method_to_call: F,
    buffer_capacity: usize,
) -> Result<String, Error>
where
    F: FnMut(*mut os::raw::c_char, i32, *mut sys::UErrorCode) -> i32,
{
    let mut status = Error::OK_CODE;
    let mut buf: Vec<u8> = vec![0; buffer_capacity];

    // Requires that any pointers that are passed in are valid.
    let full_len: i32 = {
        assert!(Error::is_ok(status));
        method_to_call(
            buf.as_mut_ptr() as *mut os::raw::c_char,
            buffer_capacity as i32,
            &mut status,
        )
    };

    // ICU methods are inconsistent in whether they silently truncate the output or treat
    // the overflow as an error, so we need to check both cases.
    if status == sys::UErrorCode::U_BUFFER_OVERFLOW_ERROR ||
       (Error::is_ok(status) &&
            full_len > buffer_capacity
                .try_into()
                .map_err(|e| Error::wrapper(e))?) {

        status = Error::OK_CODE;
        assert!(full_len > 0);
        let full_len: usize = full_len
            .try_into()
            .map_err(|e| Error::wrapper(e))?;
        buf.resize(full_len, 0);

        // Same unsafe requirements as above, plus full_len must be exactly the output
        // buffer size.
        {
            assert!(Error::is_ok(status));
            method_to_call(
                buf.as_mut_ptr() as *mut os::raw::c_char,
                full_len as i32,
                &mut status,
            )
        };
    }

    Error::ok_or_warning(status)?;

    // Adjust the size of the buffer here.
    if full_len >= 0 {
        let full_len: usize = full_len
            .try_into()
            .map_err(|e| Error::wrapper(e))?;
        buf.resize(full_len, 0);
    }
    String::from_utf8(buf).map_err(|e| e.utf8_error().into())
}

/// There is a slew of near-identical method calls which differ in the type of
/// the input argument and the name of the function to invoke.
///
/// The invocation:
///
/// ```rust ignore
/// impl ... {
///   // ...
///   format_ustring_for_type!(format_f64, unum_formatDouble, f64);
/// }
/// ```
///
/// allows us to bind the function:
///
/// ```c++ ignore
/// int32_t unum_formatDouble(
///     const UNumberFormat* fmt,
///     double number,
///     UChar* result,
///     int32_t result_length,
///     UFieldPosition* pos,
///     UErrorCode *status)
/// ```
///
/// as:
///
/// ```rust ignore
/// impl ... {
///   format_f64(&self /* format */, value: f64) -> Result<ustring::UChar, common::Error>;
/// }
/// ```
#[macro_export]
macro_rules! format_ustring_for_type{
    ($method_name:ident, $function_name:ident, $type_decl:ty) => (
        #[doc = concat!("Implements `", stringify!($function_name), "`.")]
        pub fn $method_name(&self, number: $type_decl) -> Result<String, common::Error> {
            let result = paste::item! {
                self. [< $method_name _ustring>] (number)?
            };
            String::try_from(&result)
        }

        // Should be able to use https://github.com/google/rust_icu/pull/144 to
        // make this even shorter.
        paste::item! {
            #[doc = concat!("Implements `", stringify!($function_name), "`.")]
            pub fn [<$method_name _ustring>] (&self, param: $type_decl) -> Result<ustring::UChar, common::Error> {
                const CAPACITY: usize = 200;
                buffered_uchar_method_with_retry!(
                    [< $method_name _ustring_impl >],
                    CAPACITY,
                    [ rep: *const sys::UNumberFormat, param: $type_decl, ],
                    [ field: *mut sys::UFieldPosition, ]
                    );

                [<$method_name _ustring_impl>](
                    versioned_function!($function_name),
                    self.rep.as_ptr(),
                    param,
                    // The field position is unused for now.
                    0 as *mut sys::UFieldPosition,
                    )
            }
        }
    )
}

/// Expands into a getter method that forwards all its arguments and returns a fallible value which
/// is the same as the value returned by the underlying function.
///
/// The invocation:
///
/// ```rust ignore
/// impl _ {
///     generalized_fallible_getter!(
///         get_context,
///         unum_getContext,
///         [context_type: sys::UDisplayContextType, ],
///         sys::UDisplayContext
///     );
/// }
/// ```
///
/// allows us to bind the function:
///
/// ```c++ ignore
/// UDisplayContext unum_getContext(
///     const SOMETYPE* t,
///     UDisplayContextType type,
///     UErrorCode* status
/// );
/// ```
///
/// which then becomes:
///
/// ```rust ignore
/// impl _ {
///   fn get_context(&self, context_type: sys::UDisplayContextType) -> Result<sys::UDisplayContext, common::Error>;
/// }
/// ```
/// where `Self` has an internal representation named exactly `Self::rep`.
#[macro_export]
macro_rules! generalized_fallible_getter{
    ($top_level_method_name:ident, $impl_name:ident, [ $( $arg:ident: $arg_type:ty ,)* ],  $ret_type:ty) => (
        #[doc = concat!("Implements `", stringify!($impl_name), "`.")]
        pub fn $top_level_method_name(&self, $( $arg: $arg_type, )* ) -> Result<$ret_type, common::Error> {
            let mut status = common::Error::OK_CODE;
            let result: $ret_type = unsafe {
                assert!(common::Error::is_ok(status));
                versioned_function!($impl_name)(self.rep.as_ptr(), $( $arg, )* &mut status)
            };
            common::Error::ok_or_warning(status)?;
            Ok(result)
        }
    )
}

/// Expands into a setter methods that forwards all its arguments between []'s and returns a
/// Result<(), common::Error>.
///
/// The invocation:
///
/// ```rust ignore
/// impl _ {
///     generalized_fallible_setter!(
///         get_context,
///         unum_getContext,
///         [context_type: sys::UDisplayContextType, ]
///     );
/// }
/// ```
///
/// allows us to bind the function:
///
/// ```c++ ignore
/// UDisplayContext unum_setContext(
///     const SOMETYPE* t,
///     UDisplayContext value,
///     UErrorCode* status
/// );
/// ```
///
/// which then becomes:
///
/// ```rust ignore
/// impl _ {
///   fn set_context(&self, value: sys::UDisplayContext) -> Result<(), common::Error>;
/// }
/// ```
/// where `Self` has an internal representation named exactly `Self::rep`.
#[macro_export]
macro_rules! generalized_fallible_setter{
    ($top_level_method_name:ident, $impl_name:ident, [ $( $arg:ident : $arg_type:ty, )* ]) => (
        generalized_fallible_getter!(
            $top_level_method_name,
            $impl_name,
            [ $( $arg: $arg_type, )* ],
            ());
    )
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
            let asciiz = ffi::CString::new(*elem)?;
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

    /// Returns whether the vector is empty.
    pub fn is_empty(&self) -> bool {
        self.rep.is_empty()
    }
}

// Items used by macros. Unstable private API; do not use.
#[doc(hidden)]
pub mod __private_do_not_use {
    pub use Drop;
    pub use rust_icu_sys::versioned_function;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_error_code() {
        let error = Error::ok_or_warning(sys::UErrorCode::U_BUFFER_OVERFLOW_ERROR)
            .err()
            .unwrap();
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

    #[test]
    fn test_parser_error_ok() {
        let tests = vec![
            sys::UParseError {
                line: 0,
                offset: 0,
                preContext: [0; 16usize],
                postContext: [0; 16usize],
            },
            sys::UParseError {
                line: -1,
                offset: 0,
                preContext: [0; 16usize],
                postContext: [0; 16usize],
            },
            sys::UParseError {
                line: 0,
                offset: -1,
                preContext: [0; 16usize],
                postContext: [0; 16usize],
            },
        ];
        for test in tests {
            assert!(parse_ok(test).is_ok(), "for test: {:?}", test.clone());
        }
    }

    #[test]
    fn test_parser_error_not_ok() {
        let tests = vec![
            sys::UParseError {
                line: 1,
                offset: 0,
                preContext: [0; 16usize],
                postContext: [0; 16usize],
            },
            sys::UParseError {
                line: 0,
                offset: 1,
                preContext: [0; 16usize],
                postContext: [0; 16usize],
            },
            sys::UParseError {
                line: -1,
                offset: 1,
                preContext: [0; 16usize],
                postContext: [0; 16usize],
            },
        ];
        for test in tests {
            assert!(parse_ok(test).is_err(), "for test: {:?}", test.clone());
        }
    }
}

/// A zero-value parse error, used to initialize types that get passed into FFI code.
pub static NO_PARSE_ERROR: sys::UParseError = sys::UParseError {
    line: 0,
    offset: 0,
    preContext: [0; 16usize],
    postContext: [0; 16usize],
};

/// Converts a parse error to a Result.
///
/// A parse error is an error if line or offset are positive, apparently.
pub fn parse_ok(e: sys::UParseError) -> Result<(), crate::Error> {
    if e.line > 0 || e.offset > 0 {
        return Err(Error::Wrapper(anyhow!(
            "parse error: line: {}, offset: {}",
            e.line,
            e.offset
        )));
    }
    Ok(())
}

