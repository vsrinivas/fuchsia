// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {fidl_fuchsia_ssh::MAX_SSH_KEY_LENGTH, std::str::FromStr, thiserror::Error};

#[derive(Error, Debug, PartialEq)]
/// Errors that occur while parsing a key.
pub enum ParseKeyError {
    #[error("Wrong number of fields in key")]
    WrongNumberOfFields,
    #[error("Key too long")]
    KeyTooLong,
    #[error("Invalid key type")]
    InvalidKeyType,
    #[error("Key is a comment or empty")]
    InvalidKey,
}

/// See //third_party/openssh-portable/sshd.8.
const VALID_KEY_TYPES: [&str; 7] = [
    "sk-ecdsa-sha2-nistp256@openssh.com",
    "ecdsa-sha2-nistp256",
    "ecdsa-sha2-nistp384",
    "ecdsa-sha2-nistp521",
    "sk-ssh-ed25519@openssh.com",
    "ssh-ed25519",
    "ssh-rsa",
];

fn is_valid_key_type(typ: &str) -> bool {
    VALID_KEY_TYPES.iter().any(|v| *v == typ)
}

#[derive(Debug, Clone)]
/// Represents a single SSH key. Some (minimal) validation occurs (e.g. ensuring
/// the claimed key type is supported), but otherwise a key is largely opaque.
pub struct KeyEntry {
    options: Option<String>,
    key_type: String,
    key: String,
    comment: Option<String>,
}

/// `KeyEntry`s are equal if their `key_type` and `key` are valid and equal. The
/// comparison disregards the `comment` and `options` fields.
impl PartialEq for KeyEntry {
    fn eq(&self, other: &KeyEntry) -> bool {
        return is_valid_key_type(&self.key_type)
            && is_valid_key_type(&self.key_type)
            && self.key_type == other.key_type
            && self.key == other.key;
    }
}

impl FromStr for KeyEntry {
    type Err = ParseKeyError;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        if s.len() == 0 || s.starts_with('#') {
            return Err(ParseKeyError::InvalidKey);
        }
        if s.len() > MAX_SSH_KEY_LENGTH as usize {
            return Err(ParseKeyError::KeyTooLong);
        }

        let parts: Vec<&str> = s.split(' ').collect();

        // The sshd docs say that authorized_keys fields are space-separated,
        // but in practice the tools seem to accept comments with multiple
        // spaces in them.
        if parts.len() < 2 {
            return Err(ParseKeyError::WrongNumberOfFields);
        }

        // This is fairly naive. We simply try and find a field that looks like
        // a valid key type, and base our assumption on the rest of the line off
        // that. We don't attempt to parse any of the other fields.
        let (options, key_type, key, comment_start) = if is_valid_key_type(parts[0]) {
            // If the first field is a key type, the next field is the key, and
            // the last field is the comment.
            (None, parts[0], parts[1], 2)
        } else if is_valid_key_type(parts[1]) {
            // If the second field is a key type, there should be at least 3
            // fields: options, key type, and key.
            if parts.len() < 3 {
                return Err(ParseKeyError::WrongNumberOfFields);
            }
            (Some(parts[0]), parts[1], parts[2], 3)
        } else {
            return Err(ParseKeyError::InvalidKeyType);
        };

        // TODO(fxbug.dev/88280): Consider requiring reasonable key sizes.
        // OpenSSH enforces a minimum size of 1024 bits for RSA, for example
        // (which is still too low).

        let comment =
            if parts.len() > comment_start { Some(parts[comment_start..].join(" ")) } else { None };

        Ok(KeyEntry {
            options: options.map(|v| v.to_string()),
            key_type: key_type.to_string(),
            key: key.to_string(),
            comment: comment.map(|v| v.to_string()),
        })
    }
}

impl ToString for KeyEntry {
    fn to_string(&self) -> String {
        match (self.options.as_ref(), self.comment.as_ref()) {
            (Some(a), Some(b)) => format!("{} {} {} {}", a, self.key_type, self.key, b),
            (None, Some(b)) => format!("{} {} {}", self.key_type, self.key, b),
            (Some(a), None) => format!("{} {} {}", a, self.key_type, self.key),
            (None, None) => format!("{} {}", self.key_type, self.key),
        }
    }
}

#[cfg(test)]
mod string_tests {
    use super::*;

    #[test]
    fn test_key_long_comment() {
        let key = "options ssh-rsa abcdefg comment and other text".parse::<KeyEntry>().unwrap();
        assert_eq!(key.options.as_deref(), Some("options"));
        assert_eq!(key.key_type, "ssh-rsa");
        assert_eq!(key.key, "abcdefg");
        assert_eq!(key.comment.as_deref(), Some("comment and other text"));
    }

    #[test]
    fn test_key_leading_fields() {
        assert_eq!(
            "options word ssh-ed25519 abcdefg".parse::<KeyEntry>().unwrap_err(),
            ParseKeyError::InvalidKeyType
        );
    }

