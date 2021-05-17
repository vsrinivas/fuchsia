// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use cm_rust;
use fidl_fuchsia_io2 as fio2;
use fuchsia_component_test::{builder::*, error::Error};

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
        .add_component("test/archivist", ComponentSource::url(opts.archivist_url))
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
                Event::capability_ready("diagnostics"),
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
            source: RouteEndpoint::component("test"),
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
        })?;
    Ok(builder)
}

pub async fn add_component(builder: &mut RealmBuilder, name: &str, url: &str) -> Result<(), Error> {
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
