// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod deletion_actor;
mod environment;
mod file_actor;
mod instance_actor;

use {
    argh::FromArgs,
    environment::FsEnvironment,
    fs_management::{Fxfs, Minfs},
    fuchsia_async as fasync,
    log::LevelFilter,
    stress_test::{run_test, StdoutLogger},
};

#[derive(Clone, Debug, FromArgs)]
/// Creates an instance of fvm and performs stressful operations on it
pub struct Args {
    /// seed to use for this stressor instance
    #[argh(option, short = 's')]
    seed: Option<u64>,

    /// number of operations to complete before exiting.
    #[argh(option, short = 'o')]
    num_operations: Option<u64>,

    /// filter logging by level (off, error, warn, info, debug, trace)
    #[argh(option, short = 'l')]
    log_filter: Option<LevelFilter>,

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

    /// controls how often blobfs is killed and the ramdisk is unbound
    #[argh(option, short = 'd')]
    disconnect_secs: Option<u64>,

    /// if set, the test runs for this time limit before exiting successfully.
    #[argh(option, short = 't')]
    time_limit_secs: Option<u64>,

    /// which filesystem to target (e.g. 'fxfs' or 'minfs').
    #[argh(option, short = 'f')]
    target_filesystem: String,

    /// parameter passed in by rust test runner
    #[argh(switch)]
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    nocapture: bool,
}

#[fasync::run_singlethreaded(test)]
async fn test() {
    let args: Args = argh::from_env();

    StdoutLogger::init(args.log_filter.unwrap_or(LevelFilter::Info));

    match args.target_filesystem.as_str() {
        "fxfs" => {
            let env = FsEnvironment::new(Fxfs::default(), args).await;
            run_test(env).await;
        }
        "minfs" => {
            let env = FsEnvironment::new(Minfs::default(), args).await;
            run_test(env).await;
        }
        _ => panic!("Unsupported filesystem {}", args.target_filesystem),
    }
}
