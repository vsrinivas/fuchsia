// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionKey},
        component::{
            BindReason, ComponentInstance, ExecutionState, InstanceState, Package, Runtime,
        },
        error::ModelError,
        hooks::{Event, EventError, EventErrorPayload, EventPayload, RuntimeInfo},
        namespace::IncomingNamespace,
        policy::GlobalPolicyChecker,
        runner::Runner,
    },
    ::routing::component_instance::ComponentInstanceInterface,
    async_trait::async_trait,
    fidl::endpoints::{self, Proxy, ServerEnd},
    fidl_fuchsia_component_runner as fcrunner,
    fidl_fuchsia_io::DirectoryProxy,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync, fuchsia_zircon as zx,
    log::*,
    moniker::{AbsoluteMonikerBase, PartialAbsoluteMoniker},
    std::sync::Arc,
};

/// Starts a component instance.
pub struct StartAction {
    bind_reason: BindReason,
}

impl StartAction {
    pub fn new(bind_reason: BindReason) -> Self {
        Self { bind_reason }
    }
}

#[async_trait]
impl Action for StartAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_start(component, &self.bind_reason).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Start
    }
}

async fn do_start(
    component: &Arc<ComponentInstance>,
    bind_reason: &BindReason,
) -> Result<(), ModelError> {
    // Pre-flight check: if the component is already started, or was shutd down, return now. Note
    // that `bind_at` also performs this check before scheduling the action; here, we do it again
    // while the action is registered so we avoid the risk of invoking the BeforeStart hook twice.
    {
        let state = component.lock_state().await;
        let execution = component.lock_execution().await;
        if let Some(res) =
            should_return_early(&state, &execution, &component.abs_moniker.to_partial())
        {
            return res;
        }
    }

    struct StartContext {
        component_decl: cm_rust::ComponentDecl,
        resolved_url: String,
        runner: Arc<dyn Runner>,
        pending_runtime: Runtime,
        start_info: fcrunner::ComponentStartInfo,
        controller_server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
    }

    let result = async move {
        // Resolve the component.
        let component_info = component.resolve().await?;

        // Find the runner to use.
        let runner = component.resolve_runner().await.map_err(|e| {
            warn!("Failed to resolve runner for `{}`: {}", component.abs_moniker, e);
            e
        })?;

        // Generate the Runtime which will be set in the Execution.
        let checker = component.try_get_policy_checker()?;
        let (pending_runtime, start_info, controller_server_end) = make_execution_runtime(
            &component,
            &checker,
            component_info.resolved_url.clone(),
            component_info.package,
            &component_info.decl,
        )
        .await?;

        Ok(StartContext {
            component_decl: component_info.decl,
            resolved_url: component_info.resolved_url.clone(),
            runner,
            pending_runtime,
            start_info,
            controller_server_end,
        })
    }
    .await;

    let mut start_context = match result {
        Ok(mut start_context) => {
            let event = Event::new_with_timestamp(
                component,
                Ok(EventPayload::Started {
                    component: component.into(),
                    runtime: RuntimeInfo::from_runtime(
                        &mut start_context.pending_runtime,
                        start_context.resolved_url.clone(),
                    ),
                    component_decl: start_context.component_decl.clone(),
                    bind_reason: bind_reason.clone(),
                }),
                start_context.pending_runtime.timestamp,
            );

            component.hooks.dispatch(&event).await?;
            start_context
        }
        Err(e) => {
            let event = Event::new(component, Err(EventError::new(&e, EventErrorPayload::Started)));
            component.hooks.dispatch(&event).await?;
            return Err(e);
        }
    };

    // Set the Runtime in the Execution. From component manager's perspective, this indicates
    // that the component has started. This may return early if the component is shut down.
    {
        let state = component.lock_state().await;
        let mut execution = component.lock_execution().await;
        if let Some(res) =
            should_return_early(&state, &execution, &component.abs_moniker.to_partial())
        {
            // Since we dispatched a start event, dispatch a stop event
            // TODO(fxbug.dev/87507): It is possible this issues Stop after
            // Destroyed is issued.
            component
                .hooks
                .dispatch(&Event::new_with_timestamp(
                    component,
                    Ok(EventPayload::Stopped { status: zx::Status::OK }),
                    start_context.pending_runtime.timestamp,
                ))
                .await?;
            return res;
        }
        start_context.pending_runtime.watch_for_exit(component.as_weak());
        execution.runtime = Some(start_context.pending_runtime);
    }

    // It's possible that the component is stopped before getting here. If so, that's fine: the
    // runner will start the component, but its stop or kill signal will be immediately set on the
    // component controller.
    start_context.runner.start(start_context.start_info, start_context.controller_server_end).await;

    Ok(())
}

/// Returns `Some(Result)` if `bind` should return early based on either of the following:
/// - The component instance is destroyed.
/// - The component instance is shut down.
/// - The component instance is already started.
pub fn should_return_early(
    component: &InstanceState,
    execution: &ExecutionState,
    abs_moniker: &PartialAbsoluteMoniker,
) -> Option<Result<(), ModelError>> {
    match component {
        InstanceState::New | InstanceState::Discovered | InstanceState::Resolved(_) => {}
        InstanceState::Purged => {
            return Some(Err(ModelError::instance_not_found(abs_moniker.clone())));
        }
    }
    if execution.is_shut_down() {
        Some(Err(ModelError::instance_shut_down(abs_moniker.clone())))
    } else if execution.runtime.is_some() {
        Some(Ok(()))
    } else {
        None
    }
}

