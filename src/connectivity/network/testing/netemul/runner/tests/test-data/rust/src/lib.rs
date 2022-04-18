// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

#[test]
fn pass() {
    assert_eq!(1, 1);
}

#[test]
fn fail() {
    assert!(0 > 1);
}
