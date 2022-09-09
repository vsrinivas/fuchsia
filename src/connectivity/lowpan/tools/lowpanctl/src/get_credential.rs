// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::prelude::*;

/// Contains the arguments decoded for the `get-credential` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get-credential")]
pub struct GetCredentialCommand {}

impl GetCredentialCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let device_extra = context
            .get_default_device_extra_proxy()
            .await
            .context("Unable to get device instance")?;

        let credential =
            device_extra.get_credential().await.context("unable to send get_credential command")?;

        let credential_str = match credential {
            Some(boxed) => match *boxed {
                fidl_fuchsia_lowpan_device::Credential::NetworkKey(x) => get_hex_string(&x),
                _ => "credential type not recognized".to_string(),
            },
            None => "None".to_string(),
        };

        println!("credential: {}", credential_str);

        Ok(())
    }
}
