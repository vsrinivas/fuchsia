// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// We need this file so that runtests can run echo_test_realm.cm test as it takes a file path as
// input and then goes and tries to search for corresponding cm file.
fn main() {
    panic!("This binary should never be invoked");
}
