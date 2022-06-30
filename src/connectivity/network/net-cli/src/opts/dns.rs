// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand, name = "dns")]
/// commands to control the dns resolver
pub struct Dns {
    #[argh(subcommand)]
    pub dns_cmd: DnsEnum,
}

#[derive(FromArgs, Clone, Debug, PartialEq)]
#[argh(subcommand)]
pub enum DnsEnum {
    Lookup(Lookup),
}

#[derive(Clone, Debug, FromArgs, PartialEq)]
#[argh(subcommand, name = "lookup")]
/// performs dns resolution on the specified hostname
pub struct Lookup {
    #[argh(positional)]
    pub hostname: String,
    #[argh(option, default = "true")]
    /// include ipv4 results (defaults to true)
    pub ipv4: bool,
    #[argh(option, default = "true")]
    /// include ipv6 results (defaults to true)
    pub ipv6: bool,
    #[argh(option, default = "true")]
    /// sort addresses in order of preference (defaults to true)
    pub sort: bool,
}
