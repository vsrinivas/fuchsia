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

/// This trait contains the common features of the Locale object that must be shared among
/// all the implementations.  Every implementor of `listformat` should provide their
/// own version of [Locale], and should ensure that it implements [Locale]. as
/// specified here.
///
/// For the time being we agreed that a [Locale] *must* be convertible into its string
/// form, using `Display`.
pub trait Locale: fmt::Display {}

/// A Rust implementation of ECMA 402 ListFormat API.
///
/// The [listformat] mod contains all the needed implementation bits for `Intl.ListFormat`.
///
pub mod listformat;

/// A Rust implementation of ECMA 402 PluralRules API.
///
/// The [pluralrules] mod contains all the needed implementation bits for `Intl.PluralRules`.
pub mod pluralrules;

/// A Rust implementation of ECMA 402 NumberFormat API.
///
/// The [numberformat] mod contains all the needed implementation bits for `Intl.NumberFormat`.
pub mod numberformat;

/// A Rust implementation of the ECMA402 Collator API.
///
/// The [collator] mod contains all the needed implementation bits for `Intl.Collator`.
pub mod collator;
