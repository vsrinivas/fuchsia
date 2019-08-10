// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {fuchsia_async as fasync, test_utils};

#[fasync::run_singlethreaded(test)]
async fn test() {
    test_utils::launch_and_wait_for_msg(
        "fuchsia-pkg://fuchsia.com/routing_integration_test#meta/component_manager.cmx".to_string(),
        Some(vec![
            "fuchsia-pkg://fuchsia.com/routing_integration_test#meta/echo_realm.cm".to_string()
        ]),
        "Hippos rule!\n".to_string(),
    )
}
