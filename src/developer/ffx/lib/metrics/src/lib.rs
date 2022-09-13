// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use analytics::{
    add_custom_event, init_with_invoker, make_batch, metrics_event_batch::MetricsEventBatch,
    uuid_as_str,
};
use anyhow::Result;
use ffx_config::EnvironmentContext;
use fidl_fuchsia_developer_ffx::VersionInfo;
use fuchsia_async::TimeoutExt;
use std::collections::BTreeMap;
use std::time::{Duration, Instant};
use tracing;

pub const GA_PROPERTY_ID: &str = "UA-127897021-9";

pub const FUCHSIA_DISCOVERY_LEGACY_ENV_VAR_NAME: &str = "FUCSHIA_DISABLED_legacy_discovery";

pub const ANALYTICS_LEGACY_DISCOVERY_CUSTOM_DIMENSION_KEY: &str = "cd4";

pub const ANALYTICS_CLIENTID_CUSTOM_DIMENSION_KEY: &str = "cd5";

pub async fn init_metrics_svc(build_info: VersionInfo, invoker: Option<String>) {
    let build_version = build_info.build_version;
    init_with_invoker(String::from("ffx"), build_version, GA_PROPERTY_ID.to_string(), invoker)
        .await;
}

fn legacy_discovery_env(context: &EnvironmentContext) -> String {
    let _one = "1".to_string();
    match context.env_var(FUCHSIA_DISCOVERY_LEGACY_ENV_VAR_NAME) {
        Ok(_one) => "true",
        _ => "false",
    }
    .to_string()
}

pub async fn add_ffx_launch_and_timing_events(
    context: &EnvironmentContext,
    sanitized_args: String,
    time: String,
) -> Result<()> {
    let mut batcher = make_batch().await?;
    let legacy_discovery_env = legacy_discovery_env(context);
    add_ffx_launch_event(&legacy_discovery_env, &sanitized_args, &mut batcher).await?;
    add_ffx_timing_event(&legacy_discovery_env, &sanitized_args, time, &mut batcher).await?;
    batcher.send_events().await
}

async fn add_ffx_launch_event(
    legacy_discovery_env: &str,
    sanitized_args: &str,
    batcher: &mut MetricsEventBatch,
) -> Result<()> {
    let mut custom_dimensions = BTreeMap::new();
    add_legacy_discovery_metrics(legacy_discovery_env, &mut custom_dimensions);
    add_client_id_as_custom_dimension(&mut custom_dimensions).await;
    batcher.add_custom_event(None, Some(&sanitized_args), None, custom_dimensions).await
}

fn add_legacy_discovery_metrics(
    legacy_discovery_env: &str,
    custom_dimensions: &mut BTreeMap<&str, String>,
) {
    custom_dimensions
        .insert(ANALYTICS_LEGACY_DISCOVERY_CUSTOM_DIMENSION_KEY, legacy_discovery_env.to_owned());
}

/// By adding clientId as a custom dimension, DataStudio dashboards will be able to
/// accurately count unique users of ffx subcommands.
async fn add_client_id_as_custom_dimension(custom_dimensions: &mut BTreeMap<&str, String>) {
    if let Ok(uuid) = uuid_as_str().await {
        custom_dimensions.insert(ANALYTICS_CLIENTID_CUSTOM_DIMENSION_KEY, uuid);
    }
}

async fn add_ffx_timing_event(
    legacy_discovery_env: &str,
    sanitized_args: &str,
    time: String,
    batcher: &mut MetricsEventBatch,
) -> Result<()> {
    let mut custom_dimensions = BTreeMap::new();
    add_legacy_discovery_metrics(legacy_discovery_env, &mut custom_dimensions);
    add_client_id_as_custom_dimension(&mut custom_dimensions).await;
    batcher.add_timing_event(Some(&sanitized_args), time, None, None, custom_dimensions).await
}

pub async fn add_daemon_metrics_event(request_str: &str) {
    let request = request_str.to_string();
    let analytics_start = Instant::now();
    let analytics_task = fuchsia_async::Task::local(async move {
        let custom_dimensions = BTreeMap::new();
        match add_custom_event(Some("ffx_daemon"), Some(&request), None, custom_dimensions).await {
            Err(e) => tracing::error!("metrics submission failed: {}", e),
            Ok(_) => tracing::debug!("metrics succeeded"),
        }
        Instant::now()
    });
    let analytics_done = analytics_task
        // TODO(66918): make configurable, and evaluate chosen time value.
        .on_timeout(Duration::from_secs(2), || {
            tracing::error!("metrics submission timed out");
            // Metrics timeouts should not impact user flows.
            Instant::now()
        })
        .await;
    tracing::info!("analytics time: {}", (analytics_done - analytics_start).as_secs_f32());
}

pub async fn add_daemon_launch_event() {
    add_daemon_metrics_event("start").await;
}

#[cfg(test)]
mod tests {
    use super::*;

    // #[test]
    #[fuchsia_async::run_singlethreaded(test)]
    async fn add_client_id_custom_dimension() {
        // let version_info =  VersionInfo {commit_hash: None,
        //     commit_timestamp: None, build_version: None,
        //     abi_revision: None, api_level: None,
        //     exec_path: None, build_id: None, unknown_data: None,
        //     __non_exhaustive: ()};
        let version_info = VersionInfo::EMPTY;
        init_metrics_svc(version_info, None).await;
        let mut custom_dimensions = BTreeMap::new();
        add_client_id_as_custom_dimension(&mut custom_dimensions).await;
        assert_eq!("No uuid", &custom_dimensions["cd5"]);
    }
}
