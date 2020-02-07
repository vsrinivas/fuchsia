// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! fidl table validation tools.
//!
//! This crate's macro generates code to validate fidl tables.
//!
//! Import using `fidl_table_validation::*` to inherit the macro's imports.
//!
//! ## Basic Example
//!
//! ```
//! // Some fidl table defined somewhere...
//! struct FidlTable {
//!     required: Option<usize>,
//!     optional: Option<usize>,
//!     has_default: Option<usize>,
//! }
//!
//! #[derive(ValidFidlTable)]
//! #[fidl_table_src(FidlHello)]
//! struct ValidatedFidlTable {
//!     // The default is #[fidl_field_type(required)]
//!     required: usize,
//!     #[fidl_field_type(optional)]
//!     optional: Option<usize>,
//!     #[fidl_field_type(default = 22)]
//!     has_default: usize,
//! }
//! ```
//!
//! This code generates a [TryFrom][std::convert::TryFrom]<FidlTable> implementation for
//! `ValidatedFidlTable`:
//!
//! ```
//! pub enum FidlTableValidationError {
//!     MissingField(FidlTableMissingFieldError)
//! }
//!
//! impl TryFrom<FidlTable> for ValidatedFidlTable {
//!     type Error = FidlTableValidationError;
//!     fn try_from(src: FidlTable) -> Result<ValidatedFidlTable, Self::Error> { .. }
//! }
//! ```
//! and also a [From][std::convert::From]<ValidatedFidlTable> implementation for `FidlTable`,
//! so you can get a `FidlTable` using `validated.into()`.
//!
//! ## Custom Validations
//!
//! When tables have logical relationships between fields that must be
//! checked, you can use a custom validator:
//!
//! ```
//! struct FidlTableValidator;
//!
//! impl Validate<ValidatedFidlTable> for FidlTableValidator {
//!     type Error = String; // Can be any error type.
//!     fn validate(candidate: &ValidatedFidlTable) -> Result<(), Self::Error> {
//!         match candidate.required {
//!             10 => {
//!                 Err(String::from("10 is not a valid value!"))
//!             }
//!             _ => Ok(())
//!         }
//!     }
//! }
//!
//! #[fidl_table_src(FidlHello)]
//! #[fidl_table_validator(FidlTableValidator)]
//! struct ValidFidlTable {
//! ...
//! ```
//!
///! This adds a `Logical(YourErrorType)` variant to the generated error enum.
// TODO(turnage): Infer optionality based on parsing for
//                "Option<" in field types.
pub use fidl_table_validation_derive::ValidFidlTable;

pub use anyhow;
pub use fidl::encoding::Decodable;

/// Validations on `T` that can be run during construction of a validated
/// fidl table by adding the attribute `#[fidl_table_validator(YourImpl)]`
/// to the valid struct.
pub trait Validate<T> {
    type Error;
    fn validate(candidate: &T) -> std::result::Result<(), Self::Error>;
}
