// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        actions::{Action, ActionKey},
        component::ComponentInstance,
        error::ModelError,
    },
    async_trait::async_trait,
    std::sync::Arc,
};

/// Stops a component instance.
pub struct StopAction {
    shut_down: bool,
    is_recursive: bool,
}

impl StopAction {
    pub fn new(shut_down: bool, is_recursive: bool) -> Self {
        Self { shut_down, is_recursive }
    }
}

#[async_trait]
impl Action for StopAction {
    type Output = Result<(), ModelError>;
    async fn handle(&self, component: &Arc<ComponentInstance>) -> Self::Output {
        component.stop_instance(self.shut_down, self.is_recursive).await
    }
    fn key(&self) -> ActionKey {
        ActionKey::Stop
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        crate::model::{
            actions::{test_utils::is_stopped, ActionSet},
            testing::{
                test_helpers::{component_decl_with_test_runner, ActionsTest},
                test_hook::Lifecycle,
            },
        },
        cm_rust_testing::ComponentDeclBuilder,
    };

    #[fuchsia::test]
    async fn stopped() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        // Bind to component so we can witness it getting stopped.
        test.bind(vec!["a"].into()).await;

        // Register `stopped` action, and wait for it. Component should be stopped.
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        ActionSet::register(component_a.clone(), StopAction::new(false, false))
            .await
            .expect("stop failed");
        assert!(is_stopped(&component_root, &"a:0".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(events, vec![Lifecycle::Stop(vec!["a:0"].into())],);
        }

        // Execute action again, same state and no new events.
        ActionSet::register(component_a.clone(), StopAction::new(false, false))
            .await
            .expect("stop failed");
        assert!(is_stopped(&component_root, &"a:0".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(events, vec![Lifecycle::Stop(vec!["a:0"].into())],);
        }
    }

    #[fuchsia::test]
    async fn stopped_recursive() {
        let components = vec![
            ("root", ComponentDeclBuilder::new().add_lazy_child("a").build()),
            ("a", ComponentDeclBuilder::new().add_lazy_child("aa").build()),
            ("aa", component_decl_with_test_runner()),
        ];
        let test = ActionsTest::new("root", components, None).await;

        // Bind to component so we can witness it getting stopped.
        test.bind(vec!["a"].into()).await;
        test.bind(vec!["a", "aa"].into()).await;

        // Register `stopped` action, and wait for it. Component should be stopped.
        let component_root = test.look_up(vec![].into()).await;
        let component_a = test.look_up(vec!["a"].into()).await;
        ActionSet::register(component_a.clone(), StopAction::new(false, true))
            .await
            .expect("stop failed");
        assert!(is_stopped(&component_root, &"a:0".into()).await);
        assert!(is_stopped(&component_a, &"aa:0".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a:0", "aa:0"].into()),
                    Lifecycle::Stop(vec!["a:0"].into()),
                ],
            );
        }

        // Execute action again, same state and no new events.
        ActionSet::register(component_a.clone(), StopAction::new(false, true))
            .await
            .expect("stop failed");
        assert!(is_stopped(&component_root, &"a:0".into()).await);
        assert!(is_stopped(&component_a, &"aa:0".into()).await);
        {
            let events: Vec<_> = test
                .test_hook
                .lifecycle()
                .into_iter()
                .filter(|e| match e {
                    Lifecycle::Stop(_) => true,
                    _ => false,
                })
                .collect();
            assert_eq!(
                events,
                vec![
                    Lifecycle::Stop(vec!["a:0", "aa:0"].into()),
                    Lifecycle::Stop(vec!["a:0"].into()),
                ],
            );
        }
    }
}
