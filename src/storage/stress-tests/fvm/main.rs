// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    fuchsia_async as fasync,
    fvm_stress_test_lib::run_test,
    log::info,
    log::LevelFilter,
    stress_test_utils::{Environment, StdoutLogger},
};

#[derive(Clone, Debug, FromArgs)]
/// Creates an instance of fvm and performs stressful operations on it
struct Args {
    /// seed to use for this stressor instance
    #[argh(option, short = 's')]
    seed: Option<u128>,

    /// number of operations to complete before exiting.
    #[argh(option, short = 'o')]
    num_operations: Option<u64>,

    /// if set, the test runs for this time limit before exiting successfully.
    #[argh(option, short = 't')]
    time_limit_secs: Option<u64>,

    /// filter logging by level (off, error, warn, info, debug, trace)
    #[argh(option, short = 'l')]
    log_filter: Option<LevelFilter>,

    /// number of volumes in FVM.
    /// each volume operates on a different thread and will perform
    /// the required number of operations before exiting.
    /// defaults to 3 volumes.
    #[argh(option, short = 'n', default = "3")]
    num_volumes: u64,

    /// size of one block of the ramdisk (in bytes)
    #[argh(option, default = "512")]
    ramdisk_block_size: u64,

    /// number of blocks in the ramdisk
    /// defaults to 106MiB ramdisk
    #[argh(option, default = "217088")]
    ramdisk_block_count: u64,

    /// size of one slice in FVM (in bytes)
    #[argh(option, default = "32768")]
    fvm_slice_size: u64,

    /// limits the maximum slices in a single extend operation
    #[argh(option, default = "1024")]
    max_slices_in_extend: u64,

    /// controls the density of the partition.
    #[argh(option, default = "65536")]
    max_vslice_count: u64,

    /// controls how often volumes are force-disconnected,
    /// either by crashing FVM or by rebinding the driver.
    /// disabled if set to 0.
    #[argh(option, default = "0")]
    disconnect_secs: u64,

    /// when force-disconnection is enabled, this
    /// defines the probability with which a rebind
    /// happens instead of a crash.
    #[argh(option, default = "0.0")]
    rebind_probability: f64,
}

impl Environment for Args {
    fn init_logger(&self) {
        let filter = self.log_filter.unwrap_or(LevelFilter::Info);
        StdoutLogger::init(filter);
    }

    fn seed(&self) -> Option<u128> {
        self.seed
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Get arguments from command line
    let args: Args = argh::from_env();
    let rng = args.setup_env();

    run_test(
        rng,
        args.ramdisk_block_count,
        args.fvm_slice_size,
        args.ramdisk_block_size,
        args.num_volumes,
        args.max_slices_in_extend,
        args.max_vslice_count,
        args.disconnect_secs,
        args.rebind_probability,
        args.time_limit_secs,
        args.num_operations,
    )
    .await;

    info!("Stress test is exiting successfully!");

    Ok(())
}
