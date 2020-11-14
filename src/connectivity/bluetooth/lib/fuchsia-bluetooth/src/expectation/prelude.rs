// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context, Error};

use super::*;

/// A shorthand for creating an expectation that `expected` and `actual` are equal and testing the
/// predicate.
pub fn expect_eq<'t, T: Debug + PartialEq + Send>(
    expected: &'t T,
    actual: &'t T,
    file: &str,
    line: u32,
    expected_name: &str,
    actual_name: &str,
) -> Result<(), Error> {
    let pred: Predicate<&'t T> = Predicate::Equal(
        Arc::new(expected),
        Arc::new(|a: &&T, b: &&T| a == b),
        Arc::new(|t| show_debug(t)),
    );
    pred.assert_satisfied(&actual)
        .map_err(|dstr| format_err!("{}\n{:?}", actual_name, dstr))
        .context(format_err!(
            "({}:{}) expect_eq! failed: {} != {}",
            file,
            line,
            expected_name,
            actual_name
        ))
}

#[macro_export]
macro_rules! expect_eq {
    ($expected:expr, $actual:expr) => {
        $crate::expectation::prelude::expect_eq(
            &$expected,
            &$actual,
            file!(),
            line!(),
            stringify!($expected),
            stringify!($actual),
        )
    };
}

#[test]
fn expect_eq_works() {
    expect_eq!("abc", "abc").unwrap();
    expect_eq!("abc", "def").unwrap_err();
}
