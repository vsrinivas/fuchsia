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
    ::routing::component_instance::ComponentInstanceInterface,
    async_trait::async_trait,
    cm_moniker::InstancedChildMoniker,
    moniker::AbsoluteMonikerBase,
    std::sync::Arc,
};

/// Destroys a child after shutting it down.
pub struct DestroyChildAction {
    moniker: InstancedChildMoniker,
}

impl DestroyChildAction {
    pub fn new(moniker: InstancedChildMoniker) -> Self {
        Self { moniker }
    }
}

#[async_trait]
impl Action for DestroyChildAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        do_destroyed(component, self.moniker.clone()).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::DestroyChild(self.moniker.clone())
    }
}

async fn do_destroyed(
    component: &Arc<ComponentInstance>,
    moniker: InstancedChildMoniker,
) -> Result<(), ModelError> {
    let child = {
        let state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref s) => {
                s.get_live_child(&moniker.to_child_moniker()).map(|r| r.clone())
            }
            InstanceState::Purged => None,
            InstanceState::New | InstanceState::Discovered => {
                panic!("do_destroyed: not resolved");
            }
        }
    };
    if let Some(child) = child {
        if child.instanced_moniker().path().last() != Some(&moniker) {
            // The instance of the child we pulled from our live children does not match the
            // instance of the child we were asked to delete. This is possible if this
            // `DestroyChild` action raced with a separate `DestroyChild` action followed by a
            // `CreateChild` action.
            //
            // If there's already a live child with a different instance than what we were asked to
            // destroy, then surely the instance we wanted to destroy is long gone, and we can
            // safely return without doing any work.
            return Ok(());
        }

        // For destruction to behave correctly, the component has to be shut down first.
        ActionSet::register(child.clone(), ShutdownAction::new()).await?;

        let event = Event::new(&child, Ok(EventPayload::Destroyed));
        child.hooks.dispatch(&event).await?;
        let mut state = component.lock_state().await;
        match *state {
            InstanceState::Resolved(ref mut s) => {
                s.mark_child_deleted(&moniker.to_child_moniker());
            }
            InstanceState::Purged => {}
            InstanceState::New | InstanceState::Discovered => {
                panic!("do_destroyed: not resolved");
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
            actions::{test_utils::is_destroyed, ActionSet},
            testing::{
                test_helpers::{component_decl_with_test_runner, ActionsTest},
                test_hook::Lifecycle,
            },
        },
        cm_rust_testing::ComponentDeclBuilder,
    };

    #[fuchsia::test]
    async fn destroyed() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        // start component so we can witness it getting stopped.
        test.start(vec!["a"].into()).await;

        // Register `destroyed` action, and wait for it. Component should be destroyed.
        let component_root = test.look_up(vec![].into()).await;
        ActionSet::register(component_root.clone(), DestroyChildAction::new("a:0".into()))
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&component_root, &"a:0".into()).await);
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
        ActionSet::register(component_root.clone(), DestroyChildAction::new("a:0".into()))
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&component_root, &"a:0".into()).await);
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
    async fn destroyed_in_collection() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec![].into())).await;

        // Create dynamic instances in "coll".
        test.create_dynamic_child("coll", "a").await;
        test.create_dynamic_child("coll", "b").await;

        // Start the component so we can witness it getting stopped.
        test.start(vec!["coll:a"].into()).await;

        // Register `destroyed` action for "a" only.
        let component_root = test.look_up(vec![].into()).await;
        ActionSet::register(component_root.clone(), DestroyChildAction::new("coll:a:1".into()))
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&component_root, &"coll:a:1".into()).await);
        assert!(!is_destroyed(&component_root, &"coll:b:2".into()).await);

        // Register `destroyed` action for "b".
        ActionSet::register(component_root.clone(), DestroyChildAction::new("coll:b:2".into()))
            .await
            .expect("destroy failed");
        assert!(is_destroyed(&component_root, &"coll:a:1".into()).await);
        assert!(is_destroyed(&component_root, &"coll:b:2".into()).await);
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

    #[fuchsia::test]
    async fn destroy_runs_after_new_instance_created() {
        // We want to demonstrate that running two destroy child actions for the same child
        // instance, which should be idempotent, works correctly if a new instance of the child
        // under the same name is created between them.
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_transient_collection("coll").build()),
            ("a", component_decl_with_test_runner()),
            ("b", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, Some(vec![].into())).await;

        // Create dynamic instance in "coll".
        test.create_dynamic_child("coll", "a").await;

        // Start the component so we can witness it getting stopped.
        test.start(vec!["coll:a"].into()).await;

        // We're going to run the destroy action for `a` twice. One after the other finishes, so
        // the actions semantics don't dedup them to the same work item.
        let component_root = test.look_up(vec![].into()).await;
        let destroy_fut_1 =
            ActionSet::register(component_root.clone(), DestroyChildAction::new("coll:a:1".into()));
        let destroy_fut_2 =
            ActionSet::register(component_root.clone(), DestroyChildAction::new("coll:a:1".into()));

        assert!(!is_destroyed(&component_root, &"coll:a:1".into()).await);

        destroy_fut_1.await.expect("destroy failed");
        assert!(is_destroyed(&component_root, &"coll:a:1".into()).await);

        // Now recreate `a`
        test.create_dynamic_child("coll", "a").await;
        test.start(vec!["coll:a"].into()).await;

        // Run the second destroy fut, it should leave the newly created `a` alone
        destroy_fut_2.await.expect("destroy failed");
        assert!(is_destroyed(&component_root, &"coll:a:1".into()).await);
        assert!(!is_destroyed(&component_root, &"coll:a:2".into()).await);

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
                ],
            );
        }
    }
}
