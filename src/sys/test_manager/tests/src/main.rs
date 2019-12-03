// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This unit tests a library which uses v2 framework APIs, so it needs to be launched as a
//! v2 component.

use {
    failure::{Error, ResultExt},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_test as ftest, fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::Outcome,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::{
        io::{self, AsyncRead},
        prelude::*,
        ready,
        task::{Context, Poll},
    },
    std::{cell::RefCell, pin::Pin, str::from_utf8},
};

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
    pub fn new(socket: zx::Socket) -> Result<LoggerStream, failure::Error> {
        let l = LoggerStream {
            socket: fasync::Socket::from_socket(socket).context("Invalid zircon socket")?,
        };
        Ok(l)
    }
}

fn process_log_bytes(bytes: &[u8]) -> Vec<u8> {
    bytes.to_vec()
}

impl Stream for LoggerStream {
    type Item = io::Result<Vec<u8>>;

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

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("error creating executor")?;
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn_local(async move {
            run_test_suite(stream).await.expect("failed to run test suite service")
        });
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}

fn connect_test_manager() -> Result<ftest_manager::HarnessProxy, Error> {
    let (proxy, server) = zx::Channel::create()?;
    let server = fasync::Channel::from_channel(server)?;
    fasync::spawn_local(async move {
        test_manager_lib::run_test_manager(ftest_manager::HarnessRequestStream::from_channel(
            server,
        ))
        .await
        .expect("failed to run test manager service")
    });
    let proxy = fasync::Channel::from_channel(proxy)?;
    Ok(ftest_manager::HarnessProxy::new(proxy))
}

async fn run_test(
    logger: fuchsia_zircon::Socket,
    outcome: &mut ftest::Outcome,
) -> Result<(), Error> {
    let test_url = "fuchsia-pkg://fuchsia.com/example-tests#meta/echo_test_realm.cm";

    let proxy = connect_test_manager()?;

    let (client, log) = zx::Socket::create(zx::SocketOpts::STREAM)?;

    let ls = LoggerStream::new(client)?;

    let (log_fut, log_fut_remote) = ls.try_concat().remote_handle();
    fasync::spawn_local(async move {
        log_fut.await;
    });
    let result = proxy.run_suite(test_url, log).await?;
    let logs = log_fut_remote.await?;
    let logs = from_utf8(&logs).expect("should be a valid string").to_string();

    let expected = "[RUNNING]	EchoTest
[PASSED]	EchoTest

1 out of 1 tests passed...
fuchsia-pkg://fuchsia.com/example-tests#meta/echo_test_realm.cm completed with outcome: Passed
"
    .to_owned();

    if expected != logs {
        logger.write(format!("invalid logs:\n{}", logs).as_bytes())?;
        outcome.status = Some(ftest::Status::Failed);
    }
    if result != Outcome::Passed {
        logger.write(format!("echo test failed with status {:?}", result).as_bytes())?;
        outcome.status = Some(ftest::Status::Failed);
    }

    Ok(())
}

// This implementation should eventually merge with rust test framework and we should be able to
// run this  test as a normal rust test.
async fn run_test_suite(mut stream: ftest::SuiteRequestStream) -> Result<(), Error> {
    let test_name = "TestManager.CanRunTest";
    while let Some(event) = stream.try_next().await? {
        match event {
            ftest::SuiteRequest::GetTests { responder } => {
                responder
                    .send(&mut vec![ftest::Case { name: Some(test_name.to_string()) }].into_iter())
                    .expect("Should not error out");
            }
            ftest::SuiteRequest::Run { tests, options: _, run_listener, .. } => {
                assert_eq!(tests.len(), 1);
                assert_eq!(tests[0].case, Some(ftest::Case { name: Some(test_name.to_string()) }));

                let proxy =
                    run_listener.into_proxy().expect("Can't convert listener channel to proxy");
                let (log_end, logger) =
                    fuchsia_zircon::Socket::create(fuchsia_zircon::SocketOpts::empty())
                        .expect("cannot create socket.");
                let mut outcome = ftest::Outcome { status: Some(ftest::Status::Passed) };

                proxy
                    .on_test_case_started(test_name, log_end)
                    .expect("on_test_case_started failed");
                run_test(logger, &mut outcome).await?;
                proxy
                    .on_test_case_finished(test_name, outcome)
                    .expect("on_test_case_finished failed");
            }
        }
    }
    Ok(())
}
