// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_trait::async_trait;
use fidl_fuchsia_hardware_light::{Info, LightMarker, LightProxy};

use crate::registry::device_storage::DeviceStorageCompatible;
use crate::registry::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::registry::setting_handler::{controller, ControllerError};
use crate::switchboard::base::{
    SettingRequest, SettingResponse, SettingResponseResult, SettingType, SwitchboardError,
};
use crate::switchboard::light_types::{LightGroup, LightInfo, LightState, LightType, LightValue};

impl DeviceStorageCompatible for LightInfo {
    fn default_value() -> Self {
        LightInfo { light_groups: Default::default() }
    }

    const KEY: &'static str = "light_info";
}

pub struct LightController {
    /// Provides access to common resources and functionality for controllers.
    client: ClientProxy<LightInfo>,

    /// Proxy for interacting with light hardware.
    light_proxy: LightProxy,
}

#[async_trait]
impl data_controller::Create<LightInfo> for LightController {
    async fn create(client: ClientProxy<LightInfo>) -> Result<Self, ControllerError> {
        let light_proxy = client
            .get_service_context()
            .await
            .lock()
            .await
            .connect_device_path::<LightMarker>("/dev/class/light/*")
            .await
            .or_else(|e| {
                Err(ControllerError::InitFailure {
                    description: format!(
                        "failed to connect to fuchsia.hardware.light with error: {:?}",
                        e
                    ),
                })
            })?;

        Ok(LightController { client, light_proxy })
    }
}

#[async_trait]
impl controller::Handle for LightController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingResponseResult> {
        match request {
            SettingRequest::Restore => Some(self.restore().await),
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

    async fn restore(&self) -> SettingResponseResult {
        // Read light info from hardware.
        let mut current = self.client.read().await;
        let num_lights = self.light_proxy.get_num_lights().await.or_else(|_| {
            Err(SwitchboardError::ExternalFailure {
                setting_type: SettingType::Light,
                dependency: "fuchsia.hardware.light".to_string(),
                request: "get_num_lights".to_string(),
            })
        })?;

        for i in 0..num_lights {
            let info = match self.light_proxy.get_info(i).await {
                Ok(Ok(info)) => info,
                _ => {
                    return Err(SwitchboardError::ExternalFailure {
                        setting_type: SettingType::Light,
                        dependency: "fuchsia.hardware.light".to_string(),
                        request: format!("get_info for light {}", i),
                    });
                }
            };
            let (name, group) = self.light_info_to_group(i, info).await.or_else(|e| Err(e))?;
            current.light_groups.insert(name, group);
        }

        write(&self.client, current, false).await.into_response_result()
    }

    /// Convert an Info object from fuchsia.hardware.Light into a LightGroup, the internal
    /// representation used for our service.
    async fn light_info_to_group(
        &self,
        index: u32,
        info: Info,
    ) -> Result<(String, LightGroup), SwitchboardError> {
        let light_type = info.capability.into();

        // Read the proper value depending on the light type.
        let value = match light_type {
            LightType::Brightness => LightValue::Brightness(
                match self.light_proxy.get_current_brightness_value(index).await {
                    Ok(Ok(brightness)) => brightness,
                    _ => {
                        return Err(SwitchboardError::ExternalFailure {
                            setting_type: SettingType::Light,
                            dependency: "fuchsia.hardware.light".to_string(),
                            request: format!("get_current_brightness_value for light {}", index),
                        });
                    }
                },
            ),
            LightType::Rgb => match self.light_proxy.get_current_rgb_value(index).await {
                Ok(Ok(rgb)) => rgb.into(),
                _ => {
                    return Err(SwitchboardError::ExternalFailure {
                        setting_type: SettingType::Light,
                        dependency: "fuchsia.hardware.light".to_string(),
                        request: format!("get_current_rgb_value for light {}", index),
                    });
                }
            },
            LightType::Simple => {
                LightValue::Simple(match self.light_proxy.get_current_simple_value(index).await {
                    Ok(Ok(on)) => on,
                    _ => {
                        return Err(SwitchboardError::ExternalFailure {
                            setting_type: SettingType::Light,
                            dependency: "fuchsia.hardware.light".to_string(),
                            request: format!("get_current_simple_value for light {}", index),
                        });
                    }
                })
            }
        };

        Ok((
            info.name.clone(),
            LightGroup {
                name: Some(info.name),
                enabled: Some(true),
                light_type: Some(light_type),
                lights: Some(vec![LightState { value: Some(value) }]),
            },
        ))
    }
}
