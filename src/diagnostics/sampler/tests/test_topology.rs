// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mocks;
use anyhow::Error;
use cm_rust;
use fidl_fuchsia_io2 as fio2;
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::{builder::*, mock::*, Moniker, RealmInstance};
use futures::StreamExt;
use std::sync::{Arc, Mutex};

const MOCK_COBALT_URL: &str = "fuchsia-pkg://fuchsia.com/mock_cobalt#meta/mock_cobalt.cm";
const SINGLE_COUNTER_URL: &str =
    "fuchsia-pkg://fuchsia.com/sampler-integration-tests#meta/single_counter_test_component.cm";
const SAMPLER_URL: &str =
    "fuchsia-pkg://fuchsia.com/sampler-integration-tests#meta/sampler-for-test.cm";
const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-for-embedding#meta/archivist-for-embedding.cm";

pub async fn create() -> Result<RealmInstance, Error> {
    let mut builder = RealmBuilder::new().await?;
    builder
        .add_component(
            "mocks-server",
            ComponentSource::Mock(Mock::new(move |mock_handles| {
                Box::pin(serve_mocks(mock_handles))
            })),
        )
        .await?
        .add_component("wrapper/mock_cobalt", ComponentSource::url(MOCK_COBALT_URL))
        .await?
        .add_component("wrapper/single_counter", ComponentSource::url(SINGLE_COUNTER_URL))
        .await?
        .add_component("wrapper/sampler", ComponentSource::url(SAMPLER_URL))
        .await?
        .add_component("wrapper/test_case_archivist", ComponentSource::url(ARCHIVIST_URL))
        .await?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.cobalt.test.LoggerQuerier"),
            source: RouteEndpoint::component("wrapper/mock_cobalt"),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.samplertestcontroller.SamplerTestController"),
            source: RouteEndpoint::component("wrapper/single_counter"),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.cobalt.LoggerFactory"),
            source: RouteEndpoint::component("wrapper/mock_cobalt"),
            targets: vec![RouteEndpoint::component("wrapper/sampler")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.metrics.MetricEventLoggerFactory"),
            source: RouteEndpoint::component("wrapper/mock_cobalt"),
            targets: vec![RouteEndpoint::component("wrapper/sampler")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol(
                "fuchsia.hardware.power.statecontrol.RebootMethodsWatcherRegister",
            ),
            source: RouteEndpoint::component("mocks-server"),
            targets: vec![RouteEndpoint::component("wrapper/sampler")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.mockrebootcontroller.MockRebootController"),
            source: RouteEndpoint::component("mocks-server"),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![
                RouteEndpoint::component("wrapper/test_case_archivist"),
                RouteEndpoint::component("wrapper/mock_cobalt"),
                RouteEndpoint::component("wrapper/sampler"),
                RouteEndpoint::component("wrapper/single_counter"),
            ],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("config-data", "", fio2::R_STAR_DIR),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("wrapper/sampler")],
        })?
        // TODO(fxbug.dev/76599): refactor these tests to use the single test archivist and remove
        // this archivist. We can also remove the `wrapper` realm when this is done. The
        // ArchiveAccessor and Log protocols routed here would be routed from AboveRoot instead. To
        // do so, uncomment the following routes and delete all the routes after this comment
        // involving "wrapper/test_case_archivist":
        // .add_route(CapabilityRoute {
        //     capability: Capability::protocol("fuchsia.diagnostics.ArchiveAccessor"),
        //     source: RouteEndpoint::AboveRoot,
        //     targets: vec![RouteEndpoint::component("wrapper/sampler")],
        // })?
        // .add_route(CapabilityRoute {
        //     capability: Capability::protocol("fuchsia.logger.Log"),
        //     source: RouteEndpoint::AboveRoot,
        //     targets: vec![RouteEndpoint::component("wrapper/sampler")],
        // })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.sys2.EventSource"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component("wrapper/test_case_archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Started, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("wrapper"),
            targets: vec![RouteEndpoint::component("wrapper/test_case_archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Stopped, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("wrapper"),
            targets: vec![RouteEndpoint::component("wrapper/test_case_archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Running, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("wrapper"),
            targets: vec![RouteEndpoint::component("wrapper/test_case_archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(
                Event::directory_ready("diagnostics"),
                cm_rust::EventMode::Async,
            ),
            source: RouteEndpoint::component("wrapper"),
            targets: vec![RouteEndpoint::component("wrapper/test_case_archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(
                Event::capability_requested("fuchsia.logger.LogSink"),
                cm_rust::EventMode::Async,
            ),
            source: RouteEndpoint::component("wrapper"),
            targets: vec![RouteEndpoint::component("wrapper/test_case_archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.diagnostics.ArchiveAccessor"),
            source: RouteEndpoint::component("wrapper/test_case_archivist"),
            targets: vec![RouteEndpoint::component("wrapper/sampler")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.Log"),
            source: RouteEndpoint::component("wrapper/test_case_archivist"),
            targets: vec![RouteEndpoint::component("wrapper/sampler")],
        })?;
    let mut realm = builder.build();

    // TODO(fxbug.dev/82734): RealmBuilder currently doesn't support renaming capabilities, so we
    // need to manually do it here.
    let mut wrapper_decl = realm.get_decl(&"wrapper".into()).await.unwrap();
    wrapper_decl.exposes.push(cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
        source: cm_rust::ExposeSource::Child("sampler".into()),
        target: cm_rust::ExposeTarget::Parent,
        source_name: "fuchsia.component.Binder".into(),
        target_name: "fuchsia.component.SamplerBinder".into(),
    }));
    realm.set_component(&"wrapper".into(), wrapper_decl).await.unwrap();

    let mut root_decl = realm.get_decl(&Moniker::root()).await.unwrap();
    root_decl.exposes.push(cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
        source: cm_rust::ExposeSource::Child("wrapper".into()),
        target: cm_rust::ExposeTarget::Parent,
        source_name: "fuchsia.component.SamplerBinder".into(),
        target_name: "fuchsia.component.SamplerBinder".into(),
    }));
    realm.set_component(&Moniker::root(), root_decl).await.unwrap();

    let instance = realm.create().await?;
    Ok(instance)
}

async fn serve_mocks(mock_handles: MockHandles) -> Result<(), Error> {
    let mut fs = ServiceFs::new();

    let state: Arc<Mutex<mocks::ControllerState>> = mocks::ControllerState::new();
    let controller_state = state.clone();

    fs.dir("svc")
        .add_fidl_service(move |stream| {
            mocks::serve_reboot_server(stream, controller_state.clone());
        })
        .add_fidl_service(move |stream| {
            mocks::serve_reboot_controller(stream, state.clone());
        });
    fs.serve_connection(mock_handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}
