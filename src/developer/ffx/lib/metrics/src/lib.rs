// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use analytics::{add_custom_event, init, make_batch, metrics_event_batch::MetricsEventBatch};
use anyhow::Result;
use fidl_fuchsia_developer_bridge::VersionInfo;
use fuchsia_async::TimeoutExt;
use log;
use std::collections::BTreeMap;
use std::time::{Duration, Instant};

pub const GA_PROPERTY_ID: &str = "UA-127897021-9";

pub const FUCHSIA_DISCOVERY_LEGACY_ENV_VAR_NAME: &str = "FUCSHIA_DISABLED_legacy_discovery";

pub const ANALYTICS_LEGACY_DISCOVERY_CUSTOM_DIMENSION_KEY: &str = "cd4";

pub async fn init_metrics_svc(build_info: VersionInfo) {
    let build_version = build_info.build_version;
    init(String::from("ffx"), build_version, GA_PROPERTY_ID.to_string()).await;
}

fn legacy_discovery_env() -> String {
    let _one = "1".to_string();
    match std::env::var(FUCHSIA_DISCOVERY_LEGACY_ENV_VAR_NAME) {
        Ok(_one) => "true",
        _ => "false",
    }
    .to_string()
}

pub async fn add_ffx_launch_and_timing_events(sanitized_args: String, time: String) -> Result<()> {
    let mut batcher = make_batch().await?;
    add_ffx_launch_event(&sanitized_args, &mut batcher).await?;
    add_ffx_timing_event(&sanitized_args, time, &mut batcher).await?;
    batcher.send_events().await
}

async fn add_ffx_launch_event(
    sanitized_args: &String,
    batcher: &mut MetricsEventBatch,
) -> Result<()> {
    let mut custom_dimensions = BTreeMap::new();
    add_legacy_discovery_metrics(&mut custom_dimensions);
    batcher.add_custom_event(None, Some(&sanitized_args), None, custom_dimensions).await
}

fn add_legacy_discovery_metrics(custom_dimensions: &mut BTreeMap<&str, String>) {
    custom_dimensions
        .insert(ANALYTICS_LEGACY_DISCOVERY_CUSTOM_DIMENSION_KEY, legacy_discovery_env());
}

async fn add_ffx_timing_event(
    sanitized_args: &String,
    time: String,
    batcher: &mut MetricsEventBatch,
) -> Result<()> {
    let mut custom_dimensions = BTreeMap::new();
    add_legacy_discovery_metrics(&mut custom_dimensions);
    batcher.add_timing_event(Some(&sanitized_args), time, None, None, custom_dimensions).await
}

pub async fn add_daemon_metrics_event(request_str: &str) {
    let request = request_str.to_string();
    let analytics_start = Instant::now();
    let analytics_task = fuchsia_async::Task::local(async move {
        let custom_dimensions = BTreeMap::new();
        match add_custom_event(Some("ffx_daemon"), Some(&request), None, custom_dimensions).await {
            Err(e) => log::error!("metrics submission failed: {}", e),
            Ok(_) => log::debug!("metrics succeeded"),
        }
        Instant::now()
    });
    let analytics_done = analytics_task
        // TODO(66918): make configurable, and evaluate chosen time value.
        .on_timeout(Duration::from_secs(2), || {
            log::error!("metrics submission timed out");
            // Metrics timeouts should not impact user flows.
            Instant::now()
        })
        .await;
    log::info!("analytics time: {}", (analytics_done - analytics_start).as_secs_f32());
}

pub async fn add_daemon_launch_event() {
    add_daemon_metrics_event("start").await;
}
