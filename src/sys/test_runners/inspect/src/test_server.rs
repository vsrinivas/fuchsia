// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::error::ComponentError,
    crate::eval::EvaluationContext,
    crate::spec::{Accessor, ProgramSpec},
    diagnostics_reader::{ArchiveReader, Inspect},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_diagnostics as fdiagnostics,
    fidl_fuchsia_test as ftest, fuchsia_async as fasync,
    fuchsia_component::client,
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon::{Duration, Status, Time},
    futures::{
        channel::oneshot,
        future::{abortable, select, Either},
        pin_mut, select, stream, FutureExt, StreamExt, TryStreamExt,
    },
    log::warn,
    std::convert::TryFrom,
    std::sync::Arc,
};

const NANOS_IN_SECONDS: f64 = 1_000_000_000.0;
const DEFAULT_PARALLEL: u16 = 1;

/// Output a log for the test. Automatically prepends the current monotonic time.
macro_rules! test_stdout {
    ($logger:ident, $format:literal) => {
        let formatted_with_time = format!(
            "[{:05.3}] {}\n",
            (Time::get_monotonic().into_nanos() as f64 / NANOS_IN_SECONDS),
            $format
        );
        $logger.write(formatted_with_time.as_bytes()).ok()
    };
    ($logger:ident, $format:literal, $($content:expr),*) => {
        let formatted = format!($format, $($content, )*);
        let formatted_with_time = format!(
            "[{:05.3}] {}\n",
            (Time::get_monotonic().into_nanos() as f64 / NANOS_IN_SECONDS),
            formatted
        );
        $logger.write(formatted_with_time.as_bytes()).ok()
    };
}

/// Implements `fuchsia.test.Suite` and runs provided test.
pub struct TestServer {
    spec: ProgramSpec,
    controller: ServerEnd<fcrunner::ComponentControllerMarker>,
    out_channel: fuchsia_zircon::Channel,
}

impl TestServer {
    /// Creates new test server.
    pub fn new(
        start_info: fcrunner::ComponentStartInfo,
        controller: ServerEnd<fcrunner::ComponentControllerMarker>,
    ) -> Result<Self, ComponentError> {
        match ProgramSpec::try_from(
            start_info.program.ok_or(ComponentError::MissingRequiredKey("program"))?,
        ) {
            Ok(spec) => Ok(Self {
                spec,
                controller,
                out_channel: start_info
                    .outgoing_dir
                    .ok_or(ComponentError::MissingOutgoingChannel)?
                    .into_channel(),
            }),
            Err(e) => {
                warn!("Error loading spec: {}", e);
                controller.close_with_epitaph(Status::INVALID_ARGS).unwrap_or_default();
                Err(e)
            }
        }
    }

    /// Run the individual named test case from the given ProgramSpec.
    ///
    /// Output logs are written to the given socket.
    ///
    /// Returns true on pass and false on failure.
    async fn run_case(spec: &ProgramSpec, case: &str, logs: fuchsia_zircon::Socket) -> bool {
        let case = match spec.cases.get(case) {
            Some(case) => case,
            None => {
                test_stdout!(logs, "Failed to find test case");
                return false;
            }
        };

        let svc = match spec.accessor {
            Accessor::All => "/svc/fuchsia.diagnostics.RealArchiveAccessor",
            Accessor::Feedback => "/svc/fuchsia.diagnostics.RealFeedbackArchiveAccessor",
            Accessor::Legacy => "/svc/fuchsia.diagnostics.RealLegacyMetricsArchiveAccessor",
        };

        test_stdout!(logs, "Reading `{}` from `{}`", case.key, svc);

        let context = match EvaluationContext::try_from(case) {
            Ok(c) => c,
            Err(e) => {
                test_stdout!(logs, "Failed to set up evaluation: {:?}\n", e);
                return false;
            }
        };

        let end_time = Time::get_monotonic() + Duration::from_seconds(spec.timeout_seconds);

        while end_time > Time::get_monotonic() {
            let start_time = Time::get_monotonic();

            let proxy = match client::connect_to_protocol_at_path::<
                fdiagnostics::ArchiveAccessorMarker,
            >(&svc)
            {
                Ok(p) => p,
                Err(e) => {
                    test_stdout!(logs, "Failed to connect to accessor: {:?}", e);
                    return false;
                }
            };

            test_stdout!(logs, "Attempting read");

            match ArchiveReader::new()
                .retry_if_empty(false)
                .with_archive(proxy)
                .with_timeout(end_time - start_time)
                .add_selector(case.selector.as_str())
                .snapshot_raw::<Inspect>()
                .await
            {
                Ok(json) => {
                    match context.run(&serde_json::to_string_pretty(&json).unwrap_or_default()) {
                        Ok(_) => {
                            test_stdout!(logs, "Test case passed");
                            return true;
                        }
                        Err(e) => {
                            test_stdout!(logs, "Test case attempt failed: {}", e);
                        }
                    }
                }
                Err(e) => {
                    test_stdout!(logs, "Failed to obtain data: {}", e);
                }
            }

            let sleep_time = Duration::from_seconds(1);

            if end_time - Time::get_monotonic() >= Duration::from_seconds(0) {
                test_stdout!(
                    logs,
                    "Retrying after {}s, timeout after {}s",
                    sleep_time.into_seconds(),
                    (end_time - Time::get_monotonic()).into_seconds()
                );
                fasync::Timer::new(Time::after(sleep_time)).await;
            }
        }

        false
    }

