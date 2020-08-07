// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context as _, Error};
use argh::FromArgs;

/// Contains the arguments decoded for the `get-supported-network-types` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get-supported-network-types")]
pub struct GetSupportedNetworkTypesCommand {}

impl GetSupportedNetworkTypesCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let device = context.get_default_device().await.context("Unable to get device instance")?;

        let network_types = device
            .get_supported_network_types()
            .await
            .context("Unable to send get_supported_network_types command")?;

        for network_type in network_types {
            println!("{:?}", network_type);
        }

        Ok(())
    }
}
