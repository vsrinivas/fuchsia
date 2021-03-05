// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::command_runner::SYSTEM_COMMAND_RUNNER, analytics::add_custom_event, anyhow::Result,
    std::collections::BTreeMap,
};

#[allow(unused_imports)]
use super::check::femu_graphics::{linux_find_graphics_cards, macos_find_graphics_cards};

// String constants for report_preflight_analytics().
pub static ANALYTICS_ACTION_SUCCESS: &str = "completed_success";
pub static ANALYTICS_ACTION_WARNING: &str = "completed_warning";
pub static ANALYTICS_ACTION_FAILURE_RECOVERABLE: &str = "completed_failure_recoverable";
pub static ANALYTICS_ACTION_FAILURE: &str = "completed_failure";

static ANALYTICS_CATEGORY: &str = "preflight";
static ANALYTICS_CUSTOM_DIMENSION_2_KEY: &str = "cd3";

#[cfg(target_os = "linux")]
fn get_graphics_cards() -> Result<Vec<String>> {
    linux_find_graphics_cards(&SYSTEM_COMMAND_RUNNER)
}

#[cfg(target_os = "macos")]
fn get_graphics_cards() -> Result<Vec<String>> {
    macos_find_graphics_cards(&SYSTEM_COMMAND_RUNNER)
}

pub async fn report_preflight_analytics(action: &str) {
    let mut custom_dimensions = BTreeMap::new();
    if let Ok(cards) = get_graphics_cards() {
        custom_dimensions.insert(ANALYTICS_CUSTOM_DIMENSION_2_KEY, cards.join(","));
    }

    if let Err(e) =
        add_custom_event(Some(ANALYTICS_CATEGORY), Some(action), None, custom_dimensions).await
    {
        log::error!("Preflight analytics submission failed: {}", e);
    }
}
