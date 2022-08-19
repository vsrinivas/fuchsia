// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        above_root_capabilities::AboveRootCapabilitiesForTest, constants::TEST_TYPE_REALM_MAP,
        debug_data_server, error::*, facet, facet::SuiteFacets, run_events::RunEvent,
        running_suite, scheduler, scheduler::Scheduler, self_diagnostics,
    },
    anyhow::{Context, Error},
    fidl::endpoints::ClientEnd,
    fidl::prelude::*,
    fidl_fuchsia_component_resolution::ResolverProxy,
    fidl_fuchsia_test_internal as ftest_internal, fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{
        LaunchError, RunControllerRequest, RunControllerRequestStream, SchedulingOptions,
        SuiteControllerRequest, SuiteControllerRequestStream, SuiteEvent as FidlSuiteEvent,
    },
    fuchsia_async::{self as fasync},
    fuchsia_zircon as zx,
    futures::{
        channel::{mpsc, oneshot},
        future::join_all,
        prelude::*,
        StreamExt,
    },
    std::sync::Arc,
    tracing::{error, warn},
};

pub(crate) struct Suite {
    pub test_url: String,
    pub options: ftest_manager::RunOptions,
    pub controller: SuiteControllerRequestStream,
    pub resolver: Arc<ResolverProxy>,
    pub above_root_capabilities_for_test: Arc<AboveRootCapabilitiesForTest>,
    pub facets: facet::ResolveStatus,
}

pub(crate) struct TestRunBuilder {
    pub suites: Vec<Suite>,
}

impl TestRunBuilder {
    /// Serve a RunControllerRequestStream. Returns Err if the client stops the test
    /// prematurely or there is an error serving he stream.
    async fn run_controller(
        controller: RunControllerRequestStream,
        run_task: futures::future::RemoteHandle<()>,
        stop_sender: oneshot::Sender<()>,
        event_recv: mpsc::Receiver<RunEvent>,
        inspect_node: &self_diagnostics::RunInspectNode,
    ) -> Result<(), ()> {
        let mut task = Some(run_task);
        let mut stop_sender = Some(stop_sender);
        let mut event_recv = event_recv.fuse();

        let (serve_inner, terminated) = controller.into_inner();
        let serve_inner_clone = serve_inner.clone();
        let channel_closed_fut =
            fasync::OnSignals::new(serve_inner_clone.channel(), zx::Signals::CHANNEL_PEER_CLOSED)
                .shared();
        let mut controller = RunControllerRequestStream::from_inner(serve_inner, terminated);

        let mut stop_or_kill_called = false;
        let mut events_drained = false;
        let mut events_sent_successfully = true;

        // no need to check controller error.
        let serve_controller_fut = async {
            loop {
                inspect_node
                    .set_controller_state(self_diagnostics::RunControllerState::AwaitingRequest);
                let request = match controller.try_next().await {
                    Ok(Some(request)) => request,
                    _ => break,
                };
                match request {
                    RunControllerRequest::Stop { .. } => {
                        stop_or_kill_called = true;
                        if let Some(stop_sender) = stop_sender.take() {
                            // no need to check error.
                            let _ = stop_sender.send(());
                            // after this all `senders` go away and subsequent GetEvent call will
                            // return rest of events and eventually a empty array and will close the
                            // connection after that.
                        }
                    }
                    RunControllerRequest::Kill { .. } => {
                        stop_or_kill_called = true;
                        // dropping the remote handle cancels it.
                        drop(task.take());
                        // after this all `senders` go away and subsequent GetEvent call will
                        // return rest of events and eventually a empty array and will close the
                        // connection after that.
                    }
                    RunControllerRequest::GetEvents { responder } => {
                        let mut events = vec![];
                        // TODO(fxbug.dev/91553): This can block handling Stop and Kill requests if no
                        // events are available.
                        inspect_node.set_controller_state(
                            self_diagnostics::RunControllerState::AwaitingEvents,
                        );
                        if let Some(event) = event_recv.next().await {
                            events.push(event);
                            while events.len() < EVENTS_THRESHOLD {
                                if let Some(Some(event)) = event_recv.next().now_or_never() {
                                    events.push(event);
                                } else {
                                    break;
                                }
                            }
                        }
                        let no_events_left = events.is_empty();
                        let response_err =
                            responder.send(&mut events.into_iter().map(RunEvent::into)).is_err();

                        // Order setting these variables matters. Expected is for the client to receive at
                        // least one event response. Client might send more, which is okay but we suppress
                        // response errors after the first empty vec.
                        if !events_drained && response_err {
                            events_sent_successfully = false;
                        }
                        events_drained = no_events_left;
                    }
                }
            }
        }
        .boxed();

        let _ = futures::future::select(channel_closed_fut.clone(), serve_controller_fut).await;

        inspect_node.set_controller_state(self_diagnostics::RunControllerState::Done {
            stopped_or_killed: stop_or_kill_called,
            events_drained,
            events_sent_successfully,
        });

        // Workaround to prevent zx_peer_closed error
        // TODO(fxbug.dev/87976) once fxbug.dev/87890 is fixed, the controller should be dropped
        // as soon as all events are drained.
        if let Err(e) = channel_closed_fut.await {
            warn!("Error waiting for the RunController channel to close: {:?}", e);
        }

        match stop_or_kill_called || !events_drained || !events_sent_successfully {
            true => Err(()),
            false => Ok(()),
        }
    }

