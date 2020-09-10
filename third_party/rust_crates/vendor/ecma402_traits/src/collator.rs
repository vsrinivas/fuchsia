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
/// See [Options] for the contents of the options.  See the [Collator::try_new]
/// for the use of the options.
pub mod options {

    /// Set the intended usage for this collation.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum Usage {
        /// Use collation for sorting. Default.
        Sort,
        /// Use collation for searching.
        Search,
    }

    /// Set the sensitivity of the collation.
    ///
    /// Which differences in the strings should lead to non-zero result values.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum Sensitivity {
        /// Only strings that differ in base letters compare as unequal.
        /// Examples: a ≠ b, a = á, a = A.
        Base,
        /// Only strings that differ in base letters or accents and other diacritic marks compare
        /// as unequal. Examples: a ≠ b, a ≠ á, a = A.
        Accent,
        /// Only strings that differ in base letters or case compare as unequal.
        /// Examples: a ≠ b, a = á, a ≠ A.
        Case,
        /// Strings that differ in base letters, accents and other diacritic marks, or case compare
        /// as unequal. Other differences may also be taken into consideration.
        /// Examples: a ≠ b, a ≠ á, a ≠ A.
        Variant,
    }

    /// Whether punctuation should be ignored.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum Punctuation {
        /// Ignore punctuation.
        Ignore,
        /// Honor punctuation.
        Honor,
    }

    /// Whether numeric collation should be used, such that "1" < "2" < "10".
    ///
    /// This option can be set through an options property or through a Unicode extension key; if
    /// both are provided, the options property takes precedence. Implementations are not required
    /// to support this property.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum Numeric {
        /// Use numeric comparison.
        Use,
        /// Do not use numeric comparision.
        Ignore,
    }

    /// Whether upper case or lower case should sort first.
    ///
    /// This option can be set through an options property or through a Unicode extension key; if
    /// both are provided, the options property takes precedence. Implementations are not required
    /// to support this property.
    #[derive(Eq, PartialEq, Debug, Clone)]
    pub enum CaseFirst {
        /// Sort upper case first.
        Upper,
        /// Sort lower case first.
        Lower,
        /// Use locale default for case sorting.
        False,
    }
}

/// The options set by the user at construction time.  See discussion at the top level
/// about the name choice.  Provides as a "bag of options" since we don't expect any
/// implementations to be attached to this struct.
///
/// The default values of all the options are prescribed in by the [TC39 report][tc39lf].
///
/// [tc39lf] https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/Collator/Collator
#[derive(Eq, PartialEq, Debug, Clone)]
pub struct Options {
    pub usage: options::Usage,
    pub sensitivity: options::Sensitivity,
    pub punctuation: options::Punctuation,
    pub numeric: options::Numeric,
    pub case_first: options::CaseFirst,
}

impl Default for Options {
    /// Gets the default values of [Options] if omitted at setup.  The
    /// default values are prescribed by the TC39.  See [Collator][tc39c].
    ///
    /// [tc39c]: https://developer.mozilla.org/en-US/docs/Web/JavaScript/Reference/Global_Objects/Intl/Collator/Collator
    fn default() -> Self {
        Options {
            usage: options::Usage::Sort,
            sensitivity: options::Sensitivity::Variant,
            punctuation: options::Punctuation::Honor,
            numeric: options::Numeric::Ignore,
            case_first: options::CaseFirst::False,
        }
    }
}

pub trait Collator {
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

    /// Compares two strings according to the sort order of this [Collator].
    ///
    /// Returns 0 if `first` and `second` are equal.  Returns a negative value
    /// if `first` is less, and a positive value of `second` is less.
    fn compare<P, Q>(first: P, second: Q) -> i8
    where
        P: AsRef<str>,
        Q: AsRef<str>;
}
