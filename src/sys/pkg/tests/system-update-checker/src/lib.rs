// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    fidl_fuchsia_paver::{
        BootManagerRequest, BootManagerRequestStream, PaverRequest, PaverRequestStream,
    },
    fidl_fuchsia_update::{ManagerMarker, ManagerProxy, MonitorRequest, State},
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
        fs.add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>();

        let paver = Arc::new(paver);
        let paver_clone = Arc::clone(&paver);
        fs.add_fidl_service(move |stream: PaverRequestStream| {
            fasync::spawn(Arc::clone(&paver_clone).run_service(stream));
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
async fn test_update_manager_check_now_works_after_paver_service_fails() {
    let env = TestEnv::new(MockPaver::new(Status::INTERNAL));

    let mut set_active_configuration_healthy_was_called =
        env.proxies.paver.set_active_configuration_healthy_was_called.lock();
    set_active_configuration_healthy_was_called.next().await;

    let (client_end, request_stream) =
        fidl::endpoints::create_request_stream().expect("create_request_stream");

    assert!(env
        .proxies
        .update_manager
        .check_now(
            fidl_fuchsia_update::CheckOptions {
                initiator: Some(fidl_fuchsia_update::Initiator::User),
                allow_attaching_to_existing_update_check: Some(true),
            },
            Some(client_end),
        )
        .await
        .is_ok());
    assert_eq!(
        request_stream
            .map(|r| {
                let MonitorRequest::OnState { state, responder } = r.unwrap();
                responder.send().unwrap();
                state
            })
            .collect::<Vec<State>>()
            .await,
        vec![
            State::CheckingForUpdates(fidl_fuchsia_update::CheckingForUpdatesData {}),
            State::ErrorCheckingForUpdate(fidl_fuchsia_update::ErrorCheckingForUpdateData {}),
        ]
    );
}