    pub(crate) async fn run(
        self,
        controller: RunControllerRequestStream,
        debug_controller: ftest_internal::DebugDataSetControllerProxy,
        debug_iterator: ClientEnd<ftest_manager::DebugDataIteratorMarker>,
        inspect_node: self_diagnostics::RunInspectNode,
        scheduling_options: Option<SchedulingOptions>,
    ) {
        let (stop_sender, mut stop_recv) = oneshot::channel::<()>();
        let (event_sender, event_recv) = mpsc::channel::<RunEvent>(16);

        let debug_event_fut = debug_data_server::send_debug_data_if_produced(
            event_sender.clone(),
            debug_controller.take_event_stream(),
            debug_iterator,
            &inspect_node,
        );
        let inspect_node_ref = &inspect_node;

        let max_parallel_suites = match scheduling_options {
            Some(options) => options.max_parallel_suites,
            None => None,
        };
        let max_parallel_suites_ref = &max_parallel_suites;

        // Generate a random number in an attempt to prevent realm name collisions between runs.
        let run_id: u32 = rand::random();
        let suite_scheduler_fut = async move {
            inspect_node_ref.set_execution_state(self_diagnostics::RunExecutionState::Executing);

            let serial_executor = scheduler::SerialScheduler {};

            match max_parallel_suites_ref {
                Some(max_parallel_suites) => {
                    let parallel_executor = scheduler::ParallelScheduler {
                        suite_runner: scheduler::RunSuiteObj {},
                        max_parallel_suites: *max_parallel_suites,
                    };
                    inspect_node_ref.set_used_parallel_scheduler(true);
                    let get_facets_fn = |test_url, resolver| async move {
                        facet::get_suite_facets(test_url, resolver).await
                    };
                    let (serial_suites, parallel_suites) =
                        split_suites_by_hermeticity(self.suites, get_facets_fn).await;

                    parallel_executor
                        .execute(
                            parallel_suites,
                            inspect_node_ref,
                            &mut stop_recv,
                            run_id,
                            &debug_controller,
                        )
                        .await;
                    serial_executor
                        .execute(
                            serial_suites,
                            inspect_node_ref,
                            &mut stop_recv,
                            run_id,
                            &debug_controller,
                        )
                        .await;
                }
                None => {
                    serial_executor
                        .execute(
                            self.suites,
                            inspect_node_ref,
                            &mut stop_recv,
                            run_id,
                            &debug_controller,
                        )
                        .await;
                }
            }

            debug_controller
                .finish()
                .unwrap_or_else(|e| warn!("Error finishing debug data set: {:?}", e));

            // Collect run artifacts
            let mut kernel_debug_tasks = vec![];
            kernel_debug_tasks.push(debug_data_server::send_kernel_debug_data(event_sender));
            join_all(kernel_debug_tasks).await;
            inspect_node_ref.set_execution_state(self_diagnostics::RunExecutionState::Complete);
        };

        let (remote, remote_handle) = futures::future::join(debug_event_fut, suite_scheduler_fut)
            .map(|((), ())| ())
            .remote_handle();

        let ((), controller_res) = futures::future::join(
            remote,
            Self::run_controller(
                controller,
                remote_handle,
                stop_sender,
                event_recv,
                inspect_node_ref,
            ),
        )
        .await;

        if let Err(()) = controller_res {
            warn!("Controller terminated early. Last known state: {:#?}", &inspect_node);
            inspect_node.persist();
        } else if max_parallel_suites.is_some() {
            warn!("This run used experimental parallel test suite execution");
            inspect_node.persist();
        }
    }
}

