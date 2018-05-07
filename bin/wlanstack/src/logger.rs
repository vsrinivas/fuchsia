// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use log::{self, LogLevel, LogMetadata, LogRecord};

const LOG_LEVEL: LogLevel = LogLevel::Debug;

pub struct Logger;

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
                "{} [{}]: {}",
                record.target(),
                short_log_level(&record.level()),
                record.args()
            );
        }
    }
}
