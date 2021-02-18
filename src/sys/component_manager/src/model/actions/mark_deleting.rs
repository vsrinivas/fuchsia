// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionKey},
        component::{ComponentInstance, InstanceState},
        error::ModelError,
        hooks::{Event, EventPayload},
    },
    async_trait::async_trait,
    moniker::ChildMoniker,
    std::sync::Arc,
};

/// Marks a child of a component as deleting.
pub struct MarkDeletingAction {
    moniker: ChildMoniker,
}

impl MarkDeletingAction {
    pub fn new(moniker: ChildMoniker) -> Self {
        Self { moniker }
    }
}

#[async_trait]
impl Action for MarkDeletingAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_mark_deleting(component, self.moniker.clone()).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::MarkDeleting(self.moniker.clone())
    }
}

async fn do_mark_deleting(
    component: &Arc<ComponentInstance>,
    moniker: ChildMoniker,
) -> Result<(), ModelError> {
    let partial_moniker = moniker.to_partial();
    let child = {
        let state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref s) => s.get_live_child(&partial_moniker).map(|r| r.clone()),
            InstanceState::Destroyed => None,
            InstanceState::New | InstanceState::Discovered => {
                panic!("do_mark_deleting: not resolved");
            }
        }
    };
    if let Some(child) = child {
        let event = Event::new(&child, Ok(EventPayload::MarkedForDestruction));
        child.hooks.dispatch(&event).await?;
        let mut state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref mut s) => {
                s.mark_child_deleting(&partial_moniker);
            }
            InstanceState::Destroyed => {}
            InstanceState::New | InstanceState::Discovered => {
                panic!("do_mark_deleting: not resolved");
            }
        }
    } else {
        // Child already marked deleting. Nothing to do.
    }
    Ok(())
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        crate::model::{
            actions::{test_utils::is_deleting, ActionSet},
            testing::{
                test_helpers::{
                    component_decl_with_test_runner, ActionsTest, ComponentDeclBuilder,
                },
                test_hook::Lifecycle,
            },
        },
        fuchsia_async as fasync,
    };

    #[fasync::run_singlethreaded(test)]
    async fn mark_deleting() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        // Register `mark_deleting` action, and wait for it. Component should be marked deleted.
        let component_root = test.look_up(vec![].into()).await;
        ActionSet::register(component_root.clone(), MarkDeletingAction::new("a:0".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&component_root, "a:0".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::PreDestroy(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(events, vec![Lifecycle::PreDestroy(vec!["a:0"].into())],);
        }

        // Execute action again, same state and no new events.
        ActionSet::register(component_root.clone(), MarkDeletingAction::new("a:0".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&component_root, "a:0".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::PreDestroy(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(events, vec![Lifecycle::PreDestroy(vec!["a:0"].into())],);
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn mark_deleting_in_collection() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec![].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Register `mark_deleting` action for "a" only.
        let component_root = test.look_up(vec![].into()).await;
        ActionSet::register(component_root.clone(), MarkDeletingAction::new("coll:a:1".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&component_root, "coll:a:1".into()).await);
        assert!(!is_deleting(&component_root, "coll:b:2".into()).await);

        // Register `mark_deleting` action for "b".
        ActionSet::register(component_root.clone(), MarkDeletingAction::new("coll:b:1".into()))
            .await
            .expect("mark delete failed");
        assert!(is_deleting(&component_root, "coll:a:1".into()).await);
        assert!(is_deleting(&component_root, "coll:b:2".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::PreDestroy(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::PreDestroy(vec!["coll:a:1"].into()),
                    Lifecycle::PreDestroy(vec!["coll:b:2"].into())
                ],
            );
        }
    }
}
