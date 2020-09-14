// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::Action,
        binding::Binder,
        environment::Environment,
        error::ModelError,
        moniker::AbsoluteMoniker,
        realm::{BindReason, Realm},
    },
    std::sync::Arc,
};

/// Parameters for initializing a component model, particularly the root of the component
/// instance tree.
pub struct ModelParams {
    /// The URL of the root component.
    // TODO(viktard): Merge into RuntimeConfig
    pub root_component_url: String,
    /// The environment provided to the root realm.
    pub root_environment: Environment,
}

/// The component model holds authoritative state about a tree of component instances, including
/// each instance's identity, lifecycle, capabilities, and topological relationships.  It also
/// provides operations for instantiating, destroying, querying, and controlling component
/// instances at runtime.
pub struct Model {
    pub root_realm: Arc<Realm>,
}

impl Model {
    /// Creates a new component model and initializes its topology.
    pub fn new(params: ModelParams) -> Model {
        Model {
            root_realm: Arc::new(Realm::new_root_realm(
                params.root_environment,
                params.root_component_url,
            )),
        }
    }

    /// Looks up a realm by absolute moniker. The component instance in the realm will be resolved
    /// if that has not already happened.
    pub async fn look_up_realm(
        &self,
        look_up_abs_moniker: &AbsoluteMoniker,
    ) -> Result<Arc<Realm>, ModelError> {
        let mut cur_realm = self.root_realm.clone();
        for moniker in look_up_abs_moniker.path().iter() {
            cur_realm = {
                let cur_state = cur_realm.lock_resolved_state().await?;
                if let Some(r) = cur_state.all_child_realms().get(moniker) {
                    r.clone()
                } else {
                    return Err(ModelError::instance_not_found(look_up_abs_moniker.clone()));
                }
            };
        }
        let _ = cur_realm.lock_resolved_state().await?;
        Ok(cur_realm)
    }

    /// Binds to the root realm, starting the component tree
    pub async fn start(self: &Arc<Model>) {
        let root_moniker = AbsoluteMoniker::root();
        if let Err(e) = self.bind(&root_moniker, &BindReason::Root).await {
            // If we fail binding to the root realm, but the root realm is being shutdown, that's
            // ok. The system is tearing down, so it doesn't matter any more if we never got
            // everything started that we wanted to.
            let action_set = self.root_realm.lock_actions().await;
            if !action_set.contains(&Action::Shutdown) {
                panic!(
                    "failed to bind to root component {}: {:?}",
                    self.root_realm.component_url, e
                );
            }
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {
        crate::{
            config::RuntimeConfig,
            model::actions::{Action, ActionStatus},
            model::testing::test_helpers::{new_test_model, ComponentDeclBuilder, TestModelResult},
        },
        fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
        std::sync::Arc,
    };

    #[fasync::run_singlethreaded(test)]
    async fn shutting_down_when_start_fails() {
        let components = vec![(
            "root",
            ComponentDeclBuilder::new()
                .add_child(cm_rust::ChildDecl {
                    name: "bad-scheme".to_string(),
                    url: "bad-scheme://sdf".to_string(),
                    startup: fsys::StartupMode::Eager,
                    environment: None,
                })
                .build(),
        )];

        let TestModelResult { model, .. } =
            new_test_model("root", components, RuntimeConfig::default()).await;

        model
            .root_realm
            .lock_actions()
            .await
            .rep
            .insert(Action::Shutdown, Arc::new(ActionStatus::new(false)));

        model.start().await;
    }

    #[should_panic]
    #[fasync::run_singlethreaded(test)]
    async fn not_shutting_down_when_start_fails() {
        let components = vec![(
            "root",
            ComponentDeclBuilder::new()
                .add_child(cm_rust::ChildDecl {
                    name: "bad-scheme".to_string(),
                    url: "bad-scheme://sdf".to_string(),
                    startup: fsys::StartupMode::Eager,
                    environment: None,
                })
                .build(),
        )];

        let TestModelResult { model, .. } =
            new_test_model("root", components, RuntimeConfig::default()).await;

        model.start().await;
    }
}
