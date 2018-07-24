// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The BoringSSL API.
//!
//! This module provides a safe access to the BoringSSL API.
//!
//! It accomplishes this using the following structure:
//! - The internal `raw` module provides nearly-raw access to the BoringSSL API.
//!   For each function in the BoringSSL API, it exposes an equivalent Rust
//!   function which performs error checking. Functions which return pointers
//!   return `Result<NonNull<T>, BoringError>`, functions which return status
//!   codes return `Result<(), BoringError>`, etc. This API makes it less likely
//!   to accidentally forget to check for null pointers or error status codes.
//! - The internal `wrapper` module provides types which wrap C objects and
//!   handle many of the details of their lifecycles. These include
//!   `CStackWrapper`, which handles initializing and destructing
//!   stack-allocated C objects; `CHeapWrapper`, which is analogous to Rust's
//!   `Box` or `Rc`, and handles allocation, reference counting, and freeing;
//!   and `CRef`, which is analogous to a Rust reference.
//! - This module builds on top of the `raw` and `wrapper` modules to provide a
//!   safe API. This allows us to `#![deny(unsafe_code)]` in the rest of the
//!   crate, which in turn means that this is the only module whose memory
//!   safety needs to be manually verified.
//!
//! # Usage
//!
//! Each type, `T`, from the BoringSSL API is exposed as either a
//! `CStackWrapper<T>`, a `CHeapWrapper<T>`, or a `CRef<T>`. Each function from
//! the BoringSSL API which operates on a particular type is exposed as a method
//! on the wrapped version of that type. For example, the BoringSSL `CBS_len`
//! function operates on a `CBS`; we provide the `cbs_len` method on the
//! `CStackWrapper<CBS>` type. While BoringSSL functions that operate on a
//! particular type take the form `TYPE_method`, the Rust equivalents are all
//! lower-case - `type_method`.
//!
//! Some functions which do not make sense as methods are exposed as bare
//! functions. For example, the BoringSSL `ECDSA_sign` function is exposed as a
//! bare function as `ecdsa_sign`.
//!
//! Types which can be constructed without arguments implement `Default`. Types
//! which require arguments to be constructed provide associated functions which
//! take those arguments and return a new instance of that type. For example,
//! the `CHeapWrapper<EC_KEY>::ec_key_parse_private_key` function parses a
//! private key from an input stream and returns a new `CHeapWrapper<EC_KEY>`.

#![allow(unsafe_code)]

#[macro_use]
mod wrapper;
mod raw;

// C types
pub use boringssl_sys::{CBB, CBS, EC_GROUP, EC_KEY, EVP_PKEY};
// C constants
pub use boringssl_sys::{NID_X9_62_prime256v1, NID_secp384r1, NID_secp521r1};
// wrapper types
pub use boringssl::wrapper::{CHeapWrapper, CRef, CStackWrapper};

use std::ffi::CStr;
use std::fmt::{self, Debug, Display, Formatter};
use std::num::NonZeroUsize;
use std::os::raw::{c_char, c_int, c_uint, c_void};
use std::{mem, ptr, slice};

use boringssl::raw::{CBB_data, CBB_init, CBB_len, CBS_init, CBS_len, ECDSA_sign, ECDSA_size,
                     ECDSA_verify, EC_GROUP_get_curve_name, EC_GROUP_new_by_curve_name,
                     EC_KEY_generate_key, EC_KEY_get0_group, EC_KEY_marshal_private_key,
                     EC_KEY_parse_private_key, EC_KEY_set_group, EC_curve_nid2nist,
                     ERR_print_errors_cb, EVP_PKEY_assign_EC_KEY, EVP_PKEY_get1_EC_KEY,
                     EVP_marshal_public_key, EVP_parse_public_key};

impl CStackWrapper<CBB> {
    /// Creates a new `CBB` and initializes it with `CBB_init`.
    #[must_use]
    pub fn cbb_new(initial_capacity: usize) -> Result<CStackWrapper<CBB>, BoringError> {
        unsafe {
            let mut cbb = mem::uninitialized();
            CBB_init(&mut cbb, initial_capacity)?;
            Ok(CStackWrapper::new(cbb))
        }
    }

