// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::test(add_test_attr = false)]
#[test]
fn empty_test_with_test_attribute() {
    squawk::log_expected_messages();
}
