// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{Merge, SettingInfo, SettingType};
use crate::call_async;
use crate::handler::base::Request;
use crate::handler::device_storage::{DeviceStorageAccess, DeviceStorageCompatible};
use crate::handler::setting_handler::persist::{controller as data_controller, ClientProxy};
use crate::handler::setting_handler::{
    controller, ControllerError, IntoHandlerResult, SettingHandlerResult,
};
use crate::intl::types::{HourCycle, IntlInfo, LocaleId, TemperatureUnit};
use async_trait::async_trait;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use rust_icu_uenum as uenum;
use std::collections::HashSet;
use std::sync::Arc;

impl DeviceStorageCompatible for IntlInfo {
    const KEY: &'static str = "intl_info";

    fn default_value() -> Self {
        IntlInfo {
            // `-x-fxdef` is a private use extension and a special marker denoting that the
            // setting is a fallback default, and not actually set through any user action.
            locales: Some(vec![LocaleId { id: "en-US-x-fxdef".to_string() }]),
            temperature_unit: Some(TemperatureUnit::Celsius),
            time_zone_id: Some("UTC".to_string()),
            hour_cycle: Some(HourCycle::H12),
        }
    }
}

impl From<IntlInfo> for SettingInfo {
    fn from(info: IntlInfo) -> SettingInfo {
        SettingInfo::Intl(info)
    }
}

pub struct IntlController {
    client: ClientProxy,
    time_zone_ids: std::collections::HashSet<String>,
}

impl DeviceStorageAccess for IntlController {
    const STORAGE_KEYS: &'static [&'static str] = &[IntlInfo::KEY];
}

#[async_trait]
impl data_controller::Create for IntlController {
    /// Creates the controller
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        let time_zone_ids = IntlController::load_time_zones();
        Ok(IntlController { client, time_zone_ids })
    }
}

#[async_trait]
impl controller::Handle for IntlController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::SetIntlInfo(info) => Some(self.set(info).await),
            Request::Get => Some(
                self.client
                    .read_setting_info::<IntlInfo>(fuchsia_trace::generate_nonce())
                    .await
                    .into_handler_result(),
            ),
            _ => None,
        }
    }
}

/// Controller for processing requests surrounding the Intl protocol, backed by a number of
/// services, including TimeZone.
impl IntlController {
    /// Loads the set of valid time zones from resources.
    fn load_time_zones() -> std::collections::HashSet<String> {
        let _icu_data_loader = icu_data::Loader::new().expect("icu data loaded");
        let time_zone_list = match uenum::open_time_zones() {
            Ok(time_zones) => time_zones,
            Err(err) => {
                fx_log_err!("Unable to load time zones: {:?}", err);
                return HashSet::new();
            }
        };

        time_zone_list.flatten().collect()
    }

    async fn set(&self, info: IntlInfo) -> SettingHandlerResult {
        self.validate_intl_info(info.clone())?;

        self.write_intl_info_to_service(info.clone()).await;

        let nonce = fuchsia_trace::generate_nonce();
        let current = self.client.read_setting::<IntlInfo>(nonce).await;
        self.client
            .write_setting(current.merge(info).into(), false, nonce)
            .await
            .into_handler_result()
    }

    /// Checks if the given IntlInfo is valid.
    fn validate_intl_info(&self, info: IntlInfo) -> Result<(), ControllerError> {
        if let Some(time_zone_id) = info.time_zone_id {
            // Make sure the given time zone ID is valid.
            if !self.time_zone_ids.contains(time_zone_id.as_str()) {
                return Err(ControllerError::InvalidArgument(
                    SettingType::Intl,
                    "timezone id".into(),
                    time_zone_id.into(),
                ));
            }
        }

        Ok(())
    }

    /// Writes the time zone setting to the timezone service.
    ///
    /// Errors are only logged as this is an intermediate step in a migration.
    /// TODO(fxbug.dev/41639): remove this
    async fn write_intl_info_to_service(&self, info: IntlInfo) {
        let service_context = Arc::clone(&self.client.get_service_context());
        fasync::Task::spawn(async move {
            let service_result =
                service_context.connect::<fidl_fuchsia_deprecatedtimezone::TimezoneMarker>().await;

            let proxy = match service_result {
                Ok(proxy) => proxy,
                Err(_) => {
                    fx_log_err!("Failed to connect to fuchsia.timezone");
                    return;
                }
            };

            let time_zone_id = match info.time_zone_id {
                Some(id) => id,
                None => return,
            };

            if let Err(e) = call_async!(proxy => set_timezone(time_zone_id.as_str())).await {
                fx_log_err!("Failed to write timezone to fuchsia.timezone: {:?}", e);
            }
        })
        .detach();
    }
}
