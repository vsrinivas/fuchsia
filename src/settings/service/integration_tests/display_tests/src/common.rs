// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_settings::*;
use fidl_fuchsia_ui_brightness::{ControlRequest, ControlRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};
use futures::channel::mpsc::Sender;
use futures::lock::Mutex;
use futures::{SinkExt, StreamExt, TryStreamExt};
use std::sync::atomic::{AtomicU32, Ordering};
use std::sync::Arc;
use utils;

const COMPONENT_URL: &str = "#meta/setui_service.cm";

#[derive(PartialEq, Debug)]
pub enum Request {
    SetAutoBrightness,
    SetManualBrightness,
}

#[async_trait]
pub trait Mocks {
    async fn brightness_service_impl(
        handles: LocalComponentHandles,
        manual_brightness: Arc<Mutex<Option<f32>>>,
        auto_brightness: Arc<Mutex<Option<bool>>>,
        num_changes: Arc<AtomicU32>,
        requests_sender: Sender<Request>,
    ) -> Result<(), Error>;
}

#[async_trait]
impl Mocks for DisplayTest {
    // Mock the brightness dependency and verify the settings service interacts with the brightness
    // service by checking input value changes through the various requests.
    async fn brightness_service_impl(
        handles: LocalComponentHandles,
        manual_brightness: Arc<Mutex<Option<f32>>>,
        auto_brightness: Arc<Mutex<Option<bool>>>,
        num_changes: Arc<AtomicU32>,
        requests_sender: Sender<Request>,
    ) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        let _: &mut ServiceFsDir<'_, _> =
            fs.dir("svc").add_fidl_service(move |mut stream: ControlRequestStream| {
                let auto_brightness_handle = auto_brightness.clone();
                let brightness_handle = manual_brightness.clone();
                let num_changes_handle = num_changes.clone();
                let mut requests_sender = requests_sender.clone();
                fasync::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        // Support future expansion of FIDL.
                        #[allow(unreachable_patterns)]
                        match req {
                            ControlRequest::WatchCurrentBrightness { responder } => {
                                responder
                                    .send(
                                        brightness_handle
                                            .lock()
                                            .await
                                            .expect("brightness not yet set"),
                                    )
                                    .unwrap();
                            }
                            ControlRequest::SetAutoBrightness { control_handle: _ } => {
                                *auto_brightness_handle.lock().await = Some(true);
                                let current_num = num_changes_handle.load(Ordering::Relaxed);
                                (*num_changes_handle).store(current_num + 1, Ordering::Relaxed);
                                requests_sender
                                    .send(Request::SetAutoBrightness)
                                    .await
                                    .expect("Finished processing SetAutoBrightness call");
                            }
                            ControlRequest::SetManualBrightness { value, control_handle: _ } => {
                                *brightness_handle.lock().await = Some(value);
                                let current_num = num_changes_handle.load(Ordering::Relaxed);
                                (*num_changes_handle).store(current_num + 1, Ordering::Relaxed);
                                requests_sender
                                    .send(Request::SetManualBrightness)
                                    .await
                                    .expect("Finished processing SetManualBrightness call");
                            }
                            ControlRequest::WatchAutoBrightness { responder } => {
                                responder
                                    .send(auto_brightness_handle.lock().await.unwrap_or(false))
                                    .unwrap();
                            }
                            _ => {}
                        }
                    }
                })
                .detach();
            });
        let _: &mut ServiceFs<_> = fs.serve_connection(handles.outgoing_dir).unwrap();
        fs.collect::<()>().await;
        Ok(())
    }
}

pub struct DisplayTest {}

impl DisplayTest {
    // This function creates a realm without external brightness dependency, and is used for tests
    // in the internal_display_integration_test package.
    pub async fn create_realm() -> Result<RealmInstance, Error> {
        let builder = RealmBuilder::new().await?;
        // Add setui_service as child of the realm builder.
        let setui_service =
            builder.add_child("setui_service", COMPONENT_URL, ChildOptions::new()).await?;
        let info = utils::SettingsRealmInfo {
            builder,
            settings: &setui_service,
            has_config_data: true,
            capabilities: vec!["fuchsia.settings.Display"],
        };
        // Add basic Settings service realm information.
        utils::create_realm_basic(&info).await?;
        let instance = info.builder.build().await?;
        Ok(instance)
    }