// max events to send so that we don't cross fidl limits.
// TODO(fxbug.dev/100462): Use tape measure to calculate limit.
const EVENTS_THRESHOLD: usize = 50;

impl Suite {
    async fn run_controller(
        controller: &mut SuiteControllerRequestStream,
        stop_sender: oneshot::Sender<()>,
        run_suite_remote_handle: futures::future::RemoteHandle<()>,
        event_recv: mpsc::Receiver<Result<FidlSuiteEvent, LaunchError>>,
    ) -> Result<(), Error> {
        let mut event_recv = event_recv.into_stream().fuse();
        let mut stop_sender = Some(stop_sender);
        let mut run_suite_remote_handle = Some(run_suite_remote_handle);
        'controller_loop: while let Some(event) =
            controller.try_next().await.context("error running controller")?
        {
            match event {
                SuiteControllerRequest::Stop { .. } => {
                    // no need to handle error as task might already have finished.
                    if let Some(stop) = stop_sender.take() {
                        let _ = stop.send(());
                        // after this all `senders` go away and subsequent GetEvent call will
                        // return rest of event. Eventually an empty array and will close the
                        // connection after that.
                    }
                }
                SuiteControllerRequest::GetEvents { responder } => {
                    let mut events = vec![];

                    // wait for first event
                    let mut e = event_recv.next().await;

                    while let Some(event) = e {
                        match event {
                            Ok(event) => {
                                events.push(event);
                            }
                            Err(err) => {
                                responder
                                    .send(&mut Err(err))
                                    .map_err(TestManagerError::Response)?;
                                break 'controller_loop;
                            }
                        }
                        if events.len() >= EVENTS_THRESHOLD {
                            responder.send(&mut Ok(events)).map_err(TestManagerError::Response)?;
                            continue 'controller_loop;
                        }
                        e = match event_recv.next().now_or_never() {
                            Some(e) => e,
                            None => break,
                        }
                    }

                    let len = events.len();
                    responder.send(&mut Ok(events)).map_err(TestManagerError::Response)?;
                    if len == 0 {
                        break;
                    }
                }
                SuiteControllerRequest::Kill { .. } => {
                    // Dropping the remote handle for the suite execution task cancels it.
                    drop(run_suite_remote_handle.take());
                    // after this all `senders` go away and subsequent GetEvent call will
                    // return rest of event. Eventually an empty array and will close the
                    // connection after that.
                }
            }
        }
        Ok(())
    }
}

