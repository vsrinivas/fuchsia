// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{err_msg, Error};

pub fn expect_eq<T>(expected: &T, actual: &T) -> Result<(), Error>
where
    T: std::fmt::Debug + std::cmp::PartialEq,
{
    if *expected == *actual {
        Ok(())
    } else {
        Err(err_msg(format!("failed - expected '{:#?}', found: '{:#?}'", expected, actual)))
    }
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
            Err(err_msg(format!("condition is not true: {}", stringify!($condition))))
        } as Result<(), Error>
    }
}
