// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::context::LowpanCtlContext;
use crate::prelude::*;
use openthread::ot;

/// Contains the arguments decoded for the `dataset` command.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "dataset", description = "Operational Datasets")]
pub struct DatasetCommand {
    #[argh(subcommand)]
    subcommand: DatasetSubcommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum DatasetSubcommand {
    Get(DatasetGetCommand),
    Set(DatasetSetCommand),
}

#[derive(PartialEq, Debug)]
enum DatasetFormat {
    RawTlvs,
    Rust,
}

impl std::str::FromStr for DatasetFormat {
    type Err = anyhow::Error;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        if s == "raw" || s == "tlv" || s == "tlvs" {
            Ok(DatasetFormat::RawTlvs)
        } else if s == "rust" {
            Ok(DatasetFormat::Rust)
        } else {
            Err(anyhow::format_err!("Unknown format {:?}", s))
        }
    }
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get", description = "Get operational dataset")]
pub struct DatasetGetCommand {
    #[argh(
        option,
        short = 'f',
        long = "format",
        description = "dataset format ('raw', 'rust')",
        default = "DatasetFormat::RawTlvs"
    )]
    format: DatasetFormat,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "set", description = "Set operational dataset")]
pub struct DatasetSetCommand {
    #[argh(option, short = 't', long = "tlvs", description = "dataset tlvs in hex")]
    tlvs: String,
}

impl DatasetGetCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let thread_dataset = context
            .get_default_thread_dataset_proxy()
            .await
            .context("Unable to get device instance")?;

        if let Some(tlvs) = thread_dataset.get_active_tlvs().await? {
            match self.format {
                DatasetFormat::RawTlvs => println!("{}", hex::encode(tlvs)),
                DatasetFormat::Rust => {
                    let tlvs = ot::OperationalDatasetTlvs::try_from_slice(&tlvs)?;

                    println!("{:#?}", tlvs.try_to_dataset()?);
                }
            };
        } else {
            println!("");
        }

        Ok(())
    }
}

impl DatasetSetCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        let thread_dataset = context
            .get_default_thread_dataset_proxy()
            .await
            .context("Unable to get device instance")?;

        let tlvs = hex::decode(&self.tlvs)?;

        thread_dataset.set_active_tlvs(&tlvs).await?;

        Ok(())
    }
}

impl DatasetCommand {
    pub async fn exec(&self, context: &mut LowpanCtlContext) -> Result<(), Error> {
        match &self.subcommand {
            DatasetSubcommand::Get(x) => x.exec(context).await,
            DatasetSubcommand::Set(x) => x.exec(context).await,
        }
    }
}
