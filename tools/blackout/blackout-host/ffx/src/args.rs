// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Define a standard optional subcommand setup for blackout tests to run individual steps.

/// Execute a specific test step. This assumes that a blackout target component is already running,
/// named blackout-target, in core/ffx-laboratory, and that it serves the
/// fuchsia.blackout.test.Controller protocol.
#[ffx_core::ffx_command()]
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "step")]
pub struct BlackoutCommand {
    #[argh(subcommand)]
    pub step: BlackoutSubcommand,
}

/// What test step to run
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand)]
pub enum BlackoutSubcommand {
    Setup(SetupCommand),
    Test(TestCommand),
    Verify(VerifyCommand),
}

/// Run the setup step
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "setup")]
pub struct SetupCommand {
    /// block device partition label the test is going to be run on. Setup will likely create this
    /// partition in fvm.
    #[argh(positional)]
    pub device_label: String,
    /// optional block device path to run the test on. If no path is given the test will find an
    /// appropriate device.
    #[argh(option)]
    pub device_path: Option<String>,
    /// seed to use for any random operations.
    #[argh(positional)]
    pub seed: u64,
}

/// Run the test step
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "test")]
pub struct TestCommand {
    /// block device partition label the test is going to be run on.
    #[argh(positional)]
    pub device_label: String,
    /// optional block device path to run the test on. If no path is given the test will find an
    /// appropriate device.
    #[argh(option)]
    pub device_path: Option<String>,
    /// seed to use for any random operations.
    #[argh(positional)]
    pub seed: u64,
}

/// Run the verify step
#[derive(argh::FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "verify")]
pub struct VerifyCommand {
    /// block device partition label the test is going to be run on.
    #[argh(positional)]
    pub device_label: String,
    /// optional block device path to run the test on. If no path is given the test will find an
    /// appropriate device.
    #[argh(option)]
    pub device_path: Option<String>,
    /// seed to use for any random operations.
    #[argh(positional)]
    pub seed: u64,
}
