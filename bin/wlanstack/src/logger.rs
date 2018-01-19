// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use log::{self, LogLevel, LogMetadata, LogRecord};

const LOG_LEVEL: LogLevel = LogLevel::Debug;

#[derive(Default)]
pub struct Logger {
    prefix: String,
}

impl Logger {
    pub fn new() -> Logger {
        Logger::default()
    }
    pub fn set_prefix(&mut self, prefix: &str) {
        self.prefix = prefix.to_owned();
    }
}

fn short_log_level(level: &LogLevel) -> &'static str {
    match *level {
        LogLevel::Error => "E",
        LogLevel::Warn => "W",
        LogLevel::Info => "I",
        LogLevel::Debug => "D",
        LogLevel::Trace => "T",
    }
}

impl log::Log for Logger {
    fn enabled(&self, metadata: &LogMetadata) -> bool {
        metadata.level() <= LOG_LEVEL
    }
    fn log(&self, record: &LogRecord) {
        if self.enabled(record.metadata()) {
            println!(
                "{}[{}] {}",
                self.prefix,
                short_log_level(&record.level()),
                record.args()
            );
        }
    }
}
