// Copyright 2020 Google LLC
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

use std::fmt;

/// Contains the API configuration as prescribed by [ECMA 402][ecma].
///
///    [ecma]: https://www.ecma-international.org/publications/standards/Ecma-402.htm
///
/// The meaning of the options is the same as in the similarly named
/// options in the JS version.
///
/// See [Options] for the contents of the options.  See the [NumberFormat::try_new]
/// for the use of the options.
pub mod options {

    /// Controls whether short or long form display is used.  Short is slightly
    /// more economical with spacing.  Only used when notation is Compact.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum CompactDisplay {
        /// Default.
        Short,
        /// Long display layout.
        Long,
    }

    /// The ISO currency code for a currency, if the currency formatting is being used.
    /// For example "EUR" or "USD".
    ///
    /// The value entered as currency is not validated.  This responsibility is
    /// delegated to the implementor.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub struct Currency(pub String);

    impl From<&str> for Currency {
        fn from(s: &str) -> Self {
            Self(s.to_string())
        }
    }

    /// Controls the display of currency information.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum CurrencyDisplay {
        /// Use a localized currency symbol such as €, this is the default value.
        Symbol,
        /// Narrow format symbol ("$100" rather than "US$100").
        NarrowSymbol,
        /// Use the ISO currency code.
        Code,
        /// Use a localized currency name, e.g. "dollar".
        Name,
    }

    /// Controls the display of currency sign.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum CurrencySign {
        /// Uses accounting notation such as `($100)` to mean "minus one hundred dollars".
        Accounting,
        /// Uses standard notation such as `$-100`.
        Standard,
    }

    /// Controls the number formatting.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum Notation {
        /// Example: 10,000.00.  This is the default.
        Standard,
        /// Example: 1e+4
        Scientific,
        /// Example: 10k
        Engineering,
        /// Example: 10000
        Compact,
    }

    /// Controls the number formatting.
    ///
    /// Possible values include: "arab", "arabext", " bali", "beng", "deva", "fullwide", "gujr",
    /// "guru", "hanidec", "khmr", " knda", "laoo", "latn", "limb", "mlym", " mong", "mymr",
    /// "orya", "tamldec", " telu", "thai", "tibt".
    ///
    /// The value entered as currency is not validated.  This responsibility is
    /// delegated to the implementor.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub struct NumberingSystem(pub String);

    impl From<&str> for NumberingSystem {
        fn from(s: &str) -> Self {
            Self(s.to_string())
        }
    }

    impl Default for NumberingSystem {
        fn default() -> Self {
            NumberingSystem("latn".to_string())
        }
    }

    /// When to display the sign of the number.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum SignDisplay {
        /// Display for negative numbers only.
        Auto,
        /// Never display the sign.
        Never,
        /// Always display the sign (even plus).
        Always,
        /// Display for positive and negative numbers, but not zero.
        ExceptZero,
    }

    /// When to display the sign of the number.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum Style {
        /// For plain number formatting.
        Decimal,
        /// For currency formatting ("10 EUR")
        Currency,
        /// For percentage formatting ("10%")
        Percent,
        /// For unit formatting ("10 meters")
        Unit,
    }

    /// The unit formatting style, when [Style::Unit] is used.
    ///
    /// Possible values are core unit identifiers, defined in UTS #35, Part 2, Section 6. A subset
    /// of units from the full list was selected for use in ECMAScript. Pairs of simple units can
    /// be concatenated with "-per-" to make a compound unit. There is no default value; if the
    /// style is "unit", the unit property must be provided.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum UnitDisplay {
        /// "16 litres"
        Long,
        /// "16 l"
        Short,
        /// "16l"
        Narrow,
    }

    /// The unit to use when [Style::Unit] is selected as [Style].
    ///
    /// Possible values are core unit identifiers, defined in UTS #35, Part 2, Section 6. A subset
    /// of units from the full list was selected for use in ECMAScript. Pairs of simple units can
    /// be concatenated with "-per-" to make a compound unit. There is no default value; if the
    /// style is "unit", the unit property must be provided.
    #[derive(Debug, Clone)]
    pub struct Unit(pub String);
}

/// The options set by the user at construction time.  See discussion at the top level
/// about the name choice.  Provides as a "bag of options" since we don't expect any
/// implementations to be attached to this struct.
///
/// The default values of all the options are prescribed in by the [TC39 report][tc39lf].
///
///   [tc39lf]: https://tc39.es/proposal-intl-number-format/#sec-Intl.NumberFormat
#[derive(Debug, Clone)]
pub struct Options {
    pub compact_display: Option<options::CompactDisplay>,
    pub currency: Option<options::Currency>,
    pub currency_display: options::CurrencyDisplay,
    pub currency_sign: options::CurrencySign,
    pub notation: options::Notation,
    pub numbering_system: Option<options::NumberingSystem>,
    pub sign_display: options::SignDisplay,
    pub style: options::Style,
    pub unit: Option<options::Unit>,

    pub minimum_integer_digits: Option<u8>,
    pub minimum_fraction_digits: Option<u8>,
    pub maximum_fraction_digits: Option<u8>,
    pub minimum_significant_digits: Option<u8>,
    pub maximum_significant_digits: Option<u8>,
}

impl Default for Options {
    /// Gets the default values of [Options] if omitted at setup.  The
    /// default values are prescribed in by the [TC39 report][tc39lf].
    ///
    ///   [tc39lf]: https://tc39.es/proposal-intl-list-format/#sec-Intl.ListFormat
    fn default() -> Self {
        Options {
            compact_display: None,
            currency: None,
            currency_display: options::CurrencyDisplay::Symbol,
            currency_sign: options::CurrencySign::Standard,
            notation: options::Notation::Standard,
            sign_display: options::SignDisplay::Auto,
            style: options::Style::Decimal,
            unit: None,
            numbering_system: None,
            minimum_integer_digits: None,
            minimum_fraction_digits: None,
            maximum_fraction_digits: None,
            minimum_significant_digits: None,
            maximum_significant_digits: None,
        }
    }
}

/// Formats number based on the rules configured on initialization.
pub trait NumberFormat {
    /// The type of error reported, if any.
    type Error: std::error::Error;

    /// Creates a new [NumberFormat].
    ///
    /// Creation may fail, for example, if the locale-specific data is not loaded, or if
    /// the supplied options are inconsistent.
    fn try_new<L>(l: L, opts: Options) -> Result<Self, Self::Error>
    where
        L: crate::Locale,
        Self: Sized;

    /// Formats `number` into the supplied `writer`.
    ///
    /// The function implements [`Intl.NumberFormat`][nfmt] from [ECMA 402][ecma].
    ///
    ///    [nfmt]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/NumberFormat
    ///    [ecma]: https://www.ecma-international.org/publications/standards/Ecma-402.htm
    fn format<W>(&self, number: f64, writer: &mut W) -> fmt::Result
    where
        W: fmt::Write;
}
