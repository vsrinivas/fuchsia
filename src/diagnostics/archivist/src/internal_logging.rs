// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use log::{set_logger, set_max_level, LevelFilter, Log, Metadata, Record};

/// Log implementation for when archivist itself needs to log messages.
struct Logger {
    app_name: &'static str,
}

impl Log for Logger {
    fn enabled(&self, _metadata: &Metadata<'_>) -> bool {
        true
    }

    fn log(&self, record: &Record<'_>) {
        eprintln!("[{}] {}: {}", self.app_name, record.level(), record.args());
    }

    fn flush(&self) {}
}

/// Initialize internal logging.
pub fn init(app_name: &'static str) {
    let logger = Box::leak(Box::new(Logger { app_name }));
    set_logger(logger).unwrap();
    set_max_level(LevelFilter::Info);
}
