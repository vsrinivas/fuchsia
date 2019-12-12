//! # Rust implementation of the `uenum.h` C API header for ICU.

use {
    rust_icu_common as common, rust_icu_sys as sys, rust_icu_sys::*, std::convert::TryFrom,
    std::ffi, std::str,
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

/// Creates an enumeration iterator from a vector of UTF-8 strings.
impl TryFrom<&[&str]> for Enumeration {
    type Error = common::Error;

    /// Constructs an enumeration from a string slice.
    ///
    /// Implements `uenum_openCharStringsEnumeration`
    fn try_from(v: &[&str]) -> Result<Enumeration, common::Error> {
        let raw = common::CStringVec::new(v)?;
        let mut status = common::Error::OK_CODE;
        let rep = unsafe {
            versioned_function!(uenum_openCharStringsEnumeration)(
                raw.as_c_array(),
                raw.len() as i32,
                &mut status,
            )
        };
        common::Error::ok_or_warning(status)?;
        Ok(Enumeration { rep: rep, raw: Some(raw) })
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
        assert!(self.rep.is_null() == false, "nullness: {:?}", self.rep.is_null());
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
                Some(Ok(cstring.to_str().expect("could not convert to string").to_string()))
            }
            Err(e) => Some(Err(e)),
        }
    }
}

/// Implements `ucal_openCountryTimeZones`.
// This should be in the `ucal` crate, but not possible because of the raw enum initialization.
// Tested in uenum.
pub fn open_country_time_zones(country: &str) -> Result<crate::Enumeration, common::Error> {
    let mut status = common::Error::OK_CODE;
    let asciiz_country =
        ffi::CString::new(country).map_err(|_| common::Error::string_with_interior_nul())?;
    // Requires that the asciiz country be a pointer to a valid C string.
    let raw_enum = unsafe {
        assert!(common::Error::is_ok(status));
        versioned_function!(ucal_openCountryTimeZones)(asciiz_country.as_ptr(), &mut status)
    };
    common::Error::ok_or_warning(status)?;
    Ok(crate::Enumeration { raw: None, rep: raw_enum })
}

/// Implements `ucal_openTimeZoneIDENumeration`
// This should be in the `ucal` crate, but not possible because of the raw enum initialization.
// Tested in uenum.
pub fn open_time_zone_id_enumeration(
    zone_type: sys::USystemTimeZoneType,
    region: &str,
    raw_offset: Option<i32>,
) -> Result<crate::Enumeration, common::Error> {
    let mut status = common::Error::OK_CODE;
    let asciiz_region =
        ffi::CString::new(region).map_err(|_| common::Error::string_with_interior_nul())?;
    let mut repr_raw_offset: i32 = raw_offset.unwrap_or_default();

    // asciiz_region should be a valid asciiz pointer. raw_offset is an encoding
    // of an optional value by a C pointer.
    let raw_enum = unsafe {
        assert!(common::Error::is_ok(status));
        versioned_function!(ucal_openTimeZoneIDEnumeration)(
            zone_type,
            asciiz_region.as_ptr(),
            match raw_offset {
                Some(_) => &mut repr_raw_offset,
                None => 0 as *mut i32,
            },
            &mut status,
        )
    };
    common::Error::ok_or_warning(status)?;
    Ok(crate::Enumeration { raw: None, rep: raw_enum })
}

/// Opens a list of available time zones.
///
/// Implements `ucal_openTimeZones`
// This should be in the `ucal` crate, but not possible because of the raw enum initialization.
// Tested in uenum.
pub fn open_time_zones() -> Result<crate::Enumeration, common::Error> {
    let mut status = common::Error::OK_CODE;
    let raw_enum = unsafe {
        assert!(common::Error::is_ok(status));
        versioned_function!(ucal_openTimeZones)(&mut status)
    };
    common::Error::ok_or_warning(status)?;
    assert!(!raw_enum.is_null(), "status: {:?}", status);
    Ok(crate::Enumeration { raw: None, rep: raw_enum })
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
            count = count + 1;
            results.push(elem);
        }
        assert_eq!(count, 3, "results: {:?}", results);
        assert_eq!(results, vec!["hello", "world", "💖"], "results: {:?}", results);
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
