// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::{create_proxy, ClientEnd},
    fidl::HandleBased,
    fidl_fuchsia_component as fcomponent, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_component_runner as frunner, fidl_fuchsia_data as fdata, fidl_fuchsia_io as fio,
    fidl_fuchsia_process as fprocess, fidl_fuchsia_test as ftest,
    fuchsia_component::client as fclient,
    fuchsia_runtime as fruntime, fuchsia_zircon as zx,
    futures::{StreamExt, TryStreamExt},
    rand::Rng,
    runner::component::ComponentNamespace,
    std::convert::TryInto,
    url::Url,
};

/// The name of the collection in which the starnix runner is instantiated.
const RUNNERS_COLLECTION: &str = "runners";

/// Replace the arguments in `program` with `test_arguments`, which were provided to the test
/// framework directly.
fn replace_program_args(test_arguments: Vec<String>, program: &mut fdata::Dictionary) {
    const ARGS_KEY: &str = "args";
    if test_arguments.is_empty() {
        return;
    }

    let new_args = fdata::DictionaryEntry {
        key: ARGS_KEY.to_string(),
        value: Some(Box::new(fdata::DictionaryValue::StrVec(test_arguments))),
    };
    if let Some(entries) = &mut program.entries {
        if let Some(index) = entries.iter().position(|entry| entry.key == ARGS_KEY) {
            entries.remove(index);
        }
        entries.push(new_args);
    } else {
        let entries = vec![new_args];
        program.entries = Some(entries);
    };
}

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
            ftest::SuiteRequest::Run { tests, options, listener, .. } => {
                let namespace = namespace.clone();
                let mut program = program.clone();
                let runner_name = format!("starnix-runner-{}", rand::thread_rng().gen::<u64>());
                let (starnix_runner, realm) =
                    instantiate_runner_in_realm(&namespace, &runner_name, test_url).await?;

                if let Some(test_args) = options.arguments {
                    if let Some(program) = &mut program {
                        replace_program_args(test_args, program);
                    }
                }

                run_test_cases(
                    tests,
                    test_url,
                    program,
                    listener,
                    namespace,
                    &runner_name,
                    realm,
                    starnix_runner,
                )
                .await?;
            }
        }
    }

    Ok(())
}

fn get_realm(namespace: &ComponentNamespace) -> Result<fcomponent::RealmProxy, Error> {
    namespace
        .items()
        .iter()
        .flat_map(|(s, d)| {
            if s == "/svc" {
                Some(fuchsia_component::client::connect_to_protocol_at_dir_root::<
                    fcomponent::RealmMarker,
                >(&d))
            } else {
                None
            }
        })
        .next()
        .ok_or_else(|| anyhow!("Unable to find /svc"))?
}

