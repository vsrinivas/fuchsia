// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::config::default_settings::DefaultSetting;
use crate::handler::base::Request;
use crate::handler::setting_handler::persist::{controller as data_controller, ClientProxy};
use crate::handler::setting_handler::{
    controller, ControllerError, ControllerStateResult, SettingHandlerResult,
};
use crate::input::MediaButtons;
use crate::light::light_hardware_configuration::DisableConditions;
use crate::light::types::{LightGroup, LightInfo, LightState, LightType, LightValue};
use crate::service_context::ExternalServiceProxy;
use crate::{call_async, LightHardwareConfiguration};
use async_trait::async_trait;
use fidl_fuchsia_hardware_light::{Info, LightMarker, LightProxy};
use fidl_fuchsia_settings_storage::LightGroups;
use futures::lock::Mutex;
use settings_storage::fidl_storage::{FidlStorage, FidlStorageConvertible};
use settings_storage::storage_factory::StorageAccess;
use std::collections::hash_map::Entry;
use std::collections::HashMap;
use std::convert::TryInto;
use std::sync::Arc;

/// Used as the argument field in a ControllerError::InvalidArgument to signal the FIDL handler to
/// signal that a LightError::INVALID_NAME should be returned to the client.
pub(crate) const ARG_NAME: &str = "name";

/// Hardware path used to connect to light devices.
pub(crate) const DEVICE_PATH: &str = "/dev/class/light/*";

impl FidlStorageConvertible for LightInfo {
    type Storable = LightGroups;
    const KEY: &'static str = "light_info";

    fn default_value() -> Self {
        LightInfo { light_groups: Default::default() }
    }

    fn to_storable(self) -> Self::Storable {
        LightGroups {
            groups: self
                .light_groups
                .into_iter()
                .map(|(_, group)| fidl_fuchsia_settings::LightGroup::from(group))
                .collect(),
        }
    }

    fn from_storable(storable: Self::Storable) -> Self {
        // Unwrap ok since validation would ensure non-None name before writing to storage.
        let light_groups = storable
            .groups
            .into_iter()
            .map(|group| (group.name.clone().unwrap(), group.into()))
            .collect();
        Self { light_groups }
    }
}

impl From<LightInfo> for SettingInfo {
    fn from(info: LightInfo) -> SettingInfo {
        SettingInfo::Light(info)
    }
}

pub struct LightController {
    /// Provides access to common resources and functionality for controllers.
    client: ClientProxy,

    /// Proxy for interacting with light hardware.
    light_proxy: ExternalServiceProxy<LightProxy>,

    /// Hardware configuration that determines what lights to return to the client.
    ///
    /// If present, overrides the lights from the underlying fuchsia.hardware.light API.
    light_hardware_config: Option<LightHardwareConfiguration>,

    /// Cache of data that includes hardware values. The data stored on disk does not persist the
    /// hardware values, so restoring does not bring the values back into memory. The data needs to
    /// be cached at this layer so we don't lose track of them.
    data_cache: Arc<Mutex<Option<LightInfo>>>,
}

impl StorageAccess for LightController {
    type Storage = FidlStorage;
    const STORAGE_KEYS: &'static [&'static str] = &[LightInfo::KEY];
}

#[async_trait]
impl data_controller::Create for LightController {
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        let light_hardware_config = DefaultSetting::<LightHardwareConfiguration, &str>::new(
            None,
            "/config/data/light_hardware_config.json",
        )
        .load_default_value()
        .map_err(|_| {
            ControllerError::InitFailure("Invalid default light hardware config".into())
        })?;

        LightController::create_with_config(client, light_hardware_config).await
    }
}

