// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{ActionKey, DiscoverAction},
        component::{ComponentInstance, ComponentManagerInstance, InstanceState, StartReason},
        context::ModelContext,
        environment::Environment,
        error::ModelError,
        starter::Starter,
    },
    ::routing::{component_id_index::ComponentIdIndex, config::RuntimeConfig},
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
    std::sync::Arc,
    tracing::warn,
};

/// Parameters for initializing a component model, particularly the root of the component
/// instance tree.
pub struct ModelParams {
    // TODO(viktard): Merge into RuntimeConfig
    /// The URL of the root component.
    pub root_component_url: String,
    /// The environment provided to the root.
    pub root_environment: Environment,
    /// Global runtime configuration for the component_manager.
    pub runtime_config: Arc<RuntimeConfig>,
    /// The instance at the top of the tree, representing component manager.
    pub top_instance: Arc<ComponentManagerInstance>,
}

/// The component model holds authoritative state about a tree of component instances, including
/// each instance's identity, lifecycle, capabilities, and topological relationships.  It also
/// provides operations for instantiating, destroying, querying, and controlling component
/// instances at runtime.
pub struct Model {
    /// The instance at the top of the tree, i.e. the instance representing component manager
    /// itself.
    top_instance: Arc<ComponentManagerInstance>,
    /// The instance representing the root component. Owned by `top_instance`, but cached here for
    /// efficiency.
    root: Arc<ComponentInstance>,
    context: Arc<ModelContext>,
}

impl Model {
    /// Creates a new component model and initializes its topology.
    pub async fn new(params: ModelParams) -> Result<Arc<Model>, ModelError> {
        let context = Arc::new(ModelContext::new(params.runtime_config).await?);
        let root = ComponentInstance::new_root(
            params.root_environment,
            Arc::downgrade(&context),
            Arc::downgrade(&params.top_instance),
            params.root_component_url,
        );
        let model =
            Arc::new(Model { root: root.clone(), context, top_instance: params.top_instance });
        model.top_instance.init(root).await;
        Ok(model)
    }

    /// Returns a reference to the instance at the top of the tree (component manager's own
    /// instance).
    pub fn top_instance(&self) -> &Arc<ComponentManagerInstance> {
        &self.top_instance
    }

    /// Returns a reference to the root component instance.
    pub fn root(&self) -> &Arc<ComponentInstance> {
        &self.root
    }

    pub fn component_id_index(&self) -> Arc<ComponentIdIndex> {
        self.context.component_id_index()
    }

    /// Looks up a component by absolute moniker. The component instance in the component will be
    /// resolved if that has not already happened.
    pub async fn look_up(
        &self,
        look_up_abs_moniker: &AbsoluteMoniker,
    ) -> Result<Arc<ComponentInstance>, ModelError> {
        let mut cur = self.root.clone();
        for moniker in look_up_abs_moniker.path().iter() {
            cur = {
                let cur_state = cur.lock_resolved_state().await?;
                if let Some(c) = cur_state.get_child(moniker) {
                    c.clone()
                } else {
                    return Err(ModelError::instance_not_found(look_up_abs_moniker.clone()));
                }
            };
        }
        cur.lock_resolved_state().await?;
        Ok(cur)
    }

    /// Finds a component matching the absolute moniker, if such a component exists.
    /// This function has no side-effects.
    pub async fn find(
        &self,
        look_up_abs_moniker: &AbsoluteMoniker,
    ) -> Option<Arc<ComponentInstance>> {
        let mut cur = self.root.clone();
        for moniker in look_up_abs_moniker.path().iter() {
            cur = {
                let state = cur.lock_state().await;
                match &*state {
                    InstanceState::Resolved(r) => {
                        if let Some(c) = r.get_child(moniker) {
                            c.clone()
                        } else {
                            return None;
                        }
                    }
                    _ => return None,
                }
            };
        }
        Some(cur)
    }

    /// Finds a resolved component matching the absolute moniker, if such a component exists.
    /// This function has no side-effects.
    pub async fn find_resolved(
        &self,
        find_abs_moniker: &AbsoluteMoniker,
    ) -> Option<Arc<ComponentInstance>> {
        let mut cur = self.root.clone();
        for moniker in find_abs_moniker.path().iter() {
            cur = {
                let state = cur.lock_state().await;
                match &*state {
                    InstanceState::Resolved(r) => match r.get_child(moniker) {
                        Some(c) => c.clone(),
                        _ => return None,
                    },
                    _ => return None,
                }
            };
        }
        // Found the moniker, the last child in the chain of resolved parents. Is it resolved?
        let state = cur.lock_state().await;
        match &*state {
            InstanceState::Resolved(_) => Some(cur.clone()),
            _ => None,
        }
    }

