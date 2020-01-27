// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(not(target_os = "fuchsia"))]

use anyhow::Error;
use log::{LevelFilter, Metadata, Record};

struct Logger;

impl log::Log for Logger {
    fn enabled(&self, _: &Metadata<'_>) -> bool {
        true
    }

    fn log(&self, record: &Record<'_>) {
        if self.enabled(record.metadata()) {
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
            eprintln!("{}", msg);
        }
    }

    fn flush(&self) {}
}

static LOGGER: Logger = Logger;

lazy_static::lazy_static! {
    static ref START_RESULT: Result<(), log::SetLoggerError> =
        log::set_logger(&LOGGER).map(|()| log::set_max_level(LevelFilter::Error));
}

pub fn init() -> Result<(), Error> {
    if let Err(e) = &*START_RESULT {
        return Err(anyhow::format_err!("{}", e));
    }
    Ok(())
}
