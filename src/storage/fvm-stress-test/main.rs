// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    fasync::futures::future::join_all,
    fuchsia_async as fasync,
    fvm_stress_test_lib::{
        fvm::{random_guid, GUID_TYPE},
        state::VolumeOperator,
        utils::init_fvm,
    },
    log::{info, set_logger, set_max_level, LevelFilter},
    rand::{rngs::SmallRng, FromEntropy, Rng, SeedableRng},
};

#[derive(Clone, Debug, FromArgs)]
/// Creates an instance of fvm and performs stressful operations on it
struct Args {
    /// seed to use for this stressor instance
    #[argh(option, short = 's')]
    seed: Option<u128>,

    /// number of operations to complete before exiting.
    #[argh(option, short = 'o', default = "50")]
    num_operations: u64,

    /// filter logging by level (off, error, warn, info, debug, trace)
    #[argh(option, short = 'l')]
    log_filter: Option<LevelFilter>,

    /// number of volumes in FVM.
    /// Each volume operates on a different thread and will perform
    /// the required number of operations before exiting.
    #[argh(option, short = 'n', default = "2")]
    num_volumes: u64,

    /// use stdout for stressor output
    #[argh(switch)]
    stdout: bool,

    /// size of one block of the ramdisk (in bytes)
    #[argh(option, default = "512")]
    ramdisk_block_size: u64,

    /// number of blocks in the ramdisk
    #[argh(option, default = "108544")]
    ramdisk_block_count: u64,

    /// size of one slice in FVM (in bytes)
    #[argh(option, default = "32768")]
    fvm_slice_size: usize,

    /// limits the maximum slices in a single extend operation
    #[argh(option, default = "1024")]
    max_slices_in_extend: u64,

    /// controls the density of the partition.
    /// If not specified, this value will be taken from the volume information.
    /// Usually, the volume information sets this value to 2^64.
    #[argh(option)]
    max_vslice_count: Option<u64>,
}

// A simple logger that prints to stdout
struct SimpleStdoutLogger;

impl log::Log for SimpleStdoutLogger {
    fn enabled(&self, _metadata: &log::Metadata<'_>) -> bool {
        true
    }

    fn log(&self, record: &log::Record<'_>) {
        if self.enabled(record.metadata()) {
            match record.level() {
                log::Level::Info => {
                    println!("{}", record.args());
                }
                log::Level::Error => {
                    eprintln!("{}: {}", record.level(), record.args());
                }
                _ => {
                    println!("{}: {}", record.level(), record.args());
                }
            }
        }
    }

    fn flush(&self) {}
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Get arguments from command line
    let args: Args = argh::from_env();

    if args.stdout {
        // Initialize SimpleStdoutLogger
        set_logger(&SimpleStdoutLogger).expect("Failed to set SimpleLogger as global logger");
    } else {
        // Use syslog
        fuchsia_syslog::init().unwrap();
    }

    if let Some(filter) = args.log_filter {
        set_max_level(filter);
    }

    let seed = if let Some(seed_value) = args.seed {
        seed_value
    } else {
        // Use entropy to generate a new seed
        let mut temp_rng = SmallRng::from_entropy();
        temp_rng.gen()
    };

    info!("------------------ fvm_stressor is starting -------------------");
    info!("ARGUMENTS = {:#?}", args);
    info!("SEED FOR THIS INVOCATION = {}", seed);
    info!("------------------------------------------------------------------");

    // Setup a panic handler that prints out details of this invocation
    let seed_clone = seed.clone();
    let args_clone = args.clone();
    let default_panic_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |panic_info| {
        println!("");
        println!("------------------ fvm_stressor has crashed -------------------");
        println!("ARGUMENTS = {:#?}", args_clone);
        println!("SEED FOR THIS INVOCATION = {}", seed_clone);
        println!("------------------------------------------------------------------");
        println!("");
        default_panic_hook(panic_info);
    }));

    // Setup FVM and wait until it is ready
    let (_test, _ramdisk, mut volume_manger) =
        init_fvm(args.ramdisk_block_size, args.ramdisk_block_count, args.fvm_slice_size).await;

    let mut rng = SmallRng::from_seed(seed.to_le_bytes());
    let mut tasks = vec![];

    for i in 0..args.num_volumes {
        let volume_rng_seed: u128 = rng.gen();
        let mut volume_rng = SmallRng::from_seed(volume_rng_seed.to_le_bytes());

        let volume_name = format!("testpart-{}", i);
        let volume_guid = random_guid(&mut volume_rng);

        let volume = volume_manger.new_volume(1, GUID_TYPE, volume_guid, &volume_name, 0x0).await;

        let mut operator = VolumeOperator::new(
            volume,
            volume_rng,
            args.max_slices_in_extend,
            args.max_vslice_count,
        )
        .await;

        let num_operations = args.num_operations;
        // Start a new thread to operate on this volume
        let task = fasync::Task::blocking(async move {
            for i in 0..num_operations {
                operator.do_random_operation(i + 1).await;
            }
            operator.destroy().await;
        });

        // Add this thread task to the list
        tasks.push(task);
    }

    join_all(tasks).await;
    Ok(())
}