pub(crate) async fn run_single_suite(
    suite: Suite,
    debug_controller: &ftest_internal::DebugDataSetControllerProxy,
    instance_name: &str,
    inspect_node: Arc<self_diagnostics::SuiteInspectNode>,
) {
    let (mut sender, recv) = mpsc::channel(1024);
    let (stop_sender, stop_recv) = oneshot::channel::<()>();
    let mut maybe_instance = None;
    let mut realm_moniker = None;

    let Suite {
        test_url,
        options,
        mut controller,
        resolver,
        above_root_capabilities_for_test,
        facets,
    } = suite;

    let run_test_fut = async {
        inspect_node.set_execution_state(self_diagnostics::ExecutionState::GetFacets);

        let facets = match facets {
            // Currently, all suites are passed in with unresolved facets by the
            // SerialScheduler. ParallelScheduler will pass in Resolved facets
            // once it is implemented.
            facet::ResolveStatus::Resolved(result) => {
                match result {
                    Ok(facets) => facets,

                    // This error is reported here instead of when the error was
                    // first encountered because here is where it has access to
                    // the SuiteController protocol server (Suite::run_controller)
                    // which can report the error back to the test_manager client
                    Err(error) => {
                        sender.send(Err(error.into())).await.unwrap();
                        return;
                    }
                }
            }
            facet::ResolveStatus::Unresolved => {
                match facet::get_suite_facets(test_url.clone(), resolver.clone()).await {
                    Ok(facets) => facets,
                    Err(error) => {
                        sender.send(Err(error.into())).await.unwrap();
                        return;
                    }
                }
            }
        };
        let realm_moniker_ref =
            realm_moniker.insert(format!("./{}:{}", facets.collection, instance_name));
        if let Err(e) = debug_controller.add_realm(realm_moniker_ref.as_str(), &test_url).await {
            warn!("Failed to add realm {} to debug data: {:?}", realm_moniker_ref.as_str(), e);
        }
        inspect_node.set_execution_state(self_diagnostics::ExecutionState::Launch);
        match running_suite::RunningSuite::launch(
            &test_url,
            facets,
            Some(instance_name),
            resolver,
            above_root_capabilities_for_test,
        )
        .await
        {
            Ok(instance) => {
                inspect_node.set_execution_state(self_diagnostics::ExecutionState::RunTests);
                let instance_ref = maybe_instance.insert(instance);
                instance_ref.run_tests(&test_url, options, sender, stop_recv).await;
                inspect_node.set_execution_state(self_diagnostics::ExecutionState::TestsDone);
            }
            Err(e) => {
                let _ = debug_controller.remove_realm(realm_moniker_ref.as_str());
                sender.send(Err(e.into())).await.unwrap();
            }
        }
    };
    let (run_test_remote, run_test_handle) = run_test_fut.remote_handle();

    let controller_fut = Suite::run_controller(&mut controller, stop_sender, run_test_handle, recv);
    let ((), controller_ret) = futures::future::join(run_test_remote, controller_fut).await;

    if let Err(e) = controller_ret {
        warn!("Ended test {}: {:?}", test_url, e);
    }

    if let Some(instance) = maybe_instance.take() {
        inspect_node.set_execution_state(self_diagnostics::ExecutionState::TearDown);
        if let Err(err) = instance.destroy().await {
            // Failure to destroy an instance could mean that some component events fail to send.
            // Missing events could cause debug data collection to hang as it relies on events to
            // understand when a realm has stopped.
            error!(?err, "Failed to destroy instance for {}. Debug data may be lost.", test_url);
            if let Some(moniker) = realm_moniker {
                let _ = debug_controller.remove_realm(&moniker);
            }
        }
    }
    inspect_node.set_execution_state(self_diagnostics::ExecutionState::Complete);
    // Workaround to prevent zx_peer_closed error
    // TODO(fxbug.dev/87976) once fxbug.dev/87890 is fixed, the controller should be dropped as soon as all
    // events are drained.
    let (inner, _) = controller.into_inner();
    if let Err(e) = fasync::OnSignals::new(inner.channel(), zx::Signals::CHANNEL_PEER_CLOSED).await
    {
        warn!("Error waiting for SuiteController channel to close: {:?}", e);
    }
}

