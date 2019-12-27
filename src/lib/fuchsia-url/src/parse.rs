// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

fn lowercase_alphanumeric(b: u8) -> bool {
    (b >= b'0' && b <= b'9') || (b >= b'a' && b <= b'z')
}

/// Check if a string conforms to r"^[0-9a-z\-\._]{1,100}$"
pub fn is_name(string: &str) -> bool {
    let len = string.len();
    if len == 0 || len > 100 {
        return false;
    }
    string.bytes().all(|b| lowercase_alphanumeric(b) || b == b'-' || b == b'.' || b == b'_')
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
    #[test]
    fn test_is_name() {
        use super::is_name;
        assert!(is_name("foo4738-._fazoo421"), "characters allowed");
        assert!(!is_name("foo!*&$4738-._foo421"), "other characters disallowed");
        assert!(!is_name(""), "empty disallowed");
        assert!(is_name("x"), "length one allowed");
        assert!(is_name(&*"x".repeat(100)), "length 100 allowed");
        assert!(!is_name(&*"x".repeat(101)), "length 101 disallowed");
    }

    #[test]
    fn test_is_hash() {
        use super::is_hash;
        assert!(is_hash(&*"a".repeat(64)), "alpha allowed");
        assert!(is_hash(&*"5".repeat(64)), "digits allowed");
        assert!(!is_hash(&*"_".repeat(64)), "other characters disallowed");
        assert!(!is_hash(&*"a".repeat(63)), "too short");
        assert!(!is_hash(&*"a".repeat(65)), "too long");
    }
}
