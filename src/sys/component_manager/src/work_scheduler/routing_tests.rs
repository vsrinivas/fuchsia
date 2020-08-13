use {
    crate::{
        model::testing::mocks::ManagedNamespace,
        model::testing::{routing_test_helpers::*, test_helpers::*},
        model::{binding::Binder, moniker::AbsoluteMoniker, testing::mocks::FakeBinder},
        work_scheduler::WorkScheduler,
    },
    cm_rust::{
        self, CapabilityName, CapabilityNameOrPath, CapabilityPath, DependencyType, ExposeDecl,
        ExposeProtocolDecl, ExposeSource, ExposeTarget, OfferDecl, OfferProtocolDecl,
        OfferServiceSource, OfferTarget, UseDecl, UseProtocolDecl, UseSource,
    },
    fidl_fuchsia_io::{MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE},
    fidl_fuchsia_sys2 as fsys,
    std::{convert::TryFrom, ops::Deref, path::Path, sync::Arc},
};

struct BindingWorkScheduler {
    work_scheduler: Arc<WorkScheduler>,
    // Retain `Arc` to keep `Binder` alive throughout test.
    _binder: Arc<dyn Binder>,
}

impl BindingWorkScheduler {
    async fn new() -> Self {
        let binder = FakeBinder::new();
        let work_scheduler = WorkScheduler::new(binder.clone()).await;
        Self { work_scheduler, _binder: binder }
    }
}

// `BindingWorkScheduler` API is `Arc<WorkScheduler>` API.
impl Deref for BindingWorkScheduler {
    type Target = Arc<WorkScheduler>;

    fn deref(&self) -> &Self::Target {
        &self.work_scheduler
    }
}

async fn new_work_scheduler() -> BindingWorkScheduler {
    BindingWorkScheduler::new().await
}

async fn call_work_scheduler_svc_from_namespace(
    namespace: &ManagedNamespace,
    should_succeed: bool,
) {
    let path: CapabilityPath = "/svc/fuchsia.sys2.WorkScheduler".parse().unwrap();
    let dir_proxy = capability_util::take_dir_from_namespace(namespace, &path.dirname).await;
    let node_proxy = io_util::open_node(
        &dir_proxy,
        &Path::new(&path.basename),
        OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
    )
    .expect("failed to open WorkScheduler service");
    capability_util::add_dir_to_namespace(namespace, &path.dirname, dir_proxy).await;
    let work_scheduler_proxy = fsys::WorkSchedulerProxy::new(node_proxy.into_channel().unwrap());
    let req = fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: None };
    let res = work_scheduler_proxy.schedule_work("hippos", req).await;

    match should_succeed {
        true => assert_eq!(res.expect("failed to use WorkScheduler service"), Ok(())),
        false => {
            let err = res.expect_err("used WorkScheduler service successfully when it should fail");
            assert!(err.is_closed(), "expected channel closed error, got: {:?}", err);
        }
    }
}

async fn check_use_work_scheduler(
    routing_test: &RoutingTest,
    moniker: AbsoluteMoniker,
    should_succeed: bool,
) {
    let component_name = routing_test.bind_instance(&moniker).await.expect("bind instance failed");
    let component_resolved_url = RoutingTest::resolved_url(&component_name);
    let namespace = routing_test.mock_runner.get_namespace(&component_resolved_url).unwrap();
    call_work_scheduler_svc_from_namespace(&namespace, should_succeed).await;
}

async fn call_work_scheduler_control_svc_from_namespace(
    namespace: &ManagedNamespace,
    path: CapabilityPath,
    should_succeed: bool,
) {
    let dir_proxy = capability_util::take_dir_from_namespace(namespace, &path.dirname).await;
    let node_proxy = io_util::open_node(
        &dir_proxy,
        &Path::new(&path.basename),
        OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
    )
    .expect("failed to open WorkSchedulerControl service");
    capability_util::add_dir_to_namespace(namespace, &path.dirname, dir_proxy).await;
    let work_scheduler_control_proxy =
        fsys::WorkSchedulerControlProxy::new(node_proxy.into_channel().unwrap());
    let res = work_scheduler_control_proxy.get_batch_period().await;

    match should_succeed {
        true => {
            res.expect("failed to use WorkSchedulerControl service")
                .expect("WorkSchedulerControl.GetBatchPeriod() yielded error");
        }
        false => {
            let err = res
                .expect_err("used WorkSchedulerControl service successfully when it should fail");
            assert!(err.is_closed(), "expected channel closed error, got: {:?}", err);
        }
    }
}

async fn check_use_work_scheduler_control(
    routing_test: &RoutingTest,
    moniker: AbsoluteMoniker,
    path: CapabilityPath,
    should_succeed: bool,
) {
    let component_name = routing_test.bind_instance(&moniker).await.expect("bind instance failed");
    let component_resolved_url = RoutingTest::resolved_url(&component_name);
    let namespace = routing_test.mock_runner.get_namespace(&component_resolved_url).unwrap();
    call_work_scheduler_control_svc_from_namespace(&namespace, path.clone(), should_succeed).await;
}

