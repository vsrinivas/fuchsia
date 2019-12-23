// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    failure::Error,
    fidl_fuchsia_paver::{
        BootManagerRequest, BootManagerRequestStream, PaverRequest, PaverRequestStream,
    },
    fidl_fuchsia_update::{ManagerMarker, ManagerProxy, ManagerState},
    fidl_fuchsia_update_channel::{ProviderMarker, ProviderProxy},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_zircon::Status,
    futures::{channel::mpsc, prelude::*},
    parking_lot::Mutex,
    std::{fs::File, sync::Arc},
    tempfile::TempDir,
};

const SYSTEM_UPDATE_CHECKER_CMX: &str = "fuchsia-pkg://fuchsia.com/system-update-checker-integration-tests#meta/system-update-checker-for-integration-test.cmx";

struct Mounts {
    misc_ota: TempDir,
}

struct Proxies {
    paver: Arc<MockPaver>,
    channel_provider: ProviderProxy,
    update_manager: ManagerProxy,
}

impl Mounts {
    fn new() -> Self {
        Self { misc_ota: tempfile::tempdir().expect("/tmp to exist") }
    }
}

struct TestEnv {
    _env: NestedEnvironment,
    _mounts: Mounts,
    proxies: Proxies,
    _system_update_checker: App,
}

impl TestEnv {
    fn new(paver: MockPaver) -> Self {
        let mounts = Mounts::new();

        let mut fs = ServiceFs::new();

        let paver = Arc::new(paver);
        let paver_clone = Arc::clone(&paver);
        fs.add_fidl_service(move |stream: PaverRequestStream| {
            let paver_clone = Arc::clone(&paver_clone);
            fasync::spawn(
                paver_clone
                    .run_service(stream)
                    .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
            )
        });

        let env = fs
            .create_salted_nested_environment("system-update-checker_integration_test_env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        let system_update_checker = AppBuilder::new(SYSTEM_UPDATE_CHECKER_CMX)
            .add_dir_to_namespace(
                "/misc/ota".to_string(),
                File::open(mounts.misc_ota.path()).expect("/misc/ota tempdir to open"),
            )
            .expect("/misc/ota to mount")
            .spawn(env.launcher())
            .expect("system_update_checker to launch");

        Self {
            _env: env,
            _mounts: mounts,
            proxies: Proxies {
                paver,
                channel_provider: system_update_checker
                    .connect_to_service::<ProviderMarker>()
                    .expect("connect to channel provider"),
                update_manager: system_update_checker
                    .connect_to_service::<ManagerMarker>()
                    .expect("connect to update manager"),
            },
            _system_update_checker: system_update_checker,
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
    async fn run_service(self: Arc<Self>, mut stream: PaverRequestStream) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                PaverRequest::FindBootManager { boot_manager, .. } => {
                    let mock_paver_clone = self.clone();
                    fasync::spawn(
                        mock_paver_clone
                            .run_boot_manager_service(boot_manager.into_stream()?)
                            .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
                    );
                }
                req => println!("mock Paver ignoring request: {:?}", req),
            }
        }
        Ok(())
    }
    async fn run_boot_manager_service(
        self: Arc<Self>,
        mut stream: BootManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
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
        Ok(())
    }
}

#[fasync::run_singlethreaded(test)]
// Test will hang if system-update-checker does not call paver service
async fn test_calls_paver_service() {
    let env = TestEnv::new(MockPaver::new(Status::OK));

    let mut set_active_configuration_healthy_was_called =
        env.proxies.paver.set_active_configuration_healthy_was_called.lock();
    assert_eq!(set_active_configuration_healthy_was_called.next().await, Some(()));
}

#[fasync::run_singlethreaded(test)]
// Test will hang if system-update-checker does not call paver service
async fn test_channel_provider_get_current_works_after_paver_service_fails() {
    let env = TestEnv::new(MockPaver::new(Status::INTERNAL));

    let mut set_active_configuration_healthy_was_called =
        env.proxies.paver.set_active_configuration_healthy_was_called.lock();
    set_active_configuration_healthy_was_called.next().await;

    assert_eq!(
        env.proxies.channel_provider.get_current().await.expect("get_current"),
        "".to_string()
    );
}

#[fasync::run_singlethreaded(test)]
// Test will hang if system-update-checker does not call paver service
async fn test_update_manager_get_state_works_after_paver_service_fails() {
    let env = TestEnv::new(MockPaver::new(Status::INTERNAL));

    let mut set_active_configuration_healthy_was_called =
        env.proxies.paver.set_active_configuration_healthy_was_called.lock();
    set_active_configuration_healthy_was_called.next().await;

    let state = env.proxies.update_manager.get_state().await.expect("get_state");
    assert_eq!(state.state, Some(ManagerState::Idle));
    assert_eq!(state.version_available, None);
}
