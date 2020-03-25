// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

/// Formats a number with a number of leading zeroes that ensures that all values up to `max_val`
/// would be displayed in the correct order when sorted as strings.
///
/// For example, if `max_val = 99`, then formats `5` as `"05"`
#[allow(dead_code)]
pub(crate) fn zero_pad(val: usize, max_val: usize) -> String {
    assert!(max_val >= val);
    let width: usize = ((max_val + 1) as f32).log10().ceil() as usize;
    format!("{:0width$}", val, width = width)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_zero_pad() {
        assert_eq!(zero_pad(0, 5), "0".to_string());
        assert_eq!(zero_pad(13, 99), "13".to_string());
        assert_eq!(zero_pad(13, 100), "013".to_string());
        assert_eq!(zero_pad(100, 100), "100".to_string());
    }
}
