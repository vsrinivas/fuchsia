// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! library for target side of filesystem integrity host-target interaction tests

#![deny(missing_docs)]

use {
    anyhow::Error,
    fidl_fuchsia_device::ControllerMarker,
    files_async::readdir,
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon as zx, io_util,
    rand::{distributions, rngs::StdRng, Rng, SeedableRng},
    structopt::StructOpt,
};

pub mod static_tree;

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

/// Generate a Vec<u8> of random bytes from a seed using a standard distribution.
pub fn generate_content(seed: u64) -> Vec<u8> {
    let mut rng = StdRng::seed_from_u64(seed);

    let size = rng.gen_range(1..1 << 16);
    rng.sample_iter(&distributions::Standard).take(size).collect()
}

/// Find the device in /dev/class/block that represents a given topological path. Returns the full
/// path of the device in /dev/class/block.
pub async fn find_dev(dev: &str) -> Result<String, Error> {
    let dev_class_block = io_util::open_directory_in_namespace(
        "/dev/class/block",
        io_util::OPEN_RIGHT_READABLE | io_util::OPEN_RIGHT_WRITABLE,
    )?;
    for entry in readdir(&dev_class_block).await? {
        let path = format!("/dev/class/block/{}", entry.name);
        let proxy = connect_to_protocol_at_path::<ControllerMarker>(&path)?;
        let topo_path = proxy.get_topological_path().await?.map_err(|s| zx::Status::from_raw(s))?;
        println!("{} => {}", path, topo_path);
        if dev == topo_path {
            return Ok(path);
        }
    }
    Err(anyhow::anyhow!("Couldn't find {} in /dev/class/block", dev))
}
