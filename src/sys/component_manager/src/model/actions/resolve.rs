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
    moniker::AbsoluteMonikerBase,
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
                    return Err(ModelError::instance_not_found(component.abs_moniker.to_partial()));
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
                        return Err(ModelError::instance_not_found(
                            component.abs_moniker.to_partial(),
                        ));
                    }
                    InstanceState::New | InstanceState::Discovered => {}
                }
                state.set(InstanceState::Resolved(
                    ResolvedInstanceState::new(component, component_info.decl.clone()).await?,
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
                    decl: component_info.decl.clone(),
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
