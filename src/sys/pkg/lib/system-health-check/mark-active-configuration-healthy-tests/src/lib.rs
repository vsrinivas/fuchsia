// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    fidl_fuchsia_paver::{Configuration, PaverRequestStream},
    fidl_fuchsia_sys::LauncherProxy,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::AppBuilder,
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_zircon::Status,
    futures::prelude::*,
    mock_paver::{MockPaverService, MockPaverServiceBuilder, PaverEvent},
    std::sync::Arc,
};

const PARTITION_MARKER_CMX: &str = "fuchsia-pkg://fuchsia.com/mark-active-configuration-healthy-tests#meta/mark-active-configuration-healthy.cmx";

struct TestEnv {
    env: NestedEnvironment,
    paver_service: Arc<MockPaverService>,
}

impl TestEnv {
    fn launcher(&self) -> &LauncherProxy {
        self.env.launcher()
    }

    fn new(paver_init: impl FnOnce(MockPaverServiceBuilder) -> MockPaverServiceBuilder) -> Self {
        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>();
        
        let paver_service = Arc::new(paver_init(MockPaverServiceBuilder::new()).build());
        let paver_service_clone = paver_service.clone();
        fs.add_fidl_service(move |stream: PaverRequestStream| {
            fasync::Task::spawn(
                paver_service_clone
                    .clone()
                    .run_paver_service(stream)
                    .unwrap_or_else(|e| panic!("error running paver service: {:?}", e)),
            )
            .detach()
        });

        let env = fs
            .create_salted_nested_environment("partition_marker_env")
            .expect("nested environment to create successfully");
        fasync::Task::spawn(fs.collect()).detach();

        Self { env, paver_service }
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

// We should call SetConfigurationUnbootable when the device supports ABR and is not in recovery
#[fasync::run_singlethreaded(test)]
async fn test_calls_set_configuration_unbootable_config_a_active() {
    let env = TestEnv::new(|p| p.active_config(Configuration::A));
    assert_eq!(env.paver_service.take_events(), Vec::new());
    env.run_partition_marker().await;
    assert_eq!(
        env.paver_service.take_events(),
        vec![
            PaverEvent::SetActiveConfigurationHealthy,
            PaverEvent::QueryActiveConfiguration,
            PaverEvent::SetConfigurationUnbootable { configuration: Configuration::B },
            PaverEvent::BootManagerFlush,
        ]
    );
}

// We should call SetConfigurationUnbootable when the device supports ABR and is not in recovery
#[fasync::run_singlethreaded(test)]
async fn test_calls_set_configuration_unbootable_config_b_active() {
    let env = TestEnv::new(|p| p.active_config(Configuration::B));
    assert_eq!(env.paver_service.take_events(), Vec::new());
    env.run_partition_marker().await;
    assert_eq!(
        env.paver_service.take_events(),
        vec![
            PaverEvent::SetActiveConfigurationHealthy,
            PaverEvent::QueryActiveConfiguration,
            PaverEvent::SetConfigurationUnbootable { configuration: Configuration::A },
            PaverEvent::BootManagerFlush,
        ]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_does_not_change_metadata_when_device_does_not_support_abr() {
    let env = TestEnv::new(|p| p.boot_manager_close_with_epitaph(Status::NOT_SUPPORTED));
    assert_eq!(env.paver_service.take_events(), Vec::new());
    env.run_partition_marker().await;
    assert_eq!(env.paver_service.take_events(), Vec::new());
}

#[fasync::run_singlethreaded(test)]
async fn test_calls_exclusively_set_active_configuration_healthy_when_device_in_recovery() {
    let env = TestEnv::new(|p| p.active_config(Configuration::Recovery));
    assert_eq!(env.paver_service.take_events(), Vec::new());
    env.run_partition_marker().await;
    assert_eq!(env.paver_service.take_events(), vec![PaverEvent::SetActiveConfigurationHealthy]);
}
