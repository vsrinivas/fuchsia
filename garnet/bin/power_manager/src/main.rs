// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// core
mod error;
mod message;
mod node;
mod power_manager;
mod types;
mod utils;

// nodes
mod cpu_control_handler;
mod cpu_stats_handler;
mod dev_control_handler;
mod system_power_handler;
mod temperature_handler;
mod thermal_limiter;
mod thermal_policy;

#[cfg(test)]
mod test;

use crate::power_manager::PowerManager;
use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use fuchsia_trace_provider;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Setup tracing
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    // Setup syslog
    fuchsia_syslog::init_with_tags(&["power_manager"])?;
    fuchsia_syslog::set_verbosity(1);
    fx_log_info!("started");

    // Setup the PowerManager
    let mut pm = PowerManager::new();

    // This future should never complete
    log_if_err!(pm.run().await, "Failed to run PowerManager");

    Ok(())
}
