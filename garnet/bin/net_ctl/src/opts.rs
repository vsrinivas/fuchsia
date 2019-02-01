// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use structopt::StructOpt;

#[derive(Debug, StructOpt)]
#[structopt(
    name = "net_ctl commands",
    about = "Commands to configure networking interface"
)]
pub enum Opt {
    /// Network interface configuration
    #[structopt(name = "if")]
    If(ObserverCmd),
}

#[derive(Debug, StructOpt)]
pub enum ObserverCmd {
    /// List network interfaces
    #[structopt(name = "list")]
    List,

    #[structopt(name = "get")]
    /// Query a network interface
    Get {
        /// Name of the network interface to query
        name: String,
    },
}