    /// Invokes a callback on the contents of a `CBB`.
    ///
    /// `cbb_with_data` accepts a callback, and invokes that callback, passing a
    /// slice of the current contents of this `CBB`.
    #[must_use]
    pub fn cbb_with_data<O, F: Fn(&[u8]) -> O>(&self, with_data: F) -> Result<O, BoringError> {
        unsafe {
            // NOTE: The return value of CBB_data is only valid until the next
            // operation on the CBB. (we're pretty sure that doesn't include
            // CBB_len, but just in case, we put them in this order). This
            // method is safe because the slice reference cannot outlive this
            // function body, and thus cannot live beyond another method call
            // that could invalidate the buffer.
            let len = CBB_len(self.as_const());
            let ptr = CBB_data(self.as_const())?;
            // TODO(joshlf): Can with_data use this to smuggle out the
            // reference, outliving the lifetime of self?
            Ok(with_data(slice::from_raw_parts(ptr.as_ptr(), len)))
        }
    }
}

impl CStackWrapper<CBS> {
    /// The `CBS_len` function.
    pub fn cbs_len(&self) -> usize {
        unsafe { CBS_len(self.as_const()) }
    }

    /// Invokes a callback on a temporary `CBS`.
    ///
    /// `cbs_with_temp_buffer` constructs a `CBS` from the provided byte slice,
    /// and invokes a callback on the `CBS`. The `CBS` is destructed before
    /// `cbs_with_temp_buffer` returns.
    // TODO(joshlf): Holdover until we figure out how to put lifetimes in CStackWrappers.
    #[must_use]
    pub fn cbs_with_temp_buffer<O, F: Fn(&mut CStackWrapper<CBS>) -> O>(
        bytes: &[u8], with_cbs: F,
    ) -> O {
        unsafe {
            let mut cbs = mem::uninitialized();
            CBS_init(&mut cbs, slice_to_const(bytes), bytes.len());
            let mut cbs = CStackWrapper::new(cbs);
            with_cbs(&mut cbs)
        }
    }
}

impl CRef<'static, EC_GROUP> {
    /// The `EC_GROUP_new_by_curve_name` function.
    #[must_use]
    pub fn ec_group_new_by_curve_name(nid: c_int) -> Result<CRef<'static, EC_GROUP>, BoringError> {
        unsafe { Ok(CRef::new(EC_GROUP_new_by_curve_name(nid)?)) }
    }
}

impl<'a> CRef<'a, EC_GROUP> {
    /// The `EC_GROUP_get_curve_name` function.
    #[must_use]
    pub fn ec_group_get_curve_name(&self) -> c_int {
        unsafe { EC_GROUP_get_curve_name(self.as_const()) }
    }
}

/// The `EC_curve_nid2nist` function.
#[must_use]
pub fn ec_curve_nid2nist(nid: c_int) -> Result<&'static CStr, BoringError> {
    unsafe { Ok(CStr::from_ptr(EC_curve_nid2nist(nid)?.as_ptr())) }
}

impl CHeapWrapper<EC_KEY> {
    /// The `EC_KEY_generate_key` function.
    #[must_use]
    pub fn ec_key_generate_key(&mut self) -> Result<(), BoringError> {
        unsafe { EC_KEY_generate_key(self.as_mut()) }
    }

    /// The `EC_KEY_parse_private_key` function.
    ///
    /// If `group` is `None`, then the group pointer argument to
    /// `EC_KEY_parse_private_key` will be NULL.
    #[must_use]
    pub fn ec_key_parse_private_key(
        cbs: &mut CStackWrapper<CBS>, group: Option<CRef<'static, EC_GROUP>>,
    ) -> Result<CHeapWrapper<EC_KEY>, BoringError> {
        unsafe {
            Ok(CHeapWrapper::new_from(EC_KEY_parse_private_key(
                cbs.as_mut(),
                group.map(|g| g.as_const()).unwrap_or(ptr::null()),
            )?))
        }
    }

