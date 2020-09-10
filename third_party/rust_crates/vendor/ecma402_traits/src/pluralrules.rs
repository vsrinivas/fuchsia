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

/// Contains the API configuration as prescribed by [ECMA 402][ecma].
///
///    [ecma]: https://www.ecma-international.org/publications/standards/Ecma-402.htm
///
/// The meaning of the options is the same as in the similarly named
/// options in the JS version.
///
/// See [Options] for the contents of the options.  See the [PluralRules::try_new]
/// for the use of the options.
pub mod options {
    /// The type of plural rules with respect to cardinality and ordinality.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum Type {
        /// As in "one, two, three, ..."
        Cardinal,
        /// As in: "first, second, third, ..."
        Ordinal,
    }
}

/// The options set by the user at construction time.  See discussion at the top level
/// about the name choice.  Provides as a "bag of options" since we don't expect any
/// implementations to be attached to this struct.
///
/// The default values of all the options are prescribed in by the [TC39 report][tc39lf].
///
///   [tc39lf]: https://tc39.es/proposal-intl-list-format/#sec-Intl.ListFormat
#[derive(Eq, PartialEq, Debug, Clone)]
pub struct Options {
    /// Selects a [options::Type] for the formatted list.  If unset, defaults to
    /// [options::Type::Cardinal].
    pub in_type: options::Type,
    pub minimum_integer_digits: u8,
    pub minimum_fraction_digits: u8,
    pub maximum_fraction_digits: u8,
    pub minimum_significant_digits: u8,
    pub maximum_significant_digits: u8,
}

/// Allows the use of `pluralrules::Rules::try_new(..., Default::default())`.
impl Default for Options {
    /// Gets the default values of [Options] if omitted at setup.  The
    /// default values are prescribed in by the [TC39 report][tc39lf].
    ///
    ///   [tc39lf]: https://tc39.es/proposal-intl-list-format/#sec-Intl.ListFormat
    fn default() -> Self {
        Options {
            in_type: options::Type::Cardinal,
            minimum_integer_digits: 1,
            minimum_fraction_digits: 0,
            maximum_fraction_digits: 3,
            minimum_significant_digits: 1,
            maximum_significant_digits: 21,
        }
    }
}

use std::fmt;

/// Returns the plural class of each of the supplied number.
pub trait PluralRules {
    /// The type of error reported, if any.
    type Error: std::error::Error;

    /// Creates a new [PluralRules].
    ///
    /// Creation may fail, for example, if the locale-specific data is not loaded, or if
    /// the supplied options are inconsistent.
    fn try_new<L>(l: L, opts: Options) -> Result<Self, Self::Error>
    where
        L: crate::Locale,
        Self: Sized;

    /// Formats the plural class of `number` into the supplied `writer`.
    ///
    /// The function implements [`Intl.PluralRules`][plr] from [ECMA 402][ecma].
    ///
    ///    [plr]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/PluralRules
    ///    [ecma]: https://www.ecma-international.org/publications/standards/Ecma-402.htm
    fn select<W>(&self, number: f64, writer: &mut W) -> fmt::Result
    where
        W: fmt::Write;
}
