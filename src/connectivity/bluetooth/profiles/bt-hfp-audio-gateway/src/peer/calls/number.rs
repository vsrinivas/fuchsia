// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use super::types::FidlNumber;

/// A phone number.
#[derive(Debug, Clone, PartialEq, Hash, Default, Eq)]
pub struct Number(String);

impl Number {
    /// Format value indicating no changes on the number presentation are required.
    /// See HFP v1.8, Section 4.34.2.
    const NUMBER_FORMAT: i64 = 129;

    /// Returns the numeric representation of the Number's format as specified in HFP v1.8,
    /// Section 4.34.2.
    pub fn type_(&self) -> i64 {
        Number::NUMBER_FORMAT
    }
}

impl From<Number> for String {
    fn from(x: Number) -> Self {
        x.0
    }
}

impl From<&str> for Number {
    fn from(n: &str) -> Self {
        // Phone numbers must be enclosed in double quotes
        let inner = if n.starts_with("\"") && n.ends_with("\"") {
            n.to_string()
        } else {
            format!("\"{}\"", n)
        };
        Self(inner)
    }
}

impl From<FidlNumber> for Number {
    fn from(n: FidlNumber) -> Self {
        n.as_str().into()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
    fn number_type_in_valid_range() {
        let number = Number::from("1234567");
        // type values must be in range 128-175.
        assert!(number.type_() >= 128);
        assert!(number.type_() <= 175);
    }

    #[fuchsia::test]
    fn number_str_roundtrip() {
        let number = Number::from("1234567");
        assert_eq!(number.clone(), Number::from(&*String::from(number)));
    }
}
