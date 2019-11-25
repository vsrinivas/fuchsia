// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

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
