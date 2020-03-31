// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::errors::*,
    fidl_fuchsia_test as ftest,
    fidl_fuchsia_test::{Invocation, RunListenerProxy},
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_vlog},
    fuchsia_zircon as zx,
    futures::future::abortable,
    futures::future::AbortHandle,
    futures::prelude::*,
    regex::Regex,
    std::{
        str::from_utf8,
        sync::{Arc, Weak},
    },
    test_runners_lib::{
        elf_component::{Component, SuiteServer},
        LogStreamReader,
    },
    thiserror::Error,
};

/// Error encountered while working fidl lib.
#[derive(Debug, Error)]
pub enum FidlError {
    #[error("cannot convert client end to proxy: {:?}", _0)]
    ClientEndToProxy(fidl::Error),
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
}

/// Implements `fuchsia.test.Suite` and runs provided test.
pub struct TestServer {
    /// Cache to store enumerated test names.
    test_list: Option<Vec<String>>,
}

impl SuiteServer for TestServer {
    /// Run this server.
    fn run(
        self,
        weak_test_component: Weak<Component>,
        test_url: &str,
        request_stream: fidl_fuchsia_test::SuiteRequestStream,
    ) -> AbortHandle {
        let test_url = test_url.clone().to_owned();
        let (fut, test_suite_abortable_handle) =
            abortable(self.serve_test_suite(request_stream, weak_test_component.clone()));

        fasync::spawn_local(async move {
            match fut.await {
                Ok(result) => {
                    if let Err(e) = result {
                        fx_log_err!("server failed for test {}: {:?}", test_url, e);
                    }
                }
                Err(e) => fx_log_err!("server aborted for test {}: {:?}", test_url, e),
            }
            fx_vlog!(1, "Done running server for {}.", test_url);
        });
        test_suite_abortable_handle
    }
}

impl TestServer {
    /// Creates new test server.
    /// Clients should call this function to create new object and then call `serve_test_suite`.
    pub fn new() -> Self {
        Self { test_list: None }
    }

    async fn run_test(
        &self,
        _invocation: Invocation,
        _test_component: Arc<Component>,
        _run_listener: &RunListenerProxy,
    ) -> Result<(), RunTestError> {
        Ok(())
    }

    /// Runs requested tests and sends test events to the given listener.
    // TODO(45852): Support disabled tests.
    // TODO(45853): Support test stdout, or devise a mechanism to replace it.
    pub async fn run_tests(
        &self,
        invocations: Vec<Invocation>,
        test_component: Arc<Component>,

        run_listener: &RunListenerProxy,
    ) -> Result<(), RunTestError> {
        for invocation in invocations {
            self.run_test(invocation, test_component.clone(), run_listener).await?;
        }
        Ok(())
    }

    /// Lauches a process that lists the tests without actually running any of them. It then parses
    /// the output of that process into a vector of strings.
    ///
    /// Example output for rust test process:
    ///
    /// tests::purposefully_failing_test: test
    /// tests::test_full_path: test
    /// tests::test_minimal_path: test
    ///
    /// 3 tests, 0 benchmarks
    ///
    async fn enumerate_tests(
        &mut self,
        test_component: Arc<Component>,
    ) -> Result<Vec<String>, EnumerationError> {
        if let Some(t) = &self.test_list {
            return Ok(t.clone());
        }

        let mut args = vec!["-Z".to_owned(), "unstable-options".to_owned(), "--list".to_owned()];
        args.extend(test_component.args.clone());

        // Load bearing to hold job guard.
        let (process, _job, stdlogger) =
            test_runners_lib::launch_process(test_runners_lib::LaunchProcessArgs {
                bin_path: &test_component.binary,
                process_name: &test_component.name,
                job: Some(
                    test_component.job.create_child_job().map_err(KernelError::CreateJob).unwrap(),
                ),
                ns: test_component.ns.clone().map_err(NamespaceError::Clone)?,
                args: Some(args),
                name_infos: None,
                environs: None,
            })
            .await?;

        // collect stdout in background before waiting for process termination.
        let std_reader = LogStreamReader::new(stdlogger);

        fasync::OnSignals::new(&process, zx::Signals::PROCESS_TERMINATED)
            .await
            .map_err(KernelError::ProcessExit)
            .unwrap();

        let logs = std_reader.get_logs().await?;
        // TODO(4610): logs might not be utf8, fix the code.
        let output = from_utf8(&logs)?;
        let process_info = process.info().map_err(KernelError::ProcessInfo).unwrap();
        if process_info.return_code != 0 {
            // TODO(45858): Add a error logger to API so that we can display test stdout logs.
            fx_log_err!("Failed getting list of tests:\n{}", output);
            return Err(EnumerationError::ListTest);
        }

        let mut test_names = vec![];
        let regex = Regex::new(r"^(.*): test$").unwrap();

        for test in output.split("\n") {
            if let Some(capture) = regex.captures(test) {
                if let Some(name) = capture.get(1) {
                    test_names.push(String::from(name.as_str()));
                }
            }
        }

        self.test_list = Some(test_names.clone());

        return Ok(test_names);
    }

