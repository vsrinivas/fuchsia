// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_trait::async_trait;
use fidl_fuchsia_hardware_light::{Info, LightMarker, LightProxy};

use crate::base::SettingInfo;
use crate::config::default_settings::DefaultSetting;
use crate::handler::base::SettingHandlerResult;
use crate::handler::device_storage::DeviceStorageCompatible;
use crate::handler::setting_handler::persist::{
    controller as data_controller, write, ClientProxy, WriteResult,
};
use crate::handler::setting_handler::{controller, ControllerError};
use crate::input::ButtonType;
use crate::light::light_hardware_configuration::DisableConditions;
use crate::service_context::ExternalServiceProxy;
use crate::switchboard::base::{
    ControllerStateResult, SettingRequest, SettingResponse, SettingType,
};
use crate::switchboard::light_types::{LightGroup, LightInfo, LightState, LightType, LightValue};
use crate::{call_async, LightHardwareConfiguration};
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::convert::TryInto;

/// Used as the argument field in a ControllerError::InvalidArgument to signal the FIDL handler to
/// signal that a LightError::INVALID_NAME should be returned to the client.
pub const ARG_NAME: &'static str = "name";

/// Hardware path used to connect to light devices.
pub const DEVICE_PATH: &'static str = "/dev/class/light/*";

impl DeviceStorageCompatible for LightInfo {
    fn default_value() -> Self {
        LightInfo { light_groups: Default::default() }
    }

    const KEY: &'static str = "light_info";
}

impl Into<SettingInfo> for LightInfo {
    fn into(self) -> SettingInfo {
        SettingInfo::Light(self)
    }
}

pub struct LightController {
    /// Provides access to common resources and functionality for controllers.
    client: ClientProxy<LightInfo>,

    /// Proxy for interacting with light hardware.
    light_proxy: ExternalServiceProxy<LightProxy>,

    /// Hardware configuration that determines what lights to return to the client.
    ///
    /// If present, overrides the lights from the underlying fuchsia.hardware.light API.
    light_hardware_config: Option<LightHardwareConfiguration>,
}

#[async_trait]
impl data_controller::Create<LightInfo> for LightController {
    async fn create(client: ClientProxy<LightInfo>) -> Result<Self, ControllerError> {
        let light_hardware_config = DefaultSetting::<LightHardwareConfiguration, &str>::new(
            None,
            "/config/data/light_hardware_config.json",
        )
        .get_default_value();

        LightController::create_with_config(client, light_hardware_config).await
    }
}

