// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use log::{LevelFilter, Metadata, Record};
use std::sync::Once;

struct Logger;

impl log::Log for Logger {
    fn enabled(&self, _: &Metadata<'_>) -> bool {
        true
    }

    fn log(&self, record: &Record<'_>) {
        let msg = format!(
            "{} {} {:?} {} {}{}",
            std::process::id(),
            chrono::Utc::now(),
            std::thread::current().id(),
            record.level(),
            record
                .file()
                .map(|file| {
                    if let Some(line) = record.line() {
                        format!("{}:{}: ", file, line)
                    } else {
                        format!("{}: ", file)
                    }
                })
                .unwrap_or(String::new()),
            record.args()
        );
        let _ = std::panic::catch_unwind(|| {
            eprintln!("{}", msg);
        });
    }

    fn flush(&self) {}
}

static LOGGER: Logger = Logger;
static START: Once = Once::new();

pub(crate) fn init() {
    START.call_once(|| {
        log::set_logger(&LOGGER).expect("failed to set logger");
        log::set_max_level(LevelFilter::Info);
    })
}
