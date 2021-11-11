// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    lazy_static::lazy_static,
    log::Level,
    log::{debug, error, info, trace, warn},
    std::fmt::Arguments,
};

lazy_static! {
    /// Global reference to default logger object.
    pub static ref LOGGER: DefaultFmtArgsLogger = DefaultFmtArgsLogger::new();
}

pub trait FmtArgsLogger {
    fn log(&self, level: Level, args: Arguments<'_>);
}

/// Logger is a facade around log crate macros.
pub struct DefaultFmtArgsLogger;

impl DefaultFmtArgsLogger {
    pub fn new() -> DefaultFmtArgsLogger {
        DefaultFmtArgsLogger {}
    }
}

impl FmtArgsLogger for DefaultFmtArgsLogger {
    fn log(&self, level: Level, args: Arguments<'_>) {
        match level {
            Level::Trace => {
                trace!("{}", args);
            }
            Level::Debug => {
                debug!("{}", args);
            }
            Level::Info => {
                info!("{}", args);
            }
            Level::Warn => {
                warn!("{}", args);
            }
            Level::Error => {
                error!("{}", args);
            }
        }
    }
}
