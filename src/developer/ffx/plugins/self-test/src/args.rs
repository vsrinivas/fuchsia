// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_selftest_sub_command::Subcommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "self-test", description = "Execute the ffx self-test (e2e) suite")]
pub struct SelftestCommand {
    #[argh(
        option,
        default = "280",
        description = "maximum runtime of entire test suite in seconds"
    )]
    pub timeout: u64,

    #[argh(
        option,
        default = "10",
        description = "maximum run time of a single test case in seconds"
    )]
    pub case_timeout: u64,

    #[argh(option, default = "true", description = "include target interaction tests")]
    pub include_target: bool,

    #[argh(
        option,
        description = "the path to find an ssh key for host/target communication. This command creates an isolated environment for each test. It is designed to work within the CQ infrastructure where certain environment variables are set related to the location of temp directories as well as the FUCHSIA_SSH_KEY variable. If you are running this command locally and wish to run tests where `include_target` is true, then you must also provide a way to find an ssh key. This argument is one way to do that. The other is to emulate the CQ environment and set FUCHSIA_SSH_KEY."
    )]
    pub ssh_key_path: Option<String>,

    #[argh(subcommand)]
    pub subcommand: Option<Subcommand>,
}
