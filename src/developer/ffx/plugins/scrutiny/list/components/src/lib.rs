// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_core::ffx_plugin,
    ffx_scrutiny_components_list_args::ScrutinyComponentsCommand,
    scrutiny_config::{ConfigBuilder, ModelConfig},
    scrutiny_frontend::launcher,
};

#[ffx_plugin()]
pub async fn scrutiny_components(cmd: ScrutinyComponentsCommand) -> Result<()> {
    let command = "components.urls".to_string();
    let model = ModelConfig::from_product_bundle(&cmd.product_bundle)?;
    let config = ConfigBuilder::with_model(model).command(command).build();
    launcher::launch_from_config(config)?;

    Ok(())
}
