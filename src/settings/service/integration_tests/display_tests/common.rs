// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_settings::*;
use fuchsia_component_test::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};
use futures::lock::Mutex;
use std::sync::atomic::AtomicU32;
use std::sync::Arc;
use utils;

const COMPONENT_URL: &str = "#meta/setui_service.cm";

#[async_trait]
pub trait Mocks {
    async fn brightness_service_impl(
        handles: LocalComponentHandles,
        manual_brightness: Arc<Mutex<Option<f32>>>,
        auto_brightness: Arc<Mutex<Option<bool>>>,
        num_changes: Arc<AtomicU32>,
    ) -> Result<(), Error>;
}

pub struct DisplayTest {
    pub manual_brightness: Arc<Mutex<Option<f32>>>,
    pub auto_brightness: Arc<Mutex<Option<bool>>>,
    pub num_changes: Arc<AtomicU32>,
}

impl DisplayTest {
    pub async fn create_realm_with_brightness_controller(
        manual_brightness: Arc<Mutex<Option<f32>>>,
        auto_brightness: Arc<Mutex<Option<bool>>>,
        num_changes: Arc<AtomicU32>,
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

    pub(crate) fn get_init_manual_brightness() -> Arc<Mutex<Option<f32>>> {
        Arc::new(Mutex::new(None))
    }

    pub(crate) fn get_init_auto_brightness() -> Arc<Mutex<Option<bool>>> {
        Arc::new(Mutex::new(None))
    }

    pub(crate) fn get_init_num_changes() -> Arc<AtomicU32> {
        Arc::new(AtomicU32::new(0))
    }
}