    /// The `EC_KEY_get0_group` function.
    #[must_use]
    #[allow(clippy::needless_lifetimes)] // to be more explicit
    pub fn ec_key_get0_group<'a>(&'a self) -> Result<CRef<'a, EC_GROUP>, BoringError> {
        // get0 doesn't increment the refcount; the lifetimes ensure that the
        // returned CRef can't outlive self
        unsafe { Ok(CRef::new(EC_KEY_get0_group(self.as_const())?)) }
    }

    /// The `EC_KEY_set_group` function.
    #[must_use]
    pub fn ec_key_set_group(&mut self, group: &CRef<'static, EC_GROUP>) -> Result<(), BoringError> {
        unsafe { EC_KEY_set_group(self.as_mut(), group.as_const()) }
    }

    /// The `EC_KEY_marshal_private_key` function.
    #[must_use]
    pub fn ec_key_marshal_private_key(
        &self, cbb: &mut CStackWrapper<CBB>,
    ) -> Result<(), BoringError> {
        unsafe { EC_KEY_marshal_private_key(cbb.as_mut(), self.as_const(), 0) }
    }
}

/// The `ECDSA_sign` function.
///
/// `ecdsa_sign` returns the number of bytes written to `sig`.
///
/// # Panics
///
/// `ecdsa_sign` panics if `sig` is shorter than the minimum required signature
/// size given by `ecdsa_size`.
#[must_use]
pub fn ecdsa_sign(
    digest: &[u8], sig: &mut [u8], key: &CHeapWrapper<EC_KEY>,
) -> Result<usize, BoringError> {
    unsafe {
        // If we call ECDSA_sign with sig.len() < min_size, it will invoke UB.
        let min_size = ecdsa_size(key)?;
        assert!(sig.len() >= min_size.get());

        let mut sig_len: c_uint = 0;
        ECDSA_sign(
            0,
            slice_to_const(digest),
            digest.len(),
            slice_to_mut(sig),
            &mut sig_len,
            key.as_const(),
        )?;
        assert!(sig_len as usize <= min_size.get());
        Ok(sig_len as usize)
    }
}

/// The `ECDSA_verify` function.
#[must_use]
pub fn ecdsa_verify(digest: &[u8], sig: &[u8], key: &CHeapWrapper<EC_KEY>) -> bool {
    unsafe {
        ECDSA_verify(
            0,
            slice_to_const(digest),
            digest.len(),
            slice_to_const(sig),
            sig.len(),
            key.as_const(),
        )
    }
}

/// The `ECDSA_size` function.
#[must_use]
pub fn ecdsa_size(key: &CHeapWrapper<EC_KEY>) -> Result<NonZeroUsize, BoringError> {
    unsafe { ECDSA_size(key.as_const()) }
}

impl CHeapWrapper<EVP_PKEY> {
    /// The `EVP_parse_public_key` function.
    #[must_use]
    pub fn evp_parse_public_key(
        cbs: &mut CStackWrapper<CBS>,
    ) -> Result<CHeapWrapper<EVP_PKEY>, BoringError> {
        unsafe { Ok(CHeapWrapper::new_from(EVP_parse_public_key(cbs.as_mut())?)) }
    }

    /// The `EVP_marshal_public_key` function.
    #[must_use]
    pub fn evp_marshal_public_key(&self, cbb: &mut CStackWrapper<CBB>) -> Result<(), BoringError> {
        unsafe { EVP_marshal_public_key(cbb.as_mut(), self.as_const()) }
    }

    /// The `EVP_PKEY_assign_EC_KEY` function.
    #[must_use]
    pub fn evp_pkey_assign_ec_key(
        &mut self, ec_key: CHeapWrapper<EC_KEY>,
    ) -> Result<(), BoringError> {
        unsafe {
            // NOTE: It's very important that we use 'into_mut' here so that
            // ec_key's refcount is not decremented. That's because
            // EVP_PKEY_assign_EC_KEY doesn't increment the refcount of its
            // argument.
            let key = ec_key.into_mut();
            EVP_PKEY_assign_EC_KEY(self.as_mut(), key)
        }
    }

