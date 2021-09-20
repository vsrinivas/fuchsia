// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs, ffx_core::ffx_command, fidl_fuchsia_net::SocketAddress,
    fidl_fuchsia_net_ext as net_ext, std::net::SocketAddr,
};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "tcp", description = "Forward a TCP port from the target to the host")]
pub struct TcpCommand {
    #[argh(positional, from_str_fn(parse_addr))]
    pub host_address: SocketAddress,
    #[argh(positional, from_str_fn(parse_addr))]
    pub target_address: SocketAddress,
}

fn parse_addr(addr: &str) -> Result<SocketAddress, String> {
    let sock_addr = if addr.find(':').is_none() {
        SocketAddr::new(
            [127, 0, 0, 1].into(),
            addr.parse().map_err(|_| "Invalid port number".to_owned())?,
        )
    } else {
        addr.parse::<SocketAddr>().map_err(|_| "Invalid address".to_owned())?
    };

    Ok(net_ext::SocketAddress(sock_addr).into())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_parse_addr() {
        assert!(parse_addr("2000").is_ok());
        assert!(parse_addr("127.0.0.5:2000").is_ok());
        assert!(parse_addr("[::1]:2000").is_ok());
        assert_eq!(Err("Invalid port number".to_owned()), parse_addr("127.0.0.1"));
        assert_eq!(Err("Invalid address".to_owned()), parse_addr("::1"));
        assert_eq!(Err("Invalid address".to_owned()), parse_addr("[::1]"));
    }
}
