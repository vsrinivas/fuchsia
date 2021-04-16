// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl::endpoints::{create_proxy, ClientEnd},
    fidl_fuchsia_component_runner::{
        ComponentControllerMarker, ComponentRunnerMarker, ComponentStartInfo,
    },
    fidl_fuchsia_data as fdata, fidl_fuchsia_test as ftest,
    fuchsia_component::client::connect_to_service,
    futures::TryStreamExt,
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
                let namespace = namespace.clone()?;
                let program = program.clone();
                run_test_cases(tests, test_url, program, listener, namespace).await?;
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
async fn run_test_cases(
    tests: Vec<ftest::Invocation>,
    test_url: &str,
    program: Option<fdata::Dictionary>,
    listener: ClientEnd<ftest::RunListenerMarker>,
    namespace: ComponentNamespace,
) -> Result<(), Error> {
    let run_listener_proxy =
        listener.into_proxy().context("Can't convert run listener channel to proxy")?;

    for test in tests {
        // The run listener expects a socket for the "primary log". This is not connected
        // to any actual output of the test component.
        let (log_end, _logger) =
            fuchsia_zircon::Socket::create(fuchsia_zircon::SocketOpts::empty())?;
        let (case_listener_proxy, case_listener) = create_proxy::<ftest::CaseListenerMarker>()?;
        run_listener_proxy.on_test_case_started(test, log_end, case_listener)?;

        // Connect to the starnix runner to run the test component.
        let starnix_runner = connect_to_service::<ComponentRunnerMarker>()?;
        let (_, component_controller_server_end) = create_proxy::<ComponentControllerMarker>()?;
        let ns = Some(ComponentNamespace::try_into(namespace.clone()?)?);
        let start_info = ComponentStartInfo {
            resolved_url: Some(test_url.to_string()),
            program: program.clone(),
            ns,
            outgoing_dir: None,
            runtime_dir: None,
            ..ComponentStartInfo::EMPTY
        };

        starnix_runner.start(start_info, component_controller_server_end)?;
        // The result is always set to pass. This should be updated to return a result
        // based on whether or not the test component was run successfully.
        let result =
            ftest::Result_ { status: Some(ftest::Status::Passed), ..ftest::Result_::EMPTY };

        case_listener_proxy.finished(result)?;
    }

    run_listener_proxy.on_finished()?;

    Ok(())
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
    use {super::*, fidl::endpoints::create_request_stream, fuchsia_async as fasync};

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
}
