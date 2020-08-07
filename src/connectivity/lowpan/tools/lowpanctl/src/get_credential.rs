// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use anyhow::{Context, Error};
use argh::FromArgs;

/// Contains the arguments decoded for the `get-credential` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get-credential")]
pub struct GetCredentialCommand {}

impl GetCredentialCommand {
    fn get_hex_string(&self, vec: &Vec<u8>) -> String {
        let mut string = String::from("");
        for item in vec {
            string.push_str(&format!("{:02x}", item)[..]);
        }
        string
    }

    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let (_, device_extra, _) =
            context.get_default_device_proxies().await.context("Unable to get device instance")?;

        let credential =
            device_extra.get_credential().await.context("unable to send get_credential command")?;

        let credential_str = match credential {
            Some(boxed) => match *boxed {
                fidl_fuchsia_lowpan::Credential::MasterKey(x) => self.get_hex_string(&x),
                _ => "credential type not recognized".to_string(),
            },
            None => "None".to_string(),
        };

        println!("credential: {}", credential_str);

        Ok(())
    }
}
