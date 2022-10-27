// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context as _;
use fuchsia_component::client;
use fuchsia_fs::file::read_in_namespace_to_string;
use futures::{StreamExt as _, TryStreamExt as _};

#[derive(Debug)]
struct Expectation<'a>(&'a ser::Expectation);

impl Expectation<'_> {
    fn expected_outcome(&self, invocation: &fidl_fuchsia_test::Invocation) -> Option<Outcome> {
        let Expectation(expectation) = self;
        let (ser::Matchers { matchers }, outcome) = match expectation {
            ser::Expectation::ExpectFailure(a) => (a, Outcome::Fail),
            ser::Expectation::ExpectPass(a) => (a, Outcome::Pass),
            ser::Expectation::Skip(a) => (a, Outcome::Skip),
        };
        let name = invocation
            .name
            .as_ref()
            .unwrap_or_else(|| panic!("invocation {invocation:?} did not have name"));

        matchers.iter().any(|matcher| matcher.matches(name)).then_some(outcome)
    }
}

#[derive(serde::Deserialize, Debug, Clone, Copy, PartialEq, Eq)]
enum Outcome {
    Pass,
    Fail,
    Skip,
}

impl From<fidl_fuchsia_test::Status> for Outcome {
    fn from(status: fidl_fuchsia_test::Status) -> Self {
        match status {
            fidl_fuchsia_test::Status::Passed => Outcome::Pass,
            fidl_fuchsia_test::Status::Failed => Outcome::Fail,
            fidl_fuchsia_test::Status::Skipped => Outcome::Skip,
        }
    }
}

enum ExpectationError {
    Mismatch { got: Outcome, want: Outcome },
    NoExpectationFound,
}

struct CaseStart {
    invocation: fidl_fuchsia_test::Invocation,
    std_handles: fidl_fuchsia_test::StdHandles,
}

struct CaseEnd {
    result: fidl_fuchsia_test::Result_,
}

#[derive(Debug)]
struct ExpectationsComparer {
    expectations: ser::Expectations,
}

impl ExpectationsComparer {
    fn expected_outcome(&self, invocation: &fidl_fuchsia_test::Invocation) -> Option<Outcome> {
        self.expectations
            .expectations
            .iter()
            .rev()
            .find_map(|expectation| Expectation(expectation).expected_outcome(invocation))
    }

    fn handle_result(
        &self,
        invocation: &fidl_fuchsia_test::Invocation,
        result: fidl_fuchsia_test::Result_,
    ) -> Result<fidl_fuchsia_test::Status, ExpectationError> {
        let fidl_fuchsia_test::Result_ { status, .. } = result;
        let got_outcome = Outcome::from(status.expect("fidl_fuchsia_test::Result_ had no status"));
        let want_outcome = self.expected_outcome(invocation);
        match (got_outcome, want_outcome) {
            // TODO(https://fxbug.dev/113117): Determine how to handle tests skipped at runtime.
            (Outcome::Skip, None | Some(Outcome::Fail | Outcome::Pass | Outcome::Skip)) => {
                Ok(fidl_fuchsia_test::Status::Skipped)
            }
            (Outcome::Pass | Outcome::Fail, None) => Err(ExpectationError::NoExpectationFound),
            (got_outcome, Some(want_outcome)) if got_outcome == want_outcome => {
                Ok(fidl_fuchsia_test::Status::Passed)
            }
            (got_outcome, Some(want_outcome)) => {
                Err(ExpectationError::Mismatch { got: got_outcome, want: want_outcome })
            }
        }
    }

    async fn handle_case(
        &self,
        run_listener_proxy: &fidl_fuchsia_test::RunListenerProxy,
        CaseStart { invocation, std_handles }: CaseStart,
        end_stream: impl futures::TryStream<Ok = CaseEnd, Error = anyhow::Error>,
    ) -> Result<(), anyhow::Error> {
        let (case_listener_proxy, case_listener) =
            fidl::endpoints::create_proxy().context("error creating CaseListenerProxy")?;
        run_listener_proxy
            .on_test_case_started(invocation.clone(), std_handles, case_listener)
            .context("error calling run_listener_proxy.on_test_case_started(...)")?;

        let invocation = &invocation;
        let name = invocation.name.as_ref().expect("fuchsia.test/Invocation had no name");
        let case_listener_proxy = &case_listener_proxy;
        end_stream
            .try_for_each(|CaseEnd { result }| async move {
                let status = match self.handle_result(invocation, result) {
                    Ok(status) => status,
                    Err(err) => {
                        match err {
                            ExpectationError::Mismatch { got, want } => tracing::error!(
                                // TODO(https://fxbug.dev/113119): Decide what error message to use
                                // here.
                                "Failing test case {}: got {:?}, expected {:?}",
                                name,
                                got,
                                want,
                            ),
                            ExpectationError::NoExpectationFound => {
                                return Err(anyhow::anyhow!("No expectation matches {}", name))
                            }
                        };
                        fidl_fuchsia_test::Status::Failed
                    }
                };

                case_listener_proxy
                    .finished(fidl_fuchsia_test::Result_ {
                        status: Some(status),
                        ..fidl_fuchsia_test::Result_::EMPTY
                    })
                    .map_err(anyhow::Error::from)
            })
            .await
    }

