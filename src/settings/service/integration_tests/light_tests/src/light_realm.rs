// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::{create_proxy, DiscoverableProtocolMarker, ServerEnd};
use fidl_fuchsia_hardware_light::{
    Capability, Info as HardwareInfo, LightError, LightRequest, LightRequestStream, Rgb,
};
use fidl_fuchsia_io as fio;
use fidl_fuchsia_settings::{LightMarker, LightProxy, LightValue};
use fidl_fuchsia_ui_policy::{
    DeviceListenerRegistryMarker, DeviceListenerRegistryRequest,
    DeviceListenerRegistryRequestStream, MediaButtonsListenerProxy,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::{
    Capability as ComponentCapability, ChildOptions, LocalComponentHandles, RealmBuilder,
    RealmInstance, Route,
};
use futures::channel::mpsc::Sender;
use futures::lock::Mutex;
use futures::{FutureExt, StreamExt, TryStreamExt};
use std::collections::HashMap;
use std::sync::Arc;
use utils;
use vfs::directory::entry::DirectoryEntry;
use vfs::execution_scope::ExecutionScope;
use vfs::{pseudo_directory, service};

#[derive(Clone, Debug)]
pub struct HardwareLight {
    pub name: String,
    pub value: LightValue,
}

const COMPONENT_URL: &str = "#meta/setui_service.cm";
const DEVICE_PATH: &str = "/dev/class/light";

pub struct LightRealm;

impl LightRealm {
    pub async fn create_realm(
        hardware_lights: impl Into<Option<Vec<HardwareLight>>>,
    ) -> Result<RealmInstance, Error> {
        Self::create_realm_with_input_device_registry(hardware_lights, None).await
    }

    pub async fn create_realm_with_input_device_registry(
        hardware_lights: impl Into<Option<Vec<HardwareLight>>>,
        listener_sender: impl Into<Option<Sender<MediaButtonsListenerProxy>>>,
    ) -> Result<RealmInstance, Error> {
        let hardware_lights = hardware_lights.into().unwrap_or_else(|| vec![]);
        let builder = RealmBuilder::new().await?;
        // Add setui_service as child of the realm builder.
        let setui_service =
            builder.add_child("setui_service", COMPONENT_URL, ChildOptions::new()).await?;
        let hardware_light = builder
            .add_local_child(
                "dev-light",
                move |handles| {
                    Self::mock_dev(
                        handles,
                        Self::mock_light_dev_with_light_devices(hardware_lights.clone()),
                    )
                    .boxed()
                },
                ChildOptions::new().eager(),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(
                        ComponentCapability::directory("dev-light")
                            .rights(fio::RW_STAR_DIR)
                            .path(DEVICE_PATH),
                    )
                    .from(&hardware_light)
                    .to(&setui_service),
            )
            .await?;
        if let Some(listener_sender) = listener_sender.into() {
            let input_device_registry = builder
                .add_local_child(
                    "input-device-registry",
                    move |handles| {
                        Self::input_device_registry_service(handles, listener_sender.clone())
                            .boxed()
                    },
                    ChildOptions::new().eager(),
                )
                .await?;
            builder
                .add_route(
                    Route::new()
                        .capability(ComponentCapability::protocol::<DeviceListenerRegistryMarker>())
                        .from(&input_device_registry)
                        .to(&setui_service),
                )
                .await?;
        }
        let info = utils::SettingsRealmInfo {
            builder,
            settings: &setui_service,
            has_config_data: true,
            capabilities: vec![LightMarker::PROTOCOL_NAME],
        };
        // Add basic Settings service realm information.
        utils::create_realm_basic(&info).await?;
        let instance = info.builder.build().await?;
        Ok(instance)
    }

    pub fn connect_to_light_marker(instance: &RealmInstance) -> LightProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<LightMarker>()
            .expect("connecting to Light");
    }

    async fn input_device_registry_service(
        handles: LocalComponentHandles,
        listener_sender: Sender<MediaButtonsListenerProxy>,
    ) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        let _ = fs.dir("svc").add_fidl_service(
            move |mut stream: DeviceListenerRegistryRequestStream| {
                fasync::Task::spawn({
                    let mut listener_sender = listener_sender.clone();
                    async move {
                        while let Ok(Some(request)) = stream.try_next().await {
                            match request {
                                DeviceListenerRegistryRequest::RegisterListener {
                                    listener,
                                    responder,
                                } => {
                                    if let Ok(proxy) = listener.into_proxy() {
                                        listener_sender
                                            .try_send(proxy)
                                            .expect("test should listen");
                                        // Acknowledge the registration.
                                        responder
                                            .send()
                                            .expect("failed to ack RegisterListener call");
                                    }
                                }
                                _ => {
                                    panic!("Unsupported request {request:?}")
                                }
                            }
                        }
                    }
                })
                .detach()
            },
        );

        let _ = fs.serve_connection(handles.outgoing_dir.into_channel())?;
        fs.collect::<()>().await;
        Ok(())
    }

    async fn hardware_light_service(
        mut stream: LightRequestStream,
        hardware_lights: Vec<HardwareLight>,
    ) {
        let mut light_info = vec![];
        let mut simple_values = HashMap::new();
        let mut brightness_values = HashMap::new();
        let mut rgb_values = HashMap::new();
        for (i, light) in hardware_lights.iter().enumerate() {
            let capability = match light.value {
                LightValue::On(value) => {
                    let _ = simple_values.insert(i, value);
                    Capability::Simple
                }
                LightValue::Brightness(value) => {
                    let _ = brightness_values.insert(i, value);
                    Capability::Brightness
                }
                LightValue::Color(value) => {
                    let _ = rgb_values.insert(
                        i,
                        Rgb {
                            red: value.red as f64,
                            green: value.green as f64,
                            blue: value.blue as f64,
                        },
                    );
                    Capability::Rgb
                }
            };

            light_info.push(HardwareInfo { name: light.name.clone(), capability });
        }

        let light_info = Arc::new(Mutex::new(light_info));
        let simple_values = Arc::new(Mutex::new(simple_values));
        let brightness_values = Arc::new(Mutex::new(brightness_values));
        let rgb_values = Arc::new(Mutex::new(rgb_values));

        fasync::Task::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                // Support future expansion of FIDL.
                #[allow(unreachable_patterns)]
                match req {
                    LightRequest::GetNumLights { responder } => responder
                        .send(light_info.lock().await.len() as u32)
                        .expect("get num lights"),
                    LightRequest::GetInfo { index, responder } => responder
                        .send(
                            &mut light_info
                                .lock()
                                .await
                                .get(index as usize)
                                .cloned()
                                .ok_or(LightError::InvalidIndex),
                        )
                        .expect("get info"),
                    LightRequest::GetCurrentBrightnessValue { index, responder } => responder
                        .send(
                            &mut brightness_values
                                .lock()
                                .await
                                .get(&(index as usize))
                                .copied()
                                .ok_or(LightError::InvalidIndex),
                        )
                        .expect("get brightness value"),
                    LightRequest::GetCurrentSimpleValue { index, responder } => responder
                        .send(
                            &mut simple_values
                                .lock()
                                .await
                                .get(&(index as usize))
                                .copied()
                                .ok_or(LightError::InvalidIndex),
                        )
                        .expect("get simple value"),
                    LightRequest::GetCurrentRgbValue { index, responder } => responder
                        .send(
                            &mut rgb_values
                                .lock()
                                .await
                                .get(&(index as usize))
                                .copied()
                                .ok_or(LightError::InvalidIndex),
                        )
                        .expect("get rgb value"),
                    LightRequest::SetBrightnessValue { index, value, responder } => responder
                        .send(
                            &mut brightness_values
                                .lock()
                                .await
                                .insert(index as usize, value)
                                .map(|_| ())
                                .ok_or(LightError::InvalidIndex),
                        )
                        .expect("set brightness value"),
                    LightRequest::SetSimpleValue { index, value, responder } => responder
                        .send(
                            &mut simple_values
                                .lock()
                                .await
                                .insert(index as usize, value)
                                .map(|_| ())
                                .ok_or(LightError::InvalidIndex),
                        )
                        .expect("set simple value"),
                    LightRequest::SetRgbValue { index, value, responder } => responder
                        .send(
                            &mut rgb_values
                                .lock()
                                .await
                                .insert(index as usize, value)
                                .map(|_| ())
                                .ok_or(LightError::InvalidIndex),
                        )
                        .expect("set rgb value"),
                    _ => {}
                }
            }
        })
        .detach();
    }

    async fn mock_dev(
        handles: LocalComponentHandles,
        dev_directory: Arc<dyn DirectoryEntry>,
    ) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        fs.add_remote("dev", Self::spawn_vfs(dev_directory));
        let _ = fs.serve_connection(handles.outgoing_dir.into_channel())?;
        fs.collect::<()>().await;
        Ok(())
    }

    /// Spawns a VFS handler for the provided `dir`.
    fn spawn_vfs(dir: Arc<dyn DirectoryEntry>) -> fio::DirectoryProxy {
        let (client_end, server_end) = create_proxy::<fio::DirectoryMarker>().unwrap();
        let scope = ExecutionScope::new();
        dir.open(
            scope,
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE,
            0,
            vfs::path::Path::dot(),
            ServerEnd::new(server_end.into_channel()),
        );
        client_end
    }

    fn mock_light_dev_with_light_devices(handles: Vec<HardwareLight>) -> Arc<dyn DirectoryEntry> {
        pseudo_directory! {
            "class" => pseudo_directory! {
                "light" => pseudo_directory! {
                    "000" => service::host(
                        move |stream: LightRequestStream| {
                            Self::hardware_light_service(stream, handles.clone())
                        }
                    ),
                }
            }
        }
    }
}

