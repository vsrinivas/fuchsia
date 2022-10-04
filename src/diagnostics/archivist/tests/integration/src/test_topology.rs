// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants;
use fidl::endpoints::DiscoverableProtocolMarker;
use fidl_fuchsia_diagnostics_test::ControllerMarker;
use fuchsia_component_test::{
    error::Error, Capability, ChildOptions, ChildRef, Event, RealmBuilder, Ref, Route,
    SubRealmBuilder,
};

/// Options for creating a test topology.
pub struct Options {
    /// The URL of the archivist to be used in the test.
    pub archivist_url: &'static str,
}

impl Default for Options {
    fn default() -> Self {
        Self { archivist_url: constants::INTEGRATION_ARCHIVIST_URL }
    }
}

/// Creates a new topology for tests with an archivist inside.
pub async fn create(opts: Options) -> Result<(RealmBuilder, SubRealmBuilder), Error> {
    let builder = RealmBuilder::new().await?;
    let test_realm = builder.add_child_realm("test", ChildOptions::new().eager()).await?;
    let archivist =
        test_realm.add_child("archivist", opts.archivist_url, ChildOptions::new().eager()).await?;

    let parent_to_archivist = Route::new()
        .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
        .capability(Capability::directory("config-data"))
        .capability(Capability::protocol_by_name("fuchsia.sys2.EventSource"))
        .capability(Capability::protocol_by_name("fuchsia.boot.ReadOnlyLog"))
        .capability(Capability::protocol_by_name("fuchsia.boot.WriteOnlyLog"))
        .capability(Capability::event(Event::Stopped))
        .capability(Capability::event(Event::capability_requested("fuchsia.logger.LogSink")));
    builder.add_route(parent_to_archivist.clone().from(Ref::parent()).to(&test_realm)).await?;
    test_realm.add_route(parent_to_archivist.from(Ref::parent()).to(&archivist)).await?;

    let archivist_to_parent = Route::new()
        .capability(Capability::protocol_by_name("fuchsia.diagnostics.ArchiveAccessor"))
        .capability(Capability::protocol_by_name("fuchsia.diagnostics.FeedbackArchiveAccessor"))
        .capability(Capability::protocol_by_name(
            "fuchsia.diagnostics.LegacyMetricsArchiveAccessor",
        ))
        .capability(Capability::protocol_by_name("fuchsia.diagnostics.LoWPANArchiveAccessor"))
        .capability(Capability::protocol_by_name("fuchsia.diagnostics.LogSettings"))
        .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
        .capability(Capability::protocol_by_name(ControllerMarker::PROTOCOL_NAME))
        .capability(Capability::protocol_by_name("fuchsia.logger.Log"));
    test_realm.add_route(archivist_to_parent.clone().from(&archivist).to(Ref::parent())).await?;
    builder.add_route(archivist_to_parent.from(&test_realm).to(Ref::parent())).await?;

    test_realm
        .add_route(
            Route::new()
                .capability(Capability::event(Event::directory_ready("diagnostics")))
                .from(Ref::framework())
                .to(&archivist),
        )
        .await?;

    Ok((builder, test_realm))
}

pub async fn add_eager_child(
    test_realm: &SubRealmBuilder,
    name: &str,
    url: &str,
) -> Result<ChildRef, Error> {
    let child_ref = test_realm.add_child(name, url, ChildOptions::new().eager()).await?;
    test_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::child("archivist"))
                .to(&child_ref),
        )
        .await?;
    Ok(child_ref)
}

pub async fn add_lazy_child(
    test_realm: &SubRealmBuilder,
    name: &str,
    url: &str,
) -> Result<ChildRef, Error> {
    let child_ref = test_realm.add_child(name, url, ChildOptions::new()).await?;
    test_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::child("archivist"))
                .to(&child_ref),
        )
        .await?;
    Ok(child_ref)
}

pub async fn expose_test_realm_protocol(builder: &RealmBuilder, test_realm: &SubRealmBuilder) {
    test_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.component.Realm"))
                .from(Ref::framework())
                .to(Ref::parent()),
        )
        .await
        .unwrap();
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.component.Realm"))
                .from(Ref::child("test"))
                .to(Ref::parent()),
        )
        .await
        .unwrap();
}
