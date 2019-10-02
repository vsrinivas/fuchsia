use {
    crate::{
        model::testing::{routing_test_helpers::*, test_helpers::*},
        model::*,
        work_scheduler::work_scheduler::*,
    },
    cm_rust::{
        self, CapabilityPath, ChildDecl, ComponentDecl, ExposeDecl, ExposeLegacyServiceDecl,
        ExposeSource, ExposeTarget, OfferDecl, OfferLegacyServiceDecl, OfferServiceSource,
        OfferTarget, UseDecl, UseLegacyServiceDecl, UseSource,
    },
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_io::{MODE_TYPE_SERVICE, OPEN_RIGHT_READABLE},
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    futures::lock::Mutex,
    futures::prelude::*,
    std::{collections::HashMap, convert::TryFrom, path::Path, sync::Arc},
};

async fn call_work_scheduler_svc_from_namespace(
    resolved_url: String,
    namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
    should_succeed: bool,
) {
    let path = &WORK_SCHEDULER_CAPABILITY_PATH;
    let dir_proxy =
        capability_util::get_dir_from_namespace(&path.dirname, resolved_url, namespaces).await;
    let node_proxy = io_util::open_node(
        &dir_proxy,
        &Path::new(&path.basename),
        OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
    )
    .expect("failed to open WorkScheduler service");
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
    let component_name = RoutingTest::bind_instance(&routing_test.model, &moniker).await;
    let component_resolved_url = RoutingTest::resolved_url(&component_name);
    call_work_scheduler_svc_from_namespace(
        component_resolved_url,
        routing_test.namespaces.clone(),
        should_succeed,
    )
    .await;
}

async fn call_work_scheduler_control_svc_from_namespace(
    resolved_url: String,
    namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
    path: CapabilityPath,
    should_succeed: bool,
) {
    let dir_proxy =
        capability_util::get_dir_from_namespace(&path.dirname, resolved_url, namespaces).await;
    let node_proxy = io_util::open_node(
        &dir_proxy,
        &Path::new(&path.basename),
        OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
    )
    .expect("failed to open WorkSchedulerControl service");
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
    let component_name = RoutingTest::bind_instance(&routing_test.model, &moniker).await;
    let component_resolved_url = RoutingTest::resolved_url(&component_name);
    call_work_scheduler_control_svc_from_namespace(
        component_resolved_url,
        routing_test.namespaces.clone(),
        path.clone(),
        should_succeed,
    )
    .await;
}

pub fn work_scheduler_control_builtin_service_fs() -> ServiceFs<ServiceObj<'static, ()>> {
    let mut builtin_service_fs = ServiceFs::new();
    builtin_service_fs.add_fidl_service_at(fsys::WorkSchedulerControlMarker::NAME, move |stream| {
        fasync::spawn(
            WorkScheduler::serve_root_work_scheduler_control(stream)
                .unwrap_or_else(|e| panic!("Error while serving work scheduler control: {}", e)),
        )
    });
    builtin_service_fs
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
        (
            "a",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                exposes: vec![ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                    source: ExposeSource::Self_,
                    source_path: (*WORKER_CAPABILITY_PATH).clone(),
                    target_path: (*WORKER_CAPABILITY_PATH).clone(),
                    target: ExposeTarget::Framework,
                })],
                uses: vec![UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Framework,
                    source_path: (*WORK_SCHEDULER_CAPABILITY_PATH).clone(),
                    target_path: (*WORK_SCHEDULER_CAPABILITY_PATH).clone(),
                })],
                ..default_component_decl()
            },
        ),
    ];
    let work_scheduler = WorkScheduler::new();
    let test = RoutingTest::new_with_hooks(
        "a",
        components,
        work_scheduler.hooks(),
        default_builtin_service_fs,
    )
    .await;
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
        (
            "a",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Framework,
                    source_path: (*WORK_SCHEDULER_CAPABILITY_PATH).clone(),
                    target_path: (*WORK_SCHEDULER_CAPABILITY_PATH).clone(),
                })],
                ..default_component_decl()
            },
        ),
    ];
    let work_scheduler = WorkScheduler::new();
    let test = RoutingTest::new_with_hooks(
        "a",
        components,
        work_scheduler.hooks(),
        default_builtin_service_fs,
    )
    .await;
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
        (
            "a",
            ComponentDecl {
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                exposes: vec![ExposeDecl::LegacyService(ExposeLegacyServiceDecl {
                    source: ExposeSource::Self_,
                    source_path: (*WORKER_CAPABILITY_PATH).clone(),
                    target_path: (*WORKER_CAPABILITY_PATH).clone(),
                    target: ExposeTarget::Realm,
                })],
                uses: vec![UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Framework,
                    source_path: (*WORK_SCHEDULER_CAPABILITY_PATH).clone(),
                    target_path: (*WORK_SCHEDULER_CAPABILITY_PATH).clone(),
                })],
                ..default_component_decl()
            },
        ),
    ];
    let work_scheduler = WorkScheduler::new();
    let test = RoutingTest::new_with_hooks(
        "a",
        components,
        work_scheduler.hooks(),
        default_builtin_service_fs,
    )
    .await;
    check_use_work_scheduler(&test, vec!["b:0"].into(), false).await;
}

