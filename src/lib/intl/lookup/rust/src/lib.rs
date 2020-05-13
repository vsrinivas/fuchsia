// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    libc, rust_icu_uloc as uloc,
    std::convert::From,
    std::convert::TryFrom,
    std::ffi,
    std::mem,
    std::str,
};

/// The directory where localized resources are kept.
pub(crate) const ASSETS_DIR: &str = "/pkg/data/assets/locales";

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

#[allow(dead_code)] // Clean up before reviewing
pub struct Lookup {
    hello: ffi::CString,
    requested_locales: Vec<uloc::ULoc>,
    supported_locales: Vec<uloc::ULoc>,
}

impl Lookup {
    /// Creates a new instance of Lookup, with the default ways to look up the
    /// data.
    pub fn new(requested: &[&str]) -> Lookup {
        let supported_locales =
            Lookup::get_available_locales().with_context(|| "while creating Lookup").unwrap();
        Lookup::new_internal(requested, supported_locales).expect("error (should be corrected)")
    }

    /// Create a new [Lookup] from parts.  Only to be used in tests.
    #[cfg(test)]
    pub fn new_from_parts(requested: &[impl AsRef<str>], supported: Vec<String>) -> Result<Lookup> {
        Lookup::new_internal(requested, supported)
    }

    fn new_internal(requested: &[impl AsRef<str>], supported: Vec<String>) -> Result<Lookup> {
        Ok(Lookup {
            hello: ffi::CString::new("Hello world!").unwrap(),
            requested_locales: requested
                .iter()
                .map(|l| uloc::ULoc::try_from(l.as_ref()))
                .map(|r| r.expect("could not parse"))
                .collect(),
            supported_locales: supported
                .into_iter()
                .map(|s: String| uloc::ULoc::try_from(s.as_str()))
                .collect::<Result<Vec<_>, _>>()?,
        })
    }

    #[cfg(test)]
    fn get_available_locales_for_test() -> Result<Vec<String>> {
        Lookup::get_available_locales()
    }

    // Returns the list of locales for which there are resources present in
    // the locale assets directory.  Errors are returned if the locale assets
    // directory is malformed: since it is prepared at compile time, such an
    // occurrence means that the program is corrupted.
    fn get_available_locales() -> Result<Vec<String>> {
        let locale_dirs = std::fs::read_dir(ASSETS_DIR)
            .with_context(|| format!("while reading {}", ASSETS_DIR))?;
        let mut available_locales: Vec<String> = vec![];
        for entry_or in locale_dirs {
            let entry =
                entry_or.with_context(|| format!("while reading entries in {}", ASSETS_DIR))?;
            // We only ever expect directories corresponding to locale names
            // to be UTF-8 encoded, so this conversion will normally always
            // succeed for directories in `ASSETS_DIR`.
            let name = entry.file_name().into_string().map_err(|os_string| {
                anyhow::anyhow!("OS path not convertible to UTF-8: {:?}", os_string)
            })?;
            let entry_type = entry
                .file_type()
                .with_context(|| format!("while looking up file type for: {:?}", name))?;
            if entry_type.is_dir() {
                available_locales.push(name);
            }
        }
        Ok(available_locales)
    }

    /// Looks up the message by its key.
    pub fn str(&self, _: u64) -> Result<&str, LookupStatus> {
        Ok(self.hello.to_str()?)
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
    use fidl_fuchsia_intl_test as ftest;
    use std::collections::HashSet;

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
        // Fake lookups always return "Hello world!", that's a FakeLookup
        // feature.
        assert_eq!("Hello world!", l.string(10)?.to_str()?);
        assert_eq!("Hello world!", l.string(12)?.to_str()?);
        assert_eq!(LookupStatus::Unavailable, l.string(11).unwrap_err());
        assert_eq!(LookupStatus::Unavailable, l.string(41).unwrap_err());
        Ok(())
    }

    #[test]
    fn test_real_lookup() -> Result<(), LookupStatus> {
        let l = Lookup::new(&vec!["es"]);
        assert_eq!("Hello world!", l.str(ftest::MessageIds::StringName as u64)?);
        Ok(())
    }

    /// Locales have been made part of the resources of the test package.
    #[test]
    fn test_available_locales() -> Result<()> {
        // Iteration order is not deterministic.
        let expected: HashSet<String> = ["es", "en", "fr"].iter().map(|s| s.to_string()).collect();
        assert_eq!(expected, Lookup::get_available_locales_for_test()?.into_iter().collect());
        Ok(())
    }
}
