// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::test(allow_stalls = false)]
// TODO(https://fxbug.dev/71429) uncomment
// #[should_panic]
async fn empty_async_test_no_stalls() {
    // this does IPC, which produces a "stall" from the executor's perspective and will panic
    squawk::log_expected_messages_async().await;
}
