// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionKey, ActionSet, ShutdownAction},
        component::{ComponentInstance, InstanceState},
        error::ModelError,
        hooks::{Event, EventPayload},
    },
    async_trait::async_trait,
    moniker::PartialChildMoniker,
    std::sync::Arc,
};

/// Marks a child of a component as deleted, after shutting it down.
pub struct MarkDeletedAction {
    moniker: PartialChildMoniker,
}

impl MarkDeletedAction {
    pub fn new(moniker: PartialChildMoniker) -> Self {
        Self { moniker }
    }
}

#[async_trait]
impl Action for MarkDeletedAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_mark_deleted(component, self.moniker.clone()).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::MarkDeleted(self.moniker.clone())
    }
}

async fn do_mark_deleted(
    component: &Arc<ComponentInstance>,
    moniker: PartialChildMoniker,
) -> Result<(), ModelError> {
    let child = {
        let state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref s) => s.get_live_child(&moniker).map(|r| r.clone()),
            InstanceState::Destroyed => None,
            InstanceState::New | InstanceState::Discovered => {
                panic!("do_mark_deleted: not resolved");
            }
        }
    };
    if let Some(child) = child {
        // For destruction to behave correctly, the component has to be shut down first.
        ActionSet::register(child.clone(), ShutdownAction::new()).await?;

        let event = Event::new(&child, Ok(EventPayload::MarkedForDestruction));
        child.hooks.dispatch(&event).await?;
        let mut state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref mut s) => {
                s.mark_child_deleted(&moniker);
            }
            InstanceState::Destroyed => {}
            InstanceState::New | InstanceState::Discovered => {
                panic!("do_mark_deleted: not resolved");
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
            actions::{test_utils::is_marked_deleted, ActionSet},
            testing::{
                test_helpers::{component_decl_with_test_runner, ActionsTest},
                test_hook::Lifecycle,
            },
        },
        cm_rust_testing::ComponentDeclBuilder,
    };

    #[fuchsia::test]
    async fn mark_deleted() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        // Bind to component so we can witness it getting stopped.
        test.bind(vec!["a:0"].into()).await;

        // Register `mark_deleted` action, and wait for it. Component should be marked deleted.
        let component_root = test.look_up(vec![].into()).await;
        ActionSet::register(component_root.clone(), MarkDeletedAction::new("a".into()))
            .await
            .expect("mark delete failed");
        assert!(is_marked_deleted(&component_root, &"a:0".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::PreDestroy(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a:0"].into()),
                    Lifecycle::PreDestroy(vec!["a:0"].into())
                ],
            );
        }

        // Execute action again, same state and no new events.
        ActionSet::register(component_root.clone(), MarkDeletedAction::new("a".into()))
            .await
            .expect("mark delete failed");
        assert!(is_marked_deleted(&component_root, &"a:0".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::PreDestroy(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a:0"].into()),
                    Lifecycle::PreDestroy(vec!["a:0"].into())
                ],
            );
        }
    }

    #[fuchsia::test]
    async fn mark_deleted_in_collection() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec![].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Bind to component so we can witness it getting stopped.
        test.bind(vec!["coll:a:1"].into()).await;

        // Register `mark_deleted` action for "a" only.
        let component_root = test.look_up(vec![].into()).await;
        ActionSet::register(component_root.clone(), MarkDeletedAction::new("coll:a".into()))
            .await
            .expect("mark delete failed");
        assert!(is_marked_deleted(&component_root, &"coll:a:1".into()).await);
        assert!(!is_marked_deleted(&component_root, &"coll:b:2".into()).await);

        // Register `mark_deleted` action for "b".
        ActionSet::register(component_root.clone(), MarkDeletedAction::new("coll:b".into()))
            .await
            .expect("mark delete failed");
        assert!(is_marked_deleted(&component_root, &"coll:a:1".into()).await);
        assert!(is_marked_deleted(&component_root, &"coll:b:2".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) | Lifecycle::PreDestroy(_) | Lifecycle::Destroy(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["coll:a:1"].into()),
                    Lifecycle::PreDestroy(vec!["coll:a:1"].into()),
                    Lifecycle::PreDestroy(vec!["coll:b:2"].into())
                ],
            );
        }
    }
}
