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
use std::collections::hash_map::Entry;

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
            SettingRequest::Get => {
                // Read all light values from underlying fuchsia.hardware.light before returning a
                // value to ensure we have the latest light state.
                // TODO(fxb/56319): remove once all clients are migrated.
                self.restore().await.ok();
                Some(Ok(Some(SettingResponse::Light(self.client.read().await))))
            }
            _ => None,
        }
    }
}

/// Controller for processing switchboard messages surrounding the Light
/// protocol.
impl LightController {
    async fn set(&self, name: String, state: Vec<LightState>) -> SettingResponseResult {
        let mut current = self.client.read().await;

        // TODO(fxb/55713): validate incoming values, not just name.
        let mut entry = match current.light_groups.entry(name.clone()) {
            Entry::Vacant(_) => {
                // Reject sets if the light name is not known.
                return Err(SwitchboardError::InvalidArgument {
                    setting_type: SettingType::Light,
                    argument: "name".to_string(),
                    value: name,
                });
            }
            Entry::Occupied(entry) => entry,
        };

        let group = entry.get_mut();

        if state.len() != group.lights.len() {
            // If the number of light states provided doesn't match the number of lights,
            // return an error.
            return Err(SwitchboardError::InvalidArgument {
                setting_type: SettingType::Light,
                argument: "state".to_string(),
                value: format!("{:?}", state),
            });
        }

        for (i, (light, hardware_index)) in
            state.iter().zip(group.hardware_index.iter()).enumerate()
        {
            let (set_result, method_name) = match light.clone().value {
                // No value provided for this index, just skip it and don't update the
                // stored value.
                None => continue,
                Some(LightValue::Brightness(brightness)) => (
                    self.light_proxy.set_brightness_value(*hardware_index, brightness),
                    "set_brightness_value",
                ),
                Some(LightValue::Rgb(rgb)) => (
                    self.light_proxy.set_rgb_value(*hardware_index, &mut rgb.into()),
                    "set_rgb_value",
                ),
                Some(LightValue::Simple(on)) => {
                    (self.light_proxy.set_simple_value(*hardware_index, on), "set_simple_value")
                }
            };
            set_result.await.map(|_| ()).or_else(|_| {
                Err(SwitchboardError::ExternalFailure {
                    setting_type: SettingType::Light,
                    dependency: "fuchsia.hardware.light".to_string(),
                    request: format!("{} for light {}", method_name, hardware_index),
                })
            })?;

            // Set was successful, save this light value.
            group.lights[i] = light.clone();
        }

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
                lights: vec![LightState { value: Some(value) }],
                hardware_index: vec![index],
            },
        ))
    }
}
