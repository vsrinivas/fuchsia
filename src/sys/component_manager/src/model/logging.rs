// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    log::Level,
    log::{debug, error, info, trace, warn},
    std::fmt::Arguments,
};

pub trait FmtArgsLogger {
    fn log(&self, level: Level, args: Arguments);
}

/// Global reference to default logger object.
pub static LOGGER: DefaultFmtArgsLogger = DefaultFmtArgsLogger {};

/// Logger is a facade around log crate macros.
pub struct DefaultFmtArgsLogger;

impl FmtArgsLogger for DefaultFmtArgsLogger {
    fn log(&self, level: Level, args: Arguments) {
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
