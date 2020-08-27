// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    blobfs_stress_test_lib::{state::BlobfsState, utils::init_blobfs},
    fuchsia_async as fasync,
    log::{info, set_logger, set_max_level, LevelFilter},
    rand::{rngs::SmallRng, FromEntropy, Rng, SeedableRng},
    structopt::StructOpt,
};

#[derive(Clone, StructOpt, Debug)]
#[structopt(
    name = "blobfs stress test (blobfs_stressor) tool",
    about = "Creates an instance of blobfs and performs stressful operations on it"
)]

struct Opt {
    /// Seed to use for this stressor instance
    #[structopt(short = "s", long = "seed")]
    seed: Option<u128>,

    /// Number of operations to complete before exiting.
    /// Otherwise stressor will run indefinitely.
    #[structopt(short = "ops", long = "num_operations")]
    num_operations: Option<u64>,

    /// Filter logging by level (off, error, warn, info, debug, trace)
    #[structopt(short = "l", long = "log_filter", default_value = "info")]
    log_filter: LevelFilter,
}

// A simple logger that logs to stdout
struct SimpleLogger;

impl log::Log for SimpleLogger {
    fn enabled(&self, _metadata: &log::Metadata<'_>) -> bool {
        true
    }

    fn log(&self, record: &log::Record<'_>) {
        if self.enabled(record.metadata()) {
            if record.level() == log::Level::Info {
                println!("{}", record.args());
            } else {
                println!("{}: {}", record.level(), record.args());
            }
        }
    }

    fn flush(&self) {}
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Get arguments from command line
    let opt = Opt::from_args();

    // Initialize SimpleLogger (just prints to stdout)
    set_logger(&SimpleLogger).expect("Failed to set SimpleLogger as global logger");
    set_max_level(opt.log_filter);

    let seed = if let Some(seed_value) = opt.seed {
        seed_value
    } else {
        // Use entropy to generate a new seed
        let mut temp_rng = SmallRng::from_entropy();
        temp_rng.gen()
    };

    info!("------------------ blobfs_stressor is starting -------------------");
    info!("ARGUMENTS = {:#?}", opt);
    info!("SEED FOR THIS INVOCATION = {}", seed);
    info!("------------------------------------------------------------------");

    // Setup a panic handler that prints out details of this invocation
    let seed_clone = seed.clone();
    let opt_clone = opt.clone();
    let default_panic_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(move |panic_info| {
        println!("");
        println!("------------------ blobfs_stressor has crashed -------------------");
        println!("ARGUMENTS = {:#?}", opt_clone);
        println!("SEED FOR THIS INVOCATION = {}", seed_clone);
        println!("------------------------------------------------------------------");
        println!("");
        default_panic_hook(panic_info);
    }));

    // Setup blobfs and wait until it is ready
    let (_test, root_dir) = init_blobfs().await;

    // Initialize blobfs in-memory state
    let rng = SmallRng::from_seed(seed.to_le_bytes());
    let mut state = BlobfsState::new(root_dir, rng);

    if let Some(num_operations) = opt.num_operations {
        info!("Performing {} operations...", num_operations);
        for _ in 0..num_operations {
            state.do_random_operation().await;
        }
    } else {
        info!("Running indefinitely...");
        loop {
            state.do_random_operation().await;
        }
    }

    info!("Run successful!");

    Ok(())
}
