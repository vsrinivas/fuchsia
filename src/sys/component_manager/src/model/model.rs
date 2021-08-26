// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config::RuntimeConfig,
    crate::model::{
        actions::{ActionKey, DiscoverAction},
        binding::Binder,
        component::{BindReason, ComponentInstance, ComponentManagerInstance},
        context::ModelContext,
        environment::Environment,
        error::ModelError,
    },
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, PartialAbsoluteMoniker},
    std::sync::Arc,
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
    _context: Arc<ModelContext>,
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
        let model = Arc::new(Model {
            root: root.clone(),
            _context: context,
            top_instance: params.top_instance,
        });
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

    /// Looks up a component by absolute moniker. The component instance in the component will be
    /// resolved if that has not already happened.
    pub async fn look_up(
        &self,
        look_up_abs_moniker: &PartialAbsoluteMoniker,
    ) -> Result<Arc<ComponentInstance>, ModelError> {
        let mut cur = self.root.clone();
        for moniker in look_up_abs_moniker.path().iter() {
            cur = {
                let cur_state = cur.lock_resolved_state().await?;
                if let Some(r) = cur_state.get_live_child(moniker) {
                    r
                } else {
                    return Err(ModelError::instance_not_found(look_up_abs_moniker.clone()));
                }
            };
        }
        let _ = cur.lock_resolved_state().await?;
        Ok(cur)
    }

    /// Binds to the root, starting the component tree.
    pub async fn start(self: &Arc<Model>) {
        // Normally the Discovered event is dispatched when an instance is added as a child, but
        // since the root isn't anyone's child we need to dispatch it here.
        {
            let mut actions = self.root.lock_actions().await;
            let _ = actions.register_no_wait(&self.root, DiscoverAction::new());
        }
        if let Err(e) = self.bind(&AbsoluteMoniker::root(), &BindReason::Root).await {
            // If we fail binding to the root, but the root is being shutdown, that's ok. The
            // system is tearing down, so it doesn't matter any more if we never got everything
            // started that we wanted to.
            let action_set = self.root.lock_actions().await;
            if !action_set.contains(&ActionKey::Shutdown) {
                panic!("failed to bind to root component {}: {:?}", self.root.component_url, e);
            }
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {
        crate::{
            model::actions::ShutdownAction,
            model::testing::test_helpers::{TestEnvironmentBuilder, TestModelResult},
        },
        cm_rust_testing::ComponentDeclBuilder,
        fidl_fuchsia_sys2 as fsys,
    };

    #[fuchsia::test]
    async fn shutting_down_when_start_fails() {
        let components = vec![(
            "root",
            ComponentDeclBuilder::new()
                .add_child(cm_rust::ChildDecl {
                    name: "bad-scheme".to_string(),
                    url: "bad-scheme://sdf".to_string(),
                    startup: fsys::StartupMode::Eager,
                    environment: None,
                    on_terminate: None,
                })
                .build(),
        )];

        let TestModelResult { model, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

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
                    startup: fsys::StartupMode::Eager,
                    environment: None,
                    on_terminate: None,
                })
                .build(),
        )];

        let TestModelResult { model, .. } =
            TestEnvironmentBuilder::new().set_components(components).build().await;

        model.start().await;
    }
}