    // This function creates a realm with external brightness dependency, and is used for tests in
    // the display_integration_test package.
    pub async fn create_realm_with_brightness_controller(
        manual_brightness: Arc<Mutex<Option<f32>>>,
        auto_brightness: Arc<Mutex<Option<bool>>>,
        num_changes: Arc<AtomicU32>,
        requests_sender: Sender<Request>,
    ) -> Result<RealmInstance, Error> {
        let builder = RealmBuilder::new().await?;
        // Add setui_service as child of the realm builder.
        let setui_service =
            builder.add_child("setui_service", COMPONENT_URL, ChildOptions::new()).await?;
        let info = utils::SettingsRealmInfo {
            builder,
            settings: &setui_service,
            has_config_data: true,
            capabilities: vec!["fuchsia.settings.Display"],
        };
        // Add basic Settings service realm information.
        utils::create_realm_basic(&info).await?;
        // Add mock camera dependency to test Display service with camera.
        let brightness_service = info
            .builder
            .add_local_child(
                "brightness_service",
                move |handles: LocalComponentHandles| {
                    Box::pin(DisplayTest::brightness_service_impl(
                        handles,
                        Arc::clone(&manual_brightness),
                        Arc::clone(&auto_brightness),
                        Arc::clone(&num_changes),
                        requests_sender.clone(),
                    ))
                },
                ChildOptions::new().eager(),
            )
            .await?;
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.ui.brightness.Control"))
                    .from(&brightness_service)
                    .to(Ref::parent())
                    .to(&setui_service),
            )
            .await?;
        // Provide LogSink to print out logs of the camera component for debugging purpose.
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&brightness_service),
            )
            .await?;
        let instance = info.builder.build().await?;
        Ok(instance)
    }

    pub fn connect_to_displaymarker(instance: &RealmInstance) -> DisplayProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<DisplayMarker>()
            .expect("connecting to Display");
    }

    pub fn get_init_manual_brightness() -> Arc<Mutex<Option<f32>>> {
        Arc::new(Mutex::new(None))
    }

    pub fn get_init_auto_brightness() -> Arc<Mutex<Option<bool>>> {
        Arc::new(Mutex::new(None))
    }

    pub fn get_init_num_changes() -> Arc<AtomicU32> {
        Arc::new(AtomicU32::new(0))
    }

    // This is for tests that testing screen enabled fields. It is used for both test packages.
    pub async fn test_screen_enabled(display_proxy: DisplayProxy) {
        // Test that if screen is turned off, it is reflected.
        let mut display_settings = DisplaySettings::EMPTY;
        display_settings.auto_brightness = Some(false);
        display_proxy.set(display_settings).await.expect("set completed").expect("set successful");

        let mut display_settings = DisplaySettings::EMPTY;
        display_settings.screen_enabled = Some(false);
        display_proxy.set(display_settings).await.expect("set completed").expect("set successful");

        let settings = display_proxy.watch().await.expect("watch completed");

        assert_eq!(settings.screen_enabled, Some(false));

        // Test that if display is turned back on, the display and manual brightness are on.
        let mut display_settings = DisplaySettings::EMPTY;
        display_settings.screen_enabled = Some(true);
        display_proxy.set(display_settings).await.expect("set completed").expect("set successful");

        let settings = display_proxy.watch().await.expect("watch completed");

        assert_eq!(settings.screen_enabled, Some(true));
        assert_eq!(settings.auto_brightness, Some(false));

        // Test that if auto brightness is turned on, the display and auto brightness are on.
        let mut display_settings = DisplaySettings::EMPTY;
        display_settings.auto_brightness = Some(true);
        display_proxy.set(display_settings).await.expect("set completed").expect("set successful");

        let settings = display_proxy.watch().await.expect("watch completed");

        assert_eq!(settings.auto_brightness, Some(true));
        assert_eq!(settings.screen_enabled, Some(true));
    }
}
