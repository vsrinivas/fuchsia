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

//! # Rust implementation of the `uenum.h` C API header for ICU.

use {
    rust_icu_common as common, rust_icu_sys as sys,
    rust_icu_sys::*,
    std::{convert::TryFrom, ffi, str},
};

/// Rust wrapper for the UEnumeration iterator.
///
/// Implements `UEnumeration`
#[derive(Debug)]
pub struct Enumeration {
    // The raw underlying character array, in case the underlying char array is
    // owned by this enumeration.
    raw: Option<common::CStringVec>,

    // Internal low-level representation of the enumeration.  The internal
    // representation relies on `raw` and `len` above and must live at most as
    // long.
    rep: *mut sys::UEnumeration,
}

impl Enumeration {
    /// Internal representation, for ICU4C methods that require it.
    pub fn repr(&mut self) -> *mut sys::UEnumeration {
        self.rep
    }

    /// Creates an empty `Enumeration`.
    pub fn empty() -> Self {
        Enumeration::try_from(&vec![][..]).unwrap()
    }
}

impl Default for Enumeration {
    fn default() -> Self {
        Self::empty()
    }
}

/// Creates an enumeration iterator from a vector of UTF-8 strings.
impl TryFrom<&[&str]> for Enumeration {
    type Error = common::Error;

    /// Constructs an enumeration from a string slice.
    ///
    /// Implements `uenum_openCharStringsEnumeration`
    fn try_from(v: &[&str]) -> Result<Enumeration, common::Error> {
        let raw = common::CStringVec::new(v)?;
        let mut status = common::Error::OK_CODE;
        let rep: *mut sys::UEnumeration = unsafe {
            versioned_function!(uenum_openCharStringsEnumeration)(
                raw.as_c_array(),
                raw.len() as i32,
                &mut status,
            )
        };
        common::Error::ok_or_warning(status)?;
        // rep should not be null without an error set, but:
        // https://unicode-org.atlassian.net/browse/ICU-20918
        assert!(!rep.is_null());
        Ok(Enumeration {
            rep,
            raw: Some(raw),
        })
    }
}

impl Drop for Enumeration {
    /// Drops the Enumeration, deallocating its internal representation hopefully correctly.
    ///
    /// Implements `uenum_close`
    fn drop(&mut self) {
        unsafe { versioned_function!(uenum_close)(self.rep) };
    }
}

impl Iterator for Enumeration {
    type Item = Result<String, common::Error>;

    /// Yields the next element stored in the enumeration.
    ///
    /// Implements `uenum_next`
    fn next(&mut self) -> Option<Self::Item> {
        let mut len: i32 = 0;
        let mut status = common::Error::OK_CODE;
        // Requires that self.rep is a valid pointer to a sys::UEnumeration.
        assert!(!self.rep.is_null());
        let raw = unsafe { versioned_function!(uenum_next)(self.rep, &mut len, &mut status) };
        if raw.is_null() {
            // No more elements to iterate over.
            return None;
        }
        let result = common::Error::ok_or_warning(status);
        match result {
            Ok(()) => {
                assert!(!raw.is_null());
                // Requires that raw is a valid pointer to a C string.
                let cstring = unsafe { ffi::CStr::from_ptr(raw) }; // Borrowing
                Some(Ok(cstring
                    .to_str()
                    .expect("could not convert to string")
                    .to_string()))
            }
            Err(e) => Some(Err(e)),
        }
    }
}

impl Enumeration {
    /// Constructs an [Enumeration] from a raw pointer.
    ///
    /// **DO NOT USE THIS FUNCTION UNLESS THERE IS NO OTHER CHOICE!**
    ///
    /// We tried to keep this function hidden to avoid the
    /// need to have a such a powerful unsafe function in the
    /// public API.  It worked up to a point for free functions.
    ///
    /// It no longer works on high-level methods that return
    /// enumerations, since then we'd need to depend on them to
    /// create an [Enumeration].
    #[doc(hidden)]
    pub unsafe fn from_raw_parts(
        raw: Option<common::CStringVec>,
        rep: *mut sys::UEnumeration,
    ) -> Enumeration {
        Enumeration { raw, rep }
    }
}

