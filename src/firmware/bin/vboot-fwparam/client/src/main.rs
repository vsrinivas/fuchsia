// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod param;

use crate::param::{Parameter, PARAMETERS};
use anyhow::{Context, Error};
use argh::FromArgs;
use fidl_fuchsia_vboot_fwparam::{FirmwareParamMarker, FirmwareParamProxy};
use fuchsia_zircon as zx;

#[derive(FromArgs, PartialEq, Debug)]
/// A tool to interact with vboot's firmware parameters.
struct Args {
    #[argh(subcommand)]
    cmd: SubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "get")]
/// Get the value of a key stored in nvram, or all values if no key is provided.
struct GetArgs {
    #[argh(positional)]
    /// the key to lookup.
    key: Option<String>,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "set")]
/// Set a value in nvram.
struct SetArgs {
    #[argh(positional)]
    /// the key to set.
    key: String,
    #[argh(positional)]
    /// the value to store.
    value: String,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SubCommand {
    Get(GetArgs),
    Set(SetArgs),
}

#[fuchsia::component]
async fn main() {
    let args: Args = argh::from_env();
    run_cmd(args).await.unwrap_or_else(|e| {
        println!("Error while running command: {:?}", e);
    });
}

async fn run_cmd(args: Args) -> Result<(), Error> {
    let proxy = fuchsia_component::client::connect_to_protocol::<FirmwareParamMarker>()
        .context("Connecting to firmware parameter service")?;
    match args.cmd {
        SubCommand::Get(args) => {
            if let Some(key) = args.key {
                if let Some(param) = PARAMETERS.iter().find(|p| p.name == key.as_str()) {
                    display_one(param, &proxy).await?;
                } else {
                    println!("No such parameter!");
                }
            } else {
                for param in PARAMETERS.iter() {
                    display_one(param, &proxy).await?;
                }
            }
        }
        SubCommand::Set(args) => {
            if let Some(param) = PARAMETERS.iter().find(|p| p.name == args.key.as_str()) {
                set_one(param, &proxy, &args.value).await?;
                display_one(param, &proxy).await?;
            } else {
                println!("No such parameter!");
            }
        }
    };
    Ok(())
}

async fn display_one(p: &Parameter, proxy: &FirmwareParamProxy) -> Result<(), Error> {
    let result = proxy
        .get(p.key)
        .await
        .context("Sending fidl get")?
        .map_err(zx::Status::from_raw)
        .context("Getting key")?;
    println!(
        "{}={}\t\t{} ({})",
        p.name,
        p.ty.display(result).context("Displaying result")?,
        p.desc,
        p.ty
    );

    Ok(())
}

async fn set_one(p: &Parameter, proxy: &FirmwareParamProxy, value: &str) -> Result<(), Error> {
    match p.ty.parse(value) {
        Ok(value) => proxy
            .set(p.key, value)
            .await
            .context("Sending fidl set")?
            .map_err(zx::Status::from_raw)
            .context("Setting key to value")?,
        Err(e) => println!("Invalid value: {:?}. Expected: {}", e, p.ty),
    };
    Ok(())
}