#[async_trait]
impl controller::Handle for LightController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
        match request {
            SettingRequest::Restore => Some(self.restore().await),
            SettingRequest::OnButton(ButtonType::MicrophoneMute(state)) => {
                Some(self.on_mic_mute(state).await)
            }
            SettingRequest::SetLightGroupValue(name, state) => Some(self.set(name, state).await),
            SettingRequest::Get => {
                // Read all light values from underlying fuchsia.hardware.light before returning a
                // value to ensure we have the latest light state.
                // TODO(fxbug.dev/56319): remove once all clients are migrated.
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
    /// Alternate constructor that allows specifying a configuration.
    pub(crate) async fn create_with_config(
        client: ClientProxy<LightInfo>,
        light_hardware_config: Option<LightHardwareConfiguration>,
    ) -> Result<Self, ControllerError> {
        let light_proxy = client
            .get_service_context()
            .await
            .lock()
            .await
            .connect_device_path::<LightMarker>(DEVICE_PATH)
            .await
            .or_else(|e| {
                Err(ControllerError::InitFailure(
                    format!("failed to connect to fuchsia.hardware.light with error: {:?}", e)
                        .into(),
                ))
            })?;

        Ok(LightController { client, light_proxy, light_hardware_config })
    }

    async fn set(&self, name: String, state: Vec<LightState>) -> SettingHandlerResult {
        let mut current = self.client.read().await;

        let mut entry = match current.light_groups.entry(name.clone()) {
            Entry::Vacant(_) => {
                // Reject sets if the light name is not known.
                return Err(ControllerError::InvalidArgument(
                    SettingType::Light,
                    ARG_NAME.into(),
                    name.into(),
                ));
            }
            Entry::Occupied(entry) => entry,
        };

        let group = entry.get_mut();

        if state.len() != group.lights.len() {
            // If the number of light states provided doesn't match the number of lights,
            // return an error.
            return Err(ControllerError::InvalidArgument(
                SettingType::Light,
                "state".into(),
                format!("{:?}", state).into(),
            ));
        }

        if !state.iter().filter_map(|state| state.value.clone()).all(|value| {
            match group.light_type {
                LightType::Brightness => matches!(value, LightValue::Brightness(_)),
                LightType::Rgb => matches!(value, LightValue::Rgb(_)),
                LightType::Simple => matches!(value, LightValue::Simple(_)),
            }
        }) {
            // If not all the light values match the light type of this light group, return an
            // error.
            return Err(ControllerError::InvalidArgument(
                SettingType::Light,
                "state".into(),
                format!("{:?}", state).into(),
            ));
        }

        // After the main validations, write the state to the hardware.
        self.write_light_group_to_hardware(group, &state).await?;

        write(&self.client, current, false).await.into_handler_result()
    }

    /// Writes the given list of light states for a light group to the actual hardware.
    ///
    /// None elements in the vector are ignored and not written to the hardware.
    async fn write_light_group_to_hardware(
        &self,
        group: &mut LightGroup,
        state: &Vec<LightState>,
    ) -> ControllerStateResult {
        for (i, (light, hardware_index)) in
            state.iter().zip(group.hardware_index.iter()).enumerate()
        {
            let (set_result, method_name) = match light.clone().value {
                // No value provided for this index, just skip it and don't update the
                // stored value.
                None => continue,
                Some(LightValue::Brightness(brightness)) => (
                    call_async!(self.light_proxy =>
                        set_brightness_value(*hardware_index, brightness))
                    .await,
                    "set_brightness_value",
                ),
                Some(LightValue::Rgb(rgb)) => {
                    let mut value = rgb.clone().try_into().or_else(|_| {
                        Err(ControllerError::InvalidArgument(
                            SettingType::Light,
                            "value".into(),
                            format!("{:?}", rgb).into(),
                        ))
                    })?;
                    (
                        call_async!(self.light_proxy =>
                            set_rgb_value(*hardware_index, &mut value))
                        .await,
                        "set_rgb_value",
                    )
                }
                Some(LightValue::Simple(on)) => (
                    call_async!(self.light_proxy => set_simple_value(*hardware_index, on)).await,
                    "set_simple_value",
                ),
            };
            set_result.map(|_| ()).or_else(|_| {
                Err(ControllerError::ExternalFailure(
                    SettingType::Light,
                    "fuchsia.hardware.light".into(),
                    format!("{} for light {}", method_name, hardware_index).into(),
                ))
            })?;

            // Set was successful, save this light value.
            group.lights[i] = light.clone();
        }
        Ok(())
    }

    async fn on_mic_mute(&self, mic_mute: bool) -> SettingHandlerResult {
        let mut current = self.client.read().await;

        for light in current
            .light_groups
            .values_mut()
            .filter(|l| l.disable_conditions.contains(&DisableConditions::MicSwitch))
        {
            // This condition means that the LED is hard-wired to the mute switch and will only be
            // on when the mic is disabled.
            light.enabled = mic_mute;
        }

        write(&self.client, current, false).await.into_handler_result()
    }

    async fn restore(&self) -> SettingHandlerResult {
        if let Some(config) = self.light_hardware_config.clone() {
            // Configuration is specified, restore from the configuration.
            self.restore_from_configuration(config).await
        } else {
            // Read light info from hardware.
            self.restore_from_hardware().await
        }
    }

    /// Restores the light information from a pre-defined hardware configuration. Individual light
    /// states are read from the underlying fuchsia.hardware.Light API, but the structure of the
    /// light groups is determined by the given `config`.
    async fn restore_from_configuration(
        &self,
        config: LightHardwareConfiguration,
    ) -> SettingHandlerResult {
        let current = self.client.read().await;
        let mut light_groups: HashMap<String, LightGroup> = HashMap::new();
        for group_config in config.light_groups {
            let mut light_state: Vec<LightState> = Vec::new();

            // TODO(fxbug.dev/62591): once all clients go through setui, restore state from hardware
            // only if not found in persistent storage.
            for light_index in group_config.hardware_index.iter() {
                light_state.push(
                    self.light_state_from_hardware_index(*light_index, group_config.light_type)
                        .await
                        .or_else(|e| Err(e))?,
                );
            }

            // Restore previous state.
            let enabled = current
                .light_groups
                .get(&group_config.name)
                .map(|found_group| found_group.enabled)
                .unwrap_or(true);

            light_groups.insert(
                group_config.name.clone(),
                LightGroup {
                    name: group_config.name,
                    enabled,
                    light_type: group_config.light_type,
                    lights: light_state,
                    hardware_index: group_config.hardware_index,
                    disable_conditions: group_config.disable_conditions,
                },
            );
        }

        write(&self.client, LightInfo { light_groups }, false).await.into_handler_result()
    }

    /// Restores the light information when no hardware configuration is specified by reading from
    /// the underlying fuchsia.hardware.Light API and turning each light into a [`LightGroup`].
    ///
    /// [`LightGroup`]: ../../switchboard/light_types/struct.LightGroup.html
    async fn restore_from_hardware(&self) -> SettingHandlerResult {
        let num_lights =
            self.light_proxy.call_async(LightProxy::get_num_lights).await.or_else(|_| {
                Err(ControllerError::ExternalFailure(
                    SettingType::Light,
                    "fuchsia.hardware.light".into(),
                    "get_num_lights".into(),
                ))
            })?;

        let mut current = self.client.read().await;
        for i in 0..num_lights {
            let info = match call_async!(self.light_proxy => get_info(i)).await {
                Ok(Ok(info)) => info,
                _ => {
                    return Err(ControllerError::ExternalFailure(
                        SettingType::Light,
                        "fuchsia.hardware.light".into(),
                        format!("get_info for light {}", i).into(),
                    ));
                }
            };
            let (name, group) = self.light_info_to_group(i, info).await.or_else(|e| Err(e))?;
            current.light_groups.insert(name, group);
        }

        write(&self.client, current, false).await.into_handler_result()
    }

    /// Converts an Info object from the fuchsia.hardware.Light API into a LightGroup, the internal
    /// representation used for our service.
    async fn light_info_to_group(
        &self,
        index: u32,
        info: Info,
    ) -> Result<(String, LightGroup), ControllerError> {
        let light_type: LightType = info.capability.into();

        let light_state = self.light_state_from_hardware_index(index, light_type).await?;

        Ok((
            info.name.clone(),
            LightGroup {
                name: info.name,
                // When there's no config, lights are assumed to be always enabled.
                enabled: true,
                light_type,
                lights: vec![light_state],
                hardware_index: vec![index],
                disable_conditions: vec![],
            },
        ))
    }

    /// Reads light state from the underlying fuchsia.hardware.Light API for the given hardware
    /// index and light type.
    async fn light_state_from_hardware_index(
        &self,
        index: u32,
        light_type: LightType,
    ) -> Result<LightState, ControllerError> {
        // Read the proper value depending on the light type.
        let value = match light_type {
            LightType::Brightness => LightValue::Brightness(
                match call_async!(self.light_proxy => get_current_brightness_value(index)).await {
                    Ok(Ok(brightness)) => brightness,
                    _ => {
                        return Err(ControllerError::ExternalFailure(
                            SettingType::Light,
                            "fuchsia.hardware.light".into(),
                            format!("get_current_brightness_value for light {}", index).into(),
                        ));
                    }
                },
            ),
            LightType::Rgb => {
                match call_async!(self.light_proxy => get_current_rgb_value(index)).await {
                    Ok(Ok(rgb)) => rgb.into(),
                    _ => {
                        return Err(ControllerError::ExternalFailure(
                            SettingType::Light,
                            "fuchsia.hardware.light".into(),
                            format!("get_current_rgb_value for light {}", index).into(),
                        ));
                    }
                }
            }
            LightType::Simple => LightValue::Simple(
                match call_async!(self.light_proxy => get_current_simple_value(index)).await {
                    Ok(Ok(on)) => on,
                    _ => {
                        return Err(ControllerError::ExternalFailure(
                            SettingType::Light,
                            "fuchsia.hardware.light".into(),
                            format!("get_current_simple_value for light {}", index).into(),
                        ));
                    }
                },
            ),
        };

        Ok(LightState { value: Some(value) })
    }
}
