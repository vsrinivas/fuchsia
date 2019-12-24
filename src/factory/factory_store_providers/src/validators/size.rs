// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config::ValidatorFileArgsMap,
    crate::validators::{Validator, ValidatorError},
    anyhow::{format_err, Error},
    fuchsia_syslog as syslog,
    serde_json::{map::Map, value::Value},
    std::collections::HashMap,
};

/// A constraint for the size of the contents of a file.
///
/// Either min or max may be `None` but not both. Both min and max may be the same value, but max
/// should never be less than min if it exists.
#[derive(Debug)]
struct SizeConstraint {
    pub min: Option<u64>,
    pub max: Option<u64>,
}
impl SizeConstraint {
    /// Creates a `SizeConstraint` based on the `size` value.
    ///
    /// Sets min and max to `size`.
    pub fn from_size(size: u64) -> Self {
        Self { min: Some(size), max: Some(size) }
    }

    /// Creates a `SizeConstraint` from the given `map` value.
    ///
    /// `map` is expected to have one or both of the following entries:
    ///  * min - Minimum size
    ///  * max - Maximum size
    ///  All values are expected to be convertable to `u64` values.
    pub fn from_map(map: &Map<String, Value>) -> Result<Self, Error> {
        let max = map.get("max").and_then(|val| val.as_u64());
        let min = map.get("min").and_then(|val| val.as_u64());

        match (min, max) {
            (None, None) => Err(format_err!("max or min must be an unsigned 64-bit integer")),
            (Some(min), Some(max)) if max < min => Err(format_err!("max cannot be less than min")),
            (min, max) => Ok(Self { min, max }),
        }
    }

    /// Tests whether the given `size` satisfies this `SizeConstraint`.
    pub fn test(&self, size: u64) -> Result<(), Error> {
        if let Some(min_size) = self.min {
            if size < min_size {
                return Err(format_err!(
                    "Size of contents is {}, must be at least {}",
                    size,
                    min_size,
                ));
            }
        }

        if let Some(max_size) = self.max {
            if size > max_size {
                return Err(format_err!(
                    "Size of contents is {}, must be at most {}",
                    size,
                    max_size,
                ));
            }
        }
        Ok(())
    }
}

/// A validator that tests whether a set of files satisfies a specific size requirement.
///
/// Files may be validated for a combination of their minimum and maximum size or exact size.
/// Any file passed to this validator without an associated size constraint will result in a
/// validation failure.
#[derive(Debug)]
pub struct SizeValidator {
    file_sizes: HashMap<String, SizeConstraint>,
}
impl SizeValidator {
    /// Creates a `SizeValidator` based on the given `args_map`.
    ///
    /// The `args_map` map is expected to contain entries with the following format:
    ///   filename : size | size_constraint
    /// where
    /// * filename - a string with the file's name
    /// * size - u64 integer of the file's exact size
    /// * size_constraint is an object with one or both of the following properties:
    ///     * min - Minimum size of the file in bytes
    ///     * max - Maximum size of the file in bytes
    pub fn from_file_args_map(args_map: ValidatorFileArgsMap) -> Result<Self, ValidatorError> {
        if args_map.is_empty() {
            return Err(ValidatorError::InvalidValidatorArgs {
                cause: format_err!("At least one file with size must be given in args"),
            });
        }

        let mut file_sizes = HashMap::new();
        for (file_name, size_value) in args_map.into_iter() {
            if size_value.is_null() {
                return Err(ValidatorError::InvalidValidatorArgs {
                    cause: format_err!("Size or min/max missing for {} file", &file_name),
                });
            }

            let size = size_value
                .as_u64()
                .map_or_else(
                    || match size_value.as_object() {
                        Some(obj) => SizeConstraint::from_map(obj),
                        None => Err(format_err!(
                            "Expected size value for {} to be a JSON object",
                            &file_name
                        )),
                    },
                    |v| Ok(SizeConstraint::from_size(v)),
                )
                .map_err(|cause| ValidatorError::InvalidValidatorArgs { cause })?;

            file_sizes.insert(file_name, size);
        }

        Ok(Self { file_sizes })
    }
}

impl Validator for SizeValidator {
    /// Validates a file based on its size.
    ///
    /// Any file passed to this validator whose name is not associated with a size constraint will
    /// result in a `ValidatorError::FailedToValidate` error.
    fn validate(&self, file_name: &str, contents: &[u8]) -> Result<(), ValidatorError> {
        syslog::fx_log_info!("Validating size of {}", file_name);

        let size_constraint =
            self.file_sizes.get(file_name).ok_or_else(|| ValidatorError::FailedToValidate {
                cause: format_err!("{} not found in size file list", file_name),
                file_name: file_name.to_string(),
            })?;

        let actual_size = contents.len() as u64;

        size_constraint.test(actual_size).map_err(|err| ValidatorError::FailedToValidate {
            cause: err,
            file_name: file_name.to_string(),
        })
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::config::ValidatorFileArgsMap, serde_json::json};

    const TO_VALIDATE1: &str = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const TO_VALIDATE2: &str = "Здравствуйте";

