// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use fuchsia_async as fasync;

mod test_utils;

#[fasync::run_singlethreaded()]
async fn main() {
    test_utils::launch_and_wait_for_msg(
        "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/component_manager.cmx".to_string(),
        Some(vec![
            "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_realm.cm".to_string()
        ]),
        "Test passes\n".to_string(),
    )
}
