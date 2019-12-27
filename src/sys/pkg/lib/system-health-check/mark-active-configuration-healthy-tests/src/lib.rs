// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    anyhow::Error,
    fidl_fuchsia_paver::{
        BootManagerRequest, BootManagerRequestStream, Configuration, PaverRequest,
        PaverRequestStream,
    },
    fidl_fuchsia_sys::LauncherProxy,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::AppBuilder,
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_zircon::Status,
    futures::{future::BoxFuture, prelude::*},
    parking_lot::Mutex,
    std::sync::Arc,
};

const PARTITION_MARKER_CMX: &str = "fuchsia-pkg://fuchsia.com/mark-active-configuration-healthy-tests#meta/mark-active-configuration-healthy.cmx";

struct TestEnv {
    env: NestedEnvironment,
}

impl TestEnv {
    fn launcher(&self) -> &LauncherProxy {
        self.env.launcher()
    }

    fn new(paver_service: Arc<dyn PaverService>) -> Self {
        let mut fs = ServiceFs::new();

        let paver_service_clone = paver_service.clone();
        fs.add_fidl_service(move |stream: PaverRequestStream| {
            let paver_service_clone = paver_service_clone.clone();
            fasync::spawn(
                paver_service_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
            )
        });

        let env = fs
            .create_salted_nested_environment("partition_marker_env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        Self { env }
    }

    async fn run_partition_marker(&self) {
        let launcher = self.launcher();
        let partition_marker = AppBuilder::new(PARTITION_MARKER_CMX);
        let output = partition_marker
            .output(launcher)
            .expect("partition_marker to launch")
            .await
            .expect("no errors while waiting for exit");
        output.ok().expect("component exited with status code 0");
    }
}

#[derive(Debug, PartialEq, Eq)]
enum PaverEvent {
    FindBootManager,
    SetActiveConfigurationHealthy,
    QueryActiveConfiguration,
    SetConfigurationUnbootable { config: Configuration },
}

trait PaverService: Sync + Send {
    fn run_service(
        self: Arc<Self>,
        stream: PaverRequestStream,
    ) -> BoxFuture<'static, Result<(), Error>>;
}

struct PaverServiceState {
    events: Vec<PaverEvent>,
    active_config: Configuration,
}

struct PaverServiceSupportsABR {
    state: Mutex<PaverServiceState>,
}

impl PaverServiceSupportsABR {
    fn new(active_config: Configuration) -> Self {
        Self { state: Mutex::new(PaverServiceState { events: Vec::new(), active_config }) }
    }

    fn set_active_configuration(&self, config: Configuration) {
        self.state.lock().active_config = config;
    }

    fn take_events(&self) -> Vec<PaverEvent> {
        std::mem::replace(&mut self.state.lock().events, vec![])
    }

    fn run_boot_manager_service(
        self: Arc<Self>,
        mut stream: BootManagerRequestStream,
    ) -> BoxFuture<'static, Result<(), Error>> {
        async move {
            while let Some(req) = stream.try_next().await? {
                match req {
                    BootManagerRequest::SetActiveConfigurationHealthy { responder } => {
                        self.state.lock().events.push(PaverEvent::SetActiveConfigurationHealthy);
                        let status = match self.state.lock().active_config {
                            Configuration::Recovery => Status::NOT_SUPPORTED,
                            _ => Status::OK,
                        };
                        responder.send(status.into_raw()).expect("send ok");
                    }
                    BootManagerRequest::QueryActiveConfiguration { responder } => {
                        self.state.lock().events.push(PaverEvent::QueryActiveConfiguration);

                        responder
                            .send(&mut Ok(self.state.lock().active_config))
                            .expect("send config");
                    }
                    BootManagerRequest::SetConfigurationUnbootable { responder, configuration } => {
                        self.state
                            .lock()
                            .events
                            .push(PaverEvent::SetConfigurationUnbootable { config: configuration });
                        responder.send(Status::OK.into_raw()).expect("send ok");
                    }
                    req => panic!("unhandled paver request: {:?}", req),
                }
            }
            Ok(())
        }
        .boxed()
    }
}

