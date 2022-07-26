// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_settings::*;
use fuchsia_component_test::{ChildOptions, RealmBuilder, RealmInstance};
use tracing;
use utils;

const COMPONENT_URL: &str = "#meta/setui_service.cm";

pub struct HangingGetTest;

impl HangingGetTest {
    pub async fn create_realm() -> Result<RealmInstance, Error> {
        tracing::info!("Creating realm builder for hanging get test");
        let builder = RealmBuilder::new().await?;
        // Add setui_service as child of the realm builder.
        let setui_service =
            builder.add_child("setui_service", COMPONENT_URL, ChildOptions::new()).await?;
        let info = utils::SettingsRealmInfo {
            builder,
            settings: &setui_service,
            has_config_data: true,
            capabilities: vec![DoNotDisturbMarker::PROTOCOL_NAME],
        };
        // Add basic Settings service realm information.
        utils::create_realm_basic(&info).await?;
        let instance = info.builder.build().await?;
        Ok(instance)
    }

    pub fn connect_to_dndmarker(instance: &RealmInstance) -> DoNotDisturbProxy {
        return instance
            .root
            .connect_to_protocol_at_exposed_dir::<DoNotDisturbMarker>()
            .expect("connecting to DoNotDisturb");
    }
}
