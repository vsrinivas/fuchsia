// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! library for target side of filesystem integrity host-target interaction tests

#![deny(missing_docs)]

use {
    structopt::StructOpt,
};

/// Common options for the target test binary
#[derive(Debug, StructOpt)]
#[structopt(rename_all = "kebab-case")]
pub struct CommonOpts {
    /// A seed to use for all random operations. Tests are deterministic relative to the provided
    /// seed.
    pub seed: u64,
    /// The block device on the target device to use for testing. WARNING: the test can (and likely
    /// will!) format this device. Don't use a main system partition!
    pub block_device: String,
}

/// A set of common subcommands for the target test binary
#[derive(Debug, StructOpt)]
pub enum CommonCommand {
    /// Run the verification step.
    #[structopt(name = "verify")]
    Verify,
    /// Run the test step.
    #[structopt(name = "test")]
    Test,
}
