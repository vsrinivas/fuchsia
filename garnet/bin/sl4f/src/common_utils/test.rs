// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use serde::{Deserialize, Serialize};
use serde_json::{from_value, to_value, Value};
use std::fmt::Debug;

pub fn assert_value_round_trips_as<T>(expected: T, json: Value)
where
    T: Serialize + for<'de> Deserialize<'de> + Debug + PartialEq + Eq,
{
    assert_eq!(from_value::<T>(json.clone()).unwrap(), expected);
    assert_eq!(json, to_value(expected).unwrap());
}