    pub async fn execute(self) {
        let spec = Arc::new(self.spec);
        let controller = self.controller;

        let mut fs = ServiceFs::new_local();
        let (done_sender, done_fut) = oneshot::channel::<()>();
        let done_fut = done_fut.shared();
        fs.dir("svc").add_fidl_service(move |mut stream: ftest::SuiteRequestStream| {
            let spec = spec.clone();
            let mut done_fut = done_fut.clone();
            fasync::Task::spawn(async move {
                // Listen either for the next value form the stream, or the done signal.
                while let Ok(Some(req)) = select! {
                next = stream.try_next() => next,
                _ = done_fut => Ok(None) }
                {
                    match req {
                        ftest::SuiteRequest::GetTests { iterator, .. } => {
                            let mut names = spec.test_names().into_iter().map(|n| ftest::Case {
                                name: Some(n),
                                enabled: Some(true),
                                ..ftest::Case::EMPTY
                            });
                            let mut done_fut = done_fut.clone();
                            fasync::Task::spawn(async move {
                                if let Ok(mut stream) = iterator.into_stream() {
                                    while let Ok(Some(req)) = select! {
                                    next = stream.try_next() => next,
                                    _ = done_fut => Ok(None)}
                                    {
                                        match req {
                                            ftest::CaseIteratorRequest::GetNext {
                                                responder,
                                                ..
                                            } => {
                                                // Continually drain the |names| iterator on each
                                                // call.
                                                responder
                                                    .send(&mut names.by_ref())
                                                    .unwrap_or_default();
                                            }
                                        }
                                    }
                                }
                            })
                            .detach();
                        }
                        ftest::SuiteRequest::Run { tests, options, listener, .. } => {
                            let proxy = listener
                                .into_proxy()
                                .expect("Can't convert listener channel to proxy");

                            let mut tasks = vec![];
                            let parallel = options.parallel.unwrap_or(DEFAULT_PARALLEL);
                            for test in tests.into_iter() {
                                let spec = spec.clone();
                                let proxy = proxy.clone();
                                tasks.push(async move {
                                    let (stdout_end, stdout) = fuchsia_zircon::Socket::create(
                                        fuchsia_zircon::SocketOpts::empty(),
                                    )
                                    .expect("cannot create socket.");

                                    let name = test.name.clone().unwrap_or_default();

                                    let (case_listener_proxy, case_listener) =
                                        fidl::endpoints::create_proxy::<ftest::CaseListenerMarker>(
                                        )
                                        .expect("cannot create proxy");

                                    proxy
                                        .on_test_case_started(
                                            test,
                                            ftest::StdHandles {
                                                out: Some(stdout_end),
                                                ..ftest::StdHandles::EMPTY
                                            },
                                            case_listener,
                                        )
                                        .expect("on_test_case_started failed");

                                    let status =
                                        match TestServer::run_case(&spec, &name, stdout).await {
                                            true => ftest::Status::Passed,
                                            false => ftest::Status::Failed,
                                        };

                                    let result = ftest::Result_ {
                                        status: Some(status),
                                        ..ftest::Result_::EMPTY
                                    };

                                    case_listener_proxy
                                        .finished(result)
                                        .expect("on_test_case_finished failed");
                                });
                            }
                            let done_fut = done_fut.clone();
                            fasync::Task::spawn(async move {
                                let proxy = proxy.clone();

                                let chunked_tasks_fut = stream::iter(tasks.into_iter())
                                    .buffered(parallel.into())
                                    .collect::<()>();

                                // If all tasks finished before abort, report OnFinished to the
                                // listener.
                                match select(done_fut, chunked_tasks_fut).await {
                                    Either::Right(_) => {
                                        proxy.on_finished().ok();
                                    }
                                    _ => {}
                                }
                            })
                            .detach();
                        }
                    }
                }
            })
            .detach();
        });

        if let Err(e) = fs.serve_connection(self.out_channel) {
            warn!("Failed to serve connection for child component {:?}", e);
        }

        let (fut, abort_handle) = abortable(fs.collect::<()>());

        let controller_fut = async move {
            if let Ok(mut stream) = controller.into_stream() {
                let mut done_sender = Some(done_sender);
                while let Ok(Some(request)) = stream.try_next().await {
                    match request {
                        fcrunner::ComponentControllerRequest::Stop { .. }
                        | fcrunner::ComponentControllerRequest::Kill { .. } => {
                            if let Some(done_sender) = done_sender.take() {
                                done_sender.send(()).ok();
                            }
                            abort_handle.abort();
                        }
                    }
                }
            }
        };

        pin_mut!(fut);
        pin_mut!(controller_fut);

        select(fut, controller_fut).await;
    }
}
