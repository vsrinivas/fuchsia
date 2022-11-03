// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::mocks;
use anyhow::Error;
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};
use futures::{channel::mpsc, lock::Mutex, StreamExt};
use std::sync::Arc;

const MOCK_COBALT_URL: &str = "#meta/mock_cobalt.cm";
const SINGLE_COUNTER_URL: &str = "#meta/single_counter_test_component.cm";
const SAMPLER_URL: &str = "#meta/sampler.cm";
const ARCHIVIST_URL: &str = "#meta/archivist-for-embedding.cm";

pub async fn create() -> Result<RealmInstance, Error> {
    let builder = RealmBuilder::new().await?;
    let mocks_server = builder
        .add_local_child(
            "mocks-server",
            move |handles| Box::pin(serve_mocks(handles)),
            ChildOptions::new(),
        )
        .await?;
    let wrapper_realm = builder.add_child_realm("wrapper", ChildOptions::new()).await?;
    let mock_cobalt =
        wrapper_realm.add_child("mock_cobalt", MOCK_COBALT_URL, ChildOptions::new()).await?;
    let single_counter =
        wrapper_realm.add_child("single_counter", SINGLE_COUNTER_URL, ChildOptions::new()).await?;
    let sampler = wrapper_realm.add_child("sampler", SAMPLER_URL, ChildOptions::new()).await?;
    let test_case_archivist =
        wrapper_realm.add_child("test_case_archivist", ARCHIVIST_URL, ChildOptions::new()).await?;

    wrapper_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name(
                    "fuchsia.metrics.test.MetricEventLoggerQuerier",
                ))
                .from(&mock_cobalt)
                .to(Ref::parent()),
        )
        .await?;
    wrapper_realm
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
                .capability(Capability::protocol_by_name(
                    "fuchsia.metrics.test.MetricEventLoggerQuerier",
                ))
                .capability(Capability::protocol_by_name(
                    "fuchsia.samplertestcontroller.SamplerTestController",
                ))
                .from(&wrapper_realm)
                .to(Ref::parent()),
        )
        .await?;

    wrapper_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name(
                    "fuchsia.metrics.MetricEventLoggerFactory",
                ))
                .from(&mock_cobalt)
                .to(&sampler),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name(
                    "fuchsia.hardware.power.statecontrol.RebootMethodsWatcherRegister",
                ))
                .from(&mocks_server)
                .to(&wrapper_realm),
        )
        .await?;
    wrapper_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name(
                    "fuchsia.hardware.power.statecontrol.RebootMethodsWatcherRegister",
                ))
                .from(Ref::parent())
                .to(&sampler),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name(
                    "fuchsia.mockrebootcontroller.MockRebootController",
                ))
                .from(&mocks_server)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&wrapper_realm),
        )
        .await?;
    wrapper_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .from(Ref::parent())
                .to(&test_case_archivist)
                .to(&mock_cobalt)
                .to(&sampler)
                .to(&single_counter),
        )
        .await?;
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
    builder
        .add_route(
            Route::new()
                .capability(
                    Capability::event_stream("directory_ready_v2").with_scope(&wrapper_realm),
                )
                .capability(
                    Capability::event_stream("capability_requested_v2").with_scope(&wrapper_realm),
                )
                .from(Ref::parent())
                .to(&wrapper_realm),
        )
        .await?;
    wrapper_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.sys2.EventSource"))
                .from(Ref::parent())
                .to(&test_case_archivist),
        )
        .await?;
    wrapper_realm
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.diagnostics.ArchiveAccessor"))
                .capability(Capability::protocol_by_name("fuchsia.logger.Log"))
                .from(&test_case_archivist)
                .to(&sampler),
        )
        .await?;
    wrapper_realm
        .add_route(
            Route::new()
                .capability(Capability::event_stream("directory_ready_v2"))
                .capability(Capability::event_stream("capability_requested_v2"))
                .from(Ref::parent())
                .to(&test_case_archivist),
        )
        .await?;

    wrapper_realm
        .add_route(
            Route::new()
                .capability(
                    Capability::protocol_by_name("fuchsia.component.Binder")
                        .as_("fuchsia.component.SamplerBinder"),
                )
                .from(&sampler)
                .to(Ref::parent()),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.component.SamplerBinder"))
                .from(&wrapper_realm)
                .to(Ref::parent()),
        )
        .await?;

    let instance = builder.build().await?;
    Ok(instance)
}

async fn serve_mocks(handles: LocalComponentHandles) -> Result<(), Error> {
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
    fs.serve_connection(handles.outgoing_dir)?;
    fs.collect::<()>().await;
    Ok(())
}
