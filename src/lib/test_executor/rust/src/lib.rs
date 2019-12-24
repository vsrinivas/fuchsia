// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This crate runs and collects results from a test which implements fuchsia.test.Suite protocol.

use {
    anyhow::{format_err, Context as _},
    fidl_fuchsia_sys::LauncherProxy,
    fidl_fuchsia_test::{
        Invocation,
        RunListenerRequest::{OnTestCaseFinished, OnTestCaseStarted},
    },
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_vlog, macros::*},
    fuchsia_zircon as zx,
    futures::{
        channel::mpsc,
        future::join_all,
        future::BoxFuture,
        io::{self, AsyncRead},
        prelude::*,
        ready,
        task::{Context, Poll},
    },
    std::{cell::RefCell, collections::HashMap, marker::Unpin, pin::Pin},
    zx::HandleBased,
};

/// Defines the outcome of a test case run.
#[derive(PartialEq, Debug)]
pub enum Outcome {
    /// Test case passed.
    Passed,
    /// Test case failed.
    Failed,
    /// Test case did not communicate the outcome.
    Error,
}

/// Event to send to caller of `run_test_component`
/// TestCaseStarted: Whenever a new test case is started.
/// TestCaseFinished: Whenever a test case finishes.
/// LogMessage: Whenever a test case produces a log message.
#[derive(PartialEq, Debug)]
pub enum TestEvent {
    TestCaseStarted { test_case_name: String },
    TestCaseFinished { test_case_name: String, outcome: Outcome },
    LogMessage { test_case_name: String, msg: String },
}

#[must_use = "futures/streams"]
pub struct LoggerStream {
    socket: fasync::Socket,
}
impl Unpin for LoggerStream {}

thread_local! {
    pub static BUFFER:
        RefCell<[u8; 2048]> = RefCell::new([0; 2048]);
}

impl LoggerStream {
    /// Creates a new `LoggerStream` for given `socket`.
    pub fn new(socket: zx::Socket) -> Result<LoggerStream, anyhow::Error> {
        let l = LoggerStream {
            socket: fasync::Socket::from_socket(socket).context("Invalid zircon socket")?,
        };
        Ok(l)
    }
}

fn process_log_bytes(bytes: &[u8]) -> String {
    // TODO(anmittal): Change this to consider break in logs and handle it.
    let log = std::str::from_utf8(bytes).unwrap();
    log.to_string()
}

impl Stream for LoggerStream {
    type Item = io::Result<String>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        BUFFER.with(|b| {
            let mut b = b.borrow_mut();
            let len = ready!(Pin::new(&mut self.socket).poll_read(cx, &mut *b)?);
            if len == 0 {
                return Poll::Ready(None);
            }
            Poll::Ready(Some(process_log_bytes(&b[0..len])).map(Ok))
        })
    }
}

struct LogProcessor {
    name: String,
    f: Option<BoxFuture<'static, Result<(), anyhow::Error>>>,
}

impl LogProcessor {
    pub fn new(test_case_name: String) -> LogProcessor {
        LogProcessor { name: test_case_name, f: None }
    }

    /// This will put a listener on `logger_socket`, process and send logs asynchronously in the
    /// background.
    /// Caller of this function must call `await_logs` to wait for all the logs to be collected.
    pub fn collect_and_send_logs(
        &mut self,
        logger_socket: zx::Socket,
        mut sender: mpsc::Sender<TestEvent>,
    ) {
        if logger_socket.is_invalid_handle() {
            return;
        }

        let mut ls = match LoggerStream::new(logger_socket) {
            Err(e) => {
                fx_log_err!("Logger: Failed to create fuchsia async socket: {:?}", e);
                return;
            }
            Ok(ls) => ls,
        };

        let name = self.name.clone();

        let f = async move {
            while let Some(log) = ls
                .try_next()
                .await
                .map_err(|e| format_err!("Error while reading log msg: {}", e))?
            {
                sender
                    .send(TestEvent::LogMessage { test_case_name: name.clone(), msg: log })
                    .map_err(|e| format_err!("Error while sending log msg: {}", e))
                    .await?
            }
            Ok(())
        };

        let (remote, remote_handle) = f.remote_handle();

        fasync::spawn(remote);

        self.f = Some(remote_handle.boxed());
    }

    /// This will wait for all the logs to be collected.
    pub async fn await_logs(&mut self) -> Result<(), anyhow::Error> {
        if let Some(ref mut f) = self.f.take().as_mut() {
            return Ok(f.await?);
        }
        Ok(())
    }
}