///   a
///    \
///     b
///
/// b: uses framework service /svc/fuchsia.sys2.WorkScheduler while exposing
///    /svc/fuchsia.sys2.Worker to framework
#[fuchsia_async::run_singlethreaded(test)]
async fn use_work_scheduler_with_expose_to_framework() {
    let components = vec![
        ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        (
            "b",
            ComponentDeclBuilder::new()
                .protocol(
                    ProtocolDeclBuilder::new("fuchsia.sys2.Worker")
                        .path("/svc/fuchsia.sys2.Worker")
                        .build(),
                )
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("fuchsia.sys2.Worker").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("fuchsia.sys2.Worker").unwrap(),
                    target: ExposeTarget::Framework,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityNameOrPath::try_from("fuchsia.sys2.WorkScheduler")
                        .unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.WorkScheduler")
                        .unwrap(),
                }))
                .build(),
        ),
    ];
    let work_scheduler = new_work_scheduler().await;
    let test =
        RoutingTestBuilder::new("a", components).add_hooks(work_scheduler.hooks()).build().await;
    check_use_work_scheduler(&test, vec!["b:0"].into(), true).await;
}

///   a
///    \
///     b
///
/// b: uses framework service /svc/fuchsia.sys2.WorkScheduler without exposing
///    /svc/fuchsia.sys2.Worker
#[fuchsia_async::run_singlethreaded(test)]
async fn use_work_scheduler_without_expose() {
    let components = vec![
        ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityNameOrPath::try_from("fuchsia.sys2.WorkScheduler")
                        .unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.WorkScheduler")
                        .unwrap(),
                }))
                .build(),
        ),
    ];
    let work_scheduler = new_work_scheduler().await;
    let test =
        RoutingTestBuilder::new("a", components).add_hooks(work_scheduler.hooks()).build().await;
    check_use_work_scheduler(&test, vec!["b:0"].into(), false).await;
}

///   a
///    \
///     b
///
/// b: uses framework service /svc/fuchsia.sys2.WorkScheduler while exposing
///    /svc/fuchsia.sys2.Worker to realm (not framework)
#[fuchsia_async::run_singlethreaded(test)]
async fn use_work_scheduler_with_expose_to_realm() {
    let components = vec![
        ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
        (
            "b",
            ComponentDeclBuilder::new()
                .protocol(
                    ProtocolDeclBuilder::new("fuchsia.sys2.Worker")
                        .path("/svc/fuchsia.sys2.Worker")
                        .build(),
                )
                .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                    source: ExposeSource::Self_,
                    source_path: CapabilityNameOrPath::try_from("fuchsia.sys2.Worker").unwrap(),
                    target_path: CapabilityNameOrPath::try_from("fuchsia.sys2.Worker").unwrap(),
                    target: ExposeTarget::Parent,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityNameOrPath::try_from("fuchsia.sys2.WorkScheduler")
                        .unwrap(),
                    target_path: CapabilityPath::try_from("/svc/fuchsia.sys2.WorkScheduler")
                        .unwrap(),
                }))
                .build(),
        ),
    ];
    let work_scheduler = new_work_scheduler().await;
    let test =
        RoutingTestBuilder::new("a", components).add_hooks(work_scheduler.hooks()).build().await;
    check_use_work_scheduler(&test, vec!["b:0"].into(), false).await;
}

///   a
///    \
///     b
///
/// b: uses WorkSchedulerControl offered by by a
#[fuchsia_async::run_singlethreaded(test)]
async fn use_work_scheduler_control_routed() {
    let offer_use_name = CapabilityName::from("WorkSchedulerControl");
    let use_path = CapabilityPath::try_from("/svc/WorkSchedulerControl").unwrap();
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: CapabilityNameOrPath::try_from(
                        "fuchsia.sys2.WorkSchedulerControl",
                    )
                    .unwrap(),
                    target_path: CapabilityNameOrPath::Name(offer_use_name.clone()),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_path: CapabilityNameOrPath::Name(offer_use_name.clone()),
                    target_path: use_path.clone(),
                }))
                .build(),
        ),
    ];
    let work_scheduler = new_work_scheduler().await;

    let test =
        RoutingTestBuilder::new("a", components).add_hooks(work_scheduler.hooks()).build().await;

    check_use_work_scheduler_control(&test, vec!["b:0"].into(), use_path.clone(), true).await;
}

///   a
///    \
///     b
///
/// b: uses framework service /svc/fuchsia.sys2.WorkSchedulerControl from framework (not allowed)
#[fuchsia_async::run_singlethreaded(test)]
async fn use_work_scheduler_control_error() {
    let offer_use_name = CapabilityName::from("WorkSchedulerControl");
    let use_path = CapabilityPath::try_from("/svc/WorkSchedulerControl").unwrap();
    let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferServiceSource::Parent,
                    source_path: CapabilityNameOrPath::try_from(
                        "fuchsia.sys2.WorkSchedulerControl",
                    )
                    .unwrap(),
                    target_path: CapabilityNameOrPath::Name(offer_use_name.clone()),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Framework,
                    source_path: CapabilityNameOrPath::Name(offer_use_name.clone()),
                    target_path: use_path.clone(),
                }))
                .build(),
        ),
    ];
    let work_scheduler = new_work_scheduler().await;
    let test =
        RoutingTestBuilder::new("a", components).add_hooks(work_scheduler.hooks()).build().await;

    check_use_work_scheduler_control(&test, vec!["b:0"].into(), use_path.clone(), false).await;
}
