// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use async_trait::async_trait;
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_recovery_policy::DeviceMarker;
use fidl_fuchsia_settings::{FactoryResetMarker, FactoryResetProxy};
use fuchsia_component_test::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};
use futures::channel::mpsc::Sender;
use utils;

const COMPONENT_URL: &str = "#meta/setui_service.cm";

#[async_trait]
pub trait Mocks {
    async fn recovery_policy_impl(
        handles: LocalComponentHandles,
        reset_allowed_sender: Sender<bool>,
    ) -> Result<(), Error>;
}

pub struct FactoryResetTest {}

impl FactoryResetTest {
    pub async fn create_realm(reset_allowed_sender: Sender<bool>) -> Result<RealmInstance, Error> {
        let builder = RealmBuilder::new().await?;
        // Add setui_service as child of the realm builder.
        let setui_service =
            builder.add_child("setui_service", COMPONENT_URL, ChildOptions::new()).await?;
        let info = utils::SettingsRealmInfo {
            builder,
            settings: &setui_service,
            has_config_data: true,
            capabilities: vec![FactoryResetMarker::PROTOCOL_NAME],
        };
        // Add basic Settings service realm information.
        utils::create_realm_basic(&info).await?;
        // Add mock recovery_policy dependency to test Factory Reset service.
        let recovery_policy = info
            .builder
            .add_local_child(
                "recovery_policy",
                move |handles: LocalComponentHandles| {
                    Box::pin(FactoryResetTest::recovery_policy_impl(
                        handles,
                        reset_allowed_sender.clone(),
                    ))
                },
                ChildOptions::new().eager(),
            )
            .await?;
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(DeviceMarker::PROTOCOL_NAME))
                    .from(&recovery_policy)
                    .to(Ref::parent())
                    .to(&setui_service),
            )
            .await?;
        // Provide LogSink to print out logs of the recovery policy component for debugging purpose.
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&recovery_policy),
            )
            .await?;
        let instance = info.builder.build().await?;
        Ok(instance)
    }

    pub fn connect_to_factoryresetmarker(instance: &RealmInstance) -> FactoryResetProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<FactoryResetMarker>()
            .expect("connecting to FactoryReset");
    }
}
