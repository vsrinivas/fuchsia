// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl::endpoints::{create_proxy, ClientEnd},
    fidl::HandleBased,
    fidl_fuchsia_component_runner::{
        ComponentControllerEventStream, ComponentControllerMarker, ComponentRunnerMarker,
        ComponentRunnerProxy, ComponentStartInfo,
    },
    fidl_fuchsia_data as fdata, fidl_fuchsia_process as fprocess, fidl_fuchsia_test as ftest,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_runtime as fruntime, fuchsia_zircon as zx,
    futures::{StreamExt, TryStreamExt},
    runner::component::ComponentNamespace,
    std::convert::TryInto,
};

/// Handles a single `ftest::SuiteRequestStream`.
///
/// # Parameters
/// - `test_url`: The URL for the test component to run.
/// - `program`: The program data associated with the runner request for the test component.
/// - `namespace`: The incoming namespace to provide to the test component.
/// - `stream`: The request stream to handle.
pub async fn handle_suite_requests(
    test_url: &str,
    program: Option<fdata::Dictionary>,
    namespace: ComponentNamespace,
    mut stream: ftest::SuiteRequestStream,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            ftest::SuiteRequest::GetTests { iterator, .. } => {
                let stream = iterator.into_stream()?;
                handle_case_iterator(test_url, stream).await?;
            }
            ftest::SuiteRequest::Run { tests, options: _options, listener, .. } => {
                let starnix_runner = connect_to_protocol::<ComponentRunnerMarker>()?;
                let namespace = namespace.clone();
                let program = program.clone();
                run_test_cases(tests, test_url, program, listener, namespace, starnix_runner)
                    .await?;
            }
        }
    }

    Ok(())
}

/// Runs the test cases associated with a single `ftest::SuiteRequest::Run` request.
///
/// Running the test component is delegated to an instance of the starnix runner.
///
/// # Parameters
/// - `tests`: The tests that are to be run. Each test executes an independent run of the test
/// component.
/// - `test_url`: The URL of the test component.
/// - `program`: The program data associated with the runner request for the test component.
/// - `listener`: The listener for the test run.
/// - `namespace`: The incoming namespace to provide to the test component.
/// - `starnix_runner`: A proxy to the starnix runner's `ComponentRunner`.
async fn run_test_cases(
    tests: Vec<ftest::Invocation>,
    test_url: &str,
    program: Option<fdata::Dictionary>,
    listener: ClientEnd<ftest::RunListenerMarker>,
    namespace: ComponentNamespace,
    starnix_runner: ComponentRunnerProxy,
) -> Result<(), Error> {
    let run_listener_proxy =
        listener.into_proxy().context("Can't convert run listener channel to proxy")?;

    for test in tests {
        let (case_listener_proxy, case_listener) = create_proxy::<ftest::CaseListenerMarker>()?;

        let (test_stdin, _) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let (test_stdout, stdout_client) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let (test_stderr, stderr_client) = zx::Socket::create(zx::SocketOpts::STREAM).unwrap();
        let stdin_handle_info = fprocess::HandleInfo {
            handle: test_stdin.into_handle(),
            id: fruntime::HandleInfo::new(fruntime::HandleType::FileDescriptor, 0).as_raw(),
        };
        let stdout_handle_info = fprocess::HandleInfo {
            handle: test_stdout.into_handle(),
            id: fruntime::HandleInfo::new(fruntime::HandleType::FileDescriptor, 1).as_raw(),
        };
        let stderr_handle_info = fprocess::HandleInfo {
            handle: test_stderr.into_handle(),
            id: fruntime::HandleInfo::new(fruntime::HandleType::FileDescriptor, 2).as_raw(),
        };

        run_listener_proxy.on_test_case_started(
            test,
            ftest::StdHandles {
                out: Some(stdout_client),
                err: Some(stderr_client),
                ..ftest::StdHandles::EMPTY
            },
            case_listener,
        )?;

        let (component_controller, component_controller_server_end) =
            create_proxy::<ComponentControllerMarker>()?;
        let ns = Some(ComponentNamespace::try_into(namespace.clone())?);
        let numbered_handles =
            Some(vec![stdin_handle_info, stdout_handle_info, stderr_handle_info]);
        let start_info = ComponentStartInfo {
            resolved_url: Some(test_url.to_string()),
            program: program.clone(),
            ns,
            outgoing_dir: None,
            runtime_dir: None,
            numbered_handles,
            ..ComponentStartInfo::EMPTY
        };

        starnix_runner.start(start_info, component_controller_server_end)?;
        let result = read_result(component_controller.take_event_stream()).await;
        case_listener_proxy.finished(result)?;
    }

    run_listener_proxy.on_finished()?;

    Ok(())
}

