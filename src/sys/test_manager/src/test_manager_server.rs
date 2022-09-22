// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        above_root_capabilities::AboveRootCapabilitiesForTest,
        constants,
        debug_data_processor::{DebugDataDirectory, DebugDataProcessor},
        error::TestManagerError,
        facet,
        running_suite::{enumerate_test_cases, RunningSuite},
        self_diagnostics,
        test_suite::{Suite, TestRunBuilder},
    },
    fidl::endpoints::ControlHandle,
    fidl_fuchsia_component_resolution::ResolverProxy,
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::LaunchError,
    fuchsia_async::{self as fasync},
    fuchsia_zircon as zx,
    futures::prelude::*,
    std::sync::Arc,
    tracing::warn,
};

/// Start test manager and serve it over `stream`.
pub async fn run_test_manager(
    mut stream: ftest_manager::RunBuilderRequestStream,
    resolver: Arc<ResolverProxy>,
    above_root_capabilities_for_test: Arc<AboveRootCapabilitiesForTest>,
    inspect_root: &self_diagnostics::RootInspectNode,
) -> Result<(), TestManagerError> {
    let mut builder = TestRunBuilder { suites: vec![] };
    let mut scheduling_options: Option<ftest_manager::SchedulingOptions> = None;
    while let Some(event) = stream.try_next().await.map_err(TestManagerError::Stream)? {
        match event {
            ftest_manager::RunBuilderRequest::AddSuite {
                test_url,
                options,
                controller,
                control_handle,
            } => {
                let controller = match controller.into_stream() {
                    Ok(c) => c,
                    Err(e) => {
                        warn!(
                            "Cannot add suite {}, invalid controller. Closing connection. error: {}",
                            test_url,e
                        );
                        control_handle.shutdown_with_epitaph(zx::Status::BAD_HANDLE);
                        break;
                    }
                };

                builder.suites.push(Suite {
                    test_url,
                    options,
                    controller,
                    resolver: resolver.clone(),
                    above_root_capabilities_for_test: above_root_capabilities_for_test.clone(),
                    facets: facet::ResolveStatus::Unresolved,
                });
            }
            ftest_manager::RunBuilderRequest::WithSchedulingOptions { options, .. } => {
                scheduling_options = Some(options);
            }
            ftest_manager::RunBuilderRequest::Build { controller, control_handle } => {
                let controller = match controller.into_stream() {
                    Ok(c) => c,
                    Err(e) => {
                        warn!("Invalid builder controller. Closing connection. error: {}", e);
                        control_handle.shutdown_with_epitaph(zx::Status::BAD_HANDLE);
                        break;
                    }
                };
                let run_inspect = inspect_root
                    .new_run(&format!("run_{:?}", zx::Time::get_monotonic().into_nanos()));
                builder.run(controller, run_inspect, scheduling_options).await;
                // clients should reconnect to run new tests.
                break;
            }
        }
    }
    Ok(())
}

/// Start test manager and serve it over `stream`.
pub async fn run_test_manager_query_server(
    mut stream: ftest_manager::QueryRequestStream,
    resolver: Arc<ResolverProxy>,
    above_root_capabilities_for_test: Arc<AboveRootCapabilitiesForTest>,
) -> Result<(), TestManagerError> {
    while let Some(event) = stream.try_next().await.map_err(TestManagerError::Stream)? {
        match event {
            ftest_manager::QueryRequest::Enumerate { test_url, iterator, responder } => {
                let mut iterator = match iterator.into_stream() {
                    Ok(c) => c,
                    Err(e) => {
                        warn!("Cannot query test, invalid iterator {}: {}", test_url, e);
                        let _ = responder.send(&mut Err(LaunchError::InvalidArgs));
                        break;
                    }
                };
                let (_processor, sender) = DebugDataProcessor::new(DebugDataDirectory::Isolated {
                    parent: constants::ISOLATED_TMP,
                });
                let launch_fut = facet::get_suite_facets(test_url.clone(), resolver.clone())
                    .and_then(|facets| {
                        RunningSuite::launch(
                            &test_url,
                            facets,
                            None,
                            resolver.clone(),
                            above_root_capabilities_for_test.clone(),
                            sender,
                        )
                    });
                match launch_fut.await {
                    Ok(suite_instance) => {
                        let suite = match suite_instance.connect_to_suite() {
                            Ok(proxy) => proxy,
                            Err(e) => {
                                let _ = responder.send(&mut Err(e.into()));
                                continue;
                            }
                        };
                        let enumeration_result = enumerate_test_cases(&suite, None).await;
                        let t = fasync::Task::spawn(suite_instance.destroy());
                        match enumeration_result {
                            Ok(invocations) => {
                                const NAMES_CHUNK: usize = 50;
                                let mut names = Vec::with_capacity(invocations.len());
                                if let Ok(_) =
                                    invocations.into_iter().try_for_each(|i| match i.name {
                                        Some(name) => {
                                            names.push(name);
                                            Ok(())
                                        }
                                        None => {
                                            warn!("no name for a invocation in {}", test_url);
                                            Err(())
                                        }
                                    })
                                {
                                    let _ = responder.send(&mut Ok(()));
                                    let mut names = names.chunks(NAMES_CHUNK);
                                    while let Ok(Some(request)) = iterator.try_next().await {
                                        match request {
                                            ftest_manager::CaseIteratorRequest::GetNext {
                                                responder,
                                            } => match names.next() {
                                                Some(names) => {
                                                    let _ =
                                                        responder.send(&mut names.into_iter().map(
                                                            |s| ftest_manager::Case {
                                                                name: Some(s.into()),
                                                                ..ftest_manager::Case::EMPTY
                                                            },
                                                        ));
                                                }
                                                None => {
                                                    let _ = responder.send(&mut vec![].into_iter());
                                                }
                                            },
                                        }
                                    }
                                } else {
                                    let _ = responder.send(&mut Err(LaunchError::CaseEnumeration));
                                }
                            }
                            Err(e) => {
                                warn!("cannot enumerate tests for {}: {:?}", test_url, e);
                                let _ = responder.send(&mut Err(LaunchError::CaseEnumeration));
                            }
                        }
                        if let Err(err) = t.await {
                            warn!(?err, "Error destroying test realm for {}", test_url);
                        }
                    }
                    Err(e) => {
                        let _ = responder.send(&mut Err(e.into()));
                    }
                }
            }
        }
    }
    Ok(())
}
