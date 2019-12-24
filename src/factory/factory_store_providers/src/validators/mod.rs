// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod pass;
mod size;
mod text;

use {
    crate::config::{ValidatorContext, ValidatorFileArgsMap},
    anyhow::Error,
    pass::PassValidator,
    size::SizeValidator,
    std::fmt::Debug,
    text::TextValidator,
    thiserror::Error,
};

#[derive(Debug, Error)]
pub enum ValidatorError {
    #[error("No matching validator for name {}", name)]
    NoMatchingValidator { name: String },
    #[error("Invalid args for validator: {}", cause)]
    InvalidValidatorArgs { cause: Error },
    #[error("Failed to validate contents for file {}: {}", file_name, cause)]
    FailedToValidate { cause: Error, file_name: String },
}

/// Trait for factory file validators.
///
/// All factory file validators are required to implement this trait. At a minimum, a validator must
/// be able to verify some basic properties of a file based on its name and/or contents.
pub trait Validator: Debug + Send + Sync {
    /// Validates a file based on its name and/or contents.
    ///
    /// If a file is successfully validated, `Ok` is returned, otherwise an appropriate
    /// `ValidatorError` is returned. This will almost always be `ValidatorError::FailedToValidate`
    /// which contains a more specific cause, but the implementing type may choose to return other
    /// errors if appropriate.
    fn validate(&self, file_name: &str, contents: &[u8]) -> Result<(), ValidatorError>;
}

/// Creates a new `ValidatorContext` based on the validator's `name` with the given `args_map`.
///
/// This function that should be called by dependents of the validation module to generate new
/// validators. Types that implement `Validator` should be added to this function and given a unique
/// name so that they can be intialized in the validation system.
pub fn new_validator_context_by_name(
    name: &str,
    args_map: ValidatorFileArgsMap,
) -> Result<ValidatorContext, ValidatorError> {
    let paths_to_validate = args_map.keys().map(|key| key.to_string()).collect();

    let validator = match name {
        "size" => {
            SizeValidator::from_file_args_map(args_map).map(|v| Box::new(v) as Box<dyn Validator>)
        }
        "pass" => Ok(Box::new(PassValidator) as Box<dyn Validator>),
        "text" => Ok(Box::new(TextValidator) as Box<dyn Validator>),
        _ => Err(ValidatorError::NoMatchingValidator { name: name.to_string() }),
    }?;

    Ok(ValidatorContext { name: name.to_string(), paths_to_validate, validator })
}
