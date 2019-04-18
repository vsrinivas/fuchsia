// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! fidl table validation tools.

// TODO(turnage): Infer optionality based on parsing for
//                "Option<" in field types.
// TODO(turnage): Optionally Generate Into<> implementation
//                to return to the original FIDL type.

pub use fidl_table_validation_derive::ValidFidlTable;

/// Validations on `T` that can be run during construction of a validated
/// fidl table by adding the attribute `#[fidl_table_validator(YourImpl)]`
/// to the valid struct.
pub trait Validate<T> {
    type Error;
    fn validate(candidate: &T) -> std::result::Result<(), Self::Error>;
}
