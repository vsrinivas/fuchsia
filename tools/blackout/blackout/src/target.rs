// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! library for target side of filesystem integrity host-target interaction tests

#![deny(missing_docs)]

use {
    rand::{distributions, rngs::StdRng, Rng, SeedableRng},
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
    /// Run the setup step.
    #[structopt(name = "setup")]
    Setup,
    /// Run the test step.
    #[structopt(name = "test")]
    Test,
    /// Run the verification step.
    #[structopt(name = "verify")]
    Verify,
}

/// Generate a random file name. It will be 17 characters long, start with an 'a' to confirm it's a
/// valid file name, and contain random letters and numbers.
pub fn generate_name(seed: u64) -> String {
    let mut rng = StdRng::seed_from_u64(seed);

    let mut name = String::from("a");
    name.push_str(&rng.sample_iter(&distributions::Alphanumeric).take(16).collect::<String>());
    name
}

/// Generate a Vec<u8> of random bytes from a seed using a standard distribution.
pub fn generate_content(seed: u64) -> Vec<u8> {
    let mut rng = StdRng::seed_from_u64(seed);

    let size = rng.gen_range(1, 1 << 16);
    rng.sample_iter(&distributions::Standard).take(size).collect()
}
