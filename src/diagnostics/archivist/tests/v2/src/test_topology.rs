// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants;
use cm_rust;
use cm_rust::{ExposeDecl, ExposeProtocolDecl, ExposeSource, ExposeTarget};
use fidl_fuchsia_io2 as fio2;
use fuchsia_component_test::{
    error::Error, ChildProperties, Event, Moniker, RealmBuilder, RouteBuilder, RouteEndpoint,
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
pub async fn create(opts: Options) -> Result<RealmBuilder, Error> {
    let builder = RealmBuilder::new().await?;
    builder
        .add_child("test/archivist", opts.archivist_url, ChildProperties::new().eager())
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.logger.LogSink")
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("test/archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::directory("config-data", "", fio2::R_STAR_DIR)
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("test/archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.sys2.EventSource")
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("test/archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.boot.ReadOnlyLog")
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("test/archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.boot.WriteOnlyLog")
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("test/archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.diagnostics.ArchiveAccessor")
                .source(RouteEndpoint::component("test/archivist"))
                .targets(vec![RouteEndpoint::AboveRoot]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.diagnostics.FeedbackArchiveAccessor")
                .source(RouteEndpoint::component("test/archivist"))
                .targets(vec![RouteEndpoint::AboveRoot]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.diagnostics.LegacyMetricsArchiveAccessor")
                .source(RouteEndpoint::component("test/archivist"))
                .targets(vec![RouteEndpoint::AboveRoot]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.diagnostics.LogSettings")
                .source(RouteEndpoint::component("test/archivist"))
                .targets(vec![RouteEndpoint::AboveRoot]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.logger.LogSink")
                .source(RouteEndpoint::component("test/archivist"))
                .targets(vec![RouteEndpoint::AboveRoot]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.logger.Log")
                .source(RouteEndpoint::component("test/archivist"))
                .targets(vec![RouteEndpoint::AboveRoot]),
        )
        .await?
        .add_route(
            RouteBuilder::event(Event::Started, cm_rust::EventMode::Async)
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("test/archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::event(Event::Stopped, cm_rust::EventMode::Async)
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("test/archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::event(Event::Running, cm_rust::EventMode::Async)
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("test/archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::event(Event::directory_ready("diagnostics"), cm_rust::EventMode::Async)
                .source(RouteEndpoint::component("test"))
                .targets(vec![RouteEndpoint::component("test/archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::event(
                Event::capability_requested("fuchsia.logger.LogSink"),
                cm_rust::EventMode::Async,
            )
            .source(RouteEndpoint::AboveRoot)
            .targets(vec![RouteEndpoint::component("test/archivist")]),
        )
        .await?;
    Ok(builder)
}

pub async fn add_eager_component(
    builder: &RealmBuilder,
    name: &str,
    url: &str,
) -> Result<(), Error> {
    let path = format!("test/{}", name);
    builder
        .add_child(path.as_ref(), url, ChildProperties::new().eager())
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.logger.LogSink")
                .source(RouteEndpoint::component("test/archivist"))
                .targets(vec![RouteEndpoint::component(path)]),
        )
        .await?;
    Ok(())
}

pub async fn add_child(builder: &RealmBuilder, name: &str, url: &str) -> Result<(), Error> {
    let path = format!("test/{}", name);
    builder
        .add_child(path.as_ref(), url, ChildProperties::new())
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.logger.LogSink")
                .source(RouteEndpoint::component("test/archivist"))
                .targets(vec![RouteEndpoint::component(path)]),
        )
        .await?;
    Ok(())
}

pub async fn expose_test_realm_protocol(builder: &RealmBuilder) {
    let mut test_decl = builder.get_decl("test").await.unwrap();
    test_decl.exposes.push(ExposeDecl::Protocol(ExposeProtocolDecl {
        source: ExposeSource::Framework,
        source_name: "fuchsia.component.Realm".into(),
        target: ExposeTarget::Parent,
        target_name: "fuchsia.component.Realm".into(),
    }));
    builder.set_decl("test", test_decl).await.unwrap();
    let mut root_decl = builder.get_decl(Moniker::root()).await.unwrap();
    root_decl.exposes.push(ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
        source: ExposeSource::Child("test".to_string()),
        source_name: "fuchsia.component.Realm".into(),
        target: ExposeTarget::Parent,
        target_name: "fuchsia.component.Realm".into(),
    }));
    builder.set_decl(Moniker::root(), root_decl).await.unwrap();
}
