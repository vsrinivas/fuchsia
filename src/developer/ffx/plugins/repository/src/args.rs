// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    ffx_core::ffx_command,
    std::{
        net::{Ipv4Addr, SocketAddr},
        path::PathBuf,
        str::FromStr as _,
    },
};

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "repository", description = "")]
pub struct RepositoryCommand {
    #[argh(subcommand)]
    pub sub: SubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubCommand {
    Serve(ServeCommand),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "serve", description = "")]
pub struct ServeCommand {
    /// serve the repository on this address. `ADDRESS` is optional. Defaults to `0.0.0.0:8083`.
    #[argh(option, short = 'a', default = "default_listen_addr()", from_str_fn(parse_listen_addr))]
    pub listen_address: SocketAddr,

    /// repositories will be named `NAME`. Defaults to `devhost`.
    #[argh(option, default = "\"devhost\".to_string()")]
    pub name: String,

    /// path to the package repository.
    #[argh(positional)]
    pub repo_path: PathBuf,
}

fn default_listen_addr() -> SocketAddr {
    (Ipv4Addr::UNSPECIFIED, 8083).into()
}

fn parse_listen_addr(addr: &str) -> Result<SocketAddr, String> {
    SocketAddr::from_str(addr).map_err(|err| err.to_string())
}
