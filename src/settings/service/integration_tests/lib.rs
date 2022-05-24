// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_io as fio;
use fidl_fuchsia_settings::*;
use fuchsia_component_test::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};
use std::sync::atomic::AtomicBool;
use std::sync::Arc;

const COMPONENT_URL: &str = "#meta/setui_service_with_camera.cm";
const STORE_URL: &str = "fuchsia-pkg://fuchsia.com/stash#meta/stash.cm";

#[async_trait]
pub trait Mocks {
    async fn device_watcher_impl(
        handles: LocalComponentHandles,
        cam_muted: Arc<AtomicBool>,
    ) -> Result<(), Error>;
}

pub struct SetuiServiceTest {
    pub camera_sw_muted: Arc<AtomicBool>,
}

impl SetuiServiceTest {
    pub async fn create_realm(cam_muted: Arc<AtomicBool>) -> Result<RealmInstance, Error> {
        let builder = RealmBuilder::new().await?;
        // Add setui_service as child of the realm builder.
        let setui_service =
            builder.add_child("setui_service", COMPONENT_URL, ChildOptions::new()).await?;
        // setui_service needs to connect to fidl_fuchsia_stash to start its storage.
        let store = builder.add_child("store", STORE_URL, ChildOptions::new().eager()).await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.stash.Store"))
                    .from(&store)
                    .to(&setui_service),
            )
            .await?;
        // Add mock camera dependency to test Input service with camera.
        let camera = builder
            .add_local_child(
                "camera",
                move |handles: LocalComponentHandles| {
                    Box::pin(SetuiServiceTest::device_watcher_impl(handles, Arc::clone(&cam_muted)))
                },
                ChildOptions::new().eager(),
            )
            .await?;
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.camera3.DeviceWatcher"))
                    .from(&camera)
                    .to(Ref::parent())
                    .to(&setui_service),
            )
            .await?;
        // Use for reading configuration files.
        builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("config-data")
                            .path("/config/data")
                            .rights(fio::R_STAR_DIR),
                    )
                    .from(Ref::parent())
                    .to(&setui_service),
            )
            .await?;
        // Required by the store dependency.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::storage("data").path("/data"))
                    .from(Ref::parent())
                    .to(&store),
            )
            .await?;
        // Provide LogSink to print out logs of each service for debugging purpose.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&setui_service)
                    .to(&store)
                    .to(&camera),
            )
            .await?;
        // Used for Input service test.
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.settings.Input"))
                    .from(&setui_service)
                    .to(Ref::parent()),
            )
            .await?;

        let instance = builder.build().await?;
        Ok(instance)
    }

    pub fn connect_to_inputmarker(instance: &RealmInstance) -> InputProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<InputMarker>()
            .expect("connecting to Input");
    }
}