    /// Starts root, starting the component tree.
    pub async fn start(self: &Arc<Model>) {
        // Normally the Discovered event is dispatched when an instance is added as a child, but
        // since the root isn't anyone's child we need to dispatch it here.
        {
            let mut actions = self.root.lock_actions().await;
            // This returns a Future that does not need to be polled.
            let _ = actions.register_no_wait(&self.root, DiscoverAction::new());
        }

        // In debug mode, we don't start the component root. It must be started manually from
        // the lifecycle controller.
        if self.context.runtime_config().debug {
            warn!(
                "In debug mode, the root component will not be started. Use the LifecycleController
                protocol to start the root component."
            );
        } else {
            if let Err(e) = self.start_instance(&AbsoluteMoniker::root(), &StartReason::Root).await
            {
                // If we fail to start the root, but the root is being shutdown, that's ok. The
                // system is tearing down, so it doesn't matter any more if we never got everything
                // started that we wanted to.
                let action_set = self.root.lock_actions().await;
                if !action_set.contains(&ActionKey::Shutdown) {
                    panic!("failed to start root component {}: {:?}", self.root.component_url, e);
                }
            }
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {
        crate::model::{
            actions::test_utils::is_discovered,
            actions::{ActionSet, ShutdownAction, UnresolveAction},
            testing::test_helpers::{
                component_decl_with_test_runner, ActionsTest, TestEnvironmentBuilder,
                TestModelResult,
            },
        },
        assert_matches::assert_matches,
        cm_rust_testing::ComponentDeclBuilder,
        fidl_fuchsia_component_decl as fdecl,
    };

    #[fuchsia::test]
    async fn shutting_down_when_start_fails() {
        let components = vec![(
            "root",
            ComponentDeclBuilder::new()
                .add_child(cm_rust::ChildDecl {
                    name: "bad-scheme".to_string(),
                    url: "bad-scheme://sdf".to_string(),
                    startup: fdecl::StartupMode::Eager,
                    environment: None,
                    on_terminate: None,
                })
                .build(),
        )];

        let TestModelResult { model, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        // This returns a Future that does not need to be polled.
        let _ =
            model.root().lock_actions().await.register_inner(&model.root, ShutdownAction::new());

        model.start().await;
    }

    #[should_panic]
    #[fuchsia::test]
    async fn not_shutting_down_when_start_fails() {
        let components = vec![(
            "root",
            ComponentDeclBuilder::new()
                .add_child(cm_rust::ChildDecl {
                    name: "bad-scheme".to_string(),
                    url: "bad-scheme://sdf".to_string(),
                    startup: fdecl::StartupMode::Eager,
                    environment: None,
                    on_terminate: None,
                })
                .build(),
        )];

        let TestModelResult { model, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        model.start().await;
    }

    #[fuchsia::test]
    async fn find_resolved_test() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_eager_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_eager_child("c").add_eager_child("d").build()),
            ("c", component_decl_with_test_runner()),
            ("d", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        // Not resolved, so not found.
        assert_matches!(test.model.find_resolved(&vec!["a"].into()).await, None);
        assert_matches!(test.model.find_resolved(&vec!["a", "b"].into()).await, None);
        assert_matches!(test.model.find_resolved(&vec!["a", "b", "c"].into()).await, None);
        assert_matches!(test.model.find_resolved(&vec!["a", "b", "d"].into()).await, None);

        // Resolve each component.
        test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        let component_b = test.look_up(vec!["a", "b"].into()).await;
        let component_c = test.look_up(vec!["a", "b", "c"].into()).await;
        let component_d = test.look_up(vec!["a", "b", "d"].into()).await;

        // Now they can all be found.
        assert_matches!(test.model.find_resolved(&vec!["a"].into()).await, Some(_));
        assert_eq!(
            test.model.find_resolved(&vec!["a"].into()).await.unwrap().component_url,
            "test:///a",
        );
        assert_matches!(test.model.find_resolved(&vec!["a", "b"].into()).await, Some(_));
        assert_matches!(test.model.find_resolved(&vec!["a", "b", "c"].into()).await, Some(_));
        assert_matches!(test.model.find_resolved(&vec!["a", "b", "d"].into()).await, Some(_));
        assert_matches!(test.model.find_resolved(&vec!["a", "b", "nonesuch"].into()).await, None);

        // Unresolve, recursively.
        ActionSet::register(component_a.clone(), UnresolveAction::new())
            .await
            .expect("unresolve failed");

        // Unresolved recursively, so children in Discovered state.
        assert!(is_discovered(&component_a).await);
        assert!(is_discovered(&component_b).await);
        assert!(is_discovered(&component_c).await);
        assert!(is_discovered(&component_d).await);

        assert_matches!(test.model.find_resolved(&vec!["a"].into()).await, None);
        assert_matches!(test.model.find_resolved(&vec!["a", "b"].into()).await, None);
        assert_matches!(test.model.find_resolved(&vec!["a", "b", "c"].into()).await, None);
        assert_matches!(test.model.find_resolved(&vec!["a", "b", "d"].into()).await, None);
    }
}
