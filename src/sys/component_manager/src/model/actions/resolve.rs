// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionKey, ActionSet, DiscoverAction},
        component::{
            Component, ComponentInstance, InstanceState, ResolvedInstanceState,
            WeakComponentInstance,
        },
        error::ModelError,
        hooks::{Event, EventError, EventErrorPayload, EventPayload},
        resolver::Resolver,
    },
    async_trait::async_trait,
    cm_util::io::clone_dir,
    std::convert::TryFrom,
    std::sync::Arc,
};

/// Resolves a component instance's declaration and initializes its state.
pub struct ResolveAction {}

impl ResolveAction {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait]
impl Action for ResolveAction {
    type Output = Result<Component, ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_resolve(component).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Resolve
    }
}

async fn do_resolve(component: &Arc<ComponentInstance>) -> Result<Component, ModelError> {
    {
        let execution = component.lock_execution().await;
        if execution.is_shut_down() {
            return Err(ModelError::instance_shut_down(component.abs_moniker.clone()));
        }
    }
    // Ensure `Resolved` is dispatched after `Discovered`.
    ActionSet::register(component.clone(), DiscoverAction::new()).await?;
    let result = async move {
        let first_resolve = {
            let state = component.lock_state().await;
            match *state {
                InstanceState::New => {
                    panic!("Component should be at least discovered")
                }
                InstanceState::Discovered => true,
                InstanceState::Resolved(_) => false,
                InstanceState::Purged => {
                    return Err(ModelError::instance_not_found(component.abs_moniker.clone()));
                }
            }
        };
        let component_info =
            component.environment.resolve(&component.component_url, component).await.map_err(
                |err| ModelError::ResolverError { url: component.component_url.clone(), err },
            )?;
        let component_info = Component::try_from(component_info)?;
        if first_resolve {
            {
                let mut state = component.lock_state().await;
                match *state {
                    InstanceState::Resolved(_) => {
                        panic!("Component was marked Resolved during Resolve action?");
                    }
                    InstanceState::Purged => {
                        return Err(ModelError::instance_not_found(component.abs_moniker.clone()));
                    }
                    InstanceState::New | InstanceState::Discovered => {}
                }
                state.set(InstanceState::Resolved(
                    ResolvedInstanceState::new(
                        component,
                        component_info.decl.clone(),
                        component_info.package.clone(),
                        component_info.config.clone(),
                    )
                    .await?,
                ));
            }
        }
        Ok((component_info, first_resolve))
    }
    .await;

    match result {
        Ok((component_info, false)) => Ok(component_info),
        Ok((component_info, true)) => {
            let event = Event::new(
                component,
                Ok(EventPayload::Resolved {
                    component: WeakComponentInstance::from(component),
                    resolved_url: component_info.resolved_url.clone(),
                    config: component_info.config.clone(),
                    decl: component_info.decl.clone(),
                    package_dir: component_info
                        .package
                        .as_ref()
                        .and_then(|pkg| clone_dir(Some(&pkg.package_dir))),
                }),
            );
            component.hooks.dispatch(&event).await?;
            Ok(component_info)
        }
        Err(e) => {
            let event =
                Event::new(component, Err(EventError::new(&e, EventErrorPayload::Resolved)));
            component.hooks.dispatch(&event).await?;
            Err(e)
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {
        crate::model::{
            actions::test_utils::{is_executing, is_resolved, is_stopped},
            actions::{ActionSet, ResolveAction, ShutdownAction, StartAction, StopAction},
            component::StartReason,
            error::ModelError,
            testing::test_helpers::{component_decl_with_test_runner, ActionsTest},
        },
        assert_matches::assert_matches,
        cm_rust_testing::ComponentDeclBuilder,
    };

    /// Check unresolve for _nonrecursive_ case. The system has a root with the child `a` and `a`
    /// has descendants as shown in the diagram below.
    ///  a
    ///   \
    ///    b
    ///
    /// Also tests UnresolveAction on InstanceState::Discovered.
    #[fuchsia::test]
    async fn resolve_action_test() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        // Resolve and start the components.
        let test = ActionsTest::new("root", components, None).await;
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.start(vec!["a"].into()).await;
        assert!(is_executing(&component_a).await);
        assert!(is_resolved(&component_root).await);
        assert!(is_resolved(&component_a).await);

        // Stop, then it's ok to resolve again.
        ActionSet::register(component_a.clone(), StopAction::new(false, true)).await.unwrap();
        assert!(is_resolved(&component_a).await);
        assert!(is_stopped(&component_root, &"a:0".into()).await);

        ActionSet::register(component_a.clone(), ResolveAction::new()).await.unwrap();
        assert!(is_resolved(&component_a).await);
        assert!(is_stopped(&component_root, &"a:0".into()).await);

        // Start it again then shut it down.
        ActionSet::register(component_a.clone(), StartAction::new(StartReason::Debug))
            .await
            .unwrap();
        ActionSet::register(component_a.clone(), ShutdownAction::new()).await.unwrap();

        // Error to resolve a shut-down component.
        assert_matches!(
            ActionSet::register(component_a.clone(), ResolveAction::new()).await,
            Err(ModelError::InstanceShutDown { .. })
        );
        assert!(is_resolved(&component_a).await);
        assert!(is_stopped(&component_root, &"a:0".into()).await);
    }
}
