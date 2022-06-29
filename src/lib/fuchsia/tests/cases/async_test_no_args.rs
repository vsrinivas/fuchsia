// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[fuchsia::test]
async fn empty_async_test() {
    squawk::log_expected_messages_async().await;
}
