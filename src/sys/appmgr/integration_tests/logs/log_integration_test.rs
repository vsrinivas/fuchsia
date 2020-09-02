// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

//! This test:
//!
//! 1. constructs a diagnostics archivist-for-embedding.cmx
//! 2. uses it as the LogSink for a hermetic test environment
//! 3. runs a simple component in the test environment to produce some logs
//! 4. inspects the log output of the child component

use {
    fidl_fuchsia_logger::*,
    fidl_fuchsia_sys::LauncherMarker,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{connect_to_service, AppBuilder},
        server::{ServiceFs, ServiceObj},
    },
    futures::stream::StreamExt,
    std::sync::Arc,
    validating_log_listener::validate_log_stream,
};

const CHILD_WITH_LOGS_URL: &str =
    "fuchsia-pkg://fuchsia.com/appmgr_log_integration_tests#meta/log_emitter_for_test.cmx";
const ARCHIVIST_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-for-embedding#meta/archivist-for-embedding.cmx";

#[fasync::run(2)]
async fn main() {
    if false {
        let mut builder = AppBuilder::new(ARCHIVIST_URL);
        let observer_dir_req = Arc::clone(builder.directory_request().unwrap());
        let our_launcher = connect_to_service::<LauncherMarker>().unwrap();
        let observer = builder.spawn(&our_launcher).unwrap();

        let mut child_fs = ServiceFs::<ServiceObj<'_, ()>>::new();
        child_fs.add_proxy_service_to::<LogSinkMarker, _>(observer_dir_req);
        let observed_env = child_fs.create_salted_nested_environment("appmgr_log_tests").unwrap();
        let observed_launcher = observed_env.launcher();
        fasync::Task::spawn(Box::pin(async move {
            child_fs.collect::<()>().await;
        }))
        .detach();

        let log_proxy = observer.connect_to_service::<LogMarker>().unwrap();

        let child = async {
            AppBuilder::new(CHILD_WITH_LOGS_URL)
                .spawn(&observed_launcher)
                .expect("launching child")
                .wait()
                .await
                .expect("child execution");
        };

        let validator = validate_log_stream(
            vec![LogMessage {
                severity: 0,
                tags: vec!["log_emitter_for_test".into()],
                msg: "hello, diagnostics!".into(),
                pid: 0,
                tid: 0,
                time: 0,
                dropped_logs: 0,
            }],
            log_proxy,
            None,
        );
        futures::future::join(child, validator).await;
    }
}