    #[test]
    fn create_from_file_args_map_missing_args_fails() {
        let args = ValidatorFileArgsMap::new();
        SizeValidator::from_file_args_map(args).unwrap_err();
    }

    #[test]
    fn create_from_file_args_map_invalid_arg_value_fails() {
        let mut args = ValidatorFileArgsMap::new();
        args.insert("bad/value".to_string(), json!("abcdefgh"));
        SizeValidator::from_file_args_map(args).unwrap_err();
    }

    #[test]
    fn create_from_file_args_map_missing_target_file_fails() {
        let mut args = ValidatorFileArgsMap::new();
        args.insert("unvalidated/file".to_string(), json!(0));
        let file_name_to_validate = "non/existent/file";

        let validator = SizeValidator::from_file_args_map(args).unwrap();
        match validator.validate(file_name_to_validate, &[]).unwrap_err() {
            ValidatorError::FailedToValidate { cause: _cause, file_name } => {
                assert_eq!(file_name, file_name_to_validate);
            }
            _ => panic!("Unexpected error returned"),
        }
    }

    #[test]
    fn create_from_file_args_map_succeeds() {
        let mut args = ValidatorFileArgsMap::new();
        args.insert("file_to_validate1".to_string(), json!(TO_VALIDATE1.len()));
        args.insert("file_to_validate2".to_string(), json!(TO_VALIDATE2.len()));
        SizeValidator::from_file_args_map(args).unwrap();
    }

    #[test]
    fn validate_with_zero_and_empty_contents_succeeds() {
        let mut args = ValidatorFileArgsMap::new();
        args.insert("file_to_validate1".to_string(), json!(0));
        let validator = SizeValidator::from_file_args_map(args).unwrap();
        validator.validate("file_to_validate1", &[]).unwrap();
    }

    #[test]
    fn validate_with_numbers_succeeds() {
        let mut args = ValidatorFileArgsMap::new();
        args.insert("file_to_validate1".to_string(), json!(TO_VALIDATE1.len()));
        args.insert("file_to_validate2".to_string(), json!(TO_VALIDATE2.len()));

        let validator = SizeValidator::from_file_args_map(args).unwrap();
        validator.validate("file_to_validate1", TO_VALIDATE1.as_bytes()).unwrap();
        validator.validate("file_to_validate2", TO_VALIDATE2.as_bytes()).unwrap();
    }

    #[test]
    fn validate_with_objects_succeeds() {
        let mut args = ValidatorFileArgsMap::new();
        // Set min
        args.insert("file_to_validate1".to_string(), json!({"min": TO_VALIDATE1.len()}));
        args.insert("file_to_validate2".to_string(), json!({"min": TO_VALIDATE2.len()}));
        // Set max
        args.insert("file_to_validate3".to_string(), json!({"max": TO_VALIDATE1.len()}));
        args.insert("file_to_validate4".to_string(), json!({"max": TO_VALIDATE2.len()}));
        // Set both min and max with same value.
        args.insert(
            "file_to_validate5".to_string(),
            json!({"min": TO_VALIDATE1.len(), "max": TO_VALIDATE1.len()}),
        );
        args.insert(
            "file_to_validate6".to_string(),
            json!({"min": TO_VALIDATE2.len(), "max": TO_VALIDATE2.len()}),
        );
        // Set both min and max with different values.
        args.insert(
            "file_to_validate7".to_string(),
            json!({"min": TO_VALIDATE1.len()-1, "max": TO_VALIDATE1.len()+1}),
        );
        args.insert(
            "file_to_validate8".to_string(),
            json!({"min": TO_VALIDATE2.len()-1, "max": TO_VALIDATE2.len()+1}),
        );

        let validator = SizeValidator::from_file_args_map(args).unwrap();
        validator.validate("file_to_validate1", TO_VALIDATE1.as_bytes()).unwrap();
        validator.validate("file_to_validate2", TO_VALIDATE2.as_bytes()).unwrap();
        validator.validate("file_to_validate3", TO_VALIDATE1.as_bytes()).unwrap();
        validator.validate("file_to_validate4", TO_VALIDATE2.as_bytes()).unwrap();
        validator.validate("file_to_validate5", TO_VALIDATE1.as_bytes()).unwrap();
        validator.validate("file_to_validate6", TO_VALIDATE2.as_bytes()).unwrap();
        validator.validate("file_to_validate7", TO_VALIDATE1.as_bytes()).unwrap();
        validator.validate("file_to_validate8", TO_VALIDATE2.as_bytes()).unwrap();
    }

    #[test]
    fn validate_with_bad_sizes_fails() {
        let mut args = ValidatorFileArgsMap::new();
        args.insert("file_to_validate1".to_string(), json!({"min": 0, "max": 2}));
        args.insert("file_to_validate2".to_string(), json!({"min": 3, "max": 6}));

        let validator = SizeValidator::from_file_args_map(args).unwrap();
        validator.validate("file_to_validate1", TO_VALIDATE1.as_bytes()).unwrap_err();
        validator.validate("file_to_validate2", TO_VALIDATE2.as_bytes()).unwrap_err();
    }
}