/// Returns a configured Runtime for a component and the start info (without actually starting
/// the component).
async fn make_execution_runtime(
    component: &Arc<ComponentInstance>,
    checker: &GlobalPolicyChecker,
    url: String,
    package: Option<Package>,
    decl: &cm_rust::ComponentDecl,
) -> Result<
    (Runtime, fcrunner::ComponentStartInfo, ServerEnd<fcrunner::ComponentControllerMarker>),
    ModelError,
> {
    match component.on_terminate {
        fsys::OnTerminate::Reboot => {
            checker.reboot_on_terminate_allowed(&component.abs_moniker)?;
        }
        fsys::OnTerminate::None => {}
    }

    // Create incoming/outgoing directories, and populate them.
    let (outgoing_dir_client, outgoing_dir_server) =
        zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
    let (runtime_dir_client, runtime_dir_server) =
        zx::Channel::create().map_err(|e| ModelError::namespace_creation_failed(e))?;
    let mut namespace = IncomingNamespace::new(package)?;
    let ns = namespace.populate(component.as_weak(), decl).await?;

    let (controller_client, controller_server) =
        endpoints::create_endpoints::<fcrunner::ComponentControllerMarker>()
            .expect("could not create component controller endpoints");
    let controller =
        controller_client.into_proxy().expect("failed to create ComponentControllerProxy");
    // Set up channels into/out of the new component. These are absent from non-executable
    // components.
    let outgoing_dir_client = decl.get_runner().map(|_| {
        DirectoryProxy::from_channel(fasync::Channel::from_channel(outgoing_dir_client).unwrap())
    });
    let runtime_dir_client = decl.get_runner().map(|_| {
        DirectoryProxy::from_channel(fasync::Channel::from_channel(runtime_dir_client).unwrap())
    });
    let runtime = Runtime::start_from(
        Some(namespace),
        outgoing_dir_client,
        runtime_dir_client,
        Some(controller),
    )?;
    let numbered_handles = component.numbered_handles.lock().await.take();
    let start_info = fcrunner::ComponentStartInfo {
        resolved_url: Some(url),
        program: decl.program.as_ref().map(|p| p.info.clone()),
        ns: Some(ns),
        outgoing_dir: Some(ServerEnd::new(outgoing_dir_server)),
        runtime_dir: Some(ServerEnd::new(runtime_dir_server)),
        numbered_handles,
        ..fcrunner::ComponentStartInfo::EMPTY
    };

    Ok((runtime, start_info, controller_server))
}

#[cfg(test)]
mod tests {
    use {
        crate::model::{
            actions::{ActionSet, ShutdownAction, StartAction},
            component::{BindReason, ComponentInstance},
            error::ModelError,
            hooks::{Event, EventType, Hook, HooksRegistration},
            testing::{
                test_helpers::{self, ActionsTest},
                test_hook::Lifecycle,
            },
        },
        async_trait::async_trait,
        cm_rust_testing::ComponentDeclBuilder,
        fuchsia,
        moniker::PartialAbsoluteMoniker,
        std::sync::{Arc, Weak},
    };

    struct StartHook {
        component: Arc<ComponentInstance>,
    }

    #[async_trait]
    impl Hook for StartHook {
        async fn on(self: Arc<Self>, _event: &Event) -> Result<(), ModelError> {
            ActionSet::register(self.component.clone(), ShutdownAction::new())
                .await
                .expect("shutdown failed");
            Ok(())
        }
    }
    #[fuchsia::test]
    /// Validate that if a start action is issued and the component stops
    /// the action completes we see a Stop event emitted.
    async fn start_issues_stop() {
        let child_name = "child";
        let root_name = "root";
        let components = vec![
            (root_name, ComponentDeclBuilder::new().add_lazy_child(child_name).build()),
            (child_name, test_helpers::component_decl_with_test_runner()),
        ];
        let test_topology = ActionsTest::new(components[0].0.clone(), components, None).await;

        let child = test_topology.look_up(vec![child_name].into()).await;
        let start_hook = Arc::new(StartHook { component: child.clone() });
        child
            .hooks
            .install(vec![HooksRegistration::new(
                "my_start_hook",
                vec![EventType::Started],
                Arc::downgrade(&start_hook) as Weak<dyn Hook>,
            )])
            .await;

        match ActionSet::register(child.clone(), StartAction::new(BindReason::Unsupported)).await {
            Err(ModelError::InstanceShutDown { moniker: m }) => {
                assert_eq!(PartialAbsoluteMoniker::from(vec![child_name]), m);
            }
            e => panic!("Unexpected result from component start: {:?}", e),
        }

        let events: Vec<_> = test_topology
            .test_hook
            .lifecycle()
            .into_iter()
            .filter(|event| match event {
                Lifecycle::Bind(_) | Lifecycle::Stop(_) => true,
                _ => false,
            })
            .collect();
        assert_eq!(
            events,
            vec![
                Lifecycle::Bind(vec![format!("{}:0", child_name).as_str()].into()),
                Lifecycle::Stop(vec![format!("{}:0", child_name).as_str()].into())
            ]
        );
    }
}
