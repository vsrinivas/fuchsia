// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use std::sync::Once;

/// log::Log implementation that uses stdout.
///
/// Useful when debugging tests.
struct Logger(log::Level);

fn short_log_level(level: &log::Level) -> &'static str {
    match *level {
        log::Level::Error => "E",
        log::Level::Warn => "W",
        log::Level::Info => "I",
        log::Level::Debug => "D",
        log::Level::Trace => "T",
    }
}

impl log::Log for Logger {
    fn enabled(&self, metadata: &log::Metadata<'_>) -> bool {
        metadata.level() <= self.0
    }

    fn log(&self, record: &log::Record<'_>) {
        if self.enabled(record.metadata()) {
            println!(
                "{} [{}]: {}",
                record.target(),
                short_log_level(&record.level()),
                record.args()
            );
        }
    }

    fn flush(&self) {}
}

static TEST_LOGGER: Logger = Logger(log::Level::Trace);

static LOGGER_ONCE: Once = Once::new();

/// Install a logger for tests.
///
/// Call this method at the beginning of the test for which logging is desired.
/// This function sets global program state, so all tests that run after this
/// function is called will use the logger.
pub fn set_logger_for_test() {
    // log::set_logger will panic if called multiple times; using a Once makes
    // set_logger_for_test idempotent
    LOGGER_ONCE.call_once(|| {
        log::set_logger(&TEST_LOGGER).unwrap();
        log::set_max_level(log::LevelFilter::Trace);
    })
}
