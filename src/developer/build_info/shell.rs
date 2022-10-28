// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use argh::FromArgs;
use fidl_fuchsia_buildinfo::ProviderMarker;
use fuchsia_component::client::connect_to_protocol;
use futures as _;

#[derive(Debug, Clone, Copy, PartialEq)]
enum Info {
    BoardConfig,
    LatestCommitDate,
    ProductConfig,
    Version,
}

impl std::str::FromStr for Info {
    type Err = String;

    fn from_str(value: &str) -> Result<Self, Self::Err> {
        match value {
            "board_config" => Ok(Info::BoardConfig),
            "latest_commit_date" => Ok(Info::LatestCommitDate),
            "product_config" => Ok(Info::ProductConfig),
            "version" => Ok(Info::Version),
            _ => Err(format!("Invalid key: {:?}.", value)),
        }
    }
}

/// Build Information command.
#[derive(Debug, PartialEq, FromArgs)]
struct BuildInfoCmd {
    /// valid keys: <board_config> <latest_commit_date> <product_config> <version>
    #[argh(positional)]
    info: Info,
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    let provider = connect_to_protocol::<ProviderMarker>()
        .context("Failed to connect to build info service")?;

    let args: BuildInfoCmd = argh::from_env();
    let info = provider.get_build_info().await?;
    let output = match args.info {
        Info::BoardConfig => info.board_config,
        Info::LatestCommitDate => info.latest_commit_date,
        Info::ProductConfig => info.product_config,
        Info::Version => info.version,
    };

    println!("{}", output.unwrap());
    Ok(())
}
