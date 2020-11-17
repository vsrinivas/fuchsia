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
use test_util::assert_geq;
use test_utils_lib::{injectors::*, matcher::EventMatcher, opaque_test::OpaqueTestBuilder};
use vfs::{
    directory::entry::DirectoryEntry, execution_scope::ExecutionScope, file::pcb::read_only_static,
    pseudo_directory,
};

/// Offset the maintainer component uses from backstop to set the UTC time.
const TEST_OFFSET: zx::Duration = zx::Duration::from_minutes(2);

#[fasync::run_singlethreaded(test)]
async fn builtin_time_service_and_clock_routed() -> Result<(), Error> {
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
    let test = OpaqueTestBuilder::new("fuchsia-pkg://fuchsia.com/utc-time-tests#meta/realm.cm")
        .component_manager_url(
            "fuchsia-pkg://fuchsia.com/utc-time-tests#meta/component_manager.cmx",
        )
        .config("/pkg/data/cm_config")
        .add_dir_handle("/boot", client.into())
        .build()
        .await
        .expect("failed to start the OpaqueTest");
    let event_source = test
        .connect_to_event_source()
        .await
        .expect("failed to connect to the BlockingEventSource protocol");

    // Inject TestOutcomeCapabilities which the components launched by component manager request
    // and use to report back the test outcome.
    // The capabilities can only be used once, so multiple components requesting this will panic.
    debug!("injecting capabilities");
    let (maintain_capability, maintain_fut) = test_outcome_report();
    maintain_capability.inject(&event_source, EventMatcher::ok().moniker("/maintainer:*")).await;

    let (client_capability, client_fut) = test_outcome_report();
    client_capability.inject(&event_source, EventMatcher::ok().moniker("/time_client:*")).await;

    // Unblock the component_manager.
    debug!("starting component tree");
    event_source.start_component_tree().await;

    // Await the test result.
    debug!("waiting for test outcome");
    let maintain_result = maintain_fut.await??;
    let client_result = client_fut.await??;

    // Check that times reported by the maintainer and client agree.
    assert_eq!(expected_backstop_time.into_nanos(), maintain_result.backstop);
    assert_eq!(expected_backstop_time.into_nanos(), client_result.backstop);
    let expected_current_time = expected_backstop_time + TEST_OFFSET;
    assert_geq!(expected_current_time.into_nanos(), maintain_result.current_time);
    assert_geq!(client_result.current_time, maintain_result.current_time);

    Ok(())
}

type TestResult = Result<ftest::SuccessOutcome, Error>;

/// The TestOutcomeCapability that can be injected in order for a component under test
/// to report back test outcomes.
pub struct TestOutcomeCapability {
    sender: Mutex<Option<oneshot::Sender<TestResult>>>,
}

/// Create a TestOutcomeCapability and Receiver pair.
fn test_outcome_report() -> (Arc<TestOutcomeCapability>, oneshot::Receiver<TestResult>) {
    let (sender, receiver) = oneshot::channel::<TestResult>();
    (Arc::new(TestOutcomeCapability { sender: Mutex::new(Some(sender)) }), receiver)
}

#[async_trait]
impl ProtocolInjector for TestOutcomeCapability {
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
                        responder.send().context("failed to send response")?;
                        match outcome {
                            ftest::TestOutcome::Success(success) => Ok(success),
                            ftest::TestOutcome::Failed(ftest::FailedOutcome { message }) => {
                                Err(anyhow!("test failed: {}", &message))
                            }
                        }
                    },
                ),
                None => Err(anyhow!("channel finished without a response")),
            })
            .expect("failed to send outcome on channel");
        Ok(())
    }
}
