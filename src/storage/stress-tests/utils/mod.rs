// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod data;
pub mod fvm;
pub mod io;

use {
    log::{info, set_logger, set_max_level, LevelFilter},
    rand::{rngs::SmallRng, FromEntropy, Rng, SeedableRng},
    std::{
        fmt::Debug,
        io::{stdout, Write},
    },
};

// A simple logger that prints to stdout
pub struct StdoutLogger;

impl StdoutLogger {
    pub fn init(filter: LevelFilter) {
        set_logger(&StdoutLogger).expect("Failed to set StdoutLogger as global logger");
        set_max_level(filter);
    }
}

impl log::Log for StdoutLogger {
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

    fn flush(&self) {
        stdout().flush().unwrap();
    }
}

/// This trait helps configure the stress test environment by setting up logging,
/// crash handling and random number generation.
pub trait Environment: 'static + Clone + Debug + Send + Sync {
    /// Initialize a logger, if necessary.
    fn init_logger(&self);

    /// The seed to be used for this stress test environment.
    /// If not set, a random seed will be used.
    fn seed(&self) -> Option<u128>;

    /// Returns the RNG to be used for this test.
    fn setup_env(&self) -> SmallRng {
        self.init_logger();

        // Initialize seed
        let seed = match self.seed() {
            Some(seed) => seed,
            None => random_seed(),
        };
        let rng = SmallRng::from_seed(seed.to_le_bytes());

        info!("--------------------- stressor is starting -----------------------");
        info!("ENVIRONMENT = {:#?}", self);
        info!("SEED FOR THIS INVOCATION = {}", seed);
        info!("------------------------------------------------------------------");

        // Setup a panic handler that prints out details of this invocation
        let self_clone = self.clone();
        let seed = seed.clone();
        let default_panic_hook = std::panic::take_hook();
        std::panic::set_hook(Box::new(move |panic_info| {
            eprintln!("");
            eprintln!("--------------------- stressor has crashed -----------------------");
            eprintln!("ENVIRONMENT = {:#?}", self_clone);
            eprintln!("SEED FOR THIS INVOCATION = {}", seed);
            eprintln!("------------------------------------------------------------------");
            eprintln!("");
            default_panic_hook(panic_info);
        }));

        rng
    }
}

/// Use entropy to generate a random seed
fn random_seed() -> u128 {
    let mut temp_rng = SmallRng::from_entropy();
    temp_rng.gen()
}