#[doc(hidden)]
/// Implements `ucal_openCountryTimeZones`.
// This should be in the `ucal` crate, but not possible because of the raw enum initialization.
// Tested in `ucal`.
pub fn ucal_open_country_time_zones(country: &str) -> Result<Enumeration, common::Error> {
    let mut status = common::Error::OK_CODE;
    let asciiz_country = ffi::CString::new(country)?;
    // Requires that the asciiz country be a pointer to a valid C string.
    let raw_enum = unsafe {
        assert!(common::Error::is_ok(status));
        versioned_function!(ucal_openCountryTimeZones)(asciiz_country.as_ptr(), &mut status)
    };
    common::Error::ok_or_warning(status)?;
    Ok(Enumeration {
        raw: None,
        rep: raw_enum,
    })
}

#[doc(hidden)]
/// Implements `ucal_openTimeZoneIDEnumeration`
// This should be in the `ucal` crate, but not possible because of the raw enum initialization.
// Tested in `ucal`.
pub fn ucal_open_time_zone_id_enumeration(
    zone_type: sys::USystemTimeZoneType,
    region: Option<&str>,
    raw_offset: Option<i32>,
) -> Result<Enumeration, common::Error> {
    let mut status = common::Error::OK_CODE;
    let asciiz_region = match region {
        None => None,
        Some(region) => Some(ffi::CString::new(region)?),
    };
    let mut repr_raw_offset: i32 = raw_offset.unwrap_or_default();

    // asciiz_region should be a valid asciiz pointer. raw_offset is an encoding
    // of an optional value by a C pointer.
    let raw_enum = unsafe {
        assert!(common::Error::is_ok(status));
        versioned_function!(ucal_openTimeZoneIDEnumeration)(
            zone_type,
            // Note that for the string pointer to remain valid, we must borrow the CString from
            // asciiz_region, not move the CString out.
            match &asciiz_region {
                Some(asciiz_region) => asciiz_region.as_ptr(),
                None => std::ptr::null(),
            },
            match raw_offset {
                Some(_) => &mut repr_raw_offset,
                None => std::ptr::null_mut(),
            },
            &mut status,
        )
    };
    common::Error::ok_or_warning(status)?;
    Ok(Enumeration {
        raw: None,
        rep: raw_enum,
    })
}

#[doc(hidden)]
/// Opens a list of available time zones.
///
/// Implements `ucal_openTimeZones`
// This should be in the `ucal` crate, but not possible because of the raw enum initialization.
// Tested in `ucal`.
pub fn open_time_zones() -> Result<Enumeration, common::Error> {
    let mut status = common::Error::OK_CODE;
    let raw_enum = unsafe {
        assert!(common::Error::is_ok(status));
        versioned_function!(ucal_openTimeZones)(&mut status)
    };
    common::Error::ok_or_warning(status)?;
    Ok(Enumeration {
        raw: None,
        rep: raw_enum,
    })
}

#[doc(hidden)]
/// Implements `uloc_openKeywords`.
// This should be in the `uloc` crate, but this is not possible because of the raw enum
// initialization. Tested in `uloc`.
pub fn uloc_open_keywords(locale: &str) -> Result<Enumeration, common::Error> {
    let mut status = common::Error::OK_CODE;
    let asciiz_locale = ffi::CString::new(locale)?;
    let raw_enum = unsafe {
        assert!(common::Error::is_ok(status));
        versioned_function!(uloc_openKeywords)(asciiz_locale.as_ptr(), &mut status)
    };
    common::Error::ok_or_warning(status)?;
    // "No error but null" means that there are no keywords
    if raw_enum.is_null() {
        Ok(Enumeration::empty())
    } else {
        Ok(Enumeration {
            raw: None,
            rep: raw_enum,
        })
    }
}

#[cfg(test)]
mod tests {
    use {super::*, std::convert::TryFrom};

    #[test]
    fn iter() {
        let e = Enumeration::try_from(&vec!["hello", "world", "💖"][..]).expect("enumeration?");
        let mut count = 0;
        let mut results = vec![];
        for result in e {
            let elem = result.expect("no error");
            count += 1;
            results.push(elem);
        }
        assert_eq!(count, 3, "results: {:?}", results);
        assert_eq!(
            results,
            vec!["hello", "world", "💖"],
            "results: {:?}",
            results
        );
    }

    #[test]
    fn error() {
        // A mutilated sparkle heart from https://doc.rust-lang.org/std/str/fn.from_utf8_unchecked.html
        let destroyed_sparkle_heart = vec![0, 159, 164, 150];
        let invalid_utf8 = unsafe { str::from_utf8_unchecked(&destroyed_sparkle_heart) };
        let e = Enumeration::try_from(&vec!["hello", "world", "💖", invalid_utf8][..]);
        assert!(e.is_err(), "was: {:?}", e);
    }
}
