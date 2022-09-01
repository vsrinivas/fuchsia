// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod args;

mod subcommands;

use {
    anyhow::{Context, Result},
    args::{TestNodeCommand, TestNodeSubcommand},
    fidl_fuchsia_driver_development as fdd,
};

pub async fn test_node(
    cmd: &TestNodeCommand,
    driver_development_proxy: fdd::DriverDevelopmentProxy,
) -> Result<()> {
    match cmd.subcommand {
        TestNodeSubcommand::Add(ref subcmd) => {
            subcommands::add::add_test_node(subcmd, driver_development_proxy)
                .await
                .context("Add subcommand failed")?;
        }
        TestNodeSubcommand::Remove(ref subcmd) => {
            subcommands::remove::remove_test_node(subcmd, driver_development_proxy)
                .await
                .context("Remove subcommand failed")?;
        }
    };
    Ok(())
}
