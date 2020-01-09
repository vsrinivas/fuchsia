// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    breakpoint_system_client::*,
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_test_workscheduler as fws,
    test_utils::*,
    work_scheduler_dispatch_reporter::{
        DispatchedEvent, WorkSchedulerDispatchReporter, WORK_SCHEDULER_DISPATCH_REPORTER,
    },
};

#[test]
fn work_scheduler_dispatch_reporter_paths() {
    assert_eq!(
        format!("/svc/{}", fws::WorkSchedulerDispatchReporterMarker::NAME),
        WORK_SCHEDULER_DISPATCH_REPORTER.to_string()
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn basic_work_scheduler_test() -> Result<(), Error> {
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/work_scheduler_integration_test#meta/bound_worker.cm";
    let test = BlackBoxTest::default(root_component_url).await?;

    let breakpoint_system =
        test.connect_to_breakpoint_system().await.expect("breakpoint system is unavailable");
    let receiver = breakpoint_system.set_breakpoints(vec![BeforeStartInstance::TYPE]).await?;
    let route_receiver = breakpoint_system.set_breakpoints(vec![RouteCapability::TYPE]).await?;

    breakpoint_system.start_component_manager().await?;

    // Expect the root component to be bound to
    let invocation = receiver.expect_exact::<BeforeStartInstance>("/").await?;
    invocation.resume().await?;

    let work_scheduler_dispatch_reporter = WorkSchedulerDispatchReporter::new();

    // Wait until `/` connects to `WorkSchedulerDispatchReporter` and inject
    // the capability from here.
    let invocation = route_receiver
        .wait_until_framework_capability("/", WORK_SCHEDULER_DISPATCH_REPORTER, Some("/"))
        .await?;
    invocation.inject(work_scheduler_dispatch_reporter.serve_async()).await?;
    invocation.resume().await?;

    let dispatched_event = work_scheduler_dispatch_reporter
        .wait_for_dispatched(std::time::Duration::from_secs(10))
        .await?;
    assert_eq!(DispatchedEvent::new("TEST".to_string()), dispatched_event);

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn unbound_work_scheduler_test() -> Result<(), Error> {
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/work_scheduler_integration_test#meta/unbound_child_worker_parent.cm";
    let test = BlackBoxTest::default(root_component_url).await?;

    let breakpoint_system =
        test.connect_to_breakpoint_system().await.expect("breakpoint system is unavailable");
    let receiver = breakpoint_system.set_breakpoints(vec![BeforeStartInstance::TYPE]).await?;

    breakpoint_system.start_component_manager().await?;

    // Expect the root component to be bound to
    let invocation = receiver.expect_exact::<BeforeStartInstance>("/").await?;
    invocation.resume().await?;

    let work_scheduler_dispatch_reporter = WorkSchedulerDispatchReporter::new();

    let route_receiver = breakpoint_system.set_breakpoints(vec![RouteCapability::TYPE]).await?;

    // `/worker_sibling:0` has started.
    let invocation = receiver.expect_exact::<BeforeStartInstance>("/worker_sibling:0").await?;
    invocation.resume().await?;

    // We no longer need to track `StartInstance` events.
    drop(receiver);

    // Wait until `/worker_child:0` connects to `WorkSchedulerDispatchReporter`
    // and inject the capability from here.
    let invocation = route_receiver
        .wait_until_framework_capability(
            "/worker_child:0",
            WORK_SCHEDULER_DISPATCH_REPORTER,
            Some("/worker_child:0"),
        )
        .await?;
    invocation.inject(work_scheduler_dispatch_reporter.serve_async()).await?;
    invocation.resume().await?;

    let dispatched_event = work_scheduler_dispatch_reporter
        .wait_for_dispatched(std::time::Duration::from_secs(10))
        .await?;
    assert_eq!(DispatchedEvent::new("TEST".to_string()), dispatched_event);

    Ok(())
}
