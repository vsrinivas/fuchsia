// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[derive(Debug, PartialEq)]
pub enum FilesystemFormat {
    Fxfs,
    Minfs,
}

impl std::str::FromStr for FilesystemFormat {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(match s {
            "fxfs" => Self::Fxfs,
            "minfs" => Self::Minfs,
            _ => panic!("Invalid format"),
        })
    }
}

/// Run a power-failure test on a mutable fs (e.g.  Fxfs or minfs). Load pattern: generate, move,
/// and delete random directory trees.
#[ffx_core::ffx_command()]
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "fs-tree")]
pub struct FsTreeCommand {
    /// the path of the block device on the target device to use for testing. If one isn't provided
    /// the test will pick an appropriate one. WARNING: the test can (and likely
    /// will!) format this device. Don't use a main system partition!
    #[argh(option, short = 'p')]
    pub device_path: Option<String>,
    /// which filesystem format to target.
    #[argh(option, long = "format")]
    pub format: FilesystemFormat,
    /// a seed to use for all random operations. Tests are NOT deterministic relative to the
    /// provided seed. The operations will be identical, but because of the non-deterministic
    /// timing-dependent nature of the tests, the exact time the reboot is triggered in relation to
    /// the operations is not guaranteed.
    ///
    /// One will be randomly generated if not provided. When performing the same test multiple times
    /// in one run, a new seed will be generated for each run if one was not provided.
    #[argh(option, short = 's', long = "seed")]
    pub seed: Option<u64>,
    /// path to a power relay for cutting the power to a device. Probably the highest-numbered
    /// /dev/ttyUSB[N]. If in doubt, try removing it and seeing what disappears from /dev. When a
    /// relay is provided, the harness automatically switches to use hardware reboots.
    #[argh(option, short = 'r', long = "relay")]
    pub relay: Option<std::path::PathBuf>,
    /// use dmc to trigger reboots. Only works in infra.
    #[argh(switch)]
    pub dmc_reboot: bool,
    /// run the test N number of times, collecting statistics on the number of failures.
    #[argh(option, short = 'i', long = "iterations")]
    pub iterations: Option<u64>,
    /// run the test until a verification failure is detected, then exit.
    #[argh(switch, short = 'f', long = "run-until-failure")]
    pub run_until_failure: bool,
    /// run a bootserver to serve netboot images to a device. This will only work in infra!
    #[argh(switch)]
    pub bootserver: bool,
}
