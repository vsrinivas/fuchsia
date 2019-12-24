// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};

pub fn expect_eq<T>(expected: &T, actual: &T) -> Result<(), Error>
where
    T: std::fmt::Debug + std::cmp::PartialEq,
{
    if *expected == *actual {
        Ok(())
    } else {
        Err(format_err!(format!("failed - expected '{:#?}', found: '{:#?}'", expected, actual)))
    }
}

/// Converts any Result type into a Result that returns an Error type that can be tried. This is
/// useful for many FIDL messages that have error reply types that don't derive Fail.
pub fn expect_ok<T, E>(result: Result<T, E>, msg: &str) -> Result<T, Error>
where
    E: std::fmt::Debug,
{
    result.map_err(|e| format_err!("{}: {:?}", msg, e))
}

macro_rules! expect_eq {
    ($expected:expr, $actual:expr) => {
        expect_eq(&$expected, &$actual)
    };
}

macro_rules! expect_true {
    ($condition:expr) => {
        if $condition {
            Ok(())
        } else {
            Err(format_err!(format!("condition is not true: {}", stringify!($condition))))
        } as Result<(), Error>
    }
}
