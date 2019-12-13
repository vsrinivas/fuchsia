// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_manager_lib::{
        builtin_environment::BuiltinEnvironment,
        model::{
            binding::Binder,
            hooks::{EventType, Hook, HooksRegistration},
            model::ComponentManagerConfig,
            model::Model,
            moniker::AbsoluteMoniker,
        },
        startup,
    },
    failure::{self, Error},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test_workscheduler as fws,
    std::sync::{Arc, Weak},
    work_scheduler_test_hook::{DispatchedEvent, WorkSchedulerTestHook, REPORT_SERVICE},
};

struct TestRunner {
    model: Arc<Model>,
    builtin_environment: BuiltinEnvironment,
    work_scheduler_test_hook: Arc<WorkSchedulerTestHook>,
}

async fn create_model(root_component_url: &str) -> Result<(Arc<Model>, BuiltinEnvironment), Error> {
    let root_component_url = root_component_url.to_string();
    let args = startup::Arguments {
        use_builtin_process_launcher: false,
        use_builtin_vmex: false,
        root_component_url,
        debug: false,
    };
    let model = startup::model_setup(&args).await?;
    let builtin_environment =
        BuiltinEnvironment::new(&args, &model, ComponentManagerConfig::default()).await?;
    Ok((model, builtin_environment))
}

async fn install_work_scheduler_test_hook(model: &Arc<Model>) -> Arc<WorkSchedulerTestHook> {
    let work_scheduler_test_hook = Arc::new(WorkSchedulerTestHook::new());
    model
        .root_realm
        .hooks
        .install(vec![HooksRegistration {
            events: vec![EventType::RouteCapability],
            callback: Arc::downgrade(&work_scheduler_test_hook) as Weak<dyn Hook>,
        }])
        .await;
    work_scheduler_test_hook
}

impl TestRunner {
    async fn new(root_component_url: &str) -> Result<Self, Error> {
        let (model, builtin_environment) = create_model(root_component_url).await?;
        let work_scheduler_test_hook = install_work_scheduler_test_hook(&model).await;
        Ok(Self { model, builtin_environment, work_scheduler_test_hook })
    }

    async fn bind(&self) -> Result<(), Error> {
        let res = self.model.bind(&AbsoluteMoniker::root()).await;
        assert!(res.is_ok());
        Ok(())
    }
}

#[test]
fn work_scheduler_capability_paths() {
    assert_eq!(
        format!("/svc/{}", fws::WorkSchedulerDispatchReporterMarker::NAME),
        REPORT_SERVICE.to_string()
    );
}

#[fuchsia_async::run_singlethreaded(test)]
async fn basic_work_scheduler_test() -> Result<(), Error> {
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/work_scheduler_integration_test#meta/work_scheduler_client.cm";
    let test_runner = TestRunner::new(root_component_url).await?;
    test_runner.bind().await?;

    let dispatched_event = test_runner
        .work_scheduler_test_hook
        .wait_for_dispatched(std::time::Duration::from_secs(10))
        .await?;
    assert_eq!(DispatchedEvent::new(AbsoluteMoniker::root(), "TEST".to_string()), dispatched_event);

    Ok(())
}

#[fuchsia_async::run_singlethreaded(test)]
async fn unbound_work_scheduler_test() -> Result<(), Error> {
    let root_component_url =
        "fuchsia-pkg://fuchsia.com/work_scheduler_integration_test#meta/worker_client.cm";
    let test_runner = TestRunner::new(root_component_url).await?;
    let model = test_runner.model;
    let work_scheduler = test_runner.builtin_environment.work_scheduler.clone();

    work_scheduler
        .schedule_work(
            model.root_realm.clone(),
            "TEST",
            &fsys::WorkRequest { start: Some(fsys::Start::MonotonicTime(0)), period: None },
        )
        .await
        .expect("failed to schedule work");
    work_scheduler.set_batch_period(1).await.expect("failed to set batch period");

    let dispatched_event = test_runner
        .work_scheduler_test_hook
        .wait_for_dispatched(std::time::Duration::from_secs(10))
        .await?;
    assert_eq!(DispatchedEvent::new(AbsoluteMoniker::root(), "TEST".to_string()), dispatched_event);

    Ok(())
}
