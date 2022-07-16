// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_io as fio;
use fuchsia_component_test::{Capability, ChildOptions, ChildRef, RealmBuilder, Ref, Route};

pub struct SettingsRealmInfo<'a> {
    pub builder: RealmBuilder,
    pub settings: &'a ChildRef,
    pub has_config_data: bool,
    pub capabilities: Vec<&'a str>,
}

pub async fn create_realm_basic(info: &SettingsRealmInfo<'_>) -> Result<(), Error> {
    const STORE_URL: &str = "fuchsia-pkg://fuchsia.com/stash#meta/stash.cm";

    // setui_service needs to connect to fidl_fuchsia_stash to start its storage.
    let store = info.builder.add_child("store", STORE_URL, ChildOptions::new().eager()).await?;
    info.builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.stash.Store"))
                .from(&store)
                .to(info.settings),
        )
        .await?;
    // Required by the store dependency.
    info.builder
        .add_route(
            Route::new()
                .capability(Capability::storage("data").path("/data"))
                .from(Ref::parent())
                .to(&store)
                .to(info.settings),
        )
        .await?;

    if info.has_config_data {
        // Use for reading configuration files.
        info.builder
            .add_route(
                Route::new()
                    .capability(
                        Capability::directory("config-data")
                            .path("/config/data")
                            .rights(fio::R_STAR_DIR),
                    )
                    .from(Ref::parent())
                    .to(info.settings),
            )
            .await?;
    }

    // Provide LogSink to print out logs of each service for debugging purpose.
    info.builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(info.settings)
                .to(&store),
        )
        .await?;

    for cap in &info.capabilities {
        // Route capabilities from setui_service.
        info.builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(*cap))
                    .from(info.settings)
                    .to(Ref::parent()),
            )
            .await?;
    }

    Ok(())
}
