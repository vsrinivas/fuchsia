// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_settings::{InputMarker, InputProxy};
use fuchsia_component_test::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};
use std::sync::atomic::AtomicBool;
use std::sync::Arc;
use utils;

const COMPONENT_URL: &str = "#meta/setui_service_with_camera.cm";

#[async_trait]
pub trait Mocks {
    async fn device_watcher_impl(
        handles: LocalComponentHandles,
        cam_muted: Arc<AtomicBool>,
    ) -> Result<(), Error>;
}

pub struct InputTest {
    pub camera_sw_muted: Arc<AtomicBool>,
}

impl InputTest {
    pub async fn create_realm(cam_muted: Arc<AtomicBool>) -> Result<RealmInstance, Error> {
        let builder = RealmBuilder::new().await?;
        // Add setui_service as child of the realm builder.
        let setui_service =
            builder.add_child("setui_service", COMPONENT_URL, ChildOptions::new()).await?;
        let info = utils::SettingsRealmInfo {
            builder,
            settings: &setui_service,
            has_config_data: true,
            capabilities: vec![InputMarker::PROTOCOL_NAME],
        };
        // Add basic Settings service realm information.
        utils::create_realm_basic(&info).await?;
        // Add mock camera dependency to test Input service with camera.
        let camera = info
            .builder
            .add_local_child(
                "camera",
                move |handles: LocalComponentHandles| {
                    Box::pin(InputTest::device_watcher_impl(handles, Arc::clone(&cam_muted)))
                },
                ChildOptions::new().eager(),
            )
            .await?;
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.camera3.DeviceWatcher"))
                    .from(&camera)
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
                    .to(&camera),
            )
            .await?;
        let instance = info.builder.build().await?;
        Ok(instance)
    }

    pub fn connect_to_inputmarker(instance: &RealmInstance) -> InputProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<InputMarker>()
            .expect("connecting to Input");
    }
}
