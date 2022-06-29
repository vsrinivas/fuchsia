// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::test(logging_minimum_severity = "DEBUG")]
fn empty_test_severity_debug() {
    squawk::log_expected_messages();
}
