// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod data;
pub mod fvm;
pub mod io;

use {
    log::{set_logger, set_max_level, LevelFilter},
    rand::{rngs::SmallRng, FromEntropy, Rng},
    std::io::{stdout, Write},
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

/// Use entropy to generate a random seed
pub fn random_seed() -> u128 {
    let mut temp_rng = SmallRng::from_entropy();
    temp_rng.gen()
}
