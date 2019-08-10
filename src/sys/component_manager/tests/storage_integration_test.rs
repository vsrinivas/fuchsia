// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {fuchsia_async as fasync, test_utils};

// The "real" test logic here is in storage_realm.rs. This just launches that component and checks
// for a message it prints to stdout on success.
// TODO: Find a way to run this test more directly so that results can also be consumed better,
// perhaps with the new Test Framework work.
#[fasync::run_singlethreaded(test)]
async fn test() {
    test_utils::launch_and_wait_for_msg(
        "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/component_manager.cmx".to_string(),
        Some(vec![
            "fuchsia-pkg://fuchsia.com/storage_integration_test#meta/storage_realm.cm".to_string()
        ]),
        "Test passes\n".to_string(),
    )
}
