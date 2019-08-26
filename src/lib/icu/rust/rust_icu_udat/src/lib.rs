// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    rust_icu_common as common, rust_icu_sys as sys, rust_icu_sys::versioned_function,
    rust_icu_sys::*, rust_icu_ucal as ucal, rust_icu_uloc as uloc, rust_icu_ustring as ustring,
    std::convert::TryFrom,
};

/// Implements `UDateFormat`
pub struct UDateFormat {
    // Internal C representation of UDateFormat.  It is owned by this type and
    // must be dropped by calling `udat_close`.
    rep: *mut sys::UDateFormat,
}

impl Drop for UDateFormat {
    /// Implements `udat_close`
    fn drop(&mut self) {
        unsafe {
            versioned_function!(udat_close)(self.rep);
        }
    }
}

impl UDateFormat {
    /// Implements `udat_open`
    pub fn new(
        time_style: sys::UDateFormatStyle,
        date_style: sys::UDateFormatStyle,
        loc: &uloc::ULoc,
        tz_id: &ustring::UChar,
        pattern: &ustring::UChar,
    ) -> Result<Self, common::Error> {
        let mut status = common::Error::OK_CODE;
        let asciiz = loc.as_c_str();

        // Requires that all pointers be valid. Should be guaranteed by all
        // objects passed into this function.
        let date_format = unsafe {
            assert!(common::Error::is_ok(status));
            versioned_function!(udat_open)(
                time_style,
                date_style,
                asciiz.as_ptr(),
                tz_id.as_c_ptr(),
                tz_id.len() as i32,
                pattern.as_c_ptr(),
                pattern.len() as i32,
                &mut status,
            )
        };
        common::Error::ok_or_warning(status)?;
        Ok(UDateFormat { rep: date_format })
    }

    /// Implements `udat_setCalendar`
    pub fn set_calendar(&mut self, calendar: &ucal::UCalendar) {
        unsafe {
            versioned_function!(udat_setCalendar)(self.rep, calendar.as_c_calendar());
        };
    }

    /// Formats a date using this formatter.
    ///
    /// Implements `udat_format`
    pub fn format(&self, date_to_format: sys::UDate) -> Result<String, common::Error> {
        // This approach follows the recommended practice for unicode conversions: adopt a
        // resonably-sized buffer, then repeat the conversion if it fails the first time around.
        const CAP: usize = 1024;
        let mut status = common::Error::OK_CODE;
        let mut result = ustring::UChar::new_with_capacity(CAP);

        let mut field_position_unused =
            sys::UFieldPosition { field: 0, beginIndex: 0, endIndex: 0 };

        // Requires that result is a buffer at least as long as CAP and that
        // self.rep is a valid pointer to a `sys::UDateFormat` structure.
        let total_size = unsafe {
            assert!(common::Error::is_ok(status));
            versioned_function!(udat_format)(
                self.rep,
                date_to_format,
                result.as_mut_c_ptr(),
                CAP as i32,
                &mut field_position_unused,
                &mut status,
            )
        } as usize;
        common::Error::ok_or_warning(status)?;
        result.resize(total_size as usize);
        if total_size > CAP {
            // Requires that result is a buffer that has length and capacity of
            // exactly total_size, and that self.rep is a valid pointer to
            // a `UDateFormat`.
            unsafe {
                assert!(common::Error::is_ok(status));
                versioned_function!(udat_format)(
                    self.rep,
                    date_to_format,
                    result.as_mut_c_ptr(),
                    total_size as i32,
                    &mut field_position_unused,
                    &mut status,
                );
            };
            common::Error::ok_or_warning(status)?;
        }
        String::try_from(&result)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_format() {
        struct Test {
            locale: &'static str,
            timezone: &'static str,
            date: sys::UDate,
            result: &'static str,
        };
        let tests = vec![
            Test {
                locale: "fr_FR",
                timezone: "PST",
                date: 100.0,
                result:
                    "mercredi 31 décembre 1969 à 16:00:00 heure normale du Pacifique nord-américain",
            },
            Test {
                locale: "fr_FR",
                timezone: "PST",
                date: 100000.0,
                result:
                    "mercredi 31 décembre 1969 à 16:01:40 heure normale du Pacifique nord-américain",
            },
            Test {
                locale: "sr_RS",
                timezone: "PST",
                date: 100000.0,
                result:
                    "среда, 31. децембар 1969. 16:01:40 Северноамеричко пацифичко стандардно време",
            },
            Test {
                locale: "nl_NL",
                timezone: "PST",
                date: 100000.0,
                result: "woensdag 31 december 1969 om 16:01:40 Pacific-standaardtijd",
            },
        ];
        for t in tests.iter() {
            let loc = uloc::ULoc::try_from(t.locale).expect("locale created");
            let tz_id = ustring::UChar::try_from(t.timezone).expect("tz_id created");
            let pattern = ustring::UChar::try_from("").expect("pattern created");

            let fmt = UDateFormat::new(
                sys::UDateFormatStyle::UDAT_FULL,
                sys::UDateFormatStyle::UDAT_FULL,
                &loc,
                &tz_id,
                &pattern,
            )
            .expect("formatting succeeded");

            let actual = fmt.format(t.date).expect("formatting success");
            assert_eq!(actual, t.result);
        }
    }
}
