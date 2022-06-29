// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::test(logging_tags = ["foo", "bar"])]
async fn empty_test_with_logging_tags() {
    squawk::log_expected_messages();
}
