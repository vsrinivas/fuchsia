// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionKey, ActionSet, PurgeAction},
        component::{ComponentInstance, InstanceState},
        error::ModelError,
        hooks::{Event, EventPayload},
    },
    async_trait::async_trait,
    cm_moniker::InstancedChildMoniker,
    moniker::AbsoluteMonikerBase,
    routing::component_instance::ComponentInstanceInterface,
    std::sync::Arc,
};

/// Completely deletes the given child of a component.
pub struct PurgeChildAction {
    moniker: InstancedChildMoniker,
}

impl PurgeChildAction {
    pub fn new(moniker: InstancedChildMoniker) -> Self {
        Self { moniker }
    }
}

#[async_trait]
impl Action for PurgeChildAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_purge_child(component, self.moniker.clone()).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::PurgeChild(self.moniker.clone())
    }
}

async fn do_purge_child(
    component: &Arc<ComponentInstance>,
    moniker: InstancedChildMoniker,
) -> Result<(), ModelError> {
    // The child may not exist or may already be deleted by a previous DeleteChild action.
    let child = {
        let state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref s) => {
                let child = s.get_live_child(&moniker.without_instance_id()).map(|r| r.clone());
                child
            }
            InstanceState::Purged => None,
            InstanceState::New | InstanceState::Discovered => {
                panic!("DestroyChild: target is not resolved");
            }
        }
    };
    if let Some(child) = child {
        if child.instanced_moniker().path().last() != Some(&moniker) {
            // The instance of the child we pulled from our live children does not match the
            // instance of the child we were asked to delete. This is possible if a
            // `DestroyChild` action was registered twice on the same component, and after the
            // first action was run a child with the same name was recreated.
            //
            // If there's already a live child with a different instance than what we were asked to
            // destroy, then surely the instance we wanted to destroy is long gone, and we can
            // safely return without doing any work.
            return Ok(());
        }

        // Wait for the child component to be destroyed
        ActionSet::register(child.clone(), PurgeAction::new()).await?;

        // Remove the child component from the parent's list of children
        {
            let mut state = component.lock_state().await;
            match *state {
                InstanceState::Resolved(ref mut s) => {
                    s.remove_child(&moniker);
                }
                InstanceState::Purged => {}
                InstanceState::New | InstanceState::Discovered => {
                    panic!("do_purge_child: not resolved");
                }
            }
        }

        // TODO(fxbug.dev/100652): Replace Purged event with Destroyed
        let event = Event::new(&child, Ok(EventPayload::Destroyed));
        component.hooks.dispatch(&event).await?;
        // Send the Purged event for the component
        let event = Event::new(&child, Ok(EventPayload::Purged));
        component.hooks.dispatch(&event).await?;
    }

    Ok(())
}