#[async_trait]
impl controller::Handle for LightController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::Restore => {
                Some(self.restore().await.map(|light_info| Some(SettingInfo::Light(light_info))))
            }
            Request::OnButton(MediaButtons { mic_mute: Some(mic_mute), .. }) => {
                Some(self.on_mic_mute(mic_mute).await)
            }
            Request::SetLightGroupValue(name, state) => {
                // Validate state contains valid float numbers.
                for light_state in &state {
                    if !light_state.is_finite() {
                        return Some(Err(ControllerError::InvalidArgument(
                            SettingType::Light,
                            "state".into(),
                            format!("{:?}", light_state).into(),
                        )));
                    }
                }
                Some(self.set(name, state).await)
            }
            Request::Get => {
                // Read all light values from underlying fuchsia.hardware.light before returning a
                // value to ensure we have the latest light state.
                // TODO(fxbug.dev/56319): remove once all clients are migrated.
                Some(self.restore().await.map(|light_info| Some(SettingInfo::Light(light_info))))
            }
            _ => None,
        }
    }
}

/// Controller for processing requests surrounding the Light protocol.
impl LightController {
    /// Alternate constructor that allows specifying a configuration.
    pub(crate) async fn create_with_config(
        client: ClientProxy,
        light_hardware_config: Option<LightHardwareConfiguration>,
    ) -> Result<Self, ControllerError> {
        let light_proxy = client
            .get_service_context()
            .connect_device_path::<LightMarker>(DEVICE_PATH)
            .await
            .map_err(|e| {
                ControllerError::InitFailure(
                    format!("failed to connect to fuchsia.hardware.light with error: {:?}", e)
                        .into(),
                )
            })?;

        Ok(LightController {
            client,
            light_proxy,
            light_hardware_config,
            data_cache: Arc::new(Mutex::new(None)),
        })
    }

    async fn set(&self, name: String, state: Vec<LightState>) -> SettingHandlerResult {
        let id = fuchsia_trace::Id::new();
        let mut light_info = self.data_cache.lock().await;
        // TODO(fxbug.dev/107540) Deduplicate the code here and in mic_mute if possible.
        if light_info.is_none() {
            drop(light_info);
            let _ = self.restore().await?;
            light_info = self.data_cache.lock().await;
        }

        let current = light_info
            .as_mut()
            .ok_or_else(|| ControllerError::UnexpectedError("missing data cache".into()))?;
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

        let _ = self.client.write_setting(current.clone().into(), id).await?;
        Ok(Some(current.clone().into()))
    }

