// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    fidl_fuchsia_paver::{
        BootManagerRequest, BootManagerRequestStream, PaverRequest, PaverRequestStream,
    },
    fidl_fuchsia_update::{ManagerMarker, ManagerProxy, ManagerState},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_zircon::Status,
    futures::{channel::mpsc, prelude::*},
    parking_lot::Mutex,
    std::sync::Arc,
};

const OMAHA_CLIENT_CMX: &str =
    "fuchsia-pkg://fuchsia.com/omaha-client-integration-tests#meta/omaha-client-service.cmx";

struct Proxies {
    paver: Arc<MockPaver>,
    update_manager: ManagerProxy,
}

struct TestEnv {
    _env: NestedEnvironment,
    proxies: Proxies,
    _omaha_client: App,
}

impl TestEnv {
    fn new(paver: MockPaver) -> Self {
        let mut fs = ServiceFs::new();

        let paver = Arc::new(paver);
        let paver_clone = Arc::clone(&paver);
        fs.add_fidl_service(move |stream: PaverRequestStream| {
            let paver_clone = Arc::clone(&paver_clone);
            fasync::spawn(paver_clone.run_service(stream));
        });

        let env = fs
            .create_salted_nested_environment("omaha_client_integration_test_env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        let omaha_client = AppBuilder::new(OMAHA_CLIENT_CMX)
            .spawn(env.launcher())
            .expect("omaha_client to launch");

        Self {
            _env: env,
            proxies: Proxies {
                paver,
                update_manager: omaha_client
                    .connect_to_service::<ManagerMarker>()
                    .expect("connect to update manager"),
            },
            _omaha_client: omaha_client,
        }
    }
}

struct MockPaver {
    response: Status,
    set_active_configuration_healthy_was_called_sender: mpsc::UnboundedSender<()>,
    set_active_configuration_healthy_was_called: Mutex<mpsc::UnboundedReceiver<()>>,
}

impl MockPaver {
    fn new(response: Status) -> Self {
        let (
            set_active_configuration_healthy_was_called_sender,
            set_active_configuration_healthy_was_called,
        ) = mpsc::unbounded();
        Self {
            response,
            set_active_configuration_healthy_was_called_sender,
            set_active_configuration_healthy_was_called: Mutex::new(
                set_active_configuration_healthy_was_called,
            ),
        }
    }
    async fn run_service(self: Arc<Self>, mut stream: PaverRequestStream) {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                PaverRequest::FindBootManager { boot_manager, .. } => {
                    let mock_paver_clone = self.clone();
                    fasync::spawn(
                        mock_paver_clone
                            .run_boot_manager_service(boot_manager.into_stream().unwrap()),
                    );
                }
                req => println!("mock Paver ignoring request: {:?}", req),
            }
        }
    }
    async fn run_boot_manager_service(self: Arc<Self>, mut stream: BootManagerRequestStream) {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                BootManagerRequest::SetActiveConfigurationHealthy { responder } => {
                    self.set_active_configuration_healthy_was_called_sender
                        .unbounded_send(())
                        .expect("mpsc send");
                    responder.send(self.response.clone().into_raw()).expect("send ok");
                }
                req => println!("mock Paver ignoring request: {:?}", req),
            }
        }
    }
}

#[fasync::run_singlethreaded(test)]
// Test will hang if omaha-client does not call paver service
async fn test_calls_paver_service() {
    let env = TestEnv::new(MockPaver::new(Status::OK));

    let mut set_active_configuration_healthy_was_called =
        env.proxies.paver.set_active_configuration_healthy_was_called.lock();
    assert_eq!(set_active_configuration_healthy_was_called.next().await, Some(()));
}

#[fasync::run_singlethreaded(test)]
// Test will hang if omaha-client does not call paver service
async fn test_update_manager_get_state_works_after_paver_service_fails() {
    let env = TestEnv::new(MockPaver::new(Status::INTERNAL));

    let mut set_active_configuration_healthy_was_called =
        env.proxies.paver.set_active_configuration_healthy_was_called.lock();
    set_active_configuration_healthy_was_called.next().await;

    let state = env.proxies.update_manager.get_state().await.expect("get_state");
    assert_eq!(state.state, Some(ManagerState::Idle));
    assert_eq!(state.version_available, None);
}
