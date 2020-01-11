// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fuchsia_async as fasync};

#[fasync::run_singlethreaded(test)]
async fn route_echo_service() -> Result<(), Error> {
    test_utils::launch_component_and_expect_output(
        "fuchsia-pkg://fuchsia.com/routing_integration_test#meta/echo_realm.cm",
        "Hippos rule!\n".to_string(),
    )
    .await
}
