// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_hardware_power_statecontrol::AdminMarker;
use fidl_fuchsia_settings::{SetupMarker, SetupProxy};
use fuchsia_component_test::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};
use futures::lock::Mutex;
use std::sync::Arc;
use utils;

const COMPONENT_URL: &str = "#meta/setui_service.cm";

#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy)]
pub enum Action {
    Reboot,
}

#[async_trait]
pub trait Mocks {
    async fn hardware_power_statecontrol_service_impl(
        handles: LocalComponentHandles,
        recorded_actions: Arc<Mutex<Vec<Action>>>,
    ) -> Result<(), Error>;
}

pub struct SetupTest;

impl SetupTest {
    pub async fn create_realm(
        recorded_actions: Arc<Mutex<Vec<Action>>>,
    ) -> Result<RealmInstance, Error> {
        let builder = RealmBuilder::new().await?;
        // Add setui_service as child of the realm builder.
        let setui_service =
            builder.add_child("setui_service", COMPONENT_URL, ChildOptions::new()).await?;
        let info = utils::SettingsRealmInfo {
            builder,
            settings: &setui_service,
            has_config_data: true,
            capabilities: vec![SetupMarker::PROTOCOL_NAME],
        };
        // Add basic Settings service realm information.
        utils::create_realm_basic(&info).await?;

        // Add mock power state control service dependency to test reboot.
        let power_statecontrol_service = info
            .builder
            .add_local_child(
                "power",
                move |handles: LocalComponentHandles| {
                    Box::pin(SetupTest::hardware_power_statecontrol_service_impl(
                        handles,
                        recorded_actions.clone(),
                    ))
                },
                ChildOptions::new().eager(),
            )
            .await?;
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(AdminMarker::PROTOCOL_NAME))
                    .from(&power_statecontrol_service)
                    .to(Ref::parent())
                    .to(&setui_service),
            )
            .await?;

        let instance = info.builder.build().await?;
        Ok(instance)
    }

    pub fn connect_to_setup_marker(instance: &RealmInstance) -> SetupProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<SetupMarker>()
            .expect("connecting to Setup");
    }
}
