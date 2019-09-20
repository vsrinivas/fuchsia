// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use {
    failure::Error,
    fidl_fuchsia_paver::{PaverRequest, PaverRequestStream},
    fidl_fuchsia_sys::{LauncherProxy, TerminationReason},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{AppBuilder, Output},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_zircon::Status,
    futures::prelude::*,
    parking_lot::Mutex,
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

    fn new() -> Self {
        let mut fs = ServiceFs::new();

        let paver_service = Arc::new(MockPaverService::new());
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

        Self { env, paver_service }
    }

    async fn run_partition_marker(&self) -> Output {
        let launcher = self.launcher();
        let partition_marker = AppBuilder::new(PARTITION_MARKER_CMX);
        let output = partition_marker
            .output(launcher)
            .expect("partition_marker to launch")
            .await
            .expect("no errors while waiting for exit");
        assert_eq!(output.exit_status.reason(), TerminationReason::Exited);
        output
    }
}

struct MockPaverService {
    call_count: Mutex<u64>,
}

impl MockPaverService {
    fn new() -> Self {
        Self { call_count: Mutex::new(0u64) }
    }
    async fn run_service(self: Arc<Self>, mut stream: PaverRequestStream) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                PaverRequest::MarkActiveConfigurationSuccessful { responder } => {
                    *self.call_count.lock() += 1;
                    responder.send(Status::OK.into_raw()).expect("send ok");
                }
                req => panic!("unhandled paver request: {:?}", req),
            }
        }
        Ok(())
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_calls_paver_service() {
    let env = TestEnv::new();

    env.run_partition_marker().await;

    assert_eq!(*env.paver_service.call_count.lock(), 1);
}
