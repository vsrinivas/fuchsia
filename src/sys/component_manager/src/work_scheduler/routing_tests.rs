use {
    crate::{
        model::*,
        model::{
            hooks::Hook,
            testing::{routing_test_helpers::*, test_helpers::*},
        },
        work_scheduler::work_scheduler::*,
    },
    cm_rust::{
        self, ChildDecl, ComponentDecl, ExposeDecl, ExposeLegacyServiceDecl, ExposeSource,
        ExposeTarget, UseDecl, UseLegacyServiceDecl, UseSource,
    },
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, MODE_TYPE_SERVICE},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    std::{collections::HashMap, path::Path, sync::Arc},
};

async fn call_work_scheduler_svc_from_namespace(
    resolved_url: String,
    namespaces: Arc<Mutex<HashMap<String, fsys::ComponentNamespace>>>,
    should_succeed: bool,
) {
    let path = &WORK_SCHEDULER_CAPABILITY_PATH;
    let dir_proxy = capability_util::get_dir_from_namespace(&path.dirname, resolved_url, namespaces)
        .await;
    let node_proxy = io_util::open_node(
        &dir_proxy,
        &Path::new(&path.basename),
        OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
    )
    .expect("failed to open WorkScheduler service");
    let work_scheduler_proxy =
        fsys::WorkSchedulerProxy::new(node_proxy.into_channel().unwrap());
    let req = fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: None };
    let res = work_scheduler_proxy.schedule_work("hippos", req).await;

    match should_succeed {
        true => assert_eq!(res.expect("failed to use WorkScheduler service"), Ok(())),
        false => {
            let err =
                res.expect_err("used WorkScheduler service successfully when it should fail");
            if let fidl::Error::ClientRead(status) = err {
                assert_eq!(status, zx::Status::PEER_CLOSED);
            } else {
                panic!("unexpected error value: {}", err);
            }
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
    let test = RoutingTest::new_with_hooks(
        "a",
        components,
        vec![Hook::RouteFrameworkCapability(Arc::new(WorkSchedulerHook::new()))],
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
    let test = RoutingTest::new_with_hooks(
        "a",
        components,
        vec![Hook::RouteFrameworkCapability(Arc::new(WorkSchedulerHook::new()))],
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
    let test = RoutingTest::new_with_hooks(
        "a",
        components,
        vec![Hook::RouteFrameworkCapability(Arc::new(WorkSchedulerHook::new()))],
    )
    .await;
    check_use_work_scheduler(&test, vec!["b:0"].into(), false).await;
}
