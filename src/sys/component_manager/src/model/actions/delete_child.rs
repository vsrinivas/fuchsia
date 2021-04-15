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
    moniker::ChildMoniker,
    std::sync::Arc,
};

/// Completely deletes the given child of a component.
pub struct DeleteChildAction {
    moniker: ChildMoniker,
}

impl DeleteChildAction {
    pub fn new(moniker: ChildMoniker) -> Self {
        Self { moniker }
    }
}

#[async_trait]
impl Action for DeleteChildAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_delete_child(component, self.moniker.clone()).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::DeleteChild(self.moniker.clone())
    }
}

async fn do_delete_child(
    component: &Arc<ComponentInstance>,
    moniker: ChildMoniker,
) -> Result<(), ModelError> {
    // The child may not exist or may already be deleted by a previous DeleteChild action.
    let child = {
        let state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref s) => s.all_children().get(&moniker).map(|r| r.clone()),
            InstanceState::Destroyed => None,
            InstanceState::New | InstanceState::Discovered => {
                panic!("do_delete_child: not resolved");
            }
        }
    };
    if let Some(child) = child {
        // Wait for the child component to be destroyed
        ActionSet::register(child.clone(), DestroyAction::new()).await?;

        // Remove the child component from the parent's list of children
        {
            let mut state = component.lock_state().await;
            match *state {
                InstanceState::Resolved(ref mut s) => {
                    s.remove_child(&moniker);
                }
                InstanceState::Destroyed => {}
                InstanceState::New | InstanceState::Discovered => {
                    panic!("do_delete_child: not resolved");
                }
            }
        }

        // Send the Destroyed event for the component
        let event = Event::new(&child, Ok(EventPayload::Destroyed));
        component.hooks.dispatch(&event).await?;
    }

    Ok(())
}