impl PaverService for PaverServiceSupportsABR {
    fn run_service(
        self: Arc<Self>,
        mut stream: PaverRequestStream,
    ) -> BoxFuture<'static, Result<(), Error>> {
        async move {
            while let Some(req) = stream.try_next().await? {
                match req {
                    PaverRequest::FindBootManager { boot_manager, .. } => {
                        self.state.lock().events.push(PaverEvent::FindBootManager);
                        let paver_service_clone = self.clone();
                        fasync::spawn(
                            paver_service_clone
                                .run_boot_manager_service(boot_manager.into_stream()?)
                                .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
                        );
                    }
                    req => panic!("unhandled paver request: {:?}", req),
                }
            }
            Ok(())
        }
        .boxed()
    }
}

// We should call SetConfigurationUnbootable when the device supports ABR and is not in recovery
#[fasync::run_singlethreaded(test)]
async fn test_calls_set_configuration_unbootable() {
    // Works when A is active config
    let paver_service = Arc::new(PaverServiceSupportsABR::new(Configuration::A));
    let env = TestEnv::new(paver_service.clone());
    assert_eq!(paver_service.take_events(), Vec::new());
    env.run_partition_marker().await;
    assert_eq!(
        paver_service.take_events(),
        vec![
            PaverEvent::FindBootManager,
            PaverEvent::SetActiveConfigurationHealthy,
            PaverEvent::QueryActiveConfiguration,
            PaverEvent::SetConfigurationUnbootable { config: Configuration::B }
        ]
    );

    // Works when B is active config
    paver_service.set_active_configuration(Configuration::B);
    assert_eq!(paver_service.take_events(), Vec::new());
    env.run_partition_marker().await;
    assert_eq!(
        paver_service.take_events(),
        vec![
            PaverEvent::FindBootManager,
            PaverEvent::SetActiveConfigurationHealthy,
            PaverEvent::QueryActiveConfiguration,
            PaverEvent::SetConfigurationUnbootable { config: Configuration::A }
        ]
    );
}

struct PaverServiceDoesNotSupportsABR {
    events: Mutex<Vec<PaverEvent>>,
}

impl PaverServiceDoesNotSupportsABR {
    fn new() -> Self {
        Self { events: Mutex::new(Vec::new()) }
    }

    fn take_events(&self) -> Vec<PaverEvent> {
        std::mem::replace(&mut *self.events.lock(), vec![])
    }
}

impl PaverService for PaverServiceDoesNotSupportsABR {
    fn run_service(
        self: Arc<Self>,
        mut stream: PaverRequestStream,
    ) -> BoxFuture<'static, Result<(), Error>> {
        async move {
            while let Some(req) = stream.try_next().await? {
                match req {
                    PaverRequest::FindBootManager { boot_manager, .. } => {
                        self.events.lock().push(PaverEvent::FindBootManager);
                        boot_manager.close_with_epitaph(Status::NOT_SUPPORTED)?;
                    }
                    req => panic!("unhandled paver request: {:?}", req),
                }
            }
            Ok(())
        }
        .boxed()
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_calls_exclusively_set_active_configuration_healthy_when_device_does_not_support_abr()
{
    let paver_service = Arc::new(PaverServiceDoesNotSupportsABR::new());
    let env = TestEnv::new(paver_service.clone());
    assert_eq!(paver_service.take_events(), Vec::new());
    env.run_partition_marker().await;
    assert_eq!(paver_service.take_events(), vec![PaverEvent::FindBootManager]);
}

#[fasync::run_singlethreaded(test)]
async fn test_calls_exclusively_set_active_configuration_healthy_when_device_in_recovery() {
    let paver_service = Arc::new(PaverServiceSupportsABR::new(Configuration::Recovery));
    let env = TestEnv::new(paver_service.clone());
    assert_eq!(paver_service.take_events(), Vec::new());
    env.run_partition_marker().await;
    assert_eq!(
        paver_service.take_events(),
        vec![PaverEvent::FindBootManager, PaverEvent::SetActiveConfigurationHealthy]
    );
}