/// Compares a vector of light group from the settings FIDL API and the light groups from a
/// service-internal LightInfo object for equality.
#[macro_export]
macro_rules! assert_lights_eq {
    ($groups:expr, $info:expr) => {
        let mut groups = $groups;
        // Watch returns vector, internally we use a HashMap, so convert into a vector for
        // comparison.
        let mut expected_value = $info
            .into_iter()
            .map(|(_, value)| ::fidl_fuchsia_settings::LightGroup::from(value))
            .collect::<Vec<_>>();

        assert_eq!(groups.len(), expected_value.len(), "lights length mismatch");
        // Sort by names for stability
        groups.sort_by_key(|group: &::fidl_fuchsia_settings::LightGroup| group.name.clone());
        expected_value.sort_by_key(|group| group.name.clone());
        for i in 0..groups.len() {
            $crate::assert_fidl_light_group_eq!(&groups[i], &expected_value[i]);
        }
    };
}

#[macro_export]
macro_rules! assert_fidl_light_group_eq {
    ($left:expr, $right:expr) => {
        let left = $left;
        let right = $right;
        assert_eq!(left.name, right.name, "name mismatch");
        assert_eq!(left.enabled, right.enabled, "enabled mismatch");
        assert_eq!(left.type_, right.type_, "type mismatch");
        assert_eq!(
            left.lights.as_ref().unwrap().len(),
            right.lights.as_ref().unwrap().len(),
            "group length mismatch"
        );
        if left.type_ == Some(::fidl_fuchsia_settings::LightType::Simple) {
            assert_eq!(left.lights, right.lights);
        }
    };
}
