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
    rust_icu_sys::*, rust_icu_uenum as uenum, rust_icu_ustring as ustring, std::convert::TryFrom,
    std::ffi,
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
        let asciiz_locale = ffi::CString::new(locale).map_err(common::Error::wrapper)?;
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

    /// Sets the calendar's current date/time in milliseconds since the epoch.
    ///
    /// Implements `ucal_setMillis`.
    pub fn set_millis(&mut self, date_time: sys::UDate) -> Result<(), common::Error> {
        let mut status = common::Error::OK_CODE;
        unsafe {
            versioned_function!(ucal_setMillis)(self.rep, date_time, &mut status);
        };
        common::Error::ok_or_warning(status)
    }

    /// Gets the calendar's current date/time in milliseconds since the epoch.
    ///
    /// Implements `ucal_getMillis`.
    pub fn get_millis(&self) -> Result<sys::UDate, common::Error> {
        let mut status = common::Error::OK_CODE;
        let millis = unsafe { versioned_function!(ucal_getMillis)(self.rep, &mut status) };
        common::Error::ok_or_warning(status)?;
        Ok(millis)
    }

    /// Sets the calendar's current date in the calendar's local time zone.
    ///
    /// Note that `month` is 0-based.
    ///
    /// Implements `ucal_setDate`.
    pub fn set_date(&mut self, year: i32, month: i32, date: i32) -> Result<(), common::Error> {
        let mut status = common::Error::OK_CODE;
        unsafe {
            versioned_function!(ucal_setDate)(self.rep, year, month, date, &mut status);
        }
        common::Error::ok_or_warning(status)?;
        Ok(())
    }

    /// Sets the calendar's current date and time in the calendar's local time zone.
    ///
    /// Note that `month` is 0-based.
    ///
    /// Implements `ucal_setDateTime`.
    pub fn set_date_time(
        &mut self,
        year: i32,
        month: i32,
        date: i32,
        hour: i32,
        minute: i32,
        second: i32,
    ) -> Result<(), common::Error> {
        let mut status = common::Error::OK_CODE;
        unsafe {
            versioned_function!(ucal_setDateTime)(
                self.rep,
                year,
                month,
                date,
                hour,
                minute,
                second,
                &mut status,
            );
        }
        common::Error::ok_or_warning(status)?;
        Ok(())
    }

    /// Returns the calendar's time zone's offset from UTC in milliseconds, for the calendar's
    /// current date/time.
    ///
    /// This does not include the daylight savings offset, if any. Note that the calendar's current
    /// date/time is significant because time zones are occasionally redefined -- a time zone that
    /// has a +16.5 hour offset today might have had a +17 hour offset a decade ago.
    ///
    /// Wraps `ucal_get` for `UCAL_ZONE_OFFSET`.
    pub fn get_zone_offset(&self) -> Result<i32, common::Error> {
        self.get(UCalendarDateFields::UCAL_ZONE_OFFSET)
    }

    /// Returns the calendar's daylight savings offset from its non-DST time, in milliseconds, for
    /// the calendar's current date/time. This may be 0 if the time zone does not observe DST at
    /// all, or if the time zone is not in the daylight savings period at the calendar's current
    /// date/time.
    ///
    /// Wraps `ucal_get` for `UCAL_ZONE_DST_OFFSET`.
    pub fn get_dst_offset(&self) -> Result<i32, common::Error> {
        self.get(UCalendarDateFields::UCAL_DST_OFFSET)
    }

    /// Returns true if the calendar is currently in daylight savings / summer time.
    ///
    /// Implements `ucal_inDaylightTime`.
    pub fn in_daylight_time(&self) -> Result<bool, common::Error> {
        let mut status = common::Error::OK_CODE;
        let in_daylight_time: sys::UBool =
            unsafe { versioned_function!(ucal_inDaylightTime)(self.as_c_calendar(), &mut status) };
        common::Error::ok_or_warning(status)?;
        Ok(in_daylight_time != 0)
    }

    /// Implements `ucal_get`.
    ///
    /// Consider using specific higher-level methods instead.
    pub fn get(&self, field: UCalendarDateFields) -> Result<i32, common::Error> {
        let mut status: UErrorCode = common::Error::OK_CODE;
        let value =
            unsafe { versioned_function!(ucal_get)(self.as_c_calendar(), field, &mut status) };
        common::Error::ok_or_warning(status)?;
        Ok(value)
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
        versioned_function!(ucal_getDefaultTimeZone)(std::ptr::null_mut(), 0, &mut status)
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

/// Opens a list of available time zones for the given country.
///
/// Implements `ucal_openCountryTimeZones`.
pub fn country_time_zones(country: &str) -> Result<uenum::Enumeration, common::Error> {
    uenum::ucal_open_country_time_zones(country)
}

/// Opens a list of available time zone IDs with the given filters.
///
/// Implements `ucal_openTimeZoneIDEnumeration`
pub fn time_zone_id_enumeration(
    zone_type: sys::USystemTimeZoneType,
    region: Option<&str>,
    raw_offset: Option<i32>,
) -> Result<uenum::Enumeration, common::Error> {
    uenum::ucal_open_time_zone_id_enumeration(zone_type, region, raw_offset)
}

/// Opens a list of available time zones.
///
/// Implements `ucal_openTimeZones`
pub fn time_zones() -> Result<uenum::Enumeration, common::Error> {
    uenum::open_time_zones()
}

#[cfg(test)]
mod tests {
    use {
        super::{UCalendar, *},
        regex::Regex,
        std::collections::HashSet,
    };

    #[test]
    fn test_time_zones() {
        let tz_iter = time_zones().expect("time zones opened");
        assert_eq!(
            tz_iter
                .map(|r| { r.expect("time zone is available") })
                .take(3)
                .collect::<Vec<String>>(),
            vec!["ACT", "AET", "AGT"]
        );
    }

    #[test]
    fn test_time_zone_id_enumeration_no_filters() {
        let tz_iter =
            time_zone_id_enumeration(sys::USystemTimeZoneType::UCAL_ZONE_TYPE_ANY, None, None)
                .expect("time_zone_id_enumeration() opened");

        let from_enumeration = tz_iter
            .map(|r| r.expect("timezone is available"))
            .collect::<Vec<String>>();

        let from_time_zones = time_zones()
            .expect("time_zones() opened")
            .map(|r| r.expect("time zone is available"))
            .collect::<Vec<String>>();

        assert!(!from_time_zones.is_empty());

        assert_eq!(from_enumeration, from_time_zones);
    }

    #[test]
    fn test_time_zone_id_enumeration_by_type_region() {
        let tz_iter = time_zone_id_enumeration(
            sys::USystemTimeZoneType::UCAL_ZONE_TYPE_CANONICAL,
            Some("us"),
            None,
        )
        .expect("time_zone_id_enumeration() opened");
        assert_eq!(
            tz_iter
                .map(|r| { r.expect("time zone is available") })
                .take(3)
                .collect::<Vec<String>>(),
            vec!["America/Adak", "America/Anchorage", "America/Boise"]
        );
    }

    #[test]
    fn test_time_zone_id_enumeration_by_offset() {
        let tz_iter = time_zone_id_enumeration(
            sys::USystemTimeZoneType::UCAL_ZONE_TYPE_ANY,
            None,
            Some(0), /* GMT */
        )
        .expect("time_zone_id_enumeration() opened");
        let tz_ids = tz_iter
            .map(|r| r.expect("time zone is available"))
            .collect::<HashSet<String>>();

        assert!(tz_ids.contains("UTC"));
        assert!(!tz_ids.contains("Etc/GMT-1"));
    }

    #[test]
    fn test_country_time_zones() {
        let tz_iter = country_time_zones("us").expect("time zones available");
        assert_eq!(
            tz_iter
                .map(|r| { r.expect("time zone is available") })
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
        let re = Regex::new(r"^[0-9][0-9][0-9][0-9][a-z][a-z0-9]*$").expect("valid regex");
        let tz_version = super::get_tz_data_version().expect("get_tz_data_version works");
        assert!(re.is_match(&tz_version), "version was: {:?}", &tz_version);
    }

    #[test]
    fn test_get_set_millis() -> Result<(), common::Error> {
        let now = get_now();
        let mut cal = UCalendar::new("America/New_York", "en-US", UCalendarType::UCAL_GREGORIAN)?;
        // Assert that the times are basically the same.
        // Let's assume that no more than 1 second might elapse between the execution of `get_now()`
        // and `get_millis()`.
        assert!((now - cal.get_millis()?).abs() <= 1000f64);

        let arbitrary_delta_ms = 17.0;
        let date = now + arbitrary_delta_ms;
        cal.set_millis(date)?;
        assert_eq!(cal.get_millis()?, date);
        Ok(())
    }

    #[test]
    fn test_set_date() -> Result<(), common::Error> {
        // Timestamps hard-coded, not parsed, to avoid cyclic dependency on udat.

        // 2020-05-07T21:00:00.000-04:00
        let time_a = 1588899600000f64;
        // 2020-05-04T21:00:00.000-04:00
        let time_b = 1588640400000f64;

        let mut cal = UCalendar::new("America/New_York", "en-US", UCalendarType::UCAL_GREGORIAN)?;
        cal.set_millis(time_a)?;
        cal.set_date(2020, UCalendarMonths::UCAL_MAY as i32, 4)?;
        assert_eq!(cal.get_millis()?, time_b);

        Ok(())
    }

    #[test]
    fn test_set_date_time() -> Result<(), common::Error> {
        // Timestamps hard-coded, not parsed, to avoid cyclic dependency on udat.

        // 2020-05-07T21:26:55.898-04:00
        let time_a = 1588901215898f64;
        // 2020-05-04T21:00:00.898-04:00
        let time_b = 1588640400898f64;

        let mut cal = UCalendar::new("America/New_York", "en-US", UCalendarType::UCAL_GREGORIAN)?;
        cal.set_millis(time_a)?;
        cal.set_date_time(2020, UCalendarMonths::UCAL_MAY as i32, 4, 21, 0, 0)?;

        assert_eq!(cal.get_millis()?, time_b);

        Ok(())
    }

    #[test]
    fn test_get() -> Result<(), common::Error> {
        // Timestamps hard-coded, not parsed, to avoid cyclic dependency on udat.

        // 2020-05-07T21:26:55.898-04:00
        let date_time = 1588901215898f64;

        let mut cal = UCalendar::new("America/New_York", "en-US", UCalendarType::UCAL_GREGORIAN)?;
        cal.set_millis(date_time)?;

        assert_eq!(cal.get(UCalendarDateFields::UCAL_DAY_OF_MONTH)?, 7);

        assert_eq!(cal.get(UCalendarDateFields::UCAL_MILLISECOND)?, 898);

        Ok(())
    }

    #[test]
    fn test_offsets_and_daylight_time() -> Result<(), common::Error> {
        let mut cal = UCalendar::new("America/New_York", "en-US", UCalendarType::UCAL_GREGORIAN)?;

        // -5 hours
        let expected_zone_offset_ms: i32 = -5 * 60 * 60 * 1000;
        // + 1 hour
        let expected_dst_offset_ms: i32 = 1 * 60 * 60 * 1000;

        cal.set_date_time(2020, UCalendarMonths::UCAL_MAY as i32, 7, 21, 0, 0)?;
        assert_eq!(cal.get_zone_offset()?, expected_zone_offset_ms);
        assert_eq!(cal.get_dst_offset()?, expected_dst_offset_ms);
        assert!(cal.in_daylight_time()?);

        // -5 hours
        let expected_zone_offset: i32 = -5 * 60 * 60 * 1000;
        // No offset
        let expected_dst_offset: i32 = 0;

        cal.set_date_time(2020, UCalendarMonths::UCAL_JANUARY as i32, 15, 12, 0, 0)?;
        assert_eq!(cal.get_zone_offset()?, expected_zone_offset);
        assert_eq!(cal.get_dst_offset()?, expected_dst_offset);
        assert!(!cal.in_daylight_time()?);

        Ok(())
    }
}