async fn open_exposed_directory(
    realm: &fcomponent::RealmProxy,
    child_name: &str,
    collection_name: &str,
) -> Result<fio::DirectoryProxy, Error> {
    let (directory_proxy, server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()?;
    realm
        .open_exposed_dir(
            &mut fdecl::ChildRef {
                name: child_name.into(),
                collection: Some(collection_name.into()),
            },
            server_end,
        )
        .await?
        .map_err(|e| {
            anyhow!(
                "failed to bind to child {} in collection {:?}: {:?}",
                child_name,
                collection_name,
                e
            )
        })?;
    Ok(directory_proxy)
}

/// Instantiates a starnix runner in the realm of the given namespace.
///
/// # Parameters
///   - `namespace`: The namespace in which to fetch the realm to instantiate the runner in.
///   - `runner_name`: The name of the runner child.
///   - `runner_url`: The url of the runner component, excluding the `meta/starnix_runner.cm`.
///
/// Returns a proxy to the instantiated runner as well as to the realm in which the runner is
/// instantiated.
async fn instantiate_runner_in_realm(
    namespace: &ComponentNamespace,
    runner_name: &str,
    runner_url: &str,
) -> Result<(frunner::ComponentRunnerProxy, fcomponent::RealmProxy), Error> {
    let mut runner_url = Url::parse(runner_url)?;
    runner_url.set_fragment(Some("meta/starnix_runner.cm"));

    let realm = get_realm(namespace)?;
    realm
        .create_child(
            &mut fdecl::CollectionRef { name: RUNNERS_COLLECTION.into() },
            fdecl::Child {
                name: Some(runner_name.to_string()),
                url: Some(runner_url.to_string()),
                startup: Some(fdecl::StartupMode::Lazy),
                ..fdecl::Child::EMPTY
            },
            fcomponent::CreateChildArgs::EMPTY,
        )
        .await?
        .map_err(|e| anyhow::anyhow!("failed to create runner child: {:?}", e))?;
    let runner_outgoing = open_exposed_directory(&realm, &runner_name, RUNNERS_COLLECTION).await?;
    let starnix_runner = fclient::connect_to_protocol_at_dir_root::<frunner::ComponentRunnerMarker>(
        &runner_outgoing,
    )?;

    Ok((starnix_runner, realm))
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
    runner_name: &str,
    realm: fcomponent::RealmProxy,
    starnix_runner: frunner::ComponentRunnerProxy,
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
            create_proxy::<frunner::ComponentControllerMarker>()?;
        let ns = Some(ComponentNamespace::try_into(namespace.clone())?);
        let numbered_handles =
            Some(vec![stdin_handle_info, stdout_handle_info, stderr_handle_info]);
        let (outgoing_dir, _outgoing_dir) =
            zx::Channel::create().expect("Failed to create channel.");
        let start_info = frunner::ComponentStartInfo {
            resolved_url: Some(test_url.to_string()),
            program: program.clone(),
            ns,
            outgoing_dir: Some(outgoing_dir.into()),
            runtime_dir: None,
            numbered_handles,
            ..frunner::ComponentStartInfo::EMPTY
        };

        starnix_runner.start(start_info, component_controller_server_end)?;
        let result = read_result(component_controller.take_event_stream()).await;
        case_listener_proxy.finished(result)?;
    }

    realm
        .destroy_child(&mut fdecl::ChildRef {
            name: runner_name.to_string(),
            collection: Some(RUNNERS_COLLECTION.into()),
        })
        .await?
        .map_err(|e| anyhow::anyhow!("failed to destory runner child: {:?}", e))?;
    run_listener_proxy.on_finished()?;

    Ok(())
}

/// Reads the result of the test run from `event_stream`.
///
/// The result is determined by reading the epitaph from the provided `event_stream`.
async fn read_result(mut event_stream: frunner::ComponentControllerEventStream) -> ftest::Result_ {
    let component_epitaph = match event_stream.next().await {
        Some(Err(fidl::Error::ClientChannelClosed { status, .. })) => status,
        result => {
            tracing::error!(
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
        super::*, fidl::endpoints::create_request_stream, fuchsia_async as fasync,
        futures::TryStreamExt, std::convert::TryFrom,
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
    fn spawn_runner(component_controller_epitaph: zx::Status) -> frunner::ComponentRunnerProxy {
        let (proxy, mut request_stream) =
            fidl::endpoints::create_proxy_and_stream::<frunner::ComponentRunnerMarker>().unwrap();
        fasync::Task::local(async move {
            while let Some(event) =
                request_stream.try_next().await.expect("Error in test runner request stream")
            {
                match event {
                    frunner::ComponentRunnerRequest::Start {
                        start_info: _start_info,
                        controller,
                        ..
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
        starnix_runner: frunner::ComponentRunnerProxy,
    ) {
        fasync::Task::local(async move {
            let runner_name = format!("starnix-runner-{}", rand::thread_rng().gen::<u64>());
            let (realm, _request_stream) =
                fidl::endpoints::create_proxy_and_stream::<fcomponent::RealmMarker>().unwrap();
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
                &runner_name,
                realm,
                starnix_runner,
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
        let starnix_runner = spawn_runner(zx::Status::OK);
        let (run_listener, run_listener_stream) =
            create_request_stream::<ftest::RunListenerMarker>()
                .expect("Couldn't create case listener");
        spawn_run_test_cases(run_listener, starnix_runner);
        assert_eq!(listen_to_test_result(run_listener_stream).await, Some(ftest::Status::Passed));
    }

    /// Tests that when starnix closes the component controller with an error status, the test case
    /// fails.
    #[fasync::run_singlethreaded(test)]
    async fn test_component_controller_epitaph_not_ok() {
        let starnix_runner = spawn_runner(zx::Status::INTERNAL);
        let (run_listener, run_listener_stream) =
            create_request_stream::<ftest::RunListenerMarker>()
                .expect("Couldn't create case listener");
        spawn_run_test_cases(run_listener, starnix_runner);
        assert_eq!(listen_to_test_result(run_listener_stream).await, Some(ftest::Status::Failed));
    }
}