///   a
///    \
///     b
///
/// b: uses WorkSchedulerControl offered by by a
#[fuchsia_async::run_singlethreaded(test)]
async fn use_work_scheduler_control_routed() {
    let offer_use_path = CapabilityPath::try_from("/svc/WorkSchedulerControl").unwrap();
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Realm,
                    source_path: (*WORK_SCHEDULER_CONTROL_CAPABILITY_PATH).clone(),
                    target_path: offer_use_path.clone(),
                    target: OfferTarget::Child("b".to_string()),
                })],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Realm,
                    source_path: offer_use_path.clone(),
                    target_path: offer_use_path.clone(),
                })],
                ..default_component_decl()
            },
        ),
    ];
    let work_scheduler = WorkScheduler::new();

    let test = RoutingTest::new_with_hooks(
        "a",
        components,
        work_scheduler.hooks(),
        work_scheduler_control_builtin_service_fs,
    )
    .await;

    check_use_work_scheduler_control(&test, vec!["b:0"].into(), offer_use_path.clone(), true).await;
}

///   a
///    \
///     b
///
/// b: uses framework service /svc/fuchsia.sys2.WorkSchedulerControl from framework (not allowed)
#[fuchsia_async::run_singlethreaded(test)]
async fn use_work_scheduler_control_fail() {
    let offer_use_path = CapabilityPath::try_from("/svc/WorkSchedulerControl").unwrap();
    let components = vec![
        (
            "a",
            ComponentDecl {
                offers: vec![OfferDecl::LegacyService(OfferLegacyServiceDecl {
                    source: OfferServiceSource::Realm,
                    source_path: (*WORK_SCHEDULER_CONTROL_CAPABILITY_PATH).clone(),
                    target_path: offer_use_path.clone(),
                    target: OfferTarget::Child("b".to_string()),
                })],
                children: vec![ChildDecl {
                    name: "b".to_string(),
                    url: "test:///b".to_string(),
                    startup: fsys::StartupMode::Lazy,
                }],
                ..default_component_decl()
            },
        ),
        (
            "b",
            ComponentDecl {
                uses: vec![UseDecl::LegacyService(UseLegacyServiceDecl {
                    source: UseSource::Framework,
                    source_path: offer_use_path.clone(),
                    target_path: offer_use_path.clone(),
                })],
                ..default_component_decl()
            },
        ),
    ];
    let work_scheduler = WorkScheduler::new();
    let test = RoutingTest::new_with_hooks(
        "a",
        components,
        work_scheduler.hooks(),
        work_scheduler_control_builtin_service_fs,
    )
    .await;

    check_use_work_scheduler_control(&test, vec!["b:0"].into(), offer_use_path.clone(), false)
        .await;
}
