// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    ffx_core::ffx_command,
    std::{
        net::{Ipv4Addr, SocketAddr},
        str::FromStr as _,
    },
};

#[ffx_command()]
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "serve", description = "")]
pub struct ServeCommand {
    /// serve repositories on this address. `ADDRESS` is optional. Defaults to `localhost:8084`.
    #[argh(option, short = 'a', default = "default_listen_addr()", from_str_fn(parse_listen_addr))]
    pub listen_address: SocketAddr,
}

fn default_listen_addr() -> SocketAddr {
    (Ipv4Addr::LOCALHOST, 8084).into()
}

fn parse_listen_addr(addr: &str) -> Result<SocketAddr, String> {
    let addr = SocketAddr::from_str(addr).map_err(|err| err.to_string())?;

    // FIXME(http://fxbug.dev/77015): ffx repository uses a hardcoded ssh reverse tunnel for the
    // repository. This should be removed once we can dynamically create tunnels.
    let default_addr = default_listen_addr();
    if addr != default_addr {
        return Err(format!("--listen-address is currently required to be {}", default_addr));
    }

    Ok(addr)
}
