// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// core
mod cobalt_metrics;
mod error;
mod message;
mod node;
mod power_manager;
mod shutdown_request;
mod types;
mod utils;

// nodes
mod cpu_control_handler;
mod cpu_stats_handler;
mod crash_report_handler;
mod dev_control_handler;
mod driver_manager_handler;
mod shutdown_watcher;
mod system_shutdown_handler;
mod temperature_handler;
mod thermal_limiter;
mod thermal_policy;
mod thermal_shutdown;

#[cfg(test)]
mod test;

use crate::power_manager::PowerManager;
use anyhow::Error;
use fuchsia_async as fasync;
use fuchsia_trace_provider;
use log;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Setup logging
    fuchsia_syslog::init()?;
    log::info!("started");

    // Setup tracing
    fuchsia_trace_provider::trace_provider_create_with_fdio();

    // Set up the PowerManager
    let mut pm = PowerManager::new();

    // This future should never complete
    let result = pm.run().await;
    log::error!("Unexpected exit with result: {:?}", result);
    result
}
