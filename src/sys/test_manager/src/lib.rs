// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::*,
    anyhow::Error,
    cm_rust,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_diagnostics as fdiagnostics, fidl_fuchsia_io2 as fio2, fidl_fuchsia_test as ftest,
    fidl_fuchsia_test_manager as ftest_manager,
    ftest::SuiteMarker,
    ftest_manager::{LaunchError, SuiteControllerRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    lazy_static::lazy_static,
    std::sync::Arc,
    topology_builder::{
        builder::{
            Capability, CapabilityRoute, ComponentSource, Event, RouteEndpoint, TopologyBuilder,
        },
        error::Error as TopologyBuilderError,
        mock::{Mock, MockHandles},
        Topology, TopologyInstance,
    },
    tracing::{error, warn},
};

mod diagnostics;
mod error;

pub const TEST_ROOT_REALM_NAME: &'static str = "test_root";
pub const WRAPPER_ROOT_REALM_PATH: &'static str = "test_wrapper/test_root";

lazy_static! {
    static ref ARCHIVIST_FOR_EMBEDDING_URL: &'static str =
        "fuchsia-pkg://fuchsia.com/test_manager#meta/archivist-for-embedding.cm";
    static ref READ_RIGHTS: fio2::Operations = fio2::Operations::Connect
        | fio2::Operations::Enumerate
        | fio2::Operations::Traverse
        | fio2::Operations::ReadBytes
        | fio2::Operations::GetAttributes;
    static ref READ_WRITE_RIGHTS: fio2::Operations = fio2::Operations::Connect
        | fio2::Operations::Enumerate
        | fio2::Operations::Traverse
        | fio2::Operations::ReadBytes
        | fio2::Operations::WriteBytes
        | fio2::Operations::ModifyDirectory
        | fio2::Operations::GetAttributes
        | fio2::Operations::UpdateAttributes;
    static ref ADMIN_RIGHTS: fio2::Operations = fio2::Operations::Admin;
}

/// Start test manager and serve it over `stream`.
pub async fn run_test_manager(
    mut stream: ftest_manager::HarnessRequestStream,
) -> Result<(), TestManagerError> {
    while let Some(event) = stream.try_next().await.map_err(TestManagerError::Stream)? {
        match event {
            ftest_manager::HarnessRequest::LaunchSuite {
                test_url,
                options: _,
                suite,
                controller,
                responder,
            } => {
                let controller = match controller.into_stream() {
                    Err(error) => {
                        error!(%error, component_url = %test_url, "invalid controller channel");
                        responder
                            .send(&mut Err(LaunchError::InvalidArgs))
                            .map_err(TestManagerError::Response)?;
                        // process next request
                        continue;
                    }
                    Ok(c) => c,
                };

                match launch_test(&test_url, suite).await {
                    Ok(test) => {
                        responder.send(&mut Ok(())).map_err(TestManagerError::Response)?;
                        fasync::Task::spawn(async move {
                            test.serve_controller(controller).await.unwrap_or_else(|error| {
                                error!(%error, component_url = %test_url, "serve_controller failed");
                            });
                        })
                        .detach();
                    }
                    Err(e) => {
                        error!("Failed to launch test: {:?}", e);
                        responder.send(&mut Err(e.into())).map_err(TestManagerError::Response)?;
                    }
                }
            }
        }
    }
    Ok(())
}

struct RunningTest {
    instance: TopologyInstance,
    // TODO: use this archive accessor to fetch isolated logs.
    _archive_accessor: Arc<fdiagnostics::ArchiveAccessorProxy>,
}

impl RunningTest {
    async fn destroy(mut self) {
        let destroy_waiter = self.instance.root.take_destroy_waiter();
        drop(self);
        destroy_waiter.await;
    }

    /// Serves Suite controller and destroys this test afterwards.
    pub async fn serve_controller(
        self,
        mut stream: SuiteControllerRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await? {
            match event {
                ftest_manager::SuiteControllerRequest::Kill { .. } => {
                    self.destroy().await;
                    return Ok(());
                }
            }
        }

        self.destroy().await;
        Ok(())
    }
}

/// Launch test and return the name of test used to launch it in collection.
async fn launch_test(
    test_url: &str,
    suite_request: ServerEnd<SuiteMarker>,
) -> Result<RunningTest, LaunchTestError> {
    // This archive accessor will be served by the embedded archivist.
    let (archive_accessor, archive_accessor_server_end) =
        fidl::endpoints::create_proxy::<fdiagnostics::ArchiveAccessorMarker>()
            .map_err(LaunchTestError::CreateProxyForArchiveAccessor)?;

    let archive_accessor_arc = Arc::new(archive_accessor);
    let mut topology = get_topology(archive_accessor_arc.clone(), test_url)
        .await
        .map_err(LaunchTestError::InitializeTestTopology)?;
    topology.set_collection_name("tests");
    let topology_instance = topology.create().await.map_err(LaunchTestError::CreateTestTopology)?;
    topology_instance
        .root
        .connect_request_to_protocol_at_exposed_dir::<fdiagnostics::ArchiveAccessorMarker>(
            archive_accessor_server_end,
        )
        .map_err(LaunchTestError::ConnectToArchiveAccessor)?;
    topology_instance
        .root
        .connect_request_to_protocol_at_exposed_dir(suite_request)
        .map_err(LaunchTestError::ConnectToTestSuite)?;

    Ok(RunningTest { instance: topology_instance, _archive_accessor: archive_accessor_arc })
}

