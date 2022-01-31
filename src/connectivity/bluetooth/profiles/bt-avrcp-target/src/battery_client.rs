// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use battery_client::{BatteryClient, BatteryInfo, BatteryLevel};
use fidl_fuchsia_bluetooth_avrcp as fidl_avrcp;
use futures::StreamExt;
use std::sync::Arc;
use tracing::{info, trace};

use crate::media::media_sessions::MediaSessions;

/// Returns an AVRCP-specific battery status from the provided BatteryClient `info`.
fn to_avrcp_battery_status(info: BatteryInfo) -> fidl_avrcp::BatteryStatus {
    // By default, if the local status is unavailable, we default to reporting it as external since
    // there is no equivalent AVRCP variant.
    match info {
        BatteryInfo::NotAvailable | BatteryInfo::External => fidl_avrcp::BatteryStatus::External,
        BatteryInfo::Battery(BatteryLevel::Normal(_)) => fidl_avrcp::BatteryStatus::Normal,
        BatteryInfo::Battery(BatteryLevel::Warning(_)) => fidl_avrcp::BatteryStatus::Warning,
        BatteryInfo::Battery(BatteryLevel::Critical(_)) => fidl_avrcp::BatteryStatus::Critical,
        BatteryInfo::Battery(BatteryLevel::FullCharge) => fidl_avrcp::BatteryStatus::FullCharge,
    }
}

async fn handle_battery_client_updates(
    mut battery_client: BatteryClient,
    media_sessions: Arc<MediaSessions>,
) {
    while let Some(update) = battery_client.next().await {
        match update.map(to_avrcp_battery_status) {
            Ok(status) => {
                media_sessions.update_battery_status(status);
            }
            Err(e) => {
                info!("Received error battery update: {:?}. Ignoring.", e);
            }
        }
    }
    trace!("BatteryClient stream terminated");
}

/// Connects to the power integration service and propagates battery updates to the provided
/// `media_sessions` shared state.
pub(crate) async fn process_battery_client_requests(media_sessions: Arc<MediaSessions>) {
    let battery_client = match BatteryClient::create() {
        Err(e) => {
            info!("Power integration unavailable: {:?}", e);
            return;
        }
        Ok(batt) => batt,
    };

    handle_battery_client_updates(battery_client, media_sessions).await;
}
