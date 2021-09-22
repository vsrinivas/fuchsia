// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::errors::ValidateNameError;

fn lowercase_alphanumeric(b: u8) -> bool {
    (b >= b'0' && b <= b'9') || (b >= b'a' && b <= b'z')
}

/// Check if a string conforms to r"^[0-9a-z\-\._]{1,255}$"
pub fn validate_name(string: &str) -> Result<(), ValidateNameError> {
    let len = string.len();
    if len == 0 {
        return Err(ValidateNameError::EmptyName);
    } else if len > 255 {
        return Err(ValidateNameError::NameTooLong);
    }
    for b in string.bytes() {
        if !(lowercase_alphanumeric(b) || b == b'-' || b == b'.' || b == b'_') {
            return Err(ValidateNameError::InvalidCharacter { character: b.into() });
        }
    }
    Ok(())
}

/// Check if a string conforms to r"^[0-9a-z]{64}$"
pub fn is_hash(string: &str) -> bool {
    if string.len() != 64 {
        return false;
    }
    string.bytes().all(lowercase_alphanumeric)
}

pub fn check_resource(input: &str) -> bool {
    for segment in input.split('/') {
        if segment.is_empty() || segment == "." || segment == ".." {
            return false;
        }

        if segment.bytes().find(|c| *c == b'\x00').is_some() {
            return false;
        }
    }

    true
}

#[cfg(test)]
mod test {
    use super::*;

    #[test]
    fn test_validate_name() {
        assert_eq!(validate_name("foo4738-._fazoo421"), Ok(()), "characters allowed");
        assert_eq!(
            validate_name("foo!*&$4738-._foo421"),
            Err(ValidateNameError::InvalidCharacter { character: '!' }),
            "other characters disallowed"
        );
        assert_eq!(validate_name(""), Err(ValidateNameError::EmptyName), "empty disallowed");
        assert_eq!(validate_name("x"), Ok(()), "length one allowed");
        assert_eq!(validate_name(&*"x".repeat(255)), Ok(()), "length 255 allowed");
        assert_eq!(
            validate_name(&*"x".repeat(256)),
            Err(ValidateNameError::NameTooLong),
            "length 256 disallowed"
        );
    }

    #[test]
    fn test_is_hash() {
        assert!(is_hash(&*"a".repeat(64)), "alpha allowed");
        assert!(is_hash(&*"5".repeat(64)), "digits allowed");
        assert!(!is_hash(&*"_".repeat(64)), "other characters disallowed");
        assert!(!is_hash(&*"a".repeat(63)), "too short");
        assert!(!is_hash(&*"a".repeat(65)), "too long");
    }
}