// Separate suite into a hermetic and a non-hermetic collection
// Note: F takes String and Arc<ResolverProxy> to circumvent the
// borrow checker.
async fn split_suites_by_hermeticity<F, Fut>(
    suites: Vec<Suite>,
    get_facets_fn: F,
) -> (Vec<Suite>, Vec<Suite>)
where
    F: Fn(String, Arc<ResolverProxy>) -> Fut,
    Fut: futures::future::Future<Output = Result<SuiteFacets, LaunchTestError>>,
{
    let mut serial_suites: Vec<Suite> = Vec::new();
    let mut parallel_suites: Vec<Suite> = Vec::new();

    for mut suite in suites {
        let test_url = suite.test_url.clone();
        let resolver = suite.resolver.clone();
        let facet_result = get_facets_fn(test_url, resolver).await;
        let can_run_in_parallel = match &facet_result {
            Ok(facets) => {
                facets.collection == TEST_TYPE_REALM_MAP["hermetic"]
                    || facets.collection == TEST_TYPE_REALM_MAP["hermetic-tier-2"]
            }
            Err(_) => false,
        };
        suite.facets = facet::ResolveStatus::Resolved(facet_result);
        if can_run_in_parallel {
            parallel_suites.push(suite);
        } else {
            serial_suites.push(suite);
        }
    }
    (serial_suites, parallel_suites)
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::run_events::SuiteEvents, fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_component_resolution as fresolution, self_diagnostics::RootInspectNode,
    };

    fn new_run_inspect_node() -> self_diagnostics::RunInspectNode {
        RootInspectNode::new(&fuchsia_inspect::types::Node::default()).new_run("test-run")
    }

    #[fuchsia::test]
    async fn run_controller_stop_test() {
        let (sender, recv) = mpsc::channel(1024);
        let (stop_sender, stop_recv) = oneshot::channel::<()>();
        let (task, remote_handle) = async move {
            stop_recv.await.unwrap();
            // drop event sender so that fake test can end.
            drop(sender);
        }
        .remote_handle();
        let _task = fasync::Task::spawn(task);
        let (proxy, controller) =
            create_proxy_and_stream::<ftest_manager::RunControllerMarker>().unwrap();
        let run_controller = fasync::Task::spawn(async move {
            TestRunBuilder::run_controller(
                controller,
                remote_handle,
                stop_sender,
                recv,
                &new_run_inspect_node(),
            )
            .await
        });
        proxy.stop().unwrap();

        assert_eq!(proxy.get_events().await.unwrap(), vec![]);
        drop(proxy);
        run_controller.await.unwrap_err();
    }

    #[fuchsia::test]
    async fn run_controller_abort_when_channel_closed() {
        let (_sender, recv) = mpsc::channel(1024);
        let (stop_sender, _stop_recv) = oneshot::channel::<()>();
        // Create a future that normally never resolves.
        let (task, remote_handle) = futures::future::pending().remote_handle();
        let pending_task = fasync::Task::spawn(task);
        let (proxy, controller) =
            create_proxy_and_stream::<ftest_manager::RunControllerMarker>().unwrap();
        let run_controller = fasync::Task::spawn(async move {
            TestRunBuilder::run_controller(
                controller,
                remote_handle,
                stop_sender,
                recv,
                &new_run_inspect_node(),
            )
            .await
        });
        drop(proxy);
        // After controller is dropped, both the controller future and the task it was
        // controlling should terminate.
        pending_task.await;
        run_controller.await.unwrap_err();
    }

    #[fuchsia::test]
    async fn suite_controller_stop_test() {
        let (sender, recv) = mpsc::channel(1024);
        let (stop_sender, stop_recv) = oneshot::channel::<()>();
        let (task, remote_handle) = async move {
            stop_recv.await.unwrap();
            // drop event sender so that fake test can end.
            drop(sender);
        }
        .remote_handle();
        let _task = fasync::Task::spawn(task);
        let (proxy, mut controller) =
            create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>().unwrap();
        let run_controller = fasync::Task::spawn(async move {
            Suite::run_controller(&mut controller, stop_sender, remote_handle, recv).await
        });
        proxy.stop().unwrap();

        assert_eq!(proxy.get_events().await.unwrap(), Ok(vec![]));
        // this should end
        run_controller.await.unwrap();
    }

    #[fuchsia::test]
    async fn suite_controller_abort_remote_when_controller_closed() {
        let (_sender, recv) = mpsc::channel(1024);
        let (stop_sender, _stop_recv) = oneshot::channel::<()>();
        // Create a future that normally never resolves.
        let (task, remote_handle) = futures::future::pending().remote_handle();
        let pending_task = fasync::Task::spawn(task);
        let (proxy, mut controller) =
            create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>().unwrap();
        let run_controller = fasync::Task::spawn(async move {
            Suite::run_controller(&mut controller, stop_sender, remote_handle, recv).await
        });
        drop(proxy);
        // After controller is dropped, both the controller future and the task it was
        // controlling should terminate.
        pending_task.await;
        run_controller.await.unwrap();
    }

    #[fuchsia::test]
    async fn suite_controller_get_events() {
        let (mut sender, recv) = mpsc::channel(1024);
        let (stop_sender, stop_recv) = oneshot::channel::<()>();
        let (task, remote_handle) = async {}.remote_handle();
        let _task = fasync::Task::spawn(task);
        let (proxy, mut controller) =
            create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>().unwrap();
        let run_controller = fasync::Task::spawn(async move {
            Suite::run_controller(&mut controller, stop_sender, remote_handle, recv).await
        });
        sender.send(Ok(SuiteEvents::case_found(1, "case1".to_string()).into())).await.unwrap();
        sender.send(Ok(SuiteEvents::case_found(2, "case2".to_string()).into())).await.unwrap();

        let events = proxy.get_events().await.unwrap().unwrap();
        assert_eq!(events.len(), 2);
        assert_eq!(
            events[0].payload,
            SuiteEvents::case_found(1, "case1".to_string()).into_suite_run_event().payload,
        );
        assert_eq!(
            events[1].payload,
            SuiteEvents::case_found(2, "case2".to_string()).into_suite_run_event().payload,
        );
        sender.send(Ok(SuiteEvents::case_started(2).into())).await.unwrap();
        proxy.stop().unwrap();

        // test that controller collects event after stop is called.
        sender.send(Ok(SuiteEvents::case_started(1).into())).await.unwrap();
        sender.send(Ok(SuiteEvents::case_found(3, "case3".to_string()).into())).await.unwrap();

        stop_recv.await.unwrap();
        // drop event sender so that fake test can end.
        drop(sender);
        let events = proxy.get_events().await.unwrap().unwrap();
        assert_eq!(events.len(), 3);

        assert_eq!(events[0].payload, SuiteEvents::case_started(2).into_suite_run_event().payload,);
        assert_eq!(events[1].payload, SuiteEvents::case_started(1).into_suite_run_event().payload,);
        assert_eq!(
            events[2].payload,
            SuiteEvents::case_found(3, "case3".to_string()).into_suite_run_event().payload,
        );

        let events = proxy.get_events().await.unwrap().unwrap();
        assert_eq!(events, vec![]);
        // this should end
        run_controller.await.unwrap();
    }

    async fn create_fake_suite(test_url: String) -> Suite {
        let (_controller_proxy, controller_stream) =
            create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>()
                .expect("create controller proxy");
        let (resolver_proxy, _resolver_stream) =
            create_proxy_and_stream::<fresolution::ResolverMarker>()
                .expect("create resolver proxy");
        let resolver_proxy = Arc::new(resolver_proxy);
        let routing_info = Arc::new(AboveRootCapabilitiesForTest::new_empty_for_tests());
        Suite {
            test_url,
            options: ftest_manager::RunOptions {
                parallel: None,
                arguments: None,
                run_disabled_tests: Some(false),
                timeout: None,
                case_filters_to_run: None,
                log_iterator: None,
                ..ftest_manager::RunOptions::EMPTY
            },
            controller: controller_stream,
            resolver: resolver_proxy,
            above_root_capabilities_for_test: routing_info,
            facets: facet::ResolveStatus::Unresolved,
        }
    }

    #[fuchsia::test]
    async fn split_suites_by_hermeticity_test() {
        let hermetic_suite = create_fake_suite("hermetic_suite".to_string()).await;
        let non_hermetic_suite = create_fake_suite("non_hermetic_suite".to_string()).await;
        let suites = vec![hermetic_suite, non_hermetic_suite];

        // call split_suites_by_hermeticity
        let get_facets_fn = |test_url, _resolver| async move {
            if test_url == "hermetic_suite".to_string() {
                Ok(SuiteFacets { collection: TEST_TYPE_REALM_MAP["hermetic"] })
            } else {
                Ok(SuiteFacets { collection: TEST_TYPE_REALM_MAP["system"] })
            }
        };
        let (serial_suites, parallel_suites) =
            split_suites_by_hermeticity(suites, get_facets_fn).await;

        assert_eq!(parallel_suites[0].test_url, "hermetic_suite".to_string());
        assert_eq!(serial_suites[0].test_url, "non_hermetic_suite".to_string());
    }

    #[test]
    fn suite_controller_hanging_get_events() {
        let mut executor = fasync::TestExecutor::new().unwrap();
        let (mut sender, recv) = mpsc::channel(1024);
        let (stop_sender, _stop_recv) = oneshot::channel::<()>();
        let (task, remote_handle) = async {}.remote_handle();
        let _task = fasync::Task::spawn(task);
        let (proxy, mut controller) =
            create_proxy_and_stream::<ftest_manager::SuiteControllerMarker>().unwrap();
        let _run_controller = fasync::Task::spawn(async move {
            Suite::run_controller(&mut controller, stop_sender, remote_handle, recv).await
        });

        // send get event call which would hang as there are no events.
        let mut get_events =
            fasync::Task::spawn(async move { proxy.get_events().await.unwrap().unwrap() });
        assert_eq!(executor.run_until_stalled(&mut get_events), std::task::Poll::Pending);
        executor.run_singlethreaded(async {
            sender.send(Ok(SuiteEvents::case_found(1, "case1".to_string()).into())).await.unwrap();
            sender.send(Ok(SuiteEvents::case_found(2, "case2".to_string()).into())).await.unwrap();
        });
        let events = executor.run_singlethreaded(get_events);
        assert_eq!(events.len(), 2);
        assert_eq!(
            events[0].payload,
            SuiteEvents::case_found(1, "case1".to_string()).into_suite_run_event().payload,
        );
        assert_eq!(
            events[1].payload,
            SuiteEvents::case_found(2, "case2".to_string()).into_suite_run_event().payload,
        );
    }
}
