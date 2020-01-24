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

//! # Implementation of `ucal.h`.
//!
//! As a general piece of advice, since a lot of documentation is currently elided,
//! see the unit tests for example uses of each of the wrapper functions.

use {
    log::trace, rust_icu_common as common, rust_icu_sys as sys, rust_icu_sys::versioned_function,
    rust_icu_sys::*, rust_icu_ustring as ustring, std::convert::TryFrom, std::ffi,
};

/// Implements the UCalendar type from `ucal.h`.
///
/// The naming `rust_icu_ucal::UCalendar` is a bit repetetetitive, but makes it
/// a bit more obvious what ICU type it is wrapping.
#[derive(Debug)]
pub struct UCalendar {
    // Internal representation of the UCalendar, a pointer to a C type from ICU.
    //
    // The representation is owned by this type, and must be deallocated by calling
    // `sys::ucal_close`.
    rep: *mut sys::UCalendar,
}

impl Drop for UCalendar {
    /// Deallocates the internal representation of UCalendar.
    ///
    /// Implements `ucal_close`.
    fn drop(&mut self) {
        unsafe {
            versioned_function!(ucal_close)(self.rep);
        };
    }
}

impl UCalendar {
    /// Creates a new UCalendar from a `UChar` zone ID.
    ///
    /// Use `new` to construct this from rust types only.
    fn new_from_uchar(
        zone_id: &ustring::UChar,
        locale: &str,
        cal_type: sys::UCalendarType,
    ) -> Result<UCalendar, common::Error> {
        let mut status = common::Error::OK_CODE;
        let asciiz_locale =
            ffi::CString::new(locale).map_err(|_| common::Error::string_with_interior_nul())?;
        // Requires that zone_id contains a valid Unicode character representation with known
        // beginning and length.  asciiz_locale must be a pointer to a valid C string.  The first
        // condition is assumed to be satisfied by ustring::UChar, and the second should be
        // satisfied by construction of asciiz_locale just above.
        let raw_ucal = unsafe {
            versioned_function!(ucal_open)(
                zone_id.as_c_ptr(),
                zone_id.len() as i32,
                asciiz_locale.as_ptr(),
                cal_type,
                &mut status,
            ) as *mut sys::UCalendar
        };
        common::Error::ok_or_warning(status)?;
        Ok(UCalendar { rep: raw_ucal })
    }

    /// Creates a new UCalendar.
    ///
    /// Implements `ucal_open`.
    pub fn new(
        zone_id: &str,
        locale: &str,
        cal_type: sys::UCalendarType,
    ) -> Result<UCalendar, common::Error> {
        let zone_id_uchar = ustring::UChar::try_from(zone_id)?;
        Self::new_from_uchar(&zone_id_uchar, locale, cal_type)
    }
    /// Returns this UCalendar's internal C representation.  Use only for interfacing with the C
    /// low-level API.
    pub fn as_c_calendar(&self) -> *const sys::UCalendar {
        self.rep
    }
}

/// Implements `ucal_setDefaultTimeZone`
pub fn set_default_time_zone(zone_id: &str) -> Result<(), common::Error> {
    let mut status = common::Error::OK_CODE;
    let mut zone_id_uchar = ustring::UChar::try_from(zone_id)?;
    zone_id_uchar.make_z();
    // Requires zone_id_uchar to be a valid pointer until the function returns.
    unsafe {
        assert!(common::Error::is_ok(status));
        versioned_function!(ucal_setDefaultTimeZone)(zone_id_uchar.as_c_ptr(), &mut status);
    };
    common::Error::ok_or_warning(status)
}

/// Implements `ucal_getDefaultTimeZone`
pub fn get_default_time_zone() -> Result<String, common::Error> {
    let mut status = common::Error::OK_CODE;

    // Preflight the time zone first.
    let time_zone_length = unsafe {
        assert!(common::Error::is_ok(status));
        versioned_function!(ucal_getDefaultTimeZone)(0 as *mut sys::UChar, 0, &mut status)
    } as usize;
    common::Error::ok_preflight(status)?;

    // Should this capacity include the terminating \u{0}?
    let mut status = common::Error::OK_CODE;
    let mut uchar = ustring::UChar::new_with_capacity(time_zone_length);
    trace!("length: {}", time_zone_length);

    // Requires that uchar is a valid buffer.  Should be guaranteed by the constructor above.
    unsafe {
        assert!(common::Error::is_ok(status));
        versioned_function!(ucal_getDefaultTimeZone)(
            uchar.as_mut_c_ptr(),
            time_zone_length as i32,
            &mut status,
        )
    };
    common::Error::ok_or_warning(status)?;
    trace!("result: {:?}", uchar);
    String::try_from(&uchar)
}

/// Implements `ucal_getTZDataVersion`
pub fn get_tz_data_version() -> Result<String, common::Error> {
    let mut status = common::Error::OK_CODE;

    let tz_data_version = unsafe {
        let raw_cstring = versioned_function!(ucal_getTZDataVersion)(&mut status);
        common::Error::ok_or_warning(status)?;
        ffi::CStr::from_ptr(raw_cstring)
            .to_string_lossy()
            .into_owned()
    };

    Ok(tz_data_version)
}

/// Gets the current date and time, in milliseconds since the Epoch.
///
/// Implements `ucal_getNow`.
pub fn get_now() -> f64 {
    unsafe { versioned_function!(ucal_getNow)() as f64 }
}

#[cfg(test)]
mod tests {
    use {super::*, regex::Regex, rust_icu_uenum as uenum};

    #[test]
    fn test_open_time_zones() {
        let tz_iter = uenum::open_time_zones().expect("time zones opened");
        assert_eq!(
            tz_iter
                .map(|r| { r.expect("timezone is available") })
                .take(3)
                .collect::<Vec<String>>(),
            vec!["ACT", "AET", "AGT"]
        );
    }

    #[test]
    fn test_open_time_zone_id_enumeration() {
        let tz_iter = uenum::open_time_zone_id_enumeration(
            sys::USystemTimeZoneType::UCAL_ZONE_TYPE_CANONICAL,
            "us",
            None,
        )
        .expect("time zones available");
        assert_eq!(
            tz_iter
                .map(|r| { r.expect("timezone is available") })
                .take(3)
                .collect::<Vec<String>>(),
            vec!["America/Adak", "America/Anchorage", "America/Boise"]
        );
    }

    #[test]
    fn test_open_country_time_zones() {
        let tz_iter = uenum::open_country_time_zones("us").expect("time zones available");
        assert_eq!(
            tz_iter
                .map(|r| { r.expect("timezone is available") })
                .take(3)
                .collect::<Vec<String>>(),
            vec!["AST", "America/Adak", "America/Anchorage"]
        );
    }

    #[test]
    fn test_default_time_zone() {
        super::set_default_time_zone("America/Adak").expect("time zone set with success");
        assert_eq!(
            super::get_default_time_zone().expect("time zone obtained"),
            "America/Adak",
        );
    }

    #[test]
    fn test_get_tz_data_version() {
        let re = Regex::new(r"^[0-9][0-9][0-9][0-9][a-z]$").expect("valid regex");
        let tz_version = super::get_tz_data_version().expect("get_tz_data_version works");
        assert!(re.is_match(&tz_version));
    }
}
