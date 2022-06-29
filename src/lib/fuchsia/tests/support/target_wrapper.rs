// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use diagnostics_data::{Data, Logs, Severity};
use diagnostics_message::MonikerWithUrl;
use fidl::endpoints::{create_endpoints, RequestStream};
use fidl_fuchsia_fuchsialibtest::ConnectOnDoneRequestStream;
use fidl_fuchsia_logger::{LogSinkRequest, LogSinkRequestStream};
use fidl_fuchsia_test::{
    Case, CaseIteratorMarker, CaseIteratorRequest, CaseListenerMarker, Invocation,
    RunListenerMarker, RunListenerProxy, RunListenerRequest, RunOptions, StdHandles, SuiteMarker,
    SuiteRequest, SuiteRequestStream,
};
use fuchsia_async::{Socket, Task};
use fuchsia_component::{client::connect_to_protocol, server::ServiceFs};
use futures::{Stream, StreamExt};

mod test_spec;
use test_spec::{LoggingSpec, TestKind, TestSpec};

enum IncomingRequest {
    /// we serve our own test suite because the elf test runner doesn't give us our own
    /// outgoing directory handle which we need to serve logsink and connectondone to our child
    Test(SuiteRequestStream),
    Logs(LogSinkRequestStream),
    Done(ConnectOnDoneRequestStream),
}

#[fuchsia::main]
async fn main() {
    let raw_test_spec = std::fs::read_to_string("/pkg/data/test_spec.json").unwrap();
    let test_spec: TestSpec = serde_json::from_str(&raw_test_spec).unwrap();

    let mut requests = ServiceFs::new();
    requests
        .dir("svc")
        .add_fidl_service(IncomingRequest::Test)
        .add_fidl_service(IncomingRequest::Logs)
        .add_fidl_service(IncomingRequest::Done);
    requests.take_and_serve_directory_handle().unwrap();

    // get our own test suite requests before starting our child
    let mut suite_requests =
        match requests.next().await.expect("must receive a connection to Suite") {
            IncomingRequest::Test(s) => s,
            IncomingRequest::Logs(_) => panic!("received unexpected LogSink request"),
            IncomingRequest::Done(_) => panic!("received unexpected ConnectOnDone request"),
        };

    let run_listener = handle_suite_requests(test_spec.name.clone(), &mut suite_requests).await;
    let (case_listener_client, case_listener_server) =
        create_endpoints::<CaseListenerMarker>().unwrap();
    run_listener
        .on_test_case_started(
            Invocation { name: Some(test_spec.name), ..Invocation::EMPTY },
            StdHandles { ..StdHandles::EMPTY },
            case_listener_server,
        )
        .unwrap();
    let case_listener = case_listener_client.into_proxy().unwrap();

    let log_socket = launch_child_capture_logs(requests, test_spec.kind).await;

    if let Some(LoggingSpec { ref tags, min_severity }) = test_spec.logging {
        // the executor emits its own log messages at TRACE level
        let mut logs = Box::pin(
            log_socket
                .expect("must have received a log socket before child was done")
                .into_datagram_stream()
                .map(|r| {
                    diagnostics_message::from_structured(
                        MonikerWithUrl { moniker: String::new(), url: String::new() },
                        &r.unwrap(),
                    )
                    .unwrap()
                })
                .filter(|d| {
                    // exclude any messages that were emitted by the async executor at TRACE
                    let res = if let Some(t) = &d.metadata.tags {
                        !t.contains(&"fuchsia_async".to_string())
                    } else {
                        true
                    };
                    async move { res }
                }),
        );

        let min_severity = min_severity.unwrap_or(Severity::Info);

        // our macros emit their own message at debug when they init logging
        if min_severity <= Severity::Debug {
            assert_next_message_expected(&mut logs, "Logging initialized", Severity::Debug, tags)
                .await;
        }

        // start reading logs from the user's code
        if min_severity <= Severity::Trace {
            assert_next_hello_world_expected(&mut logs, Severity::Trace, tags).await;
        }
        if min_severity <= Severity::Debug {
            assert_next_hello_world_expected(&mut logs, Severity::Debug, tags).await;
        }
        if min_severity <= Severity::Info {
            assert_next_hello_world_expected(&mut logs, Severity::Info, tags).await;
        }
        if min_severity <= Severity::Warn {
            assert_next_hello_world_expected(&mut logs, Severity::Warn, tags).await;
        }
        if min_severity <= Severity::Error {
            assert_next_hello_world_expected(&mut logs, Severity::Error, tags).await;
        }

        // panics produce extra log messages when they terminate the process
        if !test_spec.panics {
            assert_eq!(logs.next().await, None, "no more logs should be available");
        }
    }

    // we communicate failure by panicking rather than returning a failure here, so failures look
    // a little weird in test output but it would be a lot of contortion to have this binary
    // serve the fuchsia.test.Suite protocol and the fuchsia.logger.LogSink protocol and not have
    // it panic from assertions
    case_listener
        .finished(fidl_fuchsia_test::Result_ {
            status: Some(fidl_fuchsia_test::Status::Passed),
            ..fidl_fuchsia_test::Result_::EMPTY
        })
        .unwrap();
    run_listener.on_finished().unwrap();
}

#[track_caller]
async fn assert_next_hello_world_expected(
    logs: impl Stream<Item = Data<Logs>> + std::marker::Unpin,
    expected_severity: Severity,
    expected_tags: &[String],
) {
    assert_next_message_expected(logs, "Hello, World!", expected_severity, expected_tags).await
}

