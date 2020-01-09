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

//! Contains implementations of functions from ICU's `udat.h`.
//!
//! All functions that take `ustring::UChar` instead of a rust string reference do so for
//! efficiency.  The encoding of `ustring::UChar` is uniform (in contrast to say UTF-8), so
//! repeated manipulation of that string does not waste CPU cycles.
//!
//! For detailed instructions for date and time formatting please refer to the [original Unicode
//! project documentation for date and time formatting](http://userguide.icu-project.org/formatparse/datetime)

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

/// Parsed contains output of the call to `UDateFormat::parse_from_position`.
pub struct Parsed {
    /// The point in time parsed out of the date-time string.
    date: sys::UDate,

    /// The position in the input string at which the parsing ended.
    end_position: usize,
}

impl Parsed {
    /// Returns the date resulting from a call to `UDateFormat::parse_from_position`.
    pub fn date(&self) -> sys::UDate {
        self.date
    }
    /// Returns the end position resulting from a call to `UDateFormat::parse_from_position`.
    pub fn end_position(&self) -> usize {
        self.end_position
    }
}

impl UDateFormat {
    /// Creates a new `UDateFormat` based on the provided styles.
    ///
    /// Neither time_style nor date_style may be `UDAT_PATTERN`.  If you need
    /// formatting with a pattern, use instead `new_with_pattern`.
    /// Implements `udat_open`
    pub fn new_with_styles(
        time_style: sys::UDateFormatStyle,
        date_style: sys::UDateFormatStyle,
        loc: &uloc::ULoc,
        tz_id: &ustring::UChar,
    ) -> Result<Self, common::Error> {
        assert_ne!(
            time_style,
            sys::UDateFormatStyle::UDAT_PATTERN,
            "programmer error: time_style may not be UDAT_PATTERN"
        );
        assert_ne!(
            date_style,
            sys::UDateFormatStyle::UDAT_PATTERN,
            "programmer error: date_style may not be UDAT_PATTERN"
        );
        // pattern is ignored if time_style or date_style aren't equal to pattern.
        let pattern = ustring::UChar::try_from("").expect("pattern created");

        Self::new_internal(time_style, date_style, loc, tz_id, &pattern)
    }

    /// Creates a new `UDateFormat` based on the provided pattern.
    ///
    /// One example pattern is: "yyyy-MM-dd'T'HH:mm:ssXX".
    ///
    /// Implements `udat_open`
    pub fn new_with_pattern(
        loc: &uloc::ULoc,
        tz_id: &ustring::UChar,
        pattern: &ustring::UChar,
    ) -> Result<Self, common::Error> {
        Self::new_internal(
            /*timestyle=*/ sys::UDateFormatStyle::UDAT_PATTERN,
            /*datestyle=*/ sys::UDateFormatStyle::UDAT_PATTERN,
            loc,
            tz_id,
            pattern,
        )
    }

