// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::validators::{Validator, ValidatorError},
    anyhow::Error,
    fuchsia_syslog as syslog,
    std::str::from_utf8,
};

/// Validator that checks if the given file is composed entirely of UTF-8 characters.
#[derive(Debug)]
pub struct TextValidator;
impl Validator for TextValidator {
    /// Validates that the given file contains only UTF-8 encoded text.
    fn validate(&self, file_name: &str, contents: &[u8]) -> Result<(), ValidatorError> {
        syslog::fx_log_info!("Validating {} is only UTF-8 encoded text", file_name);
        from_utf8(contents).map(|_| ()).map_err(|cause| ValidatorError::FailedToValidate {
            cause: Error::from(cause),
            file_name: file_name.to_string(),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn validate_with_text_succeeds() {
        let validator = TextValidator;
        let text = b"abcdefg";
        validator.validate("file_to_validate", text).unwrap();
    }

    #[test]
    fn validate_with_empty_contents_succeeds() {
        let validator = TextValidator;
        let empty = [];
        validator.validate("file_to_validate", &empty).unwrap();
    }

    #[test]
    fn validate_with_non_utf8_chars_fails() {
        let validator = TextValidator;
        let text = [0x80, 0xbf];
        validator.validate("file_to_validate", &text).unwrap_err();
    }
}
