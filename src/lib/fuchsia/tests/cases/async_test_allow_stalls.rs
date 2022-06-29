// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::test(allow_stalls = true)]
async fn empty_async_test_allow_stalls() {
    squawk::log_expected_messages_async().await;
}
