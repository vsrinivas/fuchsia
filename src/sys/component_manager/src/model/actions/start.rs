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
    },
    ::routing::{component_instance::ComponentInstanceInterface, policy::GlobalPolicyChecker},
    async_trait::async_trait,
    cm_runner::Runner,
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

struct StartContext {
    component_decl: cm_rust::ComponentDecl,
    resolved_url: String,
    runner: Arc<dyn Runner>,
    start_info: fcrunner::ComponentStartInfo,
    controller_server_end: ServerEnd<fcrunner::ComponentControllerMarker>,
}

/// The result of trying to configure the runtime on the instance's execution
enum RuntimeConfigResult {
    Success,
    /// Configuration failed because the instance is already started
    AlreadyStarted,
    Error(ModelError),
}

async fn do_start(
    component: &Arc<ComponentInstance>,
    bind_reason: &BindReason,
) -> Result<(), ModelError> {
    // Pre-flight check: if the component is already started, or was shut down, return now. Note
    // that `bind_at` also performs this check before scheduling the action here. We do it again
    // while the action is registered to avoid the risk of dispatching the Started event twice.
    {
        let state = component.lock_state().await;
        let execution = component.lock_execution().await;
        if let Some(res) =
            should_return_early(&state, &execution, &component.abs_moniker.to_partial())
        {
            return res;
        }
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

        Ok((
            StartContext {
                component_decl: component_info.decl,
                resolved_url: component_info.resolved_url.clone(),
                runner,
                start_info,
                controller_server_end,
            },
            pending_runtime,
        ))
    }
    .await;

    let (start_context, pending_runtime) = match result {
        Ok((start_context, mut pending_runtime)) => {
            let event = Event::new_with_timestamp(
                component,
                Ok(EventPayload::Started {
                    component: component.into(),
                    runtime: RuntimeInfo::from_runtime(
                        &mut pending_runtime,
                        start_context.resolved_url.clone(),
                    ),
                    component_decl: start_context.component_decl.clone(),
                    bind_reason: bind_reason.clone(),
                }),
                pending_runtime.timestamp,
            );

            component.hooks.dispatch(&event).await?;
            (start_context, pending_runtime)
        }
        Err(e) => {
            let event = Event::new(component, Err(EventError::new(&e, EventErrorPayload::Started)));
            component.hooks.dispatch(&event).await?;
            return Err(e);
        }
    };

    match configure_component_runtime(&component, pending_runtime).await {
        RuntimeConfigResult::Success => {
            // It's possible that the component is stopped before getting here. If so, that's fine: the
            // runner will start the component, but its stop or kill signal will be immediately set on the
            // component controller.
            start_context
                .runner
                .start(start_context.start_info, start_context.controller_server_end)
                .await;
        }
        RuntimeConfigResult::Error(e) => {
            // Since we dispatched a start event, dispatch a stop event
            // TODO(fxbug.dev/87507): It is possible this issues Stop after
            // Destroyed is issued.
            component
                .hooks
                .dispatch(&Event::new(
                    component,
                    Ok(EventPayload::Stopped { status: zx::Status::OK }),
                ))
                .await?;
            return Err(e);
        }
        RuntimeConfigResult::AlreadyStarted => {}
    }

    Ok(())
}