    #[test]
    fn test_key_not_enough_fields() {
        assert_eq!(
            "options ssh-rsa".parse::<KeyEntry>().unwrap_err(),
            ParseKeyError::WrongNumberOfFields
        );
    }

    #[test]
    fn test_key_empty() {
        assert_eq!("".parse::<KeyEntry>().unwrap_err(), ParseKeyError::InvalidKey);
    }

    #[test]
    fn test_key_too_long() {
        let line = "a".repeat(8193);
        assert_eq!(line.parse::<KeyEntry>().unwrap_err(), ParseKeyError::KeyTooLong);
    }

    const VALID_ECDSA_KEY: &str = "AAAAE2VjZHNhLXNoYTItbmlzdHAyNTYAAAAIbmlzdHAyNTYAAABBBKR7FCcS2e4OfqHi8h3HAPZhu1fvZUXaXjSDEUr0NPV49jKJSgCptu7YQq1DlfKXXw3aPGJdZAyk1fixQvYli8A=";

    #[test]
    fn test_parse_valid_key_no_options_or_comment() {
        let line = format!("ecdsa-sha2-nistp256 {}", VALID_ECDSA_KEY);
        let key = line.parse::<KeyEntry>().expect("parse ok");
        assert_eq!(key.options, None);
        assert_eq!(key.key_type, "ecdsa-sha2-nistp256");
        assert_eq!(key.key, VALID_ECDSA_KEY);
        assert_eq!(key.comment, None);
        assert_eq!(line, key.to_string());
    }

    #[test]
    fn test_parse_valid_key_with_options() {
        let line = format!("options ecdsa-sha2-nistp256 {}", VALID_ECDSA_KEY);
        let key = line.parse::<KeyEntry>().expect("parse ok");
        assert_eq!(key.options.as_deref(), Some("options"));
        assert_eq!(key.key_type, "ecdsa-sha2-nistp256");
        assert_eq!(key.key, VALID_ECDSA_KEY);
        assert_eq!(key.comment, None);
        assert_eq!(line, key.to_string());
    }

    #[test]
    fn test_parse_valid_key_with_comment() {
        let line = format!("ecdsa-sha2-nistp256 {} comment", VALID_ECDSA_KEY);
        let key = line.parse::<KeyEntry>().expect("parse ok");
        assert_eq!(key.options, None);
        assert_eq!(key.key_type, "ecdsa-sha2-nistp256");
        assert_eq!(key.key, VALID_ECDSA_KEY);
        assert_eq!(key.comment.as_deref(), Some("comment"));
        assert_eq!(line, key.to_string());
    }

    #[test]
    fn test_parse_valid_key_with_comment_and_options() {
        let line = format!("options ecdsa-sha2-nistp256 {} comment", VALID_ECDSA_KEY);
        let key = line.parse::<KeyEntry>().expect("parse ok");
        assert_eq!(key.options.as_deref(), Some("options"));
        assert_eq!(key.key_type, "ecdsa-sha2-nistp256");
        assert_eq!(key.key, VALID_ECDSA_KEY);
        assert_eq!(key.comment.as_deref(), Some("comment"));
        assert_eq!(line, key.to_string());
    }

    #[test]
    fn test_parse_comment_line() {
        assert_eq!("# commented".parse::<KeyEntry>().unwrap_err(), ParseKeyError::InvalidKey);
    }
}

#[cfg(test)]
mod eq_tests {
    use super::*;

    #[test]
    fn test_eq_not_identical() {
        let a = KeyEntry {
            options: None,
            key_type: "ssh-rsa".to_owned(),
            key: "abc".to_owned(),
            comment: None,
        };
        let b = KeyEntry {
            options: None,
            key_type: "ecdsa-sha2-nistp256".to_owned(),
            key: "abc".to_owned(),
            comment: None,
        };
        assert_ne!(a, b);

        let a = KeyEntry {
            options: None,
            key_type: "ssh-rsa".to_owned(),
            key: "abc".to_owned(),
            comment: None,
        };
        let b = KeyEntry {
            options: None,
            key_type: "ssh-rsa".to_owned(),
            key: "def".to_owned(),
            comment: None,
        };
        assert_ne!(a, b);
    }

    #[test]
    fn test_eq_not_valid() {
        let a = KeyEntry {
            options: None,
            key_type: "ssh-pumpkin".to_owned(),
            key: "abc".to_owned(),
            comment: None,
        };
        let b = KeyEntry {
            options: None,
            key_type: "ssh-pumpkin".to_owned(),
            key: "abc".to_owned(),
            comment: None,
        };
        assert_ne!(a, b);
    }

    #[test]
    fn test_eq_ignores_options_and_comment() {
        let a = KeyEntry {
            options: None,
            key_type: "ssh-rsa".to_owned(),
            key: "abc".to_owned(),
            comment: None,
        };
        let b = KeyEntry {
            options: Some("test".to_owned()),
            key_type: "ssh-rsa".to_owned(),
            key: "abc".to_owned(),
            comment: Some("test".to_owned()),
        };

        assert_eq!(a, b);
    }
}