/// Reads the result of the test run from `event_stream`.
///
/// The result is determined by reading the epitaph from the provided `event_stream`.
async fn read_result(mut event_stream: ComponentControllerEventStream) -> ftest::Result_ {
    let component_epitaph = match event_stream.next().await {
        Some(Err(fidl::Error::ClientChannelClosed { status, .. })) => status,
        result => {
            fuchsia_syslog::fx_log_err!(
                "Didn't get epitaph from the component controller, instead got: {:?}",
                result
            );
            // Fail the test case here, since the component controller's epitaph couldn't be
            // read.
            zx::Status::INTERNAL
        }
    };

    match component_epitaph {
        zx::Status::OK => {
            ftest::Result_ { status: Some(ftest::Status::Passed), ..ftest::Result_::EMPTY }
        }
        _ => ftest::Result_ { status: Some(ftest::Status::Failed), ..ftest::Result_::EMPTY },
    }
}

/// Lists all the available test cases and returns them in response to
/// `ftest::CaseIteratorRequest::GetNext`.
///
/// Currently only one "test case" is returned, named `test_name`.
async fn handle_case_iterator(
    test_name: &str,
    mut stream: ftest::CaseIteratorRequestStream,
) -> Result<(), Error> {
    let mut cases_iter = vec![ftest::Case {
        name: Some(test_name.to_string()),
        enabled: Some(true),
        ..ftest::Case::EMPTY
    }]
    .into_iter();

    while let Some(event) = stream.try_next().await? {
        match event {
            ftest::CaseIteratorRequest::GetNext { responder } => {
                responder.send(&mut cases_iter)?;
            }
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::{create_proxy_and_stream, create_request_stream},
        fidl_fuchsia_component_runner::ComponentRunnerRequest,
        fuchsia_async as fasync,
        futures::TryStreamExt,
        std::convert::TryFrom,
    };

    /// Returns a `ftest::CaseIteratorProxy` that is served by `super::handle_case_iterator`.
    ///
    /// # Parameters
    /// - `test_name`: The name of the test case that is provided to `handle_case_iterator`.
    fn set_up_iterator(test_name: &str) -> ftest::CaseIteratorProxy {
        let test_name = test_name.to_string();
        let (iterator_client_end, iterator_stream) =
            create_request_stream::<ftest::CaseIteratorMarker>()
                .expect("Couldn't create case iterator");
        fasync::Task::local(async move {
            let _ = handle_case_iterator(&test_name, iterator_stream).await;
        })
        .detach();

        iterator_client_end.into_proxy().expect("Failed to create proxy")
    }

    /// Spawns a `ComponentRunnerRequestStream` server that immediately closes all incoming
    /// component controllers with the epitaph specified in `component_controller_epitaph`.
    ///
    /// This function can be used to mock the starnix runner in a way that simulates a component
    /// exiting with or without error.
    ///
    /// # Parameters
    /// - `component_controller_epitaph`: The epitaph used to close the component controller.
    ///
    /// # Returns
    /// A `ComponentRunnerProxy` that serves each run request by closing the component with the
    /// provided epitaph.
    fn spawn_runner(component_controller_epitaph: zx::Status) -> ComponentRunnerProxy {
        let (proxy, mut request_stream) =
            create_proxy_and_stream::<ComponentRunnerMarker>().unwrap();
        fasync::Task::local(async move {
            while let Some(event) =
                request_stream.try_next().await.expect("Error in test runner request stream")
            {
                match event {
                    ComponentRunnerRequest::Start {
                        start_info: _start_info, controller, ..
                    } => {
                        controller
                            .close_with_epitaph(component_controller_epitaph)
                            .expect("Could not close with epitaph");
                    }
                }
            }
        })
        .detach();
        proxy
    }

    /// Returns the status from the first test case reported to `run_listener_stream`.
    ///
    /// This is done by listening to the first `CaseListener` provided via `OnTestCaseStarted`.
    ///
    /// # Parameters
    /// - `run_listener_stream`: The run listener stream to extract the test status from.
    ///
    /// # Returns
    /// The status of the first test case that is run, or `None` if no such status is reported.
    async fn listen_to_test_result(
        mut run_listener_stream: ftest::RunListenerRequestStream,
    ) -> Option<ftest::Status> {
        match run_listener_stream.try_next().await.expect("..") {
            Some(ftest::RunListenerRequest::OnTestCaseStarted {
                invocation: _,
                std_handles: _,
                listener,
                ..
            }) => match listener
                .into_stream()
                .expect("Failed to get case listener stream")
                .try_next()
                .await
                .expect("Failed to get case listener stream request")
            {
                Some(ftest::CaseListenerRequest::Finished { result, .. }) => result.status,
                _ => None,
            },
            _ => None,
        }
    }

    /// Spawns a task that calls `super::run_test_cases` with the provided `run_listener` and
    /// `runner_proxy`. The call is made with a test cases vector consisting of one mock test case.
    fn spawn_run_test_cases(
        run_listener: ClientEnd<ftest::RunListenerMarker>,
        runner_proxy: ComponentRunnerProxy,
    ) {
        fasync::Task::local(async move {
            let _ = run_test_cases(
                vec![ftest::Invocation {
                    name: Some("".to_string()),
                    tag: Some("".to_string()),
                    ..ftest::Invocation::EMPTY
                }],
                "",
                None,
                run_listener,
                ComponentNamespace::try_from(vec![]).expect(""),
                runner_proxy,
            )
            .await;
        })
        .detach();
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_number_of_test_cases() {
        let iterator_proxy = set_up_iterator("test");
        let first_result = iterator_proxy.get_next().await.expect("Didn't get first result");
        let second_result = iterator_proxy.get_next().await.expect("Didn't get second result");

        assert_eq!(first_result.len(), 1);
        assert_eq!(second_result.len(), 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_case_name() {
        let test_name = "test_name";
        let iterator_proxy = set_up_iterator(test_name);
        let result = iterator_proxy.get_next().await.expect("Didn't get first result");
        assert_eq!(result[0].name, Some(test_name.to_string()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_case_enabled() {
        let iterator_proxy = set_up_iterator("test");
        let result = iterator_proxy.get_next().await.expect("Didn't get first result");
        assert_eq!(result[0].enabled, Some(true));
    }

    /// Tests that when starnix closes the component controller with an `OK` status, the test case
    /// passes.
    #[fasync::run_singlethreaded(test)]
    async fn test_component_controller_epitaph_ok() {
        let proxy = spawn_runner(zx::Status::OK);
        let (run_listener, run_listener_stream) =
            create_request_stream::<ftest::RunListenerMarker>()
                .expect("Couldn't create case listener");
        spawn_run_test_cases(run_listener, proxy);
        assert_eq!(listen_to_test_result(run_listener_stream).await, Some(ftest::Status::Passed));
    }

    /// Tests that when starnix closes the component controller with an error status, the test case
    /// fails.
    #[fasync::run_singlethreaded(test)]
    async fn test_component_controller_epitaph_not_ok() {
        let proxy = spawn_runner(zx::Status::INTERNAL);
        let (run_listener, run_listener_stream) =
            create_request_stream::<ftest::RunListenerMarker>()
                .expect("Couldn't create case listener");
        spawn_run_test_cases(run_listener, proxy);
        assert_eq!(listen_to_test_result(run_listener_stream).await, Some(ftest::Status::Failed));
    }
}
