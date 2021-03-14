// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        model::error::ModelError,
        model::hooks::{
            Event, EventPayload, EventType, HasEventType, Hook, HooksRegistration, RuntimeInfo,
        },
    },
    async_trait::async_trait,
    fidl_fuchsia_diagnostics_types as fdiagnostics, fuchsia_async as fasync,
    fuchsia_inspect as inspect, fuchsia_zircon as zx,
    futures::lock::Mutex,
    moniker::AbsoluteMoniker,
    std::collections::BTreeMap,
    std::sync::{Arc, Weak},
};

/// Provides stats for all components running in the system.
pub struct ComponentTreeStats {
    /// Map from a moniker of a component running in the system to its stats.
    tree: Mutex<BTreeMap<AbsoluteMoniker, ComponentStats>>,

    /// The root of the tree stats.
    node: inspect::Node,
}

impl ComponentTreeStats {
    pub fn new(node: inspect::Node) -> Self {
        Self { tree: Mutex::new(BTreeMap::new()), node }
    }

    pub fn hooks(self: &Arc<Self>) -> Vec<HooksRegistration> {
        vec![HooksRegistration::new(
            "ComponentTreeStats",
            vec![EventType::Started, EventType::Stopped],
            Arc::downgrade(self) as Weak<dyn Hook>,
        )]
    }

    async fn on_component_started(
        self: &Arc<Self>,
        moniker: AbsoluteMoniker,
        runtime: &RuntimeInfo,
        timestamp: zx::Time,
    ) {
        let mut tree_guard = self.tree.lock().await;
        if tree_guard.contains_key(&moniker) {
            return;
        }

        let mut receiver_guard = runtime.diagnostics_receiver.lock().await;
        match receiver_guard.take() {
            None => {
                tree_guard.insert(moniker, ComponentStats::new(timestamp, None));
            }
            Some(receiver) => {
                let weak_self = Arc::downgrade(&self);
                let moniker_for_fut = moniker.clone();
                let task = fasync::Task::spawn(async move {
                    if let Some(diagnostics) = receiver.await.ok() {
                        if let Some(this) = weak_self.upgrade() {
                            let child = this.node.create_child(moniker_for_fut.to_string());
                            this.tree.lock().await.entry(moniker_for_fut).and_modify(|stats| {
                                stats.init_inspect(child);
                                stats.tasks = diagnostics.tasks;
                            });
                        }
                    }
                });
                tree_guard.insert(moniker, ComponentStats::new(timestamp, Some(task)));
            }
        }
    }
}

#[async_trait]
impl Hook for ComponentTreeStats {
    async fn on(self: Arc<Self>, event: &Event) -> Result<(), ModelError> {
        let target_moniker = event
            .target_moniker
            .unwrap_instance_moniker_or(ModelError::UnexpectedComponentManagerMoniker)?;
        match event.event_type() {
            EventType::Started => {
                if let Some(EventPayload::Started { runtime, .. }) = event.result.as_ref().ok() {
                    self.on_component_started(target_moniker.clone(), runtime, event.timestamp)
                        .await;
                }
            }
            EventType::Stopped => {
                self.tree.lock().await.remove(target_moniker);
            }
            _ => {}
        }
        Ok(())
    }
}

struct ComponentStats {
    start_time: zx::Time,
    #[allow(unused)]
    tasks: Option<fdiagnostics::ComponentTasks>,
    _node: Option<inspect::Node>,
    _diagnostics_receiver_task: Option<fasync::Task<()>>,
}

impl ComponentStats {
    fn new(start_time: zx::Time, diagnostics_receiver_task: Option<fasync::Task<()>>) -> Self {
        Self {
            start_time,
            tasks: None,
            _node: None,
            _diagnostics_receiver_task: diagnostics_receiver_task,
        }
    }

    fn init_inspect(&mut self, node: inspect::Node) {
        node.record_int("start", self.start_time.into_nanos());
        self._node = Some(node);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            actions::{ActionSet, StopAction},
            testing::{
                routing_test_helpers::RoutingTest,
                test_helpers::{component_decl_with_test_runner, ComponentDeclBuilder},
            },
        },
        fuchsia_inspect::testing::{assert_inspect_tree, AnyProperty},
    };

    #[fuchsia::test]
    async fn records_to_inspect_when_receiving_diagnostics() {
        // Set up the test
        let test = RoutingTest::new(
            "root",
            vec![
                ("root", ComponentDeclBuilder::new().add_eager_child("a").build()),
                ("a", component_decl_with_test_runner()),
            ],
        )
        .await;

        // Start the component "a".
        let moniker: AbsoluteMoniker = vec!["a:0"].into();
        let stats = &test.builtin_environment.component_tree_stats;
        test.bind_instance(&moniker).await.expect("bind instance b success");

        // Wait for the diagnostics event to be received
        assert!(stats.tree.lock().await.get(&moniker).is_some());
        assert!(stats.tree.lock().await.get(&moniker).unwrap().tasks.is_some());

        // Verify the data written to inspect.
        assert_inspect_tree!(test.builtin_environment.inspector, root: {
            tree_stats: {
                "/": {
                    start: AnyProperty,
                },
                "/a:0": {
                    start: AnyProperty,
                }
            }
        });

        // Verify that after stopping the instance the data is gone.
        let component = test.model.look_up(&moniker).await.unwrap();
        ActionSet::register(component, StopAction::new()).await.expect("stopped");

        assert!(stats.tree.lock().await.get(&moniker).is_none());
        assert_inspect_tree!(test.builtin_environment.inspector, root: {
            tree_stats: {
                "/": {
                    start: AnyProperty,
                }
            }
        });

        // Verify that after restarting the instance the data is back.
        test.bind_instance(&moniker).await.expect("bind instance b success");
        assert_inspect_tree!(test.builtin_environment.inspector, root: {
            tree_stats: {
                "/": {
                    start: AnyProperty,
                },
                "/a:0": {
                    start: AnyProperty,
                }
            }
        });
    }
}
