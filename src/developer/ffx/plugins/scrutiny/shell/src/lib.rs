// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    ffx_core::ffx_plugin,
    ffx_scrutiny_shell_args::ScrutinyShellCommand,
    scrutiny_frontend::{config::Config, launcher},
};

#[ffx_plugin()]
pub async fn scrutiny_shell(_cmd: ScrutinyShellCommand) -> Result<(), Error> {
    launcher::launch_from_config(Config::default())
}
