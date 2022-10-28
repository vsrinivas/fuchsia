// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::sync::atomic::{AtomicU16, Ordering};

use anyhow::Error;
use fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route};
use lazy_static::lazy_static;
use regex::Regex;

const SINGLE_COUNTER_URL: &str = "#meta/single_counter_test_component.cm";
const PERSISTENCE_URL: &str = "#meta/persistence.cm";
lazy_static! {
    pub static ref REALM_NAME_PATTERN: Regex = Regex::new(r"persistence-test-\d{5}").unwrap();
}

pub async fn create() -> Result<RealmInstance, Error> {
    let builder = RealmBuilder::new().await?;
    let single_counter =
        builder.add_child("single_counter", SINGLE_COUNTER_URL, ChildOptions::new()).await?;
    let persistence =
        builder.add_child("persistence", PERSISTENCE_URL, ChildOptions::new()).await?;

    let config_server = crate::mock_filesystems::create_config_data(&builder).await?;
    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::directory("config-data")
                        .path("/config/data")
                        .rights(fidl_fuchsia_io::R_STAR_DIR),
                )
                .from(&config_server)
                .to(&persistence),
        )
        .await?;

    let cache_server = crate::mock_filesystems::create_cache_server(&builder).await?;
    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::directory("cache")
                        .path("/cache")
                        .rights(fidl_fuchsia_io::RW_STAR_DIR),
                )
                .from(&cache_server)
                .to(&persistence),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name(
                    "fuchsia.samplertestcontroller.SamplerTestController",
                ))
                .from(&single_counter)
                .to(Ref::parent()),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name(crate::TEST_PERSISTENCE_SERVICE_NAME))
                .from(&persistence)
                .to(Ref::parent()),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::protocol_by_name("fuchsia.diagnostics.ArchiveAccessor")
                        .as_("fuchsia.diagnostics.FeedbackArchiveAccessor"),
                )
                .from(Ref::parent())
                .to(&persistence),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.Log"))
                .from(Ref::parent())
                .to(&single_counter),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&persistence),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&single_counter),
        )
        .await?;

    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::protocol_by_name("fuchsia.component.Binder")
                        .as_("fuchsia.component.PersistenceBinder"),
                )
                .from(&persistence)
                .to(Ref::parent()),
        )
        .await?;

    static COUNTER: AtomicU16 = AtomicU16::new(0);

    // We want deterministic realm names of a fixed length. Add a fixed-size
    // variable component so that realm names are unique across test cases.
    let realm_name = format!("persistence-test-{:05}", COUNTER.fetch_add(1, Ordering::Relaxed));
    assert!(
        REALM_NAME_PATTERN.is_match(&realm_name),
        "{} does not match {:?}",
        realm_name,
        *REALM_NAME_PATTERN
    );
    let instance = builder.build_with_name(realm_name).await?;
    Ok(instance)
}
