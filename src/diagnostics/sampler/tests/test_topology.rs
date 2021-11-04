// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mocks;
use anyhow::Error;
use cm_rust;
use fidl_fuchsia_io2 as fio2;
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::{
    mock::MockHandles, ChildProperties, Event, Moniker, RealmBuilder, RealmInstance, RouteBuilder,
    RouteEndpoint,
};
use futures::{channel::mpsc, lock::Mutex, StreamExt};
use std::sync::Arc;

const MOCK_COBALT_URL: &str = "fuchsia-pkg://fuchsia.com/mock_cobalt#meta/mock_cobalt.cm";
const SINGLE_COUNTER_URL: &str =
    "fuchsia-pkg://fuchsia.com/sampler-integration-tests#meta/single_counter_test_component.cm";
const SAMPLER_URL: &str =
    "fuchsia-pkg://fuchsia.com/sampler-integration-tests#meta/sampler-for-test.cm";
const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-for-embedding#meta/archivist-for-embedding.cm";

pub async fn create() -> Result<RealmInstance, Error> {
    let builder = RealmBuilder::new().await?;
    builder
        .add_mock_child(
            "mocks-server",
            move |mock_handles| Box::pin(serve_mocks(mock_handles)),
            ChildProperties::new(),
        )
        .await?
        .add_child("wrapper/mock_cobalt", MOCK_COBALT_URL, ChildProperties::new())
        .await?
        .add_child("wrapper/single_counter", SINGLE_COUNTER_URL, ChildProperties::new())
        .await?
        .add_child("wrapper/sampler", SAMPLER_URL, ChildProperties::new())
        .await?
        .add_child("wrapper/test_case_archivist", ARCHIVIST_URL, ChildProperties::new())
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.cobalt.test.LoggerQuerier")
                .source(RouteEndpoint::component("wrapper/mock_cobalt"))
                .targets(vec![RouteEndpoint::AboveRoot]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.samplertestcontroller.SamplerTestController")
                .source(RouteEndpoint::component("wrapper/single_counter"))
                .targets(vec![RouteEndpoint::AboveRoot]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.cobalt.LoggerFactory")
                .source(RouteEndpoint::component("wrapper/mock_cobalt"))
                .targets(vec![RouteEndpoint::component("wrapper/sampler")]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.metrics.MetricEventLoggerFactory")
                .source(RouteEndpoint::component("wrapper/mock_cobalt"))
                .targets(vec![RouteEndpoint::component("wrapper/sampler")]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol(
                "fuchsia.hardware.power.statecontrol.RebootMethodsWatcherRegister",
            )
            .source(RouteEndpoint::component("mocks-server"))
            .targets(vec![RouteEndpoint::component("wrapper/sampler")]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.mockrebootcontroller.MockRebootController")
                .source(RouteEndpoint::component("mocks-server"))
                .targets(vec![RouteEndpoint::AboveRoot]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.logger.LogSink")
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![
                    RouteEndpoint::component("wrapper/test_case_archivist"),
                    RouteEndpoint::component("wrapper/mock_cobalt"),
                    RouteEndpoint::component("wrapper/sampler"),
                    RouteEndpoint::component("wrapper/single_counter"),
                ]),
        )
        .await?
        .add_route(
            RouteBuilder::directory("config-data", "", fio2::R_STAR_DIR)
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("wrapper/sampler")]),
        )
        .await?
        // TODO(fxbug.dev/76599): refactor these tests to use the single test archivist and remove
        // this archivist. We can also remove the `wrapper` realm when this is done. The
        // ArchiveAccessor and Log protocols routed here would be routed from AboveRoot instead. To
        // do so, uncomment the following routes and delete all the routes after this comment
        // involving "wrapper/test_case_archivist":
        // .add_route(RouteBuilder::protocol("fuchsia.diagnostics.ArchiveAccessor")
        //     .source(RouteEndpoint::AboveRoot)
        //     .targets(vec![RouteEndpoint::component("wrapper/sampler")])
        // }).await?
        // .add_route(RouteBuilder::protocol("fuchsia.logger.Log")
        //     .source(RouteEndpoint::AboveRoot)
        //     .targets(vec![RouteEndpoint::component("wrapper/sampler")])
        // }).await?
        .add_route(
            RouteBuilder::protocol("fuchsia.sys2.EventSource")
                .source(RouteEndpoint::AboveRoot)
                .targets(vec![RouteEndpoint::component("wrapper/test_case_archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.diagnostics.ArchiveAccessor")
                .source(RouteEndpoint::component("wrapper/test_case_archivist"))
                .targets(vec![RouteEndpoint::component("wrapper/sampler")]),
        )
        .await?
        .add_route(
            RouteBuilder::protocol("fuchsia.logger.Log")
                .source(RouteEndpoint::component("wrapper/test_case_archivist"))
                .targets(vec![RouteEndpoint::component("wrapper/sampler")]),
        )
        .await?
        .add_route(
            RouteBuilder::event(Event::Started, cm_rust::EventMode::Async)
                .source(RouteEndpoint::component("wrapper"))
                .targets(vec![RouteEndpoint::component("wrapper/test_case_archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::event(Event::Stopped, cm_rust::EventMode::Async)
                .source(RouteEndpoint::component("wrapper"))
                .targets(vec![RouteEndpoint::component("wrapper/test_case_archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::event(Event::Running, cm_rust::EventMode::Async)
                .source(RouteEndpoint::component("wrapper"))
                .targets(vec![RouteEndpoint::component("wrapper/test_case_archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::event(Event::directory_ready("diagnostics"), cm_rust::EventMode::Async)
                .source(RouteEndpoint::component("wrapper"))
                .targets(vec![RouteEndpoint::component("wrapper/test_case_archivist")]),
        )
        .await?
        .add_route(
            RouteBuilder::event(
                Event::capability_requested("fuchsia.logger.LogSink"),
                cm_rust::EventMode::Async,
            )
            .source(RouteEndpoint::component("wrapper"))
            .targets(vec![RouteEndpoint::component("wrapper/test_case_archivist")]),
        )
        .await?;

    // TODO(fxbug.dev/82734): RealmBuilder currently doesn't support renaming capabilities, so we
    // need to manually do it here.
    let mut wrapper_decl = builder.get_decl("wrapper").await.unwrap();
    wrapper_decl.exposes.push(cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
        source: cm_rust::ExposeSource::Child("sampler".into()),
        target: cm_rust::ExposeTarget::Parent,
        source_name: "fuchsia.component.Binder".into(),
        target_name: "fuchsia.component.SamplerBinder".into(),
    }));
    builder.set_decl("wrapper", wrapper_decl).await.unwrap();

    let mut root_decl = builder.get_decl(Moniker::root()).await.unwrap();
    root_decl.exposes.push(cm_rust::ExposeDecl::Protocol(cm_rust::ExposeProtocolDecl {
        source: cm_rust::ExposeSource::Child("wrapper".into()),
        target: cm_rust::ExposeTarget::Parent,
        source_name: "fuchsia.component.SamplerBinder".into(),
        target_name: "fuchsia.component.SamplerBinder".into(),
    }));
    builder.set_decl(Moniker::root(), root_decl).await.unwrap();

    let instance = builder.build().await?;
    Ok(instance)
}

async fn serve_mocks(mock_handles: MockHandles) -> Result<(), Error> {
    let mut fs = ServiceFs::new();

    let (snd, rcv) = mpsc::channel(1);
    let rcv = Arc::new(Mutex::new(rcv));

    fs.dir("svc")
        .add_fidl_service(move |stream| {
            mocks::serve_reboot_server(stream, snd.clone());
        })
        .add_fidl_service(move |stream| {
            mocks::serve_reboot_controller(stream, rcv.clone());
        });
    fs.serve_connection(mock_handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}
