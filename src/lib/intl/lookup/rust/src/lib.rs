// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {libc, std::convert::From, std::ffi, std::mem, std::str};

#[repr(i8)]
#[derive(Debug, PartialEq)]
pub enum LookupStatus {
    // No error.
    OK = 0,
    /// The value requested is not available.
    Unavailable = 1,
    /// The argument passed in by the user is not valid.
    ArgumentError = 2,
}

/// The API supported by the Lookup module.
trait API {
    /// Looks a message up by its unique `message_id`.  A nonzero status is
    /// returned if the message is not found.
    fn string(&self, message_id: u64) -> Result<&ffi::CStr, LookupStatus>;
}

impl From<str::Utf8Error> for LookupStatus {
    fn from(_: str::Utf8Error) -> Self {
        LookupStatus::Unavailable
    }
}

/// Instantiates a fake Lookup instance, which is useful for tests that don't
/// want to make a full end-to-end localization setup.  
///
/// The fake is simplistic and it is the intention that it provides you with
/// some default fake behaviors.  The behaviors are as follows at the moment,
/// and more could be added if needed.
///
/// - If `locale_ids` contains the string `en-US`, the constructor function
///   in the FFI layer will return [LookupStatus::Unavailable].
/// - If the message ID pased to `Lookup::String()` is exactly 1, the fake
///   returns `Hello {person}!`, so that you can test 1-parameter formatting.
/// - Otherwise, for an even mesage ID it returns "Hello world!", or for
///   an odd message ID returns [LookupStatus::Unavailable].
///
/// The implementation of the fake itself is done in rust behind a FFI ABI,
/// see the package //src/lib/intl/lookup/rust for details.
pub struct FakeLookup {
    hello: ffi::CString,
    hello_person: ffi::CString,
}

impl FakeLookup {
    /// Create a new `FakeLookup`.
    pub fn new() -> FakeLookup {
        let hello =
            ffi::CString::new("Hello world!").expect("CString from known value should never fail");
        let hello_person = ffi::CString::new("Hello {person}!")
            .expect("CString from known value should never fail");
        FakeLookup { hello, hello_person }
    }
}

impl API for FakeLookup {
    /// A fake implementation of `string` for testing.
    ///
    /// Returns "Hello world" if passed an even `message_id`, and `LookupStatus::UNAVAILABLE` when
    /// passed an odd message_id. Used to test the FFI.
    fn string(&self, message_id: u64) -> Result<&ffi::CStr, LookupStatus> {
        if message_id == 1 {
            return Ok(self.hello_person.as_c_str());
        }
        match message_id % 2 == 0 {
            true => Ok(self.hello.as_c_str()),
            false => Err(LookupStatus::Unavailable),
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn intl_lookup_new_fake_for_test(
    len: libc::size_t,
    array: *mut *const libc::c_char,
    status: *mut i8,
) -> *const FakeLookup {
    *status = LookupStatus::OK as i8;
    let rsize = len as usize;
    let input: Vec<*const libc::c_char> = Vec::from_raw_parts(array, rsize, rsize);
    // Do not drop the vector we don't own.
    let input = mem::ManuallyDrop::new(input);

    for raw in input.iter() {
        let cstr = ffi::CStr::from_ptr(*raw).to_str().expect("not a valid UTF-8");
        if cstr == "en-US" {
            *status = LookupStatus::Unavailable as i8;
            return std::ptr::null::<FakeLookup>();
        }
    }
    Box::into_raw(Box::new(FakeLookup::new()))
}
#[no_mangle]
pub unsafe extern "C" fn intl_lookup_delete_fake_for_test(this: *mut FakeLookup) {
    generic_delete(this);
}

#[no_mangle]
pub unsafe extern "C" fn intl_lookup_new(
    len: libc::size_t,
    array: *mut *const libc::c_char,
    status: *mut i8,
) -> *const Lookup {
    *status = LookupStatus::OK as i8;
    let rsize = len as usize;
    let input: Vec<*const libc::c_char> = Vec::from_raw_parts(array, rsize, rsize);
    // Do not drop the vector we don't own.
    let input = mem::ManuallyDrop::new(input);

    let mut locales = vec![];
    for raw in input.iter() {
        let cstr = ffi::CStr::from_ptr(*raw).to_str();
        match cstr {
            Err(_) => {
                *status = LookupStatus::Unavailable as i8;
                return std::ptr::null::<Lookup>();
            }
            Ok(s) => {
                locales.push(s);
            }
        }
    }
    Box::into_raw(Box::new(Lookup::new(&locales[..])))
}

#[no_mangle]
pub unsafe extern "C" fn intl_lookup_delete(instance: *mut Lookup) {
    generic_delete(instance);
}

#[no_mangle]
pub unsafe extern "C" fn intl_lookup_string_fake_for_test(
    this: *const FakeLookup,
    id: u64,
    status: *mut i8,
) -> *const libc::c_char {
    generic_string(this, id, status)
}

unsafe fn generic_string<T: API>(this: *const T, id: u64, status: *mut i8) -> *const libc::c_char {
    *status = LookupStatus::OK as i8;
    match this.as_ref().unwrap().string(id) {
        Err(e) => {
            *status = e as i8;
            std::ptr::null()
        }
        Ok(s) => s.as_ptr() as *const libc::c_char,
    }
}

unsafe fn generic_delete<T>(instance: *mut T) {
    let _ = Box::from_raw(instance);
}

#[no_mangle]
pub unsafe extern "C" fn intl_lookup_string(
    this: *const Lookup,
    id: u64,
    status: *mut i8,
) -> *const libc::c_char {
    *status = LookupStatus::OK as i8;
    match this.as_ref().unwrap().string(id) {
        Err(e) => {
            *status = e as i8;
            std::ptr::null()
        }
        Ok(s) => s.as_ptr() as *const libc::c_char,
    }
}

pub struct Lookup {
    hello: ffi::CString,
}

impl Lookup {
    pub fn new(_: &[&str]) -> Lookup {
        Lookup { hello: ffi::CString::new("Hello world!").unwrap() }
    }
}

impl API for Lookup {
    /// See the documentation for `API` for details.
    fn string(&self, _: u64) -> Result<&ffi::CStr, LookupStatus> {
        Ok(self.hello.as_c_str())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn all_lookups_are_the_same() -> Result<(), LookupStatus> {
        let l = Lookup::new(&vec!["foo"]);
        assert_eq!("Hello world!", l.string(42)?.to_str()?);
        assert_eq!("Hello world!", l.string(85)?.to_str()?);
        Ok(())
    }

    // Exercises the fake behaviors which are part of the fake spec.  The fake
    // behaviors may evolve in the future, but this test gives out the ones that
    // currently exist.
    #[test]
    fn test_fake_lookup() -> Result<(), LookupStatus> {
        let l = FakeLookup::new();
        assert_eq!("Hello {person}!", l.string(1)?.to_str()?);
        assert_eq!("Hello world!", l.string(10)?.to_str()?);
        assert_eq!("Hello world!", l.string(12)?.to_str()?);
        assert_eq!(LookupStatus::Unavailable, l.string(11).unwrap_err());
        assert_eq!(LookupStatus::Unavailable, l.string(41).unwrap_err());
        Ok(())
    }
}
