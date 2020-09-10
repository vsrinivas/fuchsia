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

/// Contains the API configuration as prescribed by ECMA 402.
///
/// The meaning of the options is the same as in the similarly named
/// options in the JS version.
///
/// See [Options] for the contents of the options.  See the [Format::try_new]
/// for the use of the options.
pub mod options {
    /// Chooses the list formatting approach.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum Style {
        Long,
        Short,
        Narrow,
    }
    /// Chooses between "this, that and other", and "this, that or other".
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum Type {
        /// "This, that and other".
        Conjunction,
        /// "This, that or other".
        Disjunction,
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
    /// Selects a [options::Style] for the formatted list.  If unset, defaults
    /// to [options::Style::Long].
    pub style: options::Style,
    /// Selects a [options::Type] for the formatted list.  If unset, defaults to
    /// [options::Type::Conjunction].
    pub in_type: options::Type,
}

/// Allows the use of `listformat::Format::try_new(..., Default::default())`.
impl Default for Options {
    /// Gets the default values of [Options] if omitted at setup.  The
    /// default values are prescribed in by the [TC39 report][tc39lf].
    ///
    ///   [tc39lf]: https://tc39.es/proposal-intl-list-format/#sec-Intl.ListFormat
    fn default() -> Self {
        Options {
            style: options::Style::Long,
            in_type: options::Type::Conjunction,
        }
    }
}

use std::fmt;

/// The package workhorse: formats supplied pieces of text into an ergonomically formatted
/// list.
///
/// While ECMA 402 originally has functions under `Intl`, we probably want to
/// obtain a separate factory from each implementor.
///
/// Purposely omitted:
///
/// - `supported_locales_of`.
pub trait Format {
    /// The type of error reported, if any.
    type Error: std::error::Error;

    /// Creates a new [Format].
    ///
    /// Creation may fail, for example, if the locale-specific data is not loaded, or if
    /// the supplied options are inconsistent.
    fn try_new<L>(l: L, opts: Options) -> Result<Self, Self::Error>
    where
        L: crate::Locale,
        Self: Sized;

    /// Formats `list` into the supplied standard `writer` [fmt::Write].
    ///
    /// The original [ECMA 402 function][ecma402fmt] returns a string.  This is likely the only
    /// reasonably generic option in JavaScript so it is adequate.  In Rust, however, it is
    /// possible to pass in a standard formatting strategy (through `writer`).
    ///
    ///   [ecma402fmt]:
    ///   https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/ListFormat/format
    ///
    /// This makes it unnecessary for [Format] to implement its own, and can
    /// completely avoid constructing any intermediary representation.  This, in turn,
    /// allows the user to provide a purpose built formatter, or an "exotic" one if needed.
    ///
    /// A purpose built formatter could be one that formats into a fixed-size buffer; or
    /// another that knows how to format strings into a DOM.  If ECMA 402 compatibility is
    /// needed, the user can force formatting into a string by passing the appropriate
    /// formatter.
    ///
    /// > Note:
    /// > - Should there be a convenience method that prints to string specifically?
    /// > - Do we need `format_into_parts`?
    fn format<I, L, W>(&self, list: L, writer: &mut W) -> fmt::Result
    where
        I: fmt::Display,
        L: IntoIterator<Item = I>,
        W: fmt::Write;
}
