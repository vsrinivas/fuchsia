// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_trait::async_trait;

use crate::registry::device_storage::DeviceStorageCompatible;
use crate::registry::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::registry::setting_handler::{controller, ControllerError};
use crate::switchboard::base::{SettingRequest, SettingResponse, SettingResponseResult};
use crate::switchboard::light_types::{LightGroup, LightInfo, LightState};

impl DeviceStorageCompatible for LightInfo {
    fn default_value() -> Self {
        LightInfo { light_groups: Default::default() }
    }

    const KEY: &'static str = "light_info";
}

pub struct LightController {
    client: ClientProxy<LightInfo>,
}

#[async_trait]
impl data_controller::Create<LightInfo> for LightController {
    async fn create(client: ClientProxy<LightInfo>) -> Result<Self, ControllerError> {
        Ok(LightController { client })
    }
}

#[async_trait]
impl controller::Handle for LightController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingResponseResult> {
        match request {
            SettingRequest::SetLightGroupValue(name, state) => Some(self.set(name, state).await),
            SettingRequest::Get => Some(Ok(Some(SettingResponse::Light(self.client.read().await)))),
            _ => None,
        }
    }
}

/// Controller for processing switchboard messages surrounding the Light
/// protocol.
impl LightController {
    async fn set(&self, name: String, state: Vec<LightState>) -> SettingResponseResult {
        let mut current = self.client.read().await;

        // TODO(fxb/53625): right now we automatically record any sets, need to connect to
        // fuchsia.hardware.light so we only have real lights stored and can ignore sets to invalid
        // lights.
        current
            .light_groups
            .entry(name.clone())
            .or_insert(LightGroup {
                name: Some(name),
                enabled: None,
                light_type: None,
                lights: None,
            })
            .lights = Some(state);

        write(&self.client, current, false).await.into_response_result()
    }
}
