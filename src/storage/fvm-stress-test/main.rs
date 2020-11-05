// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::FromArgs,
    fasync::futures::future::join_all,
    fuchsia_async as fasync,
    fuchsia_zircon::Vmo,
    futures::SinkExt,
    fvm_stress_test_lib::test_instance::TestInstance,
    log::{debug, info, set_logger, set_max_level, LevelFilter},
    rand::{rngs::SmallRng, FromEntropy, Rng, SeedableRng},
    std::{thread::sleep, time::Duration},
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
    } else {
        set_max_level(LevelFilter::Info);
    }

    let seed = if let Some(seed_value) = args.seed {
        seed_value
    } else {
        // Use entropy to generate a new seed
        let mut temp_rng = SmallRng::from_entropy();
        temp_rng.gen()
    };

    let mut rng = SmallRng::from_seed(seed.to_le_bytes());

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

    // Create the VMO that the ramdisk is backed by
    let vmo_size = args.ramdisk_block_count * args.ramdisk_block_size;
    let vmo = Vmo::create(vmo_size).unwrap();

    // Create the first test instance, setup FVM on the ramdisk and wait until it is ready.
    let mut instance = TestInstance::init(&vmo, args.fvm_slice_size, args.ramdisk_block_size).await;

    // Create the volume operators
    let (tasks, mut senders) = instance
        .create_volumes_and_operators(
            &mut rng,
            args.num_volumes,
            args.max_slices_in_extend,
            args.max_vslice_count,
            args.num_operations,
        )
        .await;

    if args.disconnect_secs > 0 {
        // Create the disconnection task
        fasync::Task::blocking(async move {
            loop {
                sleep(Duration::from_secs(args.disconnect_secs));

                if rng.gen_bool(args.rebind_probability) {
                    debug!("Rebinding FVM");
                    instance.rebind().await;
                } else {
                    // Crash the old instance and replace it with a new instance.
                    // This will cause the component tree to be taken down abruptly.
                    debug!("Crashing FVM");
                    instance.crash();
                    instance = TestInstance::existing(&vmo, args.ramdisk_block_size).await;
                }

                // Give the new block path to the operators.
                // Ignore the result because some operators may have completed.
                let path = instance.block_path();
                for sender in senders.iter_mut() {
                    let _ = sender.send(path.clone()).await;
                }
            }
        })
        .detach();
    }

    join_all(tasks).await;

    Ok(())
}