    /// Implements `fuchsia.test.Suite` service and runs test.
    pub async fn serve_test_suite(
        mut self,
        mut stream: ftest::SuiteRequestStream,
        component: Weak<Component>,
    ) -> Result<(), SuiteServerError> {
        while let Some(event) = stream.try_next().await.map_err(SuiteServerError::Stream)? {
            match event {
                ftest::SuiteRequest::GetTests { iterator, control_handle: _ } => {
                    let component = component.upgrade();
                    if component.is_none() {
                        // no component object, return, test has ended, channel would be closed shortly.
                        break;
                    }
                    let mut stream = iterator.into_stream().map_err(SuiteServerError::Stream)?;
                    let tests = self.enumerate_tests(component.unwrap()).await?;

                    fasync::spawn(
                        async move {
                            let mut iter =
                                tests.into_iter().map(|name| ftest::Case { name: Some(name) });
                            while let Some(ftest::CaseIteratorRequest::GetNext { responder }) =
                                stream.try_next().await?
                            {
                                const MAX_CASES_PER_PAGE: usize = 50;
                                responder
                                    .send(&mut iter.by_ref().take(MAX_CASES_PER_PAGE))
                                    .map_err(SuiteServerError::Response)?;
                            }
                            Ok(())
                        }
                        .unwrap_or_else(|e: anyhow::Error| {
                            fx_log_err!("error serving tests: {:?}", e)
                        }),
                    );
                }
                ftest::SuiteRequest::Run { tests, options: _, listener, .. } => {
                    let component = component.upgrade();
                    if component.is_none() {
                        // no component object, return, test has ended, channel would be closed shortly.
                        break;
                    }

                    let listener =
                        listener.into_proxy().map_err(FidlError::ClientEndToProxy).unwrap();

                    self.run_tests(tests, component.unwrap(), &listener).await?;
                    listener.on_finished().map_err(RunTestError::SendFinishAllTests).unwrap();
                }
            }
        }
        Ok(())
    }
}

// TODO(45854): Add integration tests.
#[cfg(test)]
mod tests {
    use {
        super::*, anyhow::Error, fidl::endpoints::ClientEnd,
        fidl_fuchsia_component_runner as fcrunner, fidl_fuchsia_io::OPEN_RIGHT_READABLE,
        fuchsia_runtime::job_default, runner::component::ComponentNamespace,
        runner::component::ComponentNamespaceError, std::convert::TryFrom, test_runners_lib::*,
    };

    fn create_ns_from_current_ns(
        dir_paths: Vec<(&str, u32)>,
    ) -> Result<ComponentNamespace, ComponentNamespaceError> {
        let mut ns = vec![];
        for (path, permission) in dir_paths {
            let chan = io_util::open_directory_in_namespace(path, permission)
                .unwrap()
                .into_channel()
                .unwrap()
                .into_zx_channel();
            let handle = ClientEnd::new(chan);

            ns.push(fcrunner::ComponentNamespaceEntry {
                path: Some(path.to_string()),
                directory: Some(handle),
            });
        }
        ComponentNamespace::try_from(ns)
    }

    macro_rules! current_job {
        () => {
            job_default().duplicate(zx::Rights::SAME_RIGHTS)?
        };
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_simple_test() -> Result<(), Error> {
        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/simple_test#test.cm".to_owned(),
            name: "bin/simple_rust_tests".to_owned(),
            binary: "bin/simple_rust_tests".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let mut server = TestServer::new();
        let mut expected = vec![
            "my_tests::simple_test_one",
            "my_tests::simple_test_two",
            "my_tests::simple_test_three",
            "my_tests::simple_test_four",
        ]
        .into_iter()
        .map(|s| s.to_owned())
        .collect::<Vec<String>>();
        let mut actual = server.enumerate_tests(component.clone()).await?;

        expected.sort();
        actual.sort();
        assert_eq!(expected, actual);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_empty_test_file() -> Result<(), Error> {
        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "bin/no_rust_tests".to_owned(),
            binary: "bin/no_rust_tests".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let mut server = TestServer::new();

        assert_eq!(server.enumerate_tests(component.clone()).await?, Vec::<String>::new());

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_huge_test() -> Result<(), Error> {
        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/simple_test#test.cm".to_owned(),
            name: "bin/huge_rust_tests".to_owned(),
            binary: "bin/huge_rust_tests".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let mut server = TestServer::new();

        let mut actual_tests = server.enumerate_tests(component.clone()).await?;

        actual_tests.sort();

        let mut expected: Vec<String> = (1..=1000).map(|i| format!("test_{}", i)).collect();
        expected.sort();
        assert_eq!(expected, actual_tests);

        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn enumerate_invalid_file() -> Result<(), Error> {
        let ns = create_ns_from_current_ns(vec![("/pkg", OPEN_RIGHT_READABLE)])?;

        let component = Arc::new(Component {
            url: "fuchsia-pkg://fuchsia.com/sample_test#test.cm".to_owned(),
            name: "bin/invalid".to_owned(),
            binary: "bin/invalid".to_owned(),
            args: vec![],
            ns: ns,
            job: current_job!(),
        });
        let mut server = TestServer::new();

        let err = server
            .enumerate_tests(component.clone())
            .await
            .expect_err("this function have error-ed out due to non-existent file.");

        match err {
            EnumerationError::LaunchTest(LaunchError::LoadInfo(
                runner::component::LaunchError::LoadingExecutable(_),
            )) => { /*this is the error we expect, do nothing*/ }
            err => panic!("invalid error:{}", err),
        };

        Ok(())
    }
}
