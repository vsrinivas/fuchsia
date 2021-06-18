// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
mod env_info;
mod ga_event;
pub mod metrics_event_batch;
mod metrics_service;
mod metrics_state;
mod notice;

use {
    anyhow::{bail, Result},
    std::collections::BTreeMap,
    std::path::PathBuf,
};

use crate::env_info::{analytics_folder, is_analytics_disabled_by_env};
use crate::metrics_event_batch::MetricsEventBatch;
use crate::metrics_service::*;
use crate::metrics_state::{MetricsState, UNKNOWN_VERSION};

const INIT_ERROR: &str = "Please call analytics::init prior to any other analytics api calls.";

/// This function initializes the metrics service so that an app
/// can make posts to the analytics service and read the current opt in status of the user
pub async fn init(app_name: String, build_version: Option<String>, ga_product_code: String) {
    METRICS_SERVICE.lock().await.inner_init(MetricsState::from_config(
        &PathBuf::from(&analytics_folder()),
        app_name,
        build_version.unwrap_or(UNKNOWN_VERSION.into()),
        ga_product_code,
        is_analytics_disabled_by_env(),
    ));
}

/// Returns a legal notice of metrics data collection if user
/// is new to all tools (full notice) or new to this tool (brief notice).
/// Returns an error if init has not been called.
pub async fn get_notice() -> Option<String> {
    let svc = METRICS_SERVICE.lock().await;
    match &svc.init_state {
        MetricsServiceInitStatus::INITIALIZED => svc.inner_get_notice(),
        MetricsServiceInitStatus::UNINITIALIZED => {
            log::error!("get_notice called on uninitialized METRICS_SERVICE");
            None
        }
    }
}

/// Records intended opt in status.
/// Returns an error if init has not been called.
pub async fn set_opt_in_status(enabled: bool) -> Result<()> {
    let mut svc = METRICS_SERVICE.lock().await;
    match &svc.init_state {
        MetricsServiceInitStatus::INITIALIZED => svc.inner_set_opt_in_status(enabled),
        MetricsServiceInitStatus::UNINITIALIZED => {
            log::error!("set_optin_status called on uninitialized METRICS_SERVICE");
            bail!(INIT_ERROR)
        }
    }
}

/// Returns current opt in status.
/// Returns an error if init has not been called.
pub async fn is_opted_in() -> bool {
    let svc = METRICS_SERVICE.lock().await;
    match &svc.init_state {
        MetricsServiceInitStatus::INITIALIZED => svc.inner_is_opted_in(),
        MetricsServiceInitStatus::UNINITIALIZED => {
            log::error!("is_opted_in called on uninitialized METRICS_SERVICE");
            false
        }
    }
}

// disable analytics for this invocation only
// this does not affect the global analytics state
pub async fn opt_out_for_this_invocation() -> Result<()> {
    let mut svc = METRICS_SERVICE.lock().await;
    match &svc.init_state {
        MetricsServiceInitStatus::INITIALIZED => svc.inner_opt_out_for_this_invocation(),
        MetricsServiceInitStatus::UNINITIALIZED => {
            log::error!("opt_out_for_this_incocation called on uninitialized METRICS_SERVICE");
            bail!(INIT_ERROR)
        }
    }
}

/// Records a launch event with the command line args used to launch app.
/// Returns an error if init has not been called.
pub async fn add_launch_event(args: Option<&str>) -> Result<()> {
    let svc = METRICS_SERVICE.lock().await;
    match &svc.init_state {
        MetricsServiceInitStatus::INITIALIZED => svc.inner_add_launch_event(args, None).await,
        MetricsServiceInitStatus::UNINITIALIZED => {
            log::error!("add_launch_event called on uninitialized METRICS_SERVICE");
            bail!(INIT_ERROR)
        }
    }
}

/// Records an error event in the app.
/// Returns an error if init has not been called.
pub async fn add_crash_event(description: &str, fatal: Option<&bool>) -> Result<()> {
    let svc = METRICS_SERVICE.lock().await;
    match &svc.init_state {
        MetricsServiceInitStatus::INITIALIZED => {
            svc.inner_add_crash_event(description, fatal, None).await
        }
        MetricsServiceInitStatus::UNINITIALIZED => {
            log::error!("add_crash_event called on uninitialized METRICS_SERVICE");
            bail!(INIT_ERROR)
        }
    }
}

/// Records a timing event from the app.
/// Returns an error if init has not been called.
pub async fn add_timing_event(
    category: Option<&str>,
    time: String,
    variable: Option<&str>,
    label: Option<&str>,
    custom_dimensions: BTreeMap<&str, String>,
) -> Result<()> {
    let svc = METRICS_SERVICE.lock().await;
    match &svc.init_state {
        MetricsServiceInitStatus::INITIALIZED => {
            svc.inner_add_timing_event(category, time, variable, label, custom_dimensions, None)
                .await
        }
        MetricsServiceInitStatus::UNINITIALIZED => {
            log::error!("add_timing_event called on uninitialized METRICS_SERVICE");
            bail!(INIT_ERROR)
        }
    }
}

/// Records an event with an option to specify every parameter.
/// Returns an error if init has not been called.
pub async fn add_custom_event(
    category: Option<&str>,
    action: Option<&str>,
    label: Option<&str>,
    custom_dimensions: BTreeMap<&str, String>,
) -> Result<()> {
    let svc = METRICS_SERVICE.lock().await;
    match &svc.init_state {
        MetricsServiceInitStatus::INITIALIZED => {
            svc.inner_add_custom_event(category, action, label, custom_dimensions, None).await
        }
        MetricsServiceInitStatus::UNINITIALIZED => {
            log::error!("add_custom_event called on uninitialized METRICS_SERVICE");
            bail!(INIT_ERROR)
        }
    }
}

pub async fn make_batch() -> Result<MetricsEventBatch> {
    let svc = METRICS_SERVICE.lock().await;
    match &svc.init_state {
        MetricsServiceInitStatus::INITIALIZED => Ok(MetricsEventBatch::new()),
        MetricsServiceInitStatus::UNINITIALIZED => {
            log::error!("make_batch called on uninitialized METRICS_SERVICE");
            bail!(INIT_ERROR)
        }
    }
}