    // Generalized constructor based on `udat_open`.  It is hidden from public eye because its
    // input parameters are not orthogonal.
    //
    // Implements `udat_open`
    fn new_internal(
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

    /// Parses a date-time given as a string into a `sys::UDate` timestamp.
    ///
    /// This version of date parsing does not allow reuse of the input parameters so it is less
    /// useful for purposes that are not one-shot. See somewhat more detailed `parse_from_position`
    /// instead.
    ///
    /// Implements `udat_parse`
    pub fn parse(&self, datetime: &str) -> Result<sys::UDate, common::Error> {
        let datetime_uc = ustring::UChar::try_from(datetime)?;
        self.parse_from_position(&datetime_uc, 0).map(|r| r.date)
    }

    /// Parses a date-time given as a string into a `sys::UDate` timestamp and a position
    /// indicating the first index into `datetime` that was not consumed in parsing.  The
    /// `position` parameter indicates the index into `datetime` that parsing should start from.
    ///
    /// Implements `udat_parse`
    pub fn parse_from_position(
        &self,
        datetime: &ustring::UChar,
        position: usize,
    ) -> Result<Parsed, common::Error> {
        let mut status = common::Error::OK_CODE;
        let mut _unused_pos: i32 = 0;

        // We do not expect positions that exceed the range of i32.
        let mut end_position: i32 = position as i32;
        // Requires that self.rep, and datetime are valid values.  Ensured by
        // the guaranteses of UDateFormat and ustring::UChar.
        let date = unsafe {
            versioned_function!(udat_parse)(
                self.rep,
                datetime.as_c_ptr(),
                datetime.len() as i32,
                &mut end_position,
                &mut status,
            )
        };
        common::Error::ok_or_warning(status)?;
        Ok(Parsed {
            date,
            end_position: end_position as usize,
        })
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

        let mut field_position_unused = sys::UFieldPosition {
            field: 0,
            beginIndex: 0,
            endIndex: 0,
        };

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
    fn test_format_default_calendar() -> Result<(), common::Error> {
        #[derive(Debug)]
        struct Test {
            name: &'static str,
            locale: &'static str,
            timezone: &'static str,
            date: sys::UDate,
            expected: &'static str,
            calendar: Option<ucal::UCalendar>,
        };
        let tests = vec![
            Test {
                name: "French default",
                locale: "fr-FR",
                timezone: "America/Los_Angeles",
                date: 100.0,
                expected:
                    "mercredi 31 décembre 1969 à 16:00:00 heure normale du Pacifique nord-américain",
                calendar: None,
            },
            Test {
                name: "French default, a few hours later",
                locale: "fr-FR",
                timezone: "America/Los_Angeles",
                date: 100000.0,
                expected:
                    "mercredi 31 décembre 1969 à 16:01:40 heure normale du Pacifique nord-américain",
                calendar: None,
            },
            Test {
                name: "Serbian default",
                locale: "sr-RS",
                timezone: "America/Los_Angeles",
                date: 100000.0,
                expected:
                    "среда, 31. децембар 1969. 16:01:40 Северноамеричко пацифичко стандардно време",
                calendar: None,
            },
            Test {
                name: "Dutch default",
                locale: "nl-NL",
                timezone: "America/Los_Angeles",
                date: 100000.0,
                expected: "woensdag 31 december 1969 om 16:01:40 Pacific-standaardtijd",
                calendar: None,
            },
            Test {
                name: "Dutch islamic overrides locale calendar and timezone",
                locale: "nl-NL-u-ca-gregorian",
                timezone: "America/Los_Angeles",
                date: 100000.0,
                expected: "woensdag 22 Sjawal 1389 om 16:01:40 Pacific-standaardtijd",
                calendar: Some(
                    ucal::UCalendar::new(
                        "America/Los_Angeles",
                        "und-u-ca-islamic",
                        sys::UCalendarType::UCAL_DEFAULT,
                    )
                    .expect("created calendar"),
                ),
            },
            Test {
                name: "Dutch islamic take from locale",
                locale: "nl-NL-u-ca-islamic",
                timezone: "America/Los_Angeles",
                date: 200000.0,
                expected: "woensdag 22 Sjawal 1389 AH om 16:03:20 Pacific-standaardtijd",
                calendar: None,
            },
            Test {
                name: "Dutch islamic take from locale",
                locale: "nl-NL-u-ca-islamic",
                timezone: "America/Los_Angeles",
                date: 200000.0,
                expected: "woensdag 22 Sjawal 1389 AH om 16:03:20 Pacific-standaardtijd",
                calendar: None,
            },
            Test {
                name: "Arabic",
                locale: "ar-SA-u-ca-islamic",
                timezone: "America/Los_Angeles",
                date: 200000.0,
                expected: "الأربعاء، ٢٢ شوال ١٣٨٩ هـ ٤:٠٣:٢٠ م توقيت المحيط الهادي الرسمي",
                calendar: None,
            },
        ];
        for t in tests {
            let loc = uloc::ULoc::try_from(t.locale)?;
            let tz_id = ustring::UChar::try_from(t.timezone)?;

            let mut fmt = UDateFormat::new_with_styles(
                sys::UDateFormatStyle::UDAT_FULL,
                sys::UDateFormatStyle::UDAT_FULL,
                &loc,
                &tz_id,
            )?;
            if let Some(ref cal) = t.calendar {
                fmt.set_calendar(&cal);
            }

            let fmt = fmt;
            let actual = fmt.format(t.date)?;
            assert_eq!(actual, t.expected, "test: {:?}", t);
        }
        Ok(())
    }

    #[test]
    fn test_format_pattern() -> Result<(), common::Error> {
        #[derive(Debug)]
        struct Test {
            date: sys::UDate,
            pattern: &'static str,
            expected: &'static str,
        };
        let tests = vec![
            Test {
                date: 100.0,
                pattern: "yyyy-MM-dd'T'HH:mm:ssXX",
                expected: "1969-12-31T19:00:00-0500",
            },
            Test {
                date: 100000.0,
                pattern: "yyyy-MM-dd'T'HH",
                expected: "1969-12-31T19",
            },
            Test {
                date: 100000.0,
                pattern: "V",
                expected: "usnyc",
            },
        ];
        let loc = uloc::ULoc::try_from("en-US")?;
        let tz_id = ustring::UChar::try_from("America/New_York")?;
        for t in tests {
            let pattern = ustring::UChar::try_from(t.pattern)?;
            let fmt = UDateFormat::new_with_pattern(&loc, &tz_id, &pattern)?;
            let actual = fmt.format(t.date)?;
            assert_eq!(
                actual, t.expected,
                "want: {:?}, got: {:?}",
                t.expected, actual
            );
        }
        Ok(())
    }

    #[test]
    fn parse_utf8() -> Result<(), common::Error> {
        #[derive(Debug)]
        struct Test {
            input: &'static str,
            pattern: &'static str,
            expected: sys::UDate,
        };
        let tests: Vec<Test> = vec![
            Test {
                input: "2018-10-30T15:30:00-07:00",
                pattern: "yyyy-MM-dd'T'HH:mm:ssXX",
                expected: 1540938600000.0 as sys::UDate,
            },
            Test {
                input: "2018-10-30T15:30:00-07:00",
                // The entire "time" portion of this string is not used.
                pattern: "yyyy-MM-dd",
                expected: 1540872000000.0 as sys::UDate,
            },
        ];

        let loc = uloc::ULoc::try_from("en-US")?;
        let tz_id = ustring::UChar::try_from("America/New_York")?;

        for test in tests {
            let pattern = ustring::UChar::try_from(test.pattern)?;
            let format = UDateFormat::new_with_pattern(&loc, &tz_id, &pattern)?;
            let actual = format.parse(test.input)?;
            assert_eq!(
                actual, test.expected,
                "want: {:?}, got: {:?}",
                test.expected, actual
            )
        }
        Ok(())
    }
}
