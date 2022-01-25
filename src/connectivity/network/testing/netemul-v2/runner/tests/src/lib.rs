// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

#[test]
fn it_works() {
    assert_eq!(1, 1);
}

// Include multiple test cases to ensure that is handled correctly by the test runner.
#[test]
fn another_test() {
    assert!(0 < 1);
}
