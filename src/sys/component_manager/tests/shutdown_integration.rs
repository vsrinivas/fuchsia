// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use {
    failure::{Error},
    fuchsia_async as fasync,
    test_utils,
};

#[fasync::run_singlethreaded(test)]
async fn test() -> Result<(), Error> {
    let app = test_utils::launch_test_component(
        "fuchsia-pkg://fuchsia.com/shutdown_integration_test#meta/component_manager.cmx".to_string(),
        Some(vec![
            "fuchsia-pkg://fuchsia.com/shutdown_integration_test#meta/shutdown_integration_root.cm".to_string(),
        ])
    ).expect("failed to launch component manager for test");

    match app.wait_with_output().await {
        Ok(output) => {
            match output.exit_status.ok() {
                Ok(()) => {
                    Ok(())
                },
                Err(e) => {
                    Err(e.into())
                }
            }
        },
        Err(e) => {
            Err(e.into())
        }
    }
}