/// Set the Runtime in the Execution and start the exit water. From component manager's
/// perspective, this indicates that the component has started. If this returns an error, the
/// component was shut down and the Runtime is not set, otherwise the function returns the
/// start context with the runtime set. This function acquires the state and execution locks on
/// `Component`.
async fn configure_component_runtime(
    component: &Arc<ComponentInstance>,
    mut pending_runtime: Runtime,
) -> RuntimeConfigResult {
    let state = component.lock_state().await;
    let mut execution = component.lock_execution().await;

    match should_return_early(&state, &execution, &component.abs_moniker.to_partial()) {
        Some(Result::Ok(())) => return RuntimeConfigResult::AlreadyStarted,
        Some(Result::Err(e)) => return RuntimeConfigResult::Error(e),
        None => {}
    }

    pending_runtime.watch_for_exit(component.as_weak());
    execution.runtime = Some(pending_runtime);
    RuntimeConfigResult::Success
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
            checker.reboot_on_terminate_allowed(&component.abs_moniker.to_partial())?;
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
            actions::{ActionSet, ShutdownAction, StartAction, StopAction},
            component::{BindReason, ComponentInstance, InstanceState},
            error::ModelError,
            hooks::{Event, EventType, Hook, HooksRegistration},
            testing::{
                test_helpers::{self, ActionsTest},
                test_hook::Lifecycle,
            },
        },
        async_trait::async_trait,
        cm_rust::ComponentDecl,
        cm_rust_testing::{ChildDeclBuilder, ComponentDeclBuilder},
        fuchsia, fuchsia_zircon as zx,
        moniker::PartialAbsoluteMoniker,
        std::sync::{Arc, Weak},
    };

    // Child name for test child components instantiated during tests.
    const TEST_CHILD_NAME: &str = "child";

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
        let (test_topology, child) = build_tree_with_single_child(TEST_CHILD_NAME).await;
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
                assert_eq!(PartialAbsoluteMoniker::from(vec![TEST_CHILD_NAME]), m);
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
                Lifecycle::Bind(vec![format!("{}:0", TEST_CHILD_NAME).as_str()].into()),
                Lifecycle::Stop(vec![format!("{}:0", TEST_CHILD_NAME).as_str()].into())
            ]
        );
    }

    #[fuchsia::test]
    async fn restart_set_execution_runtime() {
        let (_test_harness, child) = build_tree_with_single_child(TEST_CHILD_NAME).await;

        {
            let timestamp = zx::Time::get_monotonic();
            let () = ActionSet::register(child.clone(), StartAction::new(BindReason::Debug))
                .await
                .expect("failed to start child");
            let execution = child.lock_execution().await;
            let runtime = execution.runtime.as_ref().expect("child runtime is unexpectedly empty");
            assert!(runtime.timestamp > timestamp);
        }

        {
            let () = ActionSet::register(child.clone(), StopAction::new(false, false))
                .await
                .expect("failed to stop child");
            let execution = child.lock_execution().await;
            assert!(execution.runtime.is_none());
        }

        {
            let timestamp = zx::Time::get_monotonic();
            let () = ActionSet::register(child.clone(), StartAction::new(BindReason::Debug))
                .await
                .expect("failed to start child");
            let execution = child.lock_execution().await;
            let runtime = execution.runtime.as_ref().expect("child runtime is unexpectedly empty");
            assert!(runtime.timestamp > timestamp);
        }
    }

    #[fuchsia::test]
    async fn restart_does_not_refresh_resolved_state() {
        let (mut test_harness, child) = build_tree_with_single_child(TEST_CHILD_NAME).await;

        {
            let timestamp = zx::Time::get_monotonic();
            let () = ActionSet::register(child.clone(), StartAction::new(BindReason::Debug))
                .await
                .expect("failed to start child");
            let execution = child.lock_execution().await;
            let runtime = execution.runtime.as_ref().expect("child runtime is unexpectedly empty");
            assert!(runtime.timestamp > timestamp);
        }

        {
            let () = ActionSet::register(child.clone(), StopAction::new(false, false))
                .await
                .expect("failed to stop child");
            let execution = child.lock_execution().await;
            assert!(execution.runtime.is_none());
        }

        let resolver = test_harness.resolver.as_mut();
        let original_decl =
            resolver.get_component_decl(TEST_CHILD_NAME).expect("child decl not stored");
        let mut modified_decl = original_decl.clone();
        modified_decl.children.push(ChildDeclBuilder::new().name("foo").build());
        resolver.add_component(TEST_CHILD_NAME, modified_decl.clone());

        let () = ActionSet::register(child.clone(), StartAction::new(BindReason::Debug))
            .await
            .expect("failed to start child");

        let resolved_decl = get_resolved_decl(&child).await;
        assert_ne!(resolved_decl, modified_decl);
        assert_eq!(resolved_decl, original_decl);
    }

    async fn build_tree_with_single_child(
        child_name: &'static str,
    ) -> (ActionsTest, Arc<ComponentInstance>) {
        let root_name = "root";
        let components = vec![
            (root_name, ComponentDeclBuilder::new().add_lazy_child(child_name).build()),
            (child_name, test_helpers::component_decl_with_test_runner()),
        ];
        let test_topology = ActionsTest::new(components[0].0.clone(), components, None).await;

        let child = test_topology.look_up(vec![child_name].into()).await;

        (test_topology, child)
    }

    async fn get_resolved_decl(component: &Arc<ComponentInstance>) -> ComponentDecl {
        let state = component.lock_state().await;
        let resolved_state = match &*state {
            InstanceState::Resolved(resolve_state) => resolve_state,
            _ => panic!("expected component to be resolved"),
        };

        resolved_state.decl().clone()
    }
}