    /// Writes the given list of light states for a light group to the actual hardware.
    ///
    /// [LightState::None] elements in the vector are ignored and not written to the hardware.
    async fn write_light_group_to_hardware(
        &self,
        group: &mut LightGroup,
        state: &[LightState],
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
                    let mut value = rgb.clone().try_into().map_err(|_| {
                        ControllerError::InvalidArgument(
                            SettingType::Light,
                            "value".into(),
                            format!("{:?}", rgb).into(),
                        )
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
            set_result
                .map_err(|e| format!("{e:?}"))
                .and_then(|res| res.map_err(|e| format!("{e:?}")))
                .map_err(|e| {
                    ControllerError::ExternalFailure(
                        SettingType::Light,
                        "fuchsia.hardware.light".into(),
                        format!("{} for light {}", method_name, hardware_index).into(),
                        e.into(),
                    )
                })?;

            // Set was successful, save this light value.
            group.lights[i] = light.clone();
        }
        Ok(())
    }

    async fn on_mic_mute(&self, mic_mute: bool) -> SettingHandlerResult {
        let id = fuchsia_trace::Id::new();
        let mut light_info = self.data_cache.lock().await;
        if light_info.is_none() {
            drop(light_info);
            let _ = self.restore().await?;
            light_info = self.data_cache.lock().await;
        }

        let current = light_info
            .as_mut()
            .ok_or_else(|| ControllerError::UnexpectedError("missing data cache".into()))?;
        for light in current
            .light_groups
            .values_mut()
            .filter(|l| l.disable_conditions.contains(&DisableConditions::MicSwitch))
        {
            // This condition means that the LED is hard-wired to the mute switch and will only be
            // on when the mic is disabled.
            light.enabled = mic_mute;
        }

        let _ = self.client.write_setting(current.clone().into(), id).await?;
        Ok(Some(current.clone().into()))
    }

    async fn restore(&self) -> Result<LightInfo, ControllerError> {
        let light_info = if let Some(config) = self.light_hardware_config.clone() {
            // Configuration is specified, restore from the configuration.
            self.restore_from_configuration(config).await
        } else {
            // Read light info from hardware.
            self.restore_from_hardware().await
        }?;
        let mut guard = self.data_cache.lock().await;
        *guard = Some(light_info.clone());
        Ok(light_info)
    }

    /// Restores the light information from a pre-defined hardware configuration. Individual light
    /// states are read from the underlying fuchsia.hardware.Light API, but the structure of the
    /// light groups is determined by the given `config`.
    async fn restore_from_configuration(
        &self,
        config: LightHardwareConfiguration,
    ) -> Result<LightInfo, ControllerError> {
        let id = fuchsia_trace::Id::new();
        let current = self.client.read_setting::<LightInfo>(id).await;
        let mut light_groups: HashMap<String, LightGroup> = HashMap::new();
        for group_config in config.light_groups {
            let mut light_state: Vec<LightState> = Vec::new();

            // TODO(fxbug.dev/56319): once all clients go through setui, restore state from hardware
            // only if not found in persistent storage.
            for light_index in group_config.hardware_index.iter() {
                light_state.push(
                    self.light_state_from_hardware_index(*light_index, group_config.light_type)
                        .await?,
                );
            }

            // Restore previous state.
            let enabled = current
                .light_groups
                .get(&group_config.name)
                .map(|found_group| found_group.enabled)
                .unwrap_or(true);

            let _ = light_groups.insert(
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

        Ok(LightInfo { light_groups })
    }

    /// Restores the light information when no hardware configuration is specified by reading from
    /// the underlying fuchsia.hardware.Light API and turning each light into a [`LightGroup`].
    ///
    /// [`LightGroup`]: ../../light/types/struct.LightGroup.html
    async fn restore_from_hardware(&self) -> Result<LightInfo, ControllerError> {
        let num_lights = call_async!(self.light_proxy => get_num_lights()).await.map_err(|e| {
            ControllerError::ExternalFailure(
                SettingType::Light,
                "fuchsia.hardware.light".into(),
                "get_num_lights".into(),
                format!("{e:?}").into(),
            )
        })?;

        let id = fuchsia_trace::Id::new();
        let mut current = self.client.read_setting::<LightInfo>(id).await;
        for i in 0..num_lights {
            let info = call_async!(self.light_proxy => get_info(i))
                .await
                .map_err(|e| format!("{e:?}"))
                .and_then(|res| res.map_err(|e| format!("{e:?}")))
                .map_err(|e| {
                    ControllerError::ExternalFailure(
                        SettingType::Light,
                        "fuchsia.hardware.light".into(),
                        format!("get_info for light {}", i).into(),
                        e.into(),
                    )
                })?;
            let (name, group) = self.light_info_to_group(i, info).await?;
            let _ = current.light_groups.insert(name, group);
        }

        Ok(current)
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
            LightType::Brightness => {
                call_async!(self.light_proxy => get_current_brightness_value(index))
                    .await
                    .map_err(|e| format!("{e:?}"))
                    .and_then(|res| res.map_err(|e| format!("{e:?}")))
                    .map(LightValue::Brightness)
                    .map_err(|e| {
                        ControllerError::ExternalFailure(
                            SettingType::Light,
                            "fuchsia.hardware.light".into(),
                            format!("get_current_brightness_value for light {}", index).into(),
                            e.into(),
                        )
                    })?
            }
            LightType::Rgb => call_async!(self.light_proxy => get_current_rgb_value(index))
                .await
                .map_err(|e| format!("{e:?}"))
                .and_then(|res| res.map_err(|e| format!("{e:?}")))
                .map(LightValue::from)
                .map_err(|e| {
                    ControllerError::ExternalFailure(
                        SettingType::Light,
                        "fuchsia.hardware.light".into(),
                        format!("get_current_rgb_value for light {}", index).into(),
                        e.into(),
                    )
                })?,
            LightType::Simple => call_async!(self.light_proxy => get_current_simple_value(index))
                .await
                .map_err(|e| format!("{e:?}"))
                .and_then(|res| res.map_err(|e| format!("{e:?}")))
                .map(LightValue::Simple)
                .map_err(|e| {
                    ControllerError::ExternalFailure(
                        SettingType::Light,
                        "fuchsia.hardware.light".into(),
                        format!("get_current_simple_value for light {}", index).into(),
                        e.into(),
                    )
                })?,
        };

        Ok(LightState { value: Some(value) })
    }
}

#[cfg(test)]
mod tests {
    use crate::handler::setting_handler::persist::ClientProxy;
    use crate::handler::setting_handler::ClientImpl;
    use crate::light::types::{LightInfo, LightState, LightType, LightValue};
    use crate::message::base::MessengerType;
    use crate::message::MessageHubUtil;
    use crate::storage::{Payload as StoragePayload, StorageRequest, StorageResponse};
    use crate::tests::fakes::hardware_light_service::HardwareLightService;
    use crate::tests::fakes::service_registry::ServiceRegistry;
    use crate::{service, Address, LightController, ServiceContext, SettingType};
    use futures::lock::Mutex;
    use settings_storage::fidl_storage::FidlStorageConvertible;
    use settings_storage::UpdateState;
    use std::sync::Arc;

    // Verify that a set call without a restore call succeeds. This can happen when the controller
    // is shutdown after inactivity and is brought up again to handle the set call.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_set_before_restore() {
        let message_hub = service::MessageHub::create_hub();

        // Create the messenger that the client proxy uses to send messages.
        let (controller_messenger, _) = message_hub
            .create(MessengerType::Unbound)
            .await
            .expect("Unable to create agent messenger");

        // Create a fake hardware light service that responds to FIDL calls and add it to the
        // service registry so that FIDL calls are routed to this fake service.
        let service_registry = ServiceRegistry::create();
        let light_service_handle = Arc::new(Mutex::new(HardwareLightService::new()));
        service_registry.lock().await.register_service(light_service_handle.clone());

        let service_context =
            ServiceContext::new(Some(ServiceRegistry::serve(service_registry)), None);

        // Add a light to the fake service.
        light_service_handle
            .lock()
            .await
            .insert_light(0, "light_1".to_string(), LightType::Simple, LightValue::Simple(false))
            .await;

        // This isn't actually the signature for the notifier, but it's unused in this test, so just
        // provide the signature of its own messenger to the client proxy.
        let signature = controller_messenger.get_signature();

        let base_proxy = ClientImpl::for_test(
            Default::default(),
            controller_messenger,
            signature,
            Arc::new(service_context),
            SettingType::Light,
        );

        // Create a fake storage receptor used to receive and respond to storage messages.
        let (_, mut storage_receptor) = message_hub
            .create(MessengerType::Addressable(Address::Storage))
            .await
            .expect("Unable to create agent messenger");

        // Spawn a task that mimics the storage agent by responding to read/write calls.
        fuchsia_async::Task::spawn(async move {
            loop {
                if let Ok((payload, message_client)) = storage_receptor.next_payload().await {
                    if let Ok(StoragePayload::Request(storage_request)) =
                        StoragePayload::try_from(payload)
                    {
                        match storage_request {
                            StorageRequest::Read(_, _) => {
                                // Just respond with the default value as we're not testing storage.
                                let _ = message_client
                                    .reply(service::Payload::Storage(StoragePayload::Response(
                                        StorageResponse::Read(LightInfo::default_value().into()),
                                    )))
                                    .send();
                            }
                            StorageRequest::Write(_, _) => {
                                // Just respond with Unchanged as we're not testing storage.
                                let _ = message_client
                                    .reply(service::Payload::Storage(StoragePayload::Response(
                                        StorageResponse::Write(Ok(UpdateState::Unchanged)),
                                    )))
                                    .send();
                            }
                        }
                    }
                }
            }
        })
        .detach();

        let client_proxy = ClientProxy::new(Arc::new(base_proxy), SettingType::Light).await;

        // Create the light controller.
        let light_controller = LightController::create_with_config(client_proxy, None)
            .await
            .expect("Failed to create light controller");

        // Call set and verify it succeeds.
        let _ = light_controller
            .set("light_1".to_string(), vec![LightState { value: Some(LightValue::Simple(true)) }])
            .await
            .expect("Set call failed");

        // Verify the data cache is populated after the set call.
        let _ =
            light_controller.data_cache.lock().await.as_ref().expect("Data cache is not populated");
    }

    // Verify that an on_mic_mute event without a restore call succeeds. This can happen when the
    // controller is shutdown after inactivity and is brought up again to handle the set call.
    #[fuchsia_async::run_until_stalled(test)]
    async fn test_on_mic_mute_before_restore() {
        let message_hub = service::MessageHub::create_hub();

        // Create the messenger that the client proxy uses to send messages.
        let (controller_messenger, _) = message_hub
            .create(MessengerType::Unbound)
            .await
            .expect("Unable to create agent messenger");

        // Create a fake hardware light service that responds to FIDL calls and add it to the
        // service registry so that FIDL calls are routed to this fake service.
        let service_registry = ServiceRegistry::create();
        let light_service_handle = Arc::new(Mutex::new(HardwareLightService::new()));
        service_registry.lock().await.register_service(light_service_handle.clone());

        let service_context =
            ServiceContext::new(Some(ServiceRegistry::serve(service_registry)), None);

        // Add a light to the fake service.
        light_service_handle
            .lock()
            .await
            .insert_light(0, "light_1".to_string(), LightType::Simple, LightValue::Simple(false))
            .await;

        // This isn't actually the signature for the notifier, but it's unused in this test, so just
        // provide the signature of its own messenger to the client proxy.
        let signature = controller_messenger.get_signature();

        let base_proxy = ClientImpl::for_test(
            Default::default(),
            controller_messenger,
            signature,
            Arc::new(service_context),
            SettingType::Light,
        );

        // Create a fake storage receptor used to receive and respond to storage messages.
        let (_, mut storage_receptor) = message_hub
            .create(MessengerType::Addressable(Address::Storage))
            .await
            .expect("Unable to create agent messenger");

        // Spawn a task that mimics the storage agent by responding to read/write calls.
        fuchsia_async::Task::spawn(async move {
            loop {
                if let Ok((payload, message_client)) = storage_receptor.next_payload().await {
                    if let Ok(StoragePayload::Request(storage_request)) =
                        StoragePayload::try_from(payload)
                    {
                        match storage_request {
                            StorageRequest::Read(_, _) => {
                                // Just respond with the default value as we're not testing storage.
                                let _ = message_client
                                    .reply(service::Payload::Storage(StoragePayload::Response(
                                        StorageResponse::Read(LightInfo::default_value().into()),
                                    )))
                                    .send();
                            }
                            StorageRequest::Write(_, _) => {
                                // Just respond with Unchanged as we're not testing storage.
                                let _ = message_client
                                    .reply(service::Payload::Storage(StoragePayload::Response(
                                        StorageResponse::Write(Ok(UpdateState::Unchanged)),
                                    )))
                                    .send();
                            }
                        }
                    }
                }
            }
        })
        .detach();

        let client_proxy = ClientProxy::new(Arc::new(base_proxy), SettingType::Light).await;

        // Create the light controller.
        let light_controller = LightController::create_with_config(client_proxy, None)
            .await
            .expect("Failed to create light controller");

        // Call on_mic_mute and verify it succeeds.
        let _ = light_controller.on_mic_mute(false).await.expect("Set call failed");

        // Verify the data cache is populated after the set call.
        let _ =
            light_controller.data_cache.lock().await.as_ref().expect("Data cache is not populated");
    }
}
