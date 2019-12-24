// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_syslog::macros::*;

/// Perform Logging operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct LoggingFacade {}

impl LoggingFacade {
    pub fn new() -> LoggingFacade {
        LoggingFacade {}
    }

    pub async fn log_err(&self, message: String) -> Result<(), Error> {
        fx_log_err!("{:?}", message);
        Ok(())
    }

    pub async fn log_info(&self, message: String) -> Result<(), Error> {
        fx_log_info!("{:?}", message);
        Ok(())
    }

    pub async fn log_warn(&self, message: String) -> Result<(), Error> {
        fx_log_warn!("{:?}", message);
        Ok(())
    }
}