    /// The `EVP_PKEY_get1_EC_KEY` function
    #[must_use]
    pub fn evp_pkey_get1_ec_key(&mut self) -> Result<CHeapWrapper<EC_KEY>, BoringError> {
        // NOTE: It's important that we use get1 here, as it increments the
        // refcount of the EC_KEY before returning a pointer to it.
        unsafe { Ok(CHeapWrapper::new_from(EVP_PKEY_get1_EC_KEY(self.as_mut())?)) }
    }
}

// returns a pointer to the first element of `slc`
fn slice_to_mut<T>(slc: &mut [T]) -> *mut T {
    slc as *mut [T] as *mut T
}

// returns a pointer to the first element of `slc`
fn slice_to_const<T>(slc: &[T]) -> *const T {
    slc as *const [T] as *const T
}

/// An error generated by BoringSSL.
///
/// The `Debug` impl prints a stack trace. Each element of the trace corresponds
/// to a function within BoringSSL which voluntarily pushed itself onto the
/// stack. In this sense, it is not the same as a normal stack trace. Each
/// element of the trace is of the form `[thread id]:error:[error code]:[library
/// name]:OPENSSL_internal:[reason string]:[file]:[line number]:[optional string
/// data]`.
///
/// The `Display` impl prints the first element of the stack trace.
///
/// Some BoringSSL functions do not record any error in the error stack. Errors
/// generated from such functions are printed as `error calling <function name>`
/// for both `Debug` and `Display` impls.
pub struct BoringError {
    stack_trace: Vec<String>,
}

impl BoringError {
    /// Consumes the error stack.
    ///
    /// `f` is the name of the function that failed. If the error stack is empty
    /// (some BoringSSL functions do not push errors onto the stack when
    /// returning errors), the returned `BoringError` will simply note that the
    /// named function failed; both the `Debug` and `Display` implementations
    /// will return `error calling f`, where `f` is the value of the `f`
    /// argument.
    fn consume_stack(f: &str) -> BoringError {
        let stack_trace = {
            let trace = get_error_stack_trace();
            if trace.is_empty() {
                vec![format!("error calling {}", f)]
            } else {
                trace
            }
        };
        BoringError { stack_trace }
    }

    /// The number of frames in the stack trace.
    ///
    /// Guaranteed to be at least 1.
    pub fn stack_depth(&self) -> usize {
        self.stack_trace.len()
    }
}

fn get_error_stack_trace() -> Vec<String> {
    // Credit to agl@google.com for this implementation.

    unsafe extern "C" fn error_callback(s: *const c_char, s_len: usize, ctx: *mut c_void) -> c_int {
        let stack_trace = ctx as *mut Vec<String>;
        let s = ::std::slice::from_raw_parts(s as *const u8, s_len - 1);
        (*stack_trace).push(String::from_utf8_lossy(s).to_string());
        1
    }

    let mut stack_trace = Vec::new();
    unsafe {
        ERR_print_errors_cb(
            Some(error_callback),
            &mut stack_trace as *mut _ as *mut c_void,
        )
    };
    stack_trace
}

impl Display for BoringError {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        write!(f, "{}", self.stack_trace[0])
    }
}

impl Debug for BoringError {
    fn fmt(&self, f: &mut Formatter) -> Result<(), fmt::Error> {
        for elem in &self.stack_trace {
            writeln!(f, "{}", elem)?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use util::should_fail;

    #[test]
    fn test_boring_error() {
        CStackWrapper::cbs_with_temp_buffer(&[], |cbs| {
            should_fail(
                CHeapWrapper::evp_parse_public_key(cbs),
                "boringssl::EVP_parse_public_key",
                "public key routines:OPENSSL_internal:DECODE_ERROR",
            );
        });
    }
}
