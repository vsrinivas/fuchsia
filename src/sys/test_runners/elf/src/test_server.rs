// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_test::{
        self as ftest, Invocation, Result_ as TestResult, RunListenerProxy, Status,
    },
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        future::{abortable, AbortHandle},
        prelude::*,
        TryStreamExt,
    },
    log::{debug, error},
    std::sync::{Arc, Weak},
    test_runners_lib::{
        cases::TestCaseInfo,
        elf::{Component, EnumeratedTestCases, FidlError, KernelError, SuiteServer},
        errors::*,
        launch,
        logs::{buffer_and_drain_logger, LogWriter, LoggerStream},
    },
};

/// Implements `fuchsia.test.Suite` and runs provided test.
#[derive(Default)]
pub struct TestServer {}

static PARALLEL_DEFAULT: u16 = 1;

#[async_trait]
impl SuiteServer for TestServer {
    /// Launches test process and gets test list out. Returns list of tests names in the format
    /// defined by gtests, i.e FOO.Bar.
    /// It only runs enumeration logic once, caches and returns the same result back on subsequent
    /// calls.
    async fn enumerate_tests(
        &self,
        _test_component: Arc<Component>,
    ) -> Result<EnumeratedTestCases, EnumerationError> {
        Ok(Arc::new(vec![TestCaseInfo { name: "main".to_string(), enabled: true }]))
    }

    async fn run_tests(
        &self,
        invocations: Vec<Invocation>,
        run_options: ftest::RunOptions,
        component: Arc<Component>,
        run_listener: &RunListenerProxy,
    ) -> Result<(), RunTestError> {
        let num_parallel =
            Self::get_parallel_count(run_options.parallel.unwrap_or(PARALLEL_DEFAULT));

        let invocations = stream::iter(invocations);
        invocations
            .map(Ok)
            .try_for_each_concurrent(num_parallel, |invocation| {
                self.run_test(invocation, &run_options, component.clone(), run_listener)
            })
            .await
    }

    /// Run this server.
    fn run(
        self,
        weak_component: Weak<Component>,
        test_url: &str,
        stream: ftest::SuiteRequestStream,
    ) -> AbortHandle {
        let test_url = test_url.clone().to_owned();
        let (fut, test_suite_abortable_handle) =
            abortable(self.serve_test_suite(stream, weak_component.clone()));

        fasync::Task::local(async move {
            match fut.await {
                Ok(result) => {
                    if let Err(e) = result {
                        error!("server failed for test {}: {:?}", test_url, e);
                    }
                }
                Err(e) => debug!("server aborted for test {}: {:?}", test_url, e),
            }
            debug!("Done running server for {}.", test_url);
        })
        .detach();
        test_suite_abortable_handle
    }
}

impl TestServer {
    pub fn new() -> Self {
        Self {}
    }

    pub fn validate_args(_args: &Vec<String>) -> Result<(), ArgumentError> {
        // Unopinionated about args,
        // they're passed through to the test program unfiltered
        Ok(())
    }

    async fn run_test<'a>(
        &'a self,
        invocation: Invocation,
        run_options: &ftest::RunOptions,
        component: Arc<Component>,
        run_listener: &RunListenerProxy,
    ) -> Result<(), RunTestError> {
        if "main" != invocation.name.as_ref().ok_or(RunTestError::TestCaseName)?.to_string() {
            // "main" is the only valid test case name
            return Ok(());
        }

        let (test_logger, log_client) =
            zx::Socket::create(zx::SocketOpts::STREAM).map_err(KernelError::CreateSocket).unwrap();
        let (case_listener_proxy, listener) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_test::CaseListenerMarker>()
                .map_err(FidlError::CreateProxy)
                .unwrap();
        run_listener
            .on_test_case_started(invocation, log_client, listener)
            .map_err(RunTestError::SendStart)?;
        let test_logger =
            fasync::Socket::from_socket(test_logger).map_err(KernelError::SocketToAsync).unwrap();
        let mut test_logger = LogWriter::new(test_logger);

        let mut args = component.args.clone();
        if let Some(user_args) = &run_options.arguments {
            args.extend(user_args.clone());
        }

        // Launch test program
        let (process, _job, stdlogger) =
            launch_component_process::<RunTestError>(&component, args).await?;

        // Drain stdout
        buffer_and_drain_logger(stdlogger, &mut test_logger).await?;

        // Wait for test to return
        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .map_err(KernelError::ProcessExit)
            .unwrap();
        let process_info = process.info().map_err(RunTestError::ProcessInfo)?;

        // Map return value of zero to Passed, non-zero to Failed
        let status = match process_info.return_code {
            0 => Status::Passed,
            _ => Status::Failed,
        };
        case_listener_proxy
            .finished(TestResult { status: Some(status), ..TestResult::EMPTY })
            .map_err(RunTestError::SendFinish)?;
        Ok(())
    }
}

/// Convenience wrapper around [`launch::launch_process`].
async fn launch_component_process<E>(
    component: &Component,
    args: Vec<String>,
) -> Result<(zx::Process, launch::ScopedJob, LoggerStream), E>
where
    E: From<NamespaceError> + From<launch::LaunchError>,
{
    Ok(launch::launch_process(launch::LaunchProcessArgs {
        bin_path: &component.binary,
        process_name: &component.name,
        job: Some(component.job.create_child_job().map_err(KernelError::CreateJob).unwrap()),
        ns: component.ns.clone().map_err(NamespaceError::Clone)?,
        args: Some(args),
        name_infos: None,
        environs: None,
        handle_infos: None,
    })
    .await?)
}
