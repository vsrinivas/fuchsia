// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::prelude::*;

/// Contains the arguments decoded for the `attach-all-nodes-to` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(
    subcommand,
    name = "attach-all-nodes-to",
    description = "Changes the operational dataset for all nodes"
)]
pub struct AttachAllNodesToCommand {
    #[argh(option, short = 't', long = "tlvs", description = "dataset tlvs in hex")]
    tlvs: String,
}

impl AttachAllNodesToCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let thread_dataset = context
            .get_default_thread_dataset_proxy()
            .await
            .context("Unable to get device instance")?;

        let tlvs = hex::decode(&self.tlvs)?;

        let deadline = thread_dataset.attach_all_nodes_to(&tlvs).await?;

        if deadline == 0 {
            println!("Dataset updated.");
        } else {
            println!(
                "Dataset update will be complete in {:?}.",
                std::time::Duration::from_millis(
                    deadline.try_into().expect("Negative duration is prohibited")
                )
            );
        }

        Ok(())
    }
}
