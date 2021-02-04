// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {analytics::add_custom_event, ffx_core::build_info};

// String constants for report_preflight_analytics().
pub static ANALYTICS_ACTION_SUCCESS: &str = "completed_success";
pub static ANALYTICS_ACTION_WARNING: &str = "completed_warning";
pub static ANALYTICS_ACTION_FAILURE_RECOVERABLE: &str = "completed_failure_recoverable";
pub static ANALYTICS_ACTION_FAILURE: &str = "completed_failure";

static ANALYTICS_APP_NAME: &str = "ffx";
static ANALYTICS_CATEGORY: &str = "preflight";

pub async fn report_preflight_analytics(action: &str) {
    let build_info = build_info();
    let build_version = build_info.build_version;
    if let Err(e) = add_custom_event(
        ANALYTICS_APP_NAME,
        build_version.as_deref(),
        Some(ANALYTICS_CATEGORY),
        Some(action),
        None,
    )
    .await
    {
        log::error!("Preflight analytics submission failed: {}", e);
    }
}
