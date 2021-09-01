// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_component::client::{launcher, AppBuilder};

const SETUI_CLIENT_PATH: &str = "fuchsia-pkg://fuchsia.com/setui_client#meta/setui_client.cmx";
const EXPECTED_OUT_NAME: &str = "setui_client";
const EXPECTED_OUT_HELP: &str = "Prints this message or the help of the given subcommand(s)";

#[fuchsia_async::run_singlethreaded(test)]
async fn run() -> Result<(), Error> {
    let output = AppBuilder::new(SETUI_CLIENT_PATH).arg("--help").output(&launcher()?)?.await?;
    assert!(output.exit_status.success());

    let output = String::from_utf8_lossy(&output.stdout);
    assert!(output.contains(EXPECTED_OUT_NAME));
    assert!(output.contains(EXPECTED_OUT_HELP));
    Ok(())
}
