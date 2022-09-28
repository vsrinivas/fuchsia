// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionKey, ActionSet, ShutdownAction},
        component::{ComponentInstance, InstanceState},
        error::ModelError,
        hooks::{Event, EventError, EventErrorPayload, EventPayload},
    },
    anyhow::anyhow,
    async_trait::async_trait,
    routing::error::ComponentInstanceError,
    std::sync::Arc,
};

/// Returns a resolved component to the discovered state. The result is that the component can be
/// restarted, updating both the code and the manifest with destroying its resources. Unresolve can
/// only be applied to a resolved, stopped, component. This action supports the `ffx component
/// reload` command.
pub struct UnresolveAction {}

impl UnresolveAction {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl Action for UnresolveAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_unresolve(component).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Unresolve
    }
}

// Create and emit a Unresolved EventError with the given error message. Return the error.
async fn emit_unresolve_failed_event(
    component: &Arc<ComponentInstance>,
    msg: String,
) -> Result<bool, ModelError> {
    let e = ModelError::from(ComponentInstanceError::unresolve_failed(
        component.abs_moniker.clone(),
        anyhow!(msg),
    ));
    let event = Event::new(component, Err(EventError::new(&e, EventErrorPayload::Unresolved)));
    component.hooks.dispatch(&event).await?;
    Err(e)
}

// Check the component state for applicability of the UnresolveAction. Return Ok(true) if the
// component is Discovered so UnresolveAction can early-return. Return Ok(false) if the component is
// resolved so UnresolveAction can proceed. Return an error if the component is running or in an
// incompatible state.
async fn check_state(component: &Arc<ComponentInstance>) -> Result<bool, ModelError> {
    if component.lock_execution().await.runtime.is_some() {
        return emit_unresolve_failed_event(component, "component was running".to_string()).await;
    }
    match *component.lock_state().await {
        InstanceState::Unresolved => return Ok(true),
        InstanceState::Resolved(_) => return Ok(false),
        _ => {}
    }
    emit_unresolve_failed_event(component, "component was not discovered or resolved".to_string())
        .await
}

async fn unresolve_resolved_children(component: &Arc<ComponentInstance>) -> Result<(), ModelError> {
    let mut resolved_children: Vec<Arc<ComponentInstance>> = vec![];
    {
        let state = component.lock_resolved_state().await?;
        // Collect only the resolved children. It is not required that all children are resolved for
        // successful recursion. It's also unnecessary to unresolve components that are not
        // resolved, so don't include them.
        for (_, child_instance) in state.children() {
            let child_state = child_instance.lock_state().await;
            if matches!(*child_state, InstanceState::Resolved(_)) {
                resolved_children.push(child_instance.clone());
            }
        }
    };
    // Unresolve the children before unresolving the component because removing the resolved
    // state removes the ChildInstanceState that contains the list of children.
    for instance in resolved_children {
        ActionSet::register(instance.clone(), UnresolveAction::new()).await?;
    }
    Ok(())
}

// Implement the UnresolveAction by resetting the state from unresolved to unresolved and emitting
// an Unresolved event. Unresolve the component's resolved children if any.
async fn do_unresolve(component: &Arc<ComponentInstance>) -> Result<(), ModelError> {
    // Shut down the component, preventing new starts or resolves during the UnresolveAction.
    ActionSet::register(component.clone(), ShutdownAction::new()).await?;

    if check_state(&component).await? {
        return Ok(());
    }

    unresolve_resolved_children(&component).await?;

    // Move the component back to the Discovered state. We can't use a DiscoverAction for this
    // change because the system allows and does call DiscoverAction on resolved components with
    // the expectation that they will return without changing the instance state to Discovered.
    // The state may have changed during the time taken for the recursions, so recheck here.
    // TODO(fxbug.dev/100544): Investigate and change to DiscoverAction.
    let success = {
        let mut state = component.lock_state().await;
        match &*state {
            InstanceState::Resolved(_) => {
                state.set(InstanceState::Unresolved);
                true
            }
            _ => false,
        }
    };

    if !success {
        return emit_unresolve_failed_event(
            component,
            "state change failed in UnresolveAction".to_string(),
        )
        .await
        .map(|_| ());
    }

    // The component was shut down, so won't start. Re-enable it.
    component.lock_execution().await.reset_shut_down();

    let event = Event::new(&component, Ok(EventPayload::Unresolved));
    component.hooks.dispatch(&event).await
}