/// Runs the test component using `suite` and collects logs and results.
pub async fn run_and_collect_results(
    suite: fidl_fuchsia_test::SuiteProxy,
    mut sender: mpsc::Sender<TestEvent>,
    test_url: String,
) -> Result<(), anyhow::Error> {
    fx_vlog!(1, "enumerating tests");
    let cases =
        suite.get_tests().await.map_err(|e| format_err!("Error getting test steps: {}", e))?;
    fx_vlog!(1, "got test list: {:#?}", cases);
    let mut invocations = Vec::<Invocation>::new();
    for case in cases {
        invocations.push(Invocation { case: Some(case) });
    }
    let (run_listener_client, mut run_listener) =
        fidl::endpoints::create_request_stream::<fidl_fuchsia_test::RunListenerMarker>()
            .map_err(|e| format_err!("Error creating request stream: {}", e))?;
    fx_vlog!(1, "running tests");
    suite
        .run(
            &mut invocations.into_iter().map(|i| i.into()),
            fidl_fuchsia_test::RunOptions {},
            run_listener_client,
        )
        .map_err(|e| format_err!("Error running tests in '{}': {}", test_url, e))?;

    let mut log_processors = HashMap::new();
    while let Some(result_event) = run_listener
        .try_next()
        .await
        .map_err(|e| format_err!("Error waiting for listener: {}", e))?
    {
        match result_event {
            OnTestCaseStarted { name, primary_log, control_handle: _ } => {
                sender.send(TestEvent::TestCaseStarted { test_case_name: name.clone() }).await?;

                let mut log_processor = LogProcessor::new(name.clone());
                log_processor.collect_and_send_logs(primary_log, sender.clone());
                log_processors.insert(name, log_processor);
            }
            OnTestCaseFinished { name, outcome, control_handle: _ } => {
                // get all logs before sending finish event.
                match log_processors.remove(&name) {
                    Some(mut l) => l.await_logs().await?,
                    None => {}
                }
                let outcome = match outcome.status {
                    Some(status) => match status {
                        fidl_fuchsia_test::Status::Passed => Outcome::Passed,
                        fidl_fuchsia_test::Status::Failed => Outcome::Failed,
                    },
                    // This will happen when test protocol is not properly implemented
                    // by the test and it forgets to set the outcome.
                    None => Outcome::Error,
                };
                sender
                    .send(TestEvent::TestCaseFinished { test_case_name: name, outcome: outcome })
                    .await?;
            }
        }
    }

    // await for rest of logs for which test case never completed.
    join_all(log_processors.iter_mut().map(|(_, l)| l.await_logs()))
        .await
        .into_iter()
        .fold(Ok(()), |acc, r| acc.and_then(|_| r))
}

/// Runs the test component defined by `test_url` and reports `TestEvent` to sender for each test case.
pub async fn run_test_component(
    launcher: LauncherProxy,
    test_url: String,
    sender: mpsc::Sender<TestEvent>,
) -> Result<(), anyhow::Error> {
    let component_manager_for_test = "fuchsia-pkg://fuchsia.com/component_manager_for_test#\
                                      meta/component_manager_for_test.cmx";

    fx_vlog!(1, "connecting to test component {}", test_url);
    let is_v2_component = test_url.ends_with(".cm");
    let app;
    if is_v2_component {
        app = fuchsia_component::client::launch(
            &launcher,
            component_manager_for_test.to_string(),
            Some(vec![test_url.clone()]),
        )
        .map_err(|e| format_err!("Not able to launch v2 test:{}: {}", test_url, e))?;
    } else {
        app = fuchsia_component::client::launch(&launcher, test_url.clone(), None)
            .map_err(|e| format_err!("Not able to launch v1 test:{}: {}", test_url, e))?;
    }

    fx_vlog!(1, "connecting to test service");
    let suite = app
        .connect_to_service::<fidl_fuchsia_test::SuiteMarker>()
        .map_err(|e| format_err!("Error connecting to test service: {}", e))?;

    run_and_collect_results(suite, sender, test_url.clone()).await?;

    Ok(())
}

/// The full test coverage of this library lives at //garnet/bin/sl4f/tests/test_framework
/// They test that this library is able to handle various kind of tests launches and able to collect
/// and pass back results.
/// TODO(anmittal): move some of those tests here as unit tests.
#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn log_processor() {
        let (sock_server, sock_client) =
            zx::Socket::create(zx::SocketOpts::STREAM).expect("Failed while creating socket");

        let name = "test_name";
        let mut log_processor = LogProcessor::new(name.to_string());

        let (sender, mut recv) = mpsc::channel(1);
        log_processor.collect_and_send_logs(sock_client, sender);

        sock_server.write(b"test message 1").expect("Can't write msg to socket");
        sock_server.write(b"test message 2").expect("Can't write msg to socket");
        sock_server.write(b"test message 3").expect("Can't write msg to socket");

        let mut msg = recv.next().await;

        assert_eq!(
            msg,
            Some(TestEvent::LogMessage {
                test_case_name: name.to_string(),
                msg: "test message 1test message 2test message 3".to_string()
            })
        );

        // can receive messages multiple times
        sock_server.write(b"test message 4").expect("Can't write msg to socket");
        msg = recv.next().await;

        assert_eq!(
            msg,
            Some(TestEvent::LogMessage {
                test_case_name: name.to_string(),
                msg: "test message 4".to_string()
            })
        );

        // messages can be read after socket server is closed.
        sock_server.write(b"test message 5").expect("Can't write msg to socket");
        sock_server.into_handle(); // this will drop this handle and close it.
        log_processor.await_logs().await.expect("await logs should not fail");

        msg = recv.next().await;

        assert_eq!(
            msg,
            Some(TestEvent::LogMessage {
                test_case_name: name.to_string(),
                msg: "test message 5".to_string()
            })
        );

        // socket was closed, this should return None
        msg = recv.next().await;
        assert_eq!(msg, None);
    }
}
