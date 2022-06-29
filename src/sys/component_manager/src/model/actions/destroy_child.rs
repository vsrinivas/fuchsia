// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionKey, ActionSet, DestroyAction},
        component::{ComponentInstance, InstanceState},
        error::ModelError,
        hooks::{Event, EventPayload},
    },
    async_trait::async_trait,
    cm_moniker::IncarnationId,
    moniker::ChildMoniker,
    std::sync::Arc,
};

/// Completely destroys the given child of a component.
pub struct DestroyChildAction {
    moniker: ChildMoniker,
    incarnation: IncarnationId,
}

impl DestroyChildAction {
    pub fn new(moniker: ChildMoniker, incarnation: IncarnationId) -> Self {
        Self { moniker, incarnation }
    }
}

#[async_trait]
impl Action for DestroyChildAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_destroy_child(component, &self.moniker, self.incarnation).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::DestroyChild(self.moniker.clone(), self.incarnation)
    }
}

async fn do_destroy_child(
    component: &Arc<ComponentInstance>,
    moniker: &ChildMoniker,
    incarnation: IncarnationId,
) -> Result<(), ModelError> {
    // The child may not exist or may already be deleted by a previous DeleteChild action.
    let child = {
        let state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref s) => {
                let child = s.get_child(moniker).map(|r| r.clone());
                child
            }
            InstanceState::Destroyed => None,
            InstanceState::New | InstanceState::Unresolved => {
                panic!("DestroyChild: target is not resolved");
            }
        }
    };
    if let Some(child) = child {
        if child.incarnation_id() != incarnation {
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
        ActionSet::register(child.clone(), DestroyAction::new()).await?;

        // Remove the child component from the parent's list of children
        {
            let mut state = component.lock_state().await;
            match *state {
                InstanceState::Resolved(ref mut s) => {
                    s.remove_child(moniker);
                }
                InstanceState::Destroyed => {}
                InstanceState::New | InstanceState::Unresolved => {
                    panic!("do_purge_child: not resolved");
                }
            }
        }

        // Send the Destroyed event for the component
        let event = Event::new(&child, Ok(EventPayload::Destroyed));
        component.hooks.dispatch(&event).await?;
    }

    Ok(())
}