#[cfg(test)]
pub mod tests {
    use {
        crate::model::{
            actions::test_utils::{is_destroyed, is_discovered, is_executing, is_resolved},
            actions::{ActionSet, ShutdownAction, UnresolveAction},
            component::{ComponentInstance, InstanceState, StartReason},
            error::ModelError,
            events::{registry::EventSubscription, stream::EventStream},
            hooks::EventType,
            starter::Starter,
            testing::test_helpers::{component_decl_with_test_runner, ActionsTest},
        },
        assert_matches::assert_matches,
        cm_rust::EventMode,
        cm_rust_testing::{CollectionDeclBuilder, ComponentDeclBuilder},
        fidl_fuchsia_component_decl as fdecl, fuchsia_async as fasync,
        moniker::ChildMoniker,
        routing::error::ComponentInstanceError,
        std::sync::Arc,
    };

    /// Check unresolve for _recursive_ case. The system has a root with the child `a` and `a` has
    /// descendants as shown in the diagram below.
    ///  a
    ///   \
    ///    b
    ///     \
    ///      c
    ///
    /// Also tests UnresolveAction on InstanceState::Unresolved.
    #[fuchsia::test]
    async fn unresolve_action_recursive_test() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("c").build()),
            ("c", component_decl_with_test_runner()),
        ];
        // Resolve components without starting them.
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "b", "c"].into()).await;
        assert!(is_resolved(&component_root).await);
        assert!(is_resolved(&component_a).await);
        assert!(is_resolved(&component_b).await);
        assert!(is_resolved(&component_c).await);

        // Unresolve, recursively.
        ActionSet::register(component_a.clone(), UnresolveAction::new())
            .await
            .expect("unresolve failed");
        assert!(is_resolved(&component_root).await);
        // Unresolved recursively, so children in Discovered state.
        assert!(is_discovered(&component_a).await);
        assert!(is_discovered(&component_b).await);
        assert!(is_discovered(&component_c).await);

        // Unresolve again, which is ok because UnresolveAction is idempotent.
        assert_matches!(
            ActionSet::register(component_a.clone(), UnresolveAction::new()).await,
            Ok(())
        );
        // Still Discovered.
        assert!(is_discovered(&component_a).await);
    }

    /// Check unresolve with recursion on eagerly-loaded peer children. The system has a root with
    /// the child `a` and `a` has descendants as shown in the diagram below.
    ///  a
    ///   \
    ///    b
    ///   / \
    ///  c   d
    #[fuchsia::test]
    async fn unresolve_action_recursive_test2() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_eager_child("c").add_eager_child("d").build()),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        // Resolve each component.
        test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "b", "c"].into()).await;
        let component_d = test.look_up(vec!["a", "b", "d"].into()).await;

        // Unresolve, recursively.
        ActionSet::register(component_a.clone(), UnresolveAction::new())
            .await
            .expect("unresolve failed");

        // Unresolved recursively, so children in Discovered state.
        assert!(is_discovered(&component_a).await);
        assert!(is_discovered(&component_b).await);
        assert!(is_discovered(&component_c).await);
        assert!(is_discovered(&component_d).await);
    }

    async fn setup_unresolve_test_event_stream(
        test: &ActionsTest,
        event_types: Vec<EventType>,
    ) -> EventStream {
        let events: Vec<_> = event_types.into_iter().map(|e| e.into()).collect();
        let mut event_source = test
            .builtin_environment
            .lock()
            .await
            .event_source_factory
            .create_for_debug()
            .await
            .expect("create event source");
        let event_stream = event_source
            .subscribe(
                events
                    .into_iter()
                    .map(|event| EventSubscription::new(event, EventMode::Sync))
                    .collect(),
            )
            .await
            .expect("subscribe to event stream");
        let model = test.model.clone();
        fasync::Task::spawn(async move { model.start().await }).detach();
        event_stream
    }

    #[fuchsia::test]
    async fn unresolve_action_registers_unresolve_event_test() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;
        test.start(vec![].into()).await;
        let component_a = test.start(vec!["a"].into()).await;
        test.start(vec!["a", "b"].into()).await;

        let mut event_stream =
            setup_unresolve_test_event_stream(&test, vec![EventType::Unresolved]).await;

        // Register the UnresolveAction.
        let nf = {
            let mut actions = component_a.lock_actions().await;
            actions.register_no_wait(&component_a, UnresolveAction::new())
        };

        // Confirm that the Unresolved events are emitted in the expected recursive order.
        event_stream
            .wait_until(EventType::Unresolved, vec!["a", "b"].into())
            .await
            .unwrap()
            .resume();
        event_stream.wait_until(EventType::Unresolved, vec!["a"].into()).await.unwrap().resume();
        nf.await.unwrap();

        // Now attempt to unresolve again with another UnresolveAction.
        let nf2 = {
            let mut actions = component_a.lock_actions().await;
            actions.register_no_wait(&component_a, UnresolveAction::new())
        };
        // The component is not resolved anymore, so the unresolve will have no effect. It will not
        // emit an UnresolveFailed event.
        nf2.await.unwrap();
        assert!(is_discovered(&component_a).await);
    }

    /// Start a collection with the given durability. The system has a root with a container that
    /// has a collection containing children `a` and `b` as shown in the diagram below.
    ///    root
    ///      \
    ///    container
    ///     /     \
    ///  coll:a   coll:b
    ///
    async fn start_collection(
        durability: fdecl::Durability,
    ) -> (ActionsTest, Arc<ComponentInstance>, Arc<ComponentInstance>, Arc<ComponentInstance>) {
        let collection = CollectionDeclBuilder::new()
            .name("coll")
            .durability(durability)
            .allow_long_names(true)
            .build();

        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("container").build()),
            ("container", ComponentDeclBuilder::new().add_collection(collection).build()),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec!["container"].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Start the components. This should cause them to have an `Execution`.
        let component_container = test.look_up(vec!["container"].into()).await;
        let component_a = test.look_up(vec!["container", "coll:a"].into()).await;
        let component_b = test.look_up(vec!["container", "coll:b"].into()).await;
        test.model
            .start_instance(&component_container.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start container");
        test.model
            .start_instance(&component_a.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start coll:a");
        test.model
            .start_instance(&component_b.abs_moniker, &StartReason::Eager)
            .await
            .expect("could not start coll:b");
        assert!(is_executing(&component_container).await);
        assert!(is_resolved(&component_a).await);
        assert!(is_resolved(&component_b).await);
        assert!(is_executing(&component_a).await);
        assert!(is_executing(&component_b).await);
        (test, component_container, component_a, component_b)
    }

    /// Test a collection with the given durability.
    /// Also tests UnresolveAction on InstanceState::Destroyed.
    async fn test_collection(durability: fdecl::Durability) {
        let (_test, component_container, component_a, component_b) =
            start_collection(durability).await;

        // Stop the collection.
        ActionSet::register(component_container.clone(), ShutdownAction::new())
            .await
            .expect("shutdown failed");
        assert!(is_destroyed(&component_a).await);
        assert!(is_destroyed(&component_b).await);
        ActionSet::register(component_container.clone(), UnresolveAction::new())
            .await
            .expect("unresolve failed");
        assert!(is_discovered(&component_container).await);

        // Trying to unresolve a child fails because the children of a collection are destroyed when
        // the collection is stopped. Then it's an error to unresolve a Destroyed component.
        assert_matches!(
            ActionSet::register(component_a.clone(), UnresolveAction::new()).await,
            Err(ModelError::ComponentInstanceError {
                err: ComponentInstanceError::UnresolveFailed { .. }
            })
        );
        // Still Destroyed.
        assert!(is_destroyed(&component_a).await);
        assert!(is_destroyed(&component_b).await);
    }

    /// Test a collection whose children have transient durability.
    #[fuchsia::test]
    async fn unresolve_action_on_transient_collection() {
        test_collection(fdecl::Durability::Transient).await;
    }

    /// Test a collection whose children have single-run durability.
    #[fuchsia::test]
    async fn unresolve_action_on_single_run_collection() {
        test_collection(fdecl::Durability::SingleRun).await;
    }

    /// Check unresolve on a new component. The system has a root with the child `a`.
    #[fuchsia::test]
    async fn unresolve_action_fails_on_new_component() {
        let components = vec![("root", ComponentDeclBuilder::new().build())];
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        // This setup circumvents the registration of the Discover action on component_a.
        {
            let mut resolved_state = component_root.lock_resolved_state().await.unwrap();
            let child = cm_rust::ChildDecl {
                name: format!("a"),
                url: format!("test:///a"),
                startup: fdecl::StartupMode::Lazy,
                environment: None,
                on_terminate: None,
            };
            assert!(resolved_state.add_child_no_discover(&component_root, &child, None).is_ok());
        }

        let component_root = test.look_up(vec![].into()).await;
        let component_a = match *component_root.lock_state().await {
            InstanceState::Resolved(ref s) => {
                s.get_child(&ChildMoniker::from("a")).expect("child a not found").clone()
            }
            _ => panic!("not resolved"),
        };

        // Confirm component is still in New state.
        {
            let state = &*component_a.lock_state().await;
            assert_matches!(state, InstanceState::New);
        };

        // Try to unresolve, but it's an error to unresolve a New component.
        assert_matches!(
            ActionSet::register(component_a.clone(), UnresolveAction::new()).await,
            Err(ModelError::ComponentInstanceError {
                err: ComponentInstanceError::UnresolveFailed { .. }
            })
        );
        // Still new.
        {
            let state = &*component_a.lock_state().await;
            assert_matches!(state, InstanceState::New);
        };
    }
}