    async fn handle_suite_run_request(
        &self,
        suite_proxy: &fidl_fuchsia_test::SuiteProxy,
        tests: Vec<fidl_fuchsia_test::Invocation>,
        options: fidl_fuchsia_test::RunOptions,
        listener: fidl::endpoints::ClientEnd<fidl_fuchsia_test::RunListenerMarker>,
    ) -> Result<(), anyhow::Error> {
        let tests_and_expects = tests.into_iter().map(|invocation| {
            let outcome = self.expected_outcome(&invocation);
            (invocation, outcome)
        });
        let (skipped, not_skipped): (Vec<_>, Vec<_>) = tests_and_expects
            .partition(|(_invocation, outcome)| matches!(outcome, Some(Outcome::Skip)));

        let listener_proxy =
            listener.into_proxy().context("error turning RunListener client end into proxy")?;
        for (invocation, _) in skipped {
            let (case_listener_proxy, case_listener_server_end) =
                fidl::endpoints::create_proxy().context("error creating case listener proxy")?;
            listener_proxy
                .on_test_case_started(
                    invocation,
                    fidl_fuchsia_test::StdHandles::EMPTY,
                    case_listener_server_end,
                )
                .context("error while telling run listener that a skipped test case had started")?;
            case_listener_proxy
                .finished(fidl_fuchsia_test::Result_ {
                    status: Some(fidl_fuchsia_test::Status::Skipped),
                    ..fidl_fuchsia_test::Result_::EMPTY
                })
                .context(
                    "error while telling run listener that a skipped test case had finished",
                )?;
        }

        let case_stream = {
            let (listener, listener_request_stream) = fidl::endpoints::create_request_stream()
                .context("error creating run listener request stream")?;
            suite_proxy
                .run(
                    &mut not_skipped.into_iter().map(|(invocation, _outcome)| invocation),
                    options,
                    listener,
                )
                .context("error calling original test component's fuchsia.test/Suite#Run")?;
            listener_request_stream
                .try_take_while(|request| {
                    futures::future::ok(!matches!(
                        request,
                        fidl_fuchsia_test::RunListenerRequest::OnFinished { control_handle: _ }
                    ))
                })
                .map_err(anyhow::Error::new)
                .and_then(|request| match request {
                    fidl_fuchsia_test::RunListenerRequest::OnFinished { control_handle: _ } => {
                        unreachable!()
                    }
                    fidl_fuchsia_test::RunListenerRequest::OnTestCaseStarted {
                        invocation,
                        std_handles,
                        listener,
                        control_handle: _,
                    } => {
                        async move {
                            Ok((
                                CaseStart { invocation, std_handles },
                                listener
                                    .into_stream()
                                    .context("error getting CaseListener request stream")?
                                    .map_ok(
                                        |fidl_fuchsia_test::CaseListenerRequest::Finished {
                                             result,
                                             control_handle: _,
                                         }| CaseEnd {
                                            result,
                                        },
                                    )
                                    .map_err(anyhow::Error::new),
                            ))
                        }
                    }
                })
        };

        case_stream
            .try_for_each_concurrent(None, |(start, end_stream)| {
                self.handle_case(&listener_proxy, start, end_stream)
            })
            .await
            .context("error handling test case stream")?;
        listener_proxy.on_finished().context("error calling listener_proxy.on_finished()")
    }

    async fn handle_suite_request_stream(
        &self,
        suite_request_stream: fidl_fuchsia_test::SuiteRequestStream,
    ) -> Result<(), anyhow::Error> {
        let suite_proxy = &client::connect_to_protocol::<fidl_fuchsia_test::SuiteMarker>()
            .context("error connecting to original test component's fuchsia.test/Suite")?;

        suite_request_stream
            .map_err(anyhow::Error::new)
            .try_for_each_concurrent(None, |request| async move {
                match request {
                    fidl_fuchsia_test::SuiteRequest::GetTests { iterator, control_handle: _ } => {
                        suite_proxy.get_tests(iterator).context("error enumerating test cases")?;
                    }
                    fidl_fuchsia_test::SuiteRequest::Run {
                        tests,
                        options,
                        listener,
                        control_handle: _,
                    } => {
                        self.handle_suite_run_request(suite_proxy, tests, options, listener)
                            .await
                            .context("error handling Suite run request")?;
                    }
                }
                Ok(())
            })
            .await
    }
}

const EXPECTATIONS_PKG_PATH: &str = "/pkg/expectations.json5";

#[fuchsia::main]
async fn main() {
    let mut fs = fuchsia_component::server::ServiceFs::new_local();
    let _: &mut fuchsia_component::server::ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(|s: fidl_fuchsia_test::SuiteRequestStream| s);
    let _: &mut fuchsia_component::server::ServiceFs<_> =
        fs.take_and_serve_directory_handle().expect("failed to serve ServiceFs directory");

    let expectations =
        read_in_namespace_to_string(EXPECTATIONS_PKG_PATH).await.unwrap_or_else(|err| {
            panic!("failed to read expectations file at {EXPECTATIONS_PKG_PATH}: {err}")
        });
    let comparer = ExpectationsComparer {
        expectations: serde_json5::from_str(&expectations).expect("failed to parse expectations"),
    };

    fs.then(|s| comparer.handle_suite_request_stream(s))
        .for_each_concurrent(None, |result| {
            let () = result.expect("error handling fuchsia.test/Suite request stream");
            futures::future::ready(())
        })
        .await
}

#[cfg(test)]
mod test {
    #[test]
    fn a_passing_test() {
        println!("this is a passing test")
    }

    #[test]
    fn a_failing_test() {
        panic!("this is a failing test")
    }

    #[test]
    fn a_skipped_test() {
        unreachable!("this is a skipped test")
    }
}
