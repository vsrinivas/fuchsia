// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_manager_lib::{
        model::{
            self, hooks::*, AbsoluteMoniker, BuiltinEnvironment, ComponentManagerConfig, Model,
        },
        startup,
    },
    failure::{self, Error},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_test_workscheduler as fws,
    std::sync::Arc,
    work_scheduler_test_hook::*,
};

struct TestRunner {
    _model: Arc<Model>,
    _builtin_environment: BuiltinEnvironment,
    work_scheduler_test_hook: Arc<WorkSchedulerTestHook>,
}

async fn create_model(root_component_url: &str) -> Result<(Arc<Model>, BuiltinEnvironment), Error> {
    let root_component_url = root_component_url.to_string();
    let args = startup::Arguments {
        use_builtin_process_launcher: false,
        use_builtin_vmex: false,
        root_component_url,
    };
    let model = startup::model_setup(&args).await?;
    let builtin_environment =
        startup::builtin_environment_setup(&args, &model, ComponentManagerConfig::default())
            .await?;
    Ok((model, builtin_environment))
}

async fn install_work_scheduler_test_hook(model: &Model) -> Arc<WorkSchedulerTestHook> {
    let work_scheduler_test_hook = Arc::new(WorkSchedulerTestHook::new());
    model
        .root_realm
        .hooks
        .install(vec![HookRegistration {
            event_type: EventType::RouteFrameworkCapability,
            callback: work_scheduler_test_hook.clone(),
        }])
        .await;
    work_scheduler_test_hook
}

impl TestRunner {
    async fn new(root_component_url: &str) -> Result<Self, Error> {
        let (_model, _builtin_environment) = create_model(root_component_url).await?;
        let work_scheduler_test_hook = install_work_scheduler_test_hook(&_model).await;

        let res = _model.look_up_and_bind_instance(model::AbsoluteMoniker::root()).await;
        let expected_res: Result<(), model::ModelError> = Ok(());
        assert_eq!(format!("{:?}", expected_res), format!("{:?}", res));

        Ok(Self { _model, _builtin_environment, work_scheduler_test_hook })
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

    let dispatched_event = test_runner
        .work_scheduler_test_hook
        .wait_for_dispatched(std::time::Duration::from_secs(10))
        .await?;
    assert_eq!(DispatchedEvent::new(AbsoluteMoniker::root(), "TEST".to_string()), dispatched_event);

    Ok(())
}
