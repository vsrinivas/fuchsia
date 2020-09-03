// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Shared traits and methods to be used in test suite servers for tests that are executed as ELF
//! components.

use {
    crate::{
        cases::TestCaseInfo,
        elf::Component,
        errors::{EnumerationError, RunTestError},
    },
    async_trait::async_trait,
    fidl_fuchsia_test as ftest, fuchsia_async as fasync, fuchsia_zircon as zx,
    fuchsia_zircon_sys::ZX_CHANNEL_MAX_MSG_BYTES,
    futures::{
        future::{AbortHandle, Shared},
        lock::Mutex,
        prelude::*,
    },
    log::{error, warn},
    rust_measure_tape_for_case::measure,
    std::{
        pin::Pin,
        sync::{Arc, Weak},
    },
    thiserror::Error,
};

/// A pinned, boxed future whose output is `Result<T, E>`.
pub type PinnedFuture<T, E> = Pin<Box<dyn Future<Output = Result<T, E>> + Send>>;
/// `SharedFuture` wrapper around `PinnedFuture<T, E>`. Can be cloned.
type SharedFuture<T, E> = Shared<PinnedFuture<T, E>>;
/// A mutable container around `SharedFuture<T, E>` that can be filled in when the stored future is
/// first created.
pub type MemoizedFutureContainer<T, E> = Arc<Mutex<Option<SharedFuture<T, E>>>>;
/// Ordered list of `TestCaseInfo`s.
pub type EnumeratedTestCases = Arc<Vec<TestCaseInfo>>;

/// Describes a test suite server for tests that are executed as ELF components.
#[async_trait]
pub trait SuiteServer: Sized + Sync + Send {
    /// Run this server.
    ///
    /// * `component`: Test component instance.
    /// * `test_url`: URL of test component.
    /// * `stream`: Stream to serve Suite protocol on.
    ///
    /// Returns abortable handle for suite server future.
    fn run(
        self,
        component: Weak<Component>,
        test_url: &str,
        stream: ftest::SuiteRequestStream,
    ) -> AbortHandle;

    /// Retrieves test information from the test binary.
    ///
    /// A cached list of test cases should be returned by cloning a
    /// `SharedFuture<EnumeratedTestCases, EnumerationError>` that is stored in the suite server
    /// struct.
    async fn enumerate_tests(
        &self,
        test_component: Arc<Component>,
    ) -> Result<EnumeratedTestCases, EnumerationError>;

    /// Runs requested tests and sends test events to the given listener.
    async fn run_tests(
        &self,
        invocations: Vec<ftest::Invocation>,
        run_options: ftest::RunOptions,
        test_component: Arc<Component>,
        run_listener: &ftest::RunListenerProxy,
    ) -> Result<(), RunTestError>;

    fn get_parallel_count(parallel: u16) -> usize {
        if parallel == 0 {
            warn!("Client passed number of concurrent tests as 0, setting it to 1.");
            return 1;
        }
        parallel.into()
    }

    /// Implements `fuchsia.test.Suite` service and runs test.
    async fn serve_test_suite(
        mut self,
        mut stream: ftest::SuiteRequestStream,
        component: Weak<Component>,
    ) -> Result<(), SuiteServerError> {
        while let Some(event) = stream.try_next().await.map_err(SuiteServerError::Stream)? {
            match event {
                ftest::SuiteRequest::GetTests { iterator, control_handle: _ } => {
                    let component = component.upgrade();
                    if component.is_none() {
                        // no component object, return, test has ended.
                        break;
                    }
                    let mut stream = iterator.into_stream().map_err(SuiteServerError::Stream)?;
                    let tests = self.enumerate_tests(component.unwrap()).await?;

                    fasync::Task::spawn(
                        async move {
                            let mut iter = tests.iter().map(|TestCaseInfo { name, enabled }| {
                                ftest::Case { name: Some(name.clone()), enabled: Some(*enabled) }
                            });
                            while let Some(ftest::CaseIteratorRequest::GetNext { responder }) =
                                stream.try_next().await?
                            {
                                // Paginate cases
                                // Page overhead of message header + vector
                                let mut bytes_used: usize = 32;
                                let mut case_count = 0;
                                for case in iter.clone() {
                                    bytes_used += measure(&case).num_bytes;
                                    if bytes_used > ZX_CHANNEL_MAX_MSG_BYTES as usize {
                                        break;
                                    }
                                    case_count += 1;
                                }
                                responder
                                    .send(&mut iter.by_ref().take(case_count))
                                    .map_err(SuiteServerError::Response)?;
                            }
                            Ok(())
                        }
                        .unwrap_or_else(|e: anyhow::Error| error!("error serving tests: {:?}", e)),
                    )
                    .detach();
                }
                ftest::SuiteRequest::Run { tests, options, listener, .. } => {
                    let component = component.upgrade();
                    if component.is_none() {
                        // no component object, return, test has ended.
                        break;
                    }

                    let listener =
                        listener.into_proxy().map_err(FidlError::ClientEndToProxy).unwrap();

                    self.run_tests(tests, options, component.unwrap(), &listener).await?;
                    listener.on_finished().map_err(RunTestError::SendFinishAllTests).unwrap();
                }
            }
        }
        Ok(())
    }
}

/// Error encountered while running suite server
#[derive(Debug, Error)]
pub enum SuiteServerError {
    #[error("test enumeration failed: {:?}", _0)]
    Enumeration(crate::errors::EnumerationError),

    #[error("error running test: {:?}", _0)]
    RunTest(crate::errors::RunTestError),

    #[error("stream failed: {:?}", _0)]
    Stream(fidl::Error),

    #[error("Cannot send fidl response: {:?}", _0)]
    Response(fidl::Error),
}

impl From<EnumerationError> for SuiteServerError {
    fn from(error: crate::errors::EnumerationError) -> Self {
        SuiteServerError::Enumeration(error)
    }
}

impl From<RunTestError> for SuiteServerError {
    fn from(error: crate::errors::RunTestError) -> Self {
        SuiteServerError::RunTest(error)
    }
}

/// Error encountered while working with the FIDL library.
#[derive(Debug, Error)]
pub enum FidlError {
    #[error("cannot convert proxy to channel")]
    ProxyToChannel,

    #[error("cannot convert client end to proxy: {:?}", _0)]
    ClientEndToProxy(fidl::Error),

    #[error("cannot create fidl proxy: {:?}", _0)]
    CreateProxy(fidl::Error),
}

/// Error encountered while working with kernel object.
#[derive(Debug, Error)]
pub enum KernelError {
    #[error("job creation failed: {:?}", _0)]
    CreateJob(zx::Status),

    #[error("error waiting for test process to exit: {:?}", _0)]
    ProcessExit(zx::Status),

    #[error("error getting info from process: {:?}", _0)]
    ProcessInfo(zx::Status),

    #[error("error creating socket: {:?}", _0)]
    CreateSocket(zx::Status),

    #[error("cannot convert zircon socket to async socket: {:?}", _0)]
    SocketToAsync(zx::Status),
}
