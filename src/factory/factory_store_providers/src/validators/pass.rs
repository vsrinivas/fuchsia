// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::validators::{Validator, ValidatorError},
    fuchsia_syslog as syslog,
};

/// Validator that always returns `Ok`.
///
/// Used for allowing files to pass validation without any checks as by default, no files are
/// allowed to be exposed without going through at least one validator.
#[derive(Debug)]
pub struct PassValidator;
impl Validator for PassValidator {
    /// Pass through validation function. Always returns `Ok`.
    fn validate(&self, file_name: &str, _contents: &[u8]) -> Result<(), ValidatorError> {
        syslog::fx_log_info!("Passing validation of {}", file_name);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validate_with_text_succeeds() {
        let validator = PassValidator;
        let text = b"abcdefg";
        validator.validate("file_to_validate", text).unwrap();
    }

    #[test]
    fn validate_with_empty_contents_succeeds() {
        let validator = PassValidator;
        let empty = [];
        validator.validate("file_to_validate", &empty).unwrap();
    }
}
