// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, eui48::MacAddress, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "deprecated", description = "Controls to-be-deleted WLAN functionality.")]
pub struct DeprecatedCommand {
    #[argh(subcommand)]
    pub subcommand: DeprecatedSubcommand,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum DeprecatedSubcommand {
    SuggestMac(SuggestMac),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "suggest-mac",
    description = "Notifies WLAN policy of a MAC address that ought to be used when creating new \
                   AP interfaces",
    example = "To suggest an AP MAC address

    $ ffx wlan deprecated suggest-mac 01:02:03:04:05:06"
)]
pub struct SuggestMac {
    #[argh(positional, description = "WLAN MAC address")]
    pub mac: MacAddress,
}
