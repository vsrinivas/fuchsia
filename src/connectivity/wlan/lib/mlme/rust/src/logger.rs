// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/41417): Remove this entire file once devmgr installs a Rust logger.

// This file is copied from the wlanstack logger in src/connectivity/wlan/wlanstack/src/logger.rs.
//
// We intentionally don't share this code with wlanstack as this entire file will go away once
// devmgr provides its own logger.

const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;

const LOG_LEVEL: log::Level = log::Level::Debug;

pub struct Logger;

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
        metadata.level() <= LOG_LEVEL
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

pub fn install() {
    if let Err(e) = log::set_logger(&Logger) {
        println!("Failed to install logger (this is probably fine!): {:?}", e);
        return;
    }

    log::set_max_level(MAX_LOG_LEVEL);
}
