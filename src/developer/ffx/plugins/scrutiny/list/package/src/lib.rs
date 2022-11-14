// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    ffx_core::ffx_plugin,
    ffx_scrutiny_package_list_args::ScrutinyPackageCommand,
    scrutiny_config::{ConfigBuilder, ModelConfig},
    scrutiny_frontend::{command_builder::CommandBuilder, launcher},
};

#[ffx_plugin()]
pub async fn scrutiny_package(cmd: ScrutinyPackageCommand) -> Result<(), Error> {
    let url_string = format!("{}", cmd.url);
    let command = CommandBuilder::new("search.package.list").param("url", url_string).build();
    let model = ModelConfig::from_product_bundle(&cmd.product_bundle)?;
    let config = ConfigBuilder::with_model(model).command(command).build();
    launcher::launch_from_config(config)?;

    Ok(())
}
