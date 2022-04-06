// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[ffx_core::ffx_command()]
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "blobfs-checkerboard",
    description = "Run a power-failure test on blobfs with a checkerboard load pattern"
)]
pub struct BlobfsCheckerboardCommand {
    /// the block device on the target device to use for testing. WARNING: the test can (and likely
    /// will!) format this device. Don't use a main system partition!
    #[argh(positional)]
    pub block_device: String,
    /// the target device to ssh into and execute the test on. A good way to configure this locally
    /// is by using `$(fx get-device-addr)`.
    #[argh(positional)]
    pub target: String,
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
    /// run the test N number of times, collecting statistics on the number of failures.
    #[argh(option, short = 'i', long = "iterations")]
    pub iterations: Option<u64>,
    /// run the test until a verification failure is detected, then exit.
    #[argh(switch, short = 'f', long = "run-until-failure")]
    pub run_until_failure: bool,
    /// path to the ssh private key to use when authenticating with the target device. If neither
    /// this flag or the --ssh-agent flag is set, the test will open `$FUCHSIA_DIR/.ssh/pkey` if
    /// `$FUCHSIA_DIR` exists, otherwise it will attempt to open `$CWD/.ssh/pkey`. If none of these
    /// attempts succeed the test will fail to run.
    #[argh(option, short = 'k', long = "ssh-key")]
    pub ssh_key: Option<String>,
    /// use the ssh agent to authenticate with the target device. If both this option and --ssh-key
    /// are provided, --ssk-key will take precedence.
    #[argh(switch, short = 'a', long = "ssh-agent")]
    pub ssh_agent: bool,
}
