// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::build_info;
use analytics::{init, make_batch, metrics_event_batch::MetricsEventBatch};
use anyhow::Result;
use std::collections::BTreeMap;

pub const GA_PROPERTY_ID: &str = "UA-127897021-9";

pub const FUCHSIA_DISCOVERY_LEGACY_ENV_VAR_NAME: &str = "FUCSHIA_DISABLED_legacy_discovery";

pub const ANALYTICS_LEGACY_DISCOVERY_CUSTOM_DIMENSION_KEY: &str = "cd4";

pub async fn init_metrics_svc() {
    let build_info = build_info();
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
