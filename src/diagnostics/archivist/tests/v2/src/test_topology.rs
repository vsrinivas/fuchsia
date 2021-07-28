// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cm_rust;
use cm_rust::{ExposeDecl, ExposeProtocolDecl, ExposeSource, ExposeTarget};
use fidl_fuchsia_io2 as fio2;
use fuchsia_component_test::{builder::*, error::Error, Moniker, Realm};

const INTEGRATION_ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests-v2#meta/archivist.cm";

/// Options for creating a test topology.
pub struct Options {
    /// The URL of the archivist to be used in the test.
    pub archivist_url: &'static str,
}

impl Default for Options {
    fn default() -> Self {
        Self { archivist_url: INTEGRATION_ARCHIVIST_URL }
    }
}

/// Creates a new topology for tests with an archivist inside.
pub async fn create(opts: Options) -> Result<RealmBuilder, Error> {
    let mut builder = RealmBuilder::new().await?;
    builder
        .add_eager_component("test/archivist", ComponentSource::url(opts.archivist_url))
        .await?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("test/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("config-data", "", fio2::R_STAR_DIR),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("test/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.sys2.EventSource"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("test/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Started, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("test"),
            targets: vec![RouteEndpoint::component("test/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Stopped, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("test"),
            targets: vec![RouteEndpoint::component("test/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Running, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("test"),
            targets: vec![RouteEndpoint::component("test/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(
                Event::directory_ready("diagnostics"),
                cm_rust::EventMode::Async,
            ),
            source: RouteEndpoint::component("test"),
            targets: vec![RouteEndpoint::component("test/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(
                Event::capability_requested("fuchsia.logger.LogSink"),
                cm_rust::EventMode::Async,
            ),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("test/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.diagnostics.ArchiveAccessor"),
            source: RouteEndpoint::component("test/archivist"),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.diagnostics.FeedbackArchiveAccessor"),
            source: RouteEndpoint::component("test/archivist"),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.diagnostics.LegacyMetricsArchiveAccessor"),
            source: RouteEndpoint::component("test/archivist"),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::component("test/archivist"),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.Log"),
            source: RouteEndpoint::component("test/archivist"),
            targets: vec![RouteEndpoint::AboveRoot],
        })?;
    Ok(builder)
}

pub async fn add_eager_component(
    builder: &mut RealmBuilder,
    name: &str,
    url: &str,
) -> Result<(), Error> {
    let path = format!("test/{}", name);
    builder.add_eager_component(path.as_ref(), ComponentSource::url(url)).await?.add_route(
        CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::component("test/archivist"),
            targets: vec![RouteEndpoint::component(path)],
        },
    )?;
    Ok(())
}

pub async fn add_component(builder: &mut RealmBuilder, name: &str, url: &str) -> Result<(), Error> {
    let path = format!("test/{}", name);
    builder.add_component(path.as_ref(), ComponentSource::url(url)).await?.add_route(
        CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::component("test/archivist"),
            targets: vec![RouteEndpoint::component(path)],
        },
    )?;
    Ok(())
}

pub async fn expose_test_realm_protocol(realm: &mut Realm) {
    let mut test_decl = realm.get_decl(&"test".into()).await.unwrap();
    test_decl.exposes.push(ExposeDecl::Protocol(ExposeProtocolDecl {
        source: ExposeSource::Framework,
        source_name: "fuchsia.sys2.Realm".into(),
        target: ExposeTarget::Parent,
        target_name: "fuchsia.sys2.Realm".into(),
    }));
    realm.set_component(&"test".into(), test_decl).await.unwrap();
    let mut root_decl = realm.get_decl(&Moniker::root()).await.unwrap();
    root_decl.exposes.push(ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
        source: ExposeSource::Child("test".to_string()),
        source_name: "fuchsia.sys2.Realm".into(),
        target: ExposeTarget::Parent,
        target_name: "fuchsia.sys2.Realm".into(),
    }));
    realm.set_component(&Moniker::root(), root_decl).await.unwrap();
}
