// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Command line argument parsing for fgsutil.

use argh::FromArgs;

#[derive(Debug, FromArgs, PartialEq)]
#[argh(
    name = "fgsutil",
    description = "GCS download utility for Fuchsia",
    note = "GCS authentication credentials are stored in ~/.fuchsia/fgsutil/config",
    error_code(1, "Invalid credentials.")
)]
pub struct Args {
    /// display version
    #[argh(switch)]
    pub version: bool,

    #[argh(subcommand)]
    pub cmd: Option<SubCommand>,
}

#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand)]
pub enum SubCommand {
    Cat(CatArgs),
    Config(ConfigArgs),
    Cp(CpArgs),
    List(ListArgs),
}

#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "cat", description = "concatenate object content to stdout")]
pub struct CatArgs {
    /// one or more objects to download.
    #[argh(positional)]
    pub gs_url: Vec<String>,
}

#[derive(Debug, FromArgs, PartialEq)]
#[argh(
    subcommand,
    name = "config",
    description = "obtain credentials and create configuration file"
)]
pub struct ConfigArgs {}

#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "cp", description = "copy files and objects")]
pub struct CpArgs {
    /// where to copy from (gs url).
    #[argh(positional)]
    pub source: String,

    /// where to copy to (local file).
    #[argh(positional)]
    pub destination: String,
}

#[derive(Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "list", description = "list providers, buckets, or objects")]
pub struct ListArgs {}
