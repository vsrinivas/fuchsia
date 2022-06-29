// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::test(logging = true)]
fn empty_test_with_logging() {
    squawk::log_expected_messages();
}
