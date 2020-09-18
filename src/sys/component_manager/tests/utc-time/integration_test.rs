// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context, Error};
use async_trait::async_trait;
use fidl::endpoints::ServerEnd;
use fidl_componentmanager_test as ftest;
use fidl_fuchsia_io as fio;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::{channel::oneshot, lock::Mutex, prelude::*};
use log::*;
use std::sync::Arc;
use test_utils_lib::{events::*, opaque_test::OpaqueTestBuilder};
use vfs::{
    directory::entry::DirectoryEntry, execution_scope::ExecutionScope, file::pcb::read_only_static,
    pseudo_directory,
};

#[fasync::run_singlethreaded(test)]
async fn builtin_time_service_routed() -> Result<(), Error> {
    fuchsia_syslog::init().unwrap();
    fuchsia_syslog::set_severity(fuchsia_syslog::levels::WARN);

    let expected_backstop_time = zx::Time::from_nanos(1589910459000000000);
    let expected_backstop_time_sec_str = "1589910459";

    // Construct a pseudo-directory to mock the component manager's configured
    // backstop time.
    let dir = pseudo_directory! {
        "config" => pseudo_directory! {
            "build_info" => pseudo_directory! {
                // The backstop time is stored in seconds.
                "minimum_utc_stamp" => read_only_static(expected_backstop_time_sec_str),
            },
        },
    };

    let (client, server) = zx::Channel::create().expect("failed to create channel pair");
    let scope = ExecutionScope::new();
    dir.open(
        scope,
        fio::OPEN_FLAG_DIRECTORY | fio::OPEN_RIGHT_READABLE,
        0,
        vfs::path::Path::empty(),
        ServerEnd::new(server),
    );

    // Start a component_manager as a v1 component, with the extra `--maintain-utc-clock` flag.
    debug!("starting component_manager");
    let test = OpaqueTestBuilder::new(
        "fuchsia-pkg://fuchsia.com/utc-time-tests#meta/consumer-component.cm",
    )
    .component_manager_url("fuchsia-pkg://fuchsia.com/utc-time-tests#meta/component_manager.cmx")
    .config("/pkg/data/cm_config")
    .add_dir_handle("/boot", client.into())
    .build()
    .await
    .expect("failed to start the OpaqueTest");
    let event_source = test
        .connect_to_event_source()
        .await
        .expect("failed to connect to the BlockingEventSource protocol");

    // Inject a TestOutcomeCapability which the component under test will request
    // and use to report back the test outcome.
    // The TestOutcomeCapability can only be used once, so multiple components
    // requesting this will panic.
    debug!("injecting TestOutcomeCapability");
    let (capability, test_case) = test_outcome_report();
    event_source
        .install_injector(capability, None)
        .await
        .expect("failed to install TestOutcomeCapability");

    // Unblock the component_manager.
    debug!("starting component tree");
    event_source.start_component_tree().await;

    // Await the test result.
    debug!("waiting for test outcome");
    let actual_backstop_time = test_case.await.expect("failed to wait on test")?;
    assert_eq!(actual_backstop_time, expected_backstop_time);
    Ok(())
}

/// The TestOutcomeCapability that can be injected in order for a component under test
/// to report back test outcomes.
pub struct TestOutcomeCapability {
    sender: Mutex<Option<oneshot::Sender<Result<zx::Time, Error>>>>,
}

/// Create a TestOutcomeCapablity and Receiver pair.
fn test_outcome_report() -> (Arc<TestOutcomeCapability>, oneshot::Receiver<Result<zx::Time, Error>>)
{
    let (sender, receiver) = oneshot::channel::<Result<zx::Time, Error>>();
    (Arc::new(TestOutcomeCapability { sender: Mutex::new(Some(sender)) }), receiver)
}

#[async_trait]
impl Injector for TestOutcomeCapability {
    type Marker = ftest::TestOutcomeReportMarker;

    async fn serve(
        self: Arc<Self>,
        mut request_stream: ftest::TestOutcomeReportRequestStream,
    ) -> Result<(), Error> {
        let sender = match self.sender.lock().await.take() {
            Some(sender) => sender,
            None => panic!("TestOutcomeCapability already reported"),
        };
        sender
            .send(match request_stream.next().await {
                Some(outcome) => outcome.map_err(|e| e.into()).and_then(
                    |ftest::TestOutcomeReportRequest::Report { outcome, responder }| {
                        responder.send().context("failed to send response to client")?;
                        match outcome {
                            ftest::TestOutcome::Success(ftest::SuccessOutcome {
                                backstop_nanos,
                            }) => Ok(zx::Time::from_nanos(backstop_nanos)),
                            ftest::TestOutcome::Failed(ftest::FailedOutcome { message }) => {
                                Err(anyhow!("test failed: {}", &message))
                            }
                        }
                    },
                ),
                None => Err(anyhow!("test component finished without a response")),
            })
            .expect("failed to send outcome on channel");
        Ok(())
    }
}
