// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[deny(warnings)]
use failure::Error;
use std::env;
use std::net::SocketAddr;
use std::process;

fn main() -> Result<(), Error> {
    let args: Vec<String> = env::args().collect();
    if args.len() != 2 {
        println!("Takes exactly one argument:\n");
        println!("  <IPv4 Address> - IPv4 address to use for the test");
        process::exit(1);
    }
    let ip = &args[1].to_string();
    let dst_port = "5000".to_string();
    let newdst_port = "5001".to_string();
    let dst: SocketAddr = format!("{}:{}", ip, dst_port).parse()?;
    let newdst: SocketAddr = format!("{}:{}", ip, newdst_port).parse()?;
    println!("DstHost: {}, DstPort: {}", dst.ip(), dst.port());
    println!("NewDstHost: {}, NewDstPort: {}", newdst.ip(), newdst.port());

    // TODO(cgibson): Make requests to the filter FIDL API to modify packet filter rules.

    Ok(())
}
