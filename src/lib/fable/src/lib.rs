// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_export]
/// Shorthand for creating a FIDL table in a way that will not cause build breakages if new fields
/// are added to the table in the future.
///
/// - The values do not have to be wrapped in `Some`; this is inferred automatically.
/// - Empty fields can either be omitted or explicitly set to `None`.
/// - When a field name matches the name of an in-scope variable or parameter, a shorthand notation
///   is available (just like in Rust struct initializers).
///
/// Example:
/// ```rust
/// use fidl_fuchsia_intl::{CalendarId, LocaleId, Profile, TemperatureUnit};
///
/// let calendars = vec![CalendarId { id: "gregorian".to_string() }];
/// let time_zones = None;
///
/// let table = fable! {
///   Profile {
///     locales: Some(vec![LocaleId { id: "en-US".to_string() }]),
///     // `Some` can be omitted
///     temperature_unit: TemperatureUnit::Fahrenheit,
///     // Shorthand notation when the field and variable names match
///     calendars,
///     time_zones,
///   }
/// };
/// ```
macro_rules! fable {
    // Entry point
    ($fidl_type:path { $($rest:tt)* }) => {
        {
            use $fidl_type as FidlType;
            let mut _table = FidlType::EMPTY;
            fable!(@internal _table $($rest)*);
            _table
        }
    };

    // Full notation
    (@internal $table:ident $field:ident : $value:expr, $($rest:tt)*) => {
        $table.$field = ($value).into();
        fable!(@internal $table $($rest)*);
    };

    // Shorthand notation
    (@internal $table:ident $field:ident, $($rest:tt)*) => {
        $table.$field = $field.into();
        fable!(@internal $table $($rest)*);
    };

    // End
    (@internal $table:ident) => {};
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_intl::{CalendarId, LocaleId, Profile, TemperatureUnit},
    };

    #[test]
    fn test_combinations() {
        let mut expected = Profile::EMPTY;
        expected.locales = Some(vec![LocaleId { id: "en-US".to_string() }]);
        expected.temperature_unit = Some(TemperatureUnit::Fahrenheit);
        expected.calendars = Some(vec![CalendarId { id: "gregory".to_string() }]);

        let calendars = vec![CalendarId { id: "gregory".to_string() }];
        let time_zones = None;

        let actual = fable! {
            Profile {
                locales: Some(vec![LocaleId { id: "en-US".to_string() }]),
                temperature_unit: TemperatureUnit::Fahrenheit,
                calendars,
                time_zones,
            }
        };

        assert_eq!(actual, expected);
    }
}
