// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This unit tests a library which uses v2 framework APIs, so it needs to be launched as a
//! v2 component.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::Result_,
    fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_component::client::connect_to_protocol_at_dir,
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
    pub fn new(socket: zx::Socket) -> Result<LoggerStream, anyhow::Error> {
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

/// split and sort output as output can come in any order.
/// `output` is of type vec<u8> and `expected_output` is a string.
macro_rules! assert_output {
    ($output:expr, $expected_output:expr) => {
        let mut expected_output = $expected_output.split("\n").collect::<Vec<_>>();
        let mut output = from_utf8(&$output)
            .expect("we should not get utf8 error.")
            .split("\n")
            .collect::<Vec<_>>();

        expected_output.sort();
        output.sort();

        assert_eq!(output, expected_output);
    };
}

async fn connect_test_manager() -> Result<ftest_manager::HarnessProxy, Error> {
    let realm = client::connect_to_service::<fsys::RealmMarker>()
        .context("could not connect to Realm service")?;

    let mut child_ref = fsys::ChildRef { name: "test_manager".to_owned(), collection: None };
    let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>()?;
    realm
        .bind_child(&mut child_ref, server_end)
        .await
        .context("bind_child fidl call failed for test manager")?
        .map_err(|e| format_err!("failed to create test manager: {:?}", e))?;

    connect_to_protocol_at_dir::<ftest_manager::HarnessMarker>(&dir)
        .context("failed to open test suite service")
}

async fn run_test(test_url: &str) -> Result<(Result_, Vec<u8>), Error> {
    let proxy = connect_test_manager().await?;
    let (client, log) = zx::Socket::create(zx::SocketOpts::STREAM)?;
    let ls = LoggerStream::new(client)?;
    let (log_fut, log_fut_remote) = ls.try_concat().remote_handle();
    fasync::spawn_local(async move {
        log_fut.await;
    });
    let suite_result = proxy.run_suite(test_url, log).await.context("cannot get suite results")?;
    let logs = log_fut_remote.await.context("Cannot get logs")?;
    Ok((suite_result, logs))
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_echo_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/example-tests#meta/echo_test_realm.cm";
    let (suite_result, logs) = run_test(test_url).await.unwrap();
    let expected = format!(
        "[RUNNING]	EchoTest
[PASSED]	EchoTest

1 out of 1 tests passed...
{} completed with result: Passed
",
        test_url
    );

    assert_output!(logs, expected);
    assert_eq!(suite_result, Result_::Passed)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_no_on_finished() {
    let test_url =
        "fuchsia-pkg://fuchsia.com/example-tests#meta/no-onfinished-after-test-example.cm";
    let (suite_result, logs) = run_test(test_url).await.unwrap();
    let expected = format!(
        "[RUNNING]	Example.Test1
[Example.Test1]	log1 for Example.Test1
[Example.Test1]	log2 for Example.Test1
[Example.Test1]	log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
[Example.Test2]	log1 for Example.Test2
[Example.Test2]	log2 for Example.Test2
[Example.Test2]	log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
[Example.Test3]	log1 for Example.Test3
[Example.Test3]	log2 for Example.Test3
[Example.Test3]	log3 for Example.Test3
[PASSED]	Example.Test3

3 out of 3 tests passed...
{} completed with result: Passed
{} did not complete successfully
",
        test_url, test_url
    );

    assert_output!(logs, expected);
    assert_eq!(suite_result, Result_::Passed)
}

#[fuchsia_async::run_singlethreaded(test)]
async fn launch_and_test_gtest_runner_sample_test() {
    let test_url = "fuchsia-pkg://fuchsia.com/gtest_runner_example_tests#meta/sample_tests.cm";
    let (suite_result, logs) = run_test(test_url).await.unwrap();
    let expected = format!(
        "[RUNNING]\tSampleDisabled.DISABLED_Test1
[PASSED]\tSampleDisabled.DISABLED_Test1
[RUNNING]\tSampleFixture.Test1
[RUNNING]\tSampleFixture.Test2
[PASSED]\tSampleFixture.Test1
[PASSED]\tSampleFixture.Test2
[RUNNING]\tSampleTest1.Crashing
[RUNNING]\tSampleTest1.SimpleFail
[RUNNING]\tSampleTest2.SimplePass
[RUNNING]\tTests/SampleParameterizedTestFixture.Test/0
[RUNNING]\tTests/SampleParameterizedTestFixture.Test/1
[RUNNING]\tTests/SampleParameterizedTestFixture.Test/2
[RUNNING]\tTests/SampleParameterizedTestFixture.Test/3
[PASSED]\tTests/SampleParameterizedTestFixture.Test/0
[PASSED]\tTests/SampleParameterizedTestFixture.Test/1
[PASSED]\tTests/SampleParameterizedTestFixture.Test/2
[PASSED]\tTests/SampleParameterizedTestFixture.Test/3
[FAILED]\tSampleTest1.Crashing
[FAILED]\tSampleTest1.SimpleFail
[PASSED]\tSampleTest2.SimplePass
[SampleTest1.Crashing]\t  Actual: true
[SampleTest1.Crashing]\tExpected: false
[SampleTest1.Crashing]\tValue of: true
[SampleTest1.Crashing]\tfailure: ../../src/sys/test_runners/gtest/test_data/sample_tests.cc:9
[SampleTest1.SimpleFail]\t  Actual: true
[SampleTest1.SimpleFail]\tExpected: false
[SampleTest1.SimpleFail]\tValue of: true
[SampleTest1.SimpleFail]\tfailure: ../../src/sys/test_runners/gtest/test_data/sample_tests.cc:9

8 out of 10 tests passed...
{} completed with result: Failed
",
        test_url
    );

    assert_output!(logs, expected);
    assert_eq!(suite_result, Result_::Failed)
}