async fn get_topology(
    archive_accessor: Arc<fdiagnostics::ArchiveAccessorProxy>,
    test_url: &str,
) -> Result<Topology, TopologyBuilderError> {
    let mut builder = TopologyBuilder::new().await?;
    let test_wrapper_test_root = format!("test_wrapper/{}", TEST_ROOT_REALM_NAME);
    builder
        .add_eager_component(test_wrapper_test_root.as_ref(), ComponentSource::url(test_url))
        .await?
        .add_component(
            "mocks-server",
            ComponentSource::Mock(Mock::new(move |mock_handles| {
                Box::pin(serve_mocks(archive_accessor.clone(), mock_handles))
            })),
        )
        .await?
        .add_component("test_wrapper/archivist", ComponentSource::url(*ARCHIVIST_FOR_EMBEDDING_URL))
        .await?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.process.Launcher"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.boot.WriteOnlyLog"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.sys2.EventSource"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![
                RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH),
                RouteEndpoint::component("test_wrapper/archivist"),
            ],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::storage("temp", "/tmp"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.logger.LogSink"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![
                RouteEndpoint::component("test_wrapper/archivist"),
                // TODO(fxbug.dev/67960): use embedded archivist LogSink)
                RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH),
            ],
        })?
        // TODO(fxbug.dev/67960): uncomment when fetching isolated logs is implemented.
        // .add_route(CapabilityRoute {
        //     capability: Capability::protocol("fuchsia.logger.LogSink"),
        //     source: RouteEndpoint::component("test_wrapper/archivist"),
        //     targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        // })?
        // .add_route(CapabilityRoute {
        //     capability: Capability::protocol("fuchsia.logger.Log"),
        //     source: RouteEndpoint::component("test_wrapper/archivist"),
        //     targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        // })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.diagnostics.ArchiveAccessor"),
            source: RouteEndpoint::component("mocks-server"),
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.diagnostics.ArchiveAccessor"),
            source: RouteEndpoint::component("test_wrapper/archivist"),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Started, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("test_wrapper"),
            targets: vec![RouteEndpoint::component("test_wrapper/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Stopped, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("test_wrapper"),
            targets: vec![RouteEndpoint::component("test_wrapper/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(Event::Running, cm_rust::EventMode::Async),
            source: RouteEndpoint::component("test_wrapper"),
            targets: vec![RouteEndpoint::component("test_wrapper/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(
                Event::capability_ready("diagnostics"),
                cm_rust::EventMode::Async,
            ),
            source: RouteEndpoint::component("test_wrapper"),
            targets: vec![RouteEndpoint::component("test_wrapper/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::Event(
                Event::capability_requested("fuchsia.logger.LogSink"),
                cm_rust::EventMode::Async,
            ),
            source: RouteEndpoint::component("test_wrapper"),
            targets: vec![RouteEndpoint::component("test_wrapper/archivist")],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.test.Suite"),
            source: RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH),
            targets: vec![RouteEndpoint::AboveRoot],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.hardware.display.Provider"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.scheduler.ProfileProvider"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.sysmem.Allocator"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::protocol("fuchsia.tracing.provider.Registry"),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("config-ssl", "", *READ_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("config-data", "", *READ_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory(
                "deprecated-tmp",
                "",
                *ADMIN_RIGHTS | *READ_WRITE_RIGHTS,
            ),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("dev-input-report", "", *READ_WRITE_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?
        .add_route(CapabilityRoute {
            capability: Capability::directory("dev-display-controller", "", *READ_WRITE_RIGHTS),
            source: RouteEndpoint::AboveRoot,
            targets: vec![RouteEndpoint::component(WRAPPER_ROOT_REALM_PATH)],
        })?;

    Ok(builder.build())
}

async fn serve_mocks(
    archive_accessor: Arc<fdiagnostics::ArchiveAccessorProxy>,
    mock_handles: MockHandles,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    fs.dir("svc").add_fidl_service(move |stream| {
        let archive_accessor_clone = archive_accessor.clone();
        fasync::Task::spawn(async move {
            diagnostics::run_intermediary_archive_accessor(archive_accessor_clone, stream)
                .await
                .unwrap_or_else(|e| {
                    warn!("Couldn't run proxied ArchiveAccessor: {:?}", e);
                })
        })
        .detach()
    });
    fs.serve_connection(mock_handles.outgoing_dir.into_channel())?;
    fs.collect::<()>().await;
    Ok(())
}