#[track_caller]
async fn assert_next_message_expected(
    mut logs: impl Stream<Item = Data<Logs>> + std::marker::Unpin,
    expected_message: &str,
    expected_severity: Severity,
    expected_tags: &[String],
) {
    let observed_message = logs.next().await.unwrap();
    assert_eq!(
        observed_message.msg().unwrap(),
        expected_message,
        "full message received: {observed_message:#?}",
    );
    assert_eq!(observed_message.metadata.severity, expected_severity);
    assert_eq!(observed_message.metadata.tags.as_ref().unwrap(), expected_tags);
}

/// Listen to fuchsia.test.Suite, respond that we're able to run a single test case, and return
/// the RunListener if we're asked to run our single test case.
// TODO(https://fxbug.dev/99854) use ELF test runner instead of serving Suite
async fn handle_suite_requests(
    test_name: String,
    requests: &mut SuiteRequestStream,
) -> RunListenerProxy {
    while let Some(r) = requests.next().await {
        match r {
            Ok(SuiteRequest::GetTests { iterator, .. }) => {
                let mut iterator = iterator.into_stream().unwrap();
                let mut cases = vec![Case {
                    name: Some(test_name.clone()),
                    enabled: Some(true),
                    ..Case::EMPTY
                }];
                while let Some(Ok(r)) = iterator.next().await {
                    let CaseIteratorRequest::GetNext { responder } = r;
                    responder.send(&mut cases.into_iter()).unwrap();

                    // we need to send an empty vector to signal we're done, this will
                    // get hit on next iteration
                    cases = vec![];
                }
            }
            // handle requests to run the tests, ignore options for parallelism and ignored tests
            Ok(SuiteRequest::Run { listener, tests, .. }) => {
                assert_eq!(tests.len(), 1, "only one test case is supported by this runner");
                let invocation = tests.into_iter().next().unwrap();
                let name = invocation.name.expect("invocation must have a name it requests");
                assert_eq!(&name, &test_name, "we should only be asked to run our 1 test");

                // TODO(https://fxbug.dev/99854) stop serving Suite ourselves and support count
                // we only support a single Run() call which will work with infra's MULTIPLY but
                // will cause failures if invoked as `ffx test run --count N ...`
                return listener.into_proxy().unwrap();
            }
            Err(e) => panic!("failed to handle our own suite requests: {:?}", e),
        }
    }

    // didn't get any tests to run and the suite was closed, exit
    std::process::exit(0);
}

/// Launch our child-under-test, capture its structured log socket, and wait for it to exit.
// TODO(https://fxbug.dev/99854) stop serving LogSink ourselves, use ArchiveAccessor
async fn launch_child_capture_logs(
    mut requests: impl Stream<Item = IncomingRequest> + std::marker::Unpin,
    test_kind: TestKind,
) -> Option<Socket> {
    // start our child so it will generate logs
    // if this is a test, drive our own child's suite protocol so that we get some results
    if let TestKind::Test = test_kind {
        Task::spawn(drive_child_test_cases()).detach();
    } else {
        connect_to_protocol::<fidl_fuchsia_component::BinderMarker>().unwrap();
    }

    // wait for the child to finish, collecting its log socket (if any)
    let mut log_socket = None;
    'wait_for_child: loop {
        if let Some(request) = requests.next().await {
            match request {
                IncomingRequest::Test(_) => {
                    panic!("no more connections to our suite protocol should occur");
                }
                IncomingRequest::Logs(mut l) => {
                    while let Some(logsink_request) = l.next().await {
                        match logsink_request {
                            Ok(LogSinkRequest::ConnectStructured { socket, .. }) => {
                                log_socket = Some(Socket::from_socket(socket).unwrap());
                                break;
                            }
                            Err(e) => panic!("failed to listen to requests from child: {e}"),

                            // we only care about getting the structured socket
                            _ => (),
                        }
                    }
                }
                IncomingRequest::Done(on_done_requests) => {
                    on_done_requests.control_handle().send_ack().ok();
                    break 'wait_for_child;
                }
            }
        } else {
            panic!("didn't get any requests from child, expected at least ConnectOnDone");
        }
    }
    log_socket
}

async fn drive_child_test_cases() {
    let suite = connect_to_protocol::<SuiteMarker>().unwrap();
    let (cases_client, cases_server) = create_endpoints::<CaseIteratorMarker>().unwrap();
    suite.get_tests(cases_server).unwrap();
    let case_iterator = cases_client.into_proxy().unwrap();

    let mut invocations = vec![];
    loop {
        let cases = case_iterator.get_next().await.unwrap();
        if cases.is_empty() {
            break;
        }
        for case in cases {
            let case_name = case.name.expect("case names are required");
            invocations.push(Invocation { name: Some(case_name), tag: None, ..Invocation::EMPTY });
        }
    }

    assert_eq!(invocations.len(), 1, "each test file should only have one test case");

    let (run_listen_client, run_listen_server) = create_endpoints::<RunListenerMarker>().unwrap();
    suite
        .run(
            &mut invocations.into_iter().map(|i| i.into()),
            RunOptions { ..RunOptions::EMPTY },
            run_listen_client,
        )
        .unwrap();
    let mut run_listener_requests = run_listen_server.into_stream().unwrap();

    // wait for all the test cases to finish
    while let Some(req) = run_listener_requests.next().await {
        match req.unwrap() {
            RunListenerRequest::OnFinished { .. } => break,
            _ => (),
        }
    }
}
