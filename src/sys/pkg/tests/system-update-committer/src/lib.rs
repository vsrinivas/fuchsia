// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fidl_fuchsia_paver::Configuration,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{AppBuilder, Output},
        server::{NestedEnvironment, ServiceFs},
    },
    futures::prelude::*,
    mock_paver::{MockPaverService, MockPaverServiceBuilder, PaverEvent},
    std::sync::Arc,
};

const SYSTEM_UPDATE_COMMITTER_CMX: &str =
    "fuchsia-pkg://fuchsia.com/system-update-committer-integration-tests#meta/system-update-committer.cmx";

struct TestEnv {
    env: NestedEnvironment,
    paver_service: Arc<MockPaverService>,
}

impl TestEnv {
    fn new() -> Self {
        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>();

        // Set up paver service.
        let paver_service = Arc::new(MockPaverServiceBuilder::new().build());
        let paver_service_clone = Arc::clone(&paver_service);
        fs.add_fidl_service(move |stream| {
            fasync::Task::spawn(
                Arc::clone(&paver_service_clone)
                    .run_paver_service(stream)
                    .unwrap_or_else(|e| panic!("error running paver service: {:#}", anyhow!(e))),
            )
            .detach()
        });

        let env = fs
            .create_salted_nested_environment("system_update_committer_env")
            .expect("nested environment to create successfully");
        fasync::Task::spawn(fs.collect()).detach();

        Self { env, paver_service }
    }

    async fn run_system_update_committer(&self) -> Output {
        AppBuilder::new(SYSTEM_UPDATE_COMMITTER_CMX.to_owned())
            .output(self.env.launcher())
            .expect("system-update-committer to launch")
            .await
            .expect("no errors while waiting for exit")
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_calls_paver() {
    let env = TestEnv::new();

    let output = env.run_system_update_committer().await;

    let () = output.ok().expect("system-update-committer to exit with status code 0");
    assert_eq!(
        env.paver_service.take_events(),
        vec![
            PaverEvent::QueryCurrentConfiguration,
            PaverEvent::QueryConfigurationStatus { configuration: Configuration::A },
            PaverEvent::SetConfigurationHealthy { configuration: Configuration::A },
            PaverEvent::SetConfigurationUnbootable { configuration: Configuration::B },
            PaverEvent::BootManagerFlush
        ]
    );
}
