// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::{
        types::{Node, StringProperty},
        InspectType, Property,
    },
    fuchsia_inspect_contrib::nodes::BoundedListNode,
    std::{
        fmt::Debug,
        sync::{Arc, Mutex, Weak},
    },
};

/// Top level inspect node for test_manager.
pub struct RootInspectNode {
    /// Node under which inspect for currently running test runs is stored.
    executing_runs_node: Node,
    /// Node under which inspect for previously executed test runs is stored.
    /// The caller chooses whether or not to persist runs.
    finished_runs_node: Arc<Mutex<BoundedListNode>>,
}

impl RootInspectNode {
    const MAX_PERSISTED_RUNS: usize = 3;
    pub fn new(root: &Node) -> Self {
        Self {
            executing_runs_node: root.create_child("executing"),
            finished_runs_node: Arc::new(Mutex::new(BoundedListNode::new(
                root.create_child("finished"),
                Self::MAX_PERSISTED_RUNS,
            ))),
        }
    }

    /// Create an inspect node for a new test run.
    pub fn new_run(&self, run_name: &str) -> RunInspectNode {
        RunInspectNode::new(
            &self.executing_runs_node,
            Arc::downgrade(&self.finished_runs_node),
            run_name,
        )
    }
}

/// Inspect node containing diagnostics for a single test run.
pub struct RunInspectNode {
    node: Node,
    finished_runs_node: Weak<Mutex<BoundedListNode>>,
    suites_node: Arc<Node>,
    execution_state: DebugStringProperty<RunExecutionState>,
    debug_data_state: DebugStringProperty<DebugDataState>,
    controller_state: DebugStringProperty<RunControllerState>,
}

impl RunInspectNode {
    /// Create a new run under |executing_root|.
    fn new(
        executing_root: &Node,
        finished_runs_node: Weak<Mutex<BoundedListNode>>,
        node_name: &str,
    ) -> Self {
        let node = executing_root.create_child(node_name);
        Self {
            execution_state: DebugStringProperty::new(
                &node,
                "execution_state",
                RunExecutionState::NotStarted,
            ),
            debug_data_state: DebugStringProperty::new(
                &node,
                "debug_data_state",
                DebugDataState::PendingDebugDataProduced,
            ),
            controller_state: DebugStringProperty::new(
                &node,
                "controller_state",
                RunControllerState::AwaitingRequest,
            ),
            suites_node: Arc::new(node.create_child("suites")),
            node,
            finished_runs_node,
        }
    }

    pub fn set_execution_state(&self, state: RunExecutionState) {
        self.execution_state.set(state);
    }

    pub fn set_debug_data_state(&self, state: DebugDataState) {
        self.debug_data_state.set(state);
    }

    pub fn set_controller_state(&self, state: RunControllerState) {
        self.controller_state.set(state);
    }

    pub fn new_suite(&self, name: &str, url: &str) -> SuiteInspectNode {
        SuiteInspectNode::new(name, url, &self.suites_node)
    }

    pub fn persist(self) {
        let Self {
            node,
            finished_runs_node,
            suites_node,
            execution_state,
            debug_data_state,
            controller_state,
        } = self;

        if let Some(finished_runs) = finished_runs_node.upgrade() {
            let mut node_lock = finished_runs.lock().unwrap();
            let parent = node_lock.create_entry();
            node.record(NodeWrapper(suites_node));
            node.record(execution_state);
            node.record(debug_data_state);
            node.record(controller_state);
            let _ = parent.adopt(&node);
            parent.record(node);
        }
    }
}

/// Inspect node containing state for a single test suite.
pub struct SuiteInspectNode {
    parent_node: Weak<Node>,
    node: Option<Node>,
    execution_state: Option<DebugStringProperty<ExecutionState>>,
}

impl SuiteInspectNode {
    fn new(name: &str, url: &str, parent_node: &Arc<Node>) -> Self {
        let node = parent_node.create_child(name);
        node.record_string("url", url);
        Self {
            execution_state: Some(DebugStringProperty::new(
                &node,
                "execution_state",
                ExecutionState::Pending,
            )),
            node: Some(node),
            parent_node: Arc::downgrade(parent_node),
        }
    }

    pub fn set_execution_state(&self, state: ExecutionState) {
        self.execution_state.as_ref().unwrap().set(state);
    }
}

impl Drop for SuiteInspectNode {
    fn drop(&mut self) {
        if let Some(parent) = self.parent_node.upgrade() {
            let node = self.node.take().unwrap();
            let execution_state = self.execution_state.take().unwrap();
            node.record(execution_state);
            parent.record(node);
        }
    }
}

/// The current execution state of a test suite.
#[derive(Debug)]
pub enum ExecutionState {
    Pending,
    GetFacets,
    Launch,
    RunTests,
    TestsDone,
    TearDown,
    Complete,
}

/// An enumeration of the states run execution may be in.
#[derive(Debug)]
pub enum RunExecutionState {
    /// Suites not started yet.
    NotStarted,
    /// Suites are currently running.
    Executing,
    /// Suites are complete.
    Complete,
}

/// An enumeration of the states debug data reporting may be in.
#[derive(Debug)]
pub enum DebugDataState {
    /// Waiting for debug_data to signal if debug data is available.
    PendingDebugDataProduced,
    /// Debug data has been produced.
    DebugDataProduced,
    /// No debug data has been produced.
    NoDebugData,
}

#[derive(Debug)]
pub enum RunControllerState {
    AwaitingEvents,
    AwaitingRequest,
    Done { stopped_or_killed: bool, events_drained: bool, events_sent_successfully: bool },
}

struct DebugStringProperty<T: Debug> {
    inner: StringProperty,
    _type: std::marker::PhantomData<T>,
}

impl<T: Debug> DebugStringProperty<T> {
    fn new(parent: &Node, name: &str, initial: T) -> Self {
        Self {
            inner: parent.create_string(name, &format!("{:#?}", initial)),
            _type: std::marker::PhantomData,
        }
    }

    fn set(&self, new: T) {
        self.inner.set(&format!("{:#?}", new));
    }
}

impl<T: Debug + Send + Sync> InspectType for DebugStringProperty<T> {}
struct NodeWrapper(Arc<Node>);
impl InspectType for NodeWrapper {}

#[cfg(test)]
mod test {
    use super::*;
    use fuchsia_inspect::{testing::assert_data_tree, Inspector};

    #[fuchsia::test]
    fn empty_root() {
        let inspector = Inspector::new();
        let _root_node = RootInspectNode::new(inspector.root());

        assert_data_tree!(
            inspector,
            root: {
                executing: {},
                finished: {}
            }
        );
    }

    #[fuchsia::test]
    fn single_run() {
        let inspector = Inspector::new();
        let root_node = RootInspectNode::new(inspector.root());
        let run = root_node.new_run("run_1");
        assert_data_tree!(
            inspector,
            root: {
                executing: {
                    run_1: {
                        controller_state: "AwaitingRequest",
                        execution_state: "NotStarted",
                        debug_data_state: "PendingDebugDataProduced",
                        suites: {}
                    }
                },
                finished: {}
            }
        );

        run.set_execution_state(RunExecutionState::Executing);
        run.set_debug_data_state(DebugDataState::NoDebugData);
        assert_data_tree!(
            inspector,
            root: {
                executing: {
                    run_1: {
                        controller_state: "AwaitingRequest",
                        execution_state: "Executing",
                        debug_data_state: "NoDebugData",
                        suites: {}
                    }
                },
                finished: {}
            }
        );

        let suite = run.new_suite("suite_1", "suite-url");
        assert_data_tree!(
            inspector,
            root: {
                executing: {
                    run_1: {
                        controller_state: "AwaitingRequest",
                        execution_state: "Executing",
                        debug_data_state: "NoDebugData",
                        suites: {
                            suite_1: {
                                url: "suite-url",
                                execution_state: "Pending"
                            }
                        }
                    }
                },
                finished: {}
            }
        );

        drop(suite);
        assert_data_tree!(
            inspector,
            root: {
                executing: {
                    run_1: {
                        controller_state: "AwaitingRequest",
                        execution_state: "Executing",
                        debug_data_state: "NoDebugData",
                        suites: {
                            suite_1: {
                                url: "suite-url",
                                execution_state: "Pending"
                            }
                        },
                    }
                },
                finished: {}
            }
        );

        drop(run);
        assert_data_tree!(
            inspector,
            root: {
                executing: {},
                finished: {}
            }
        );
    }

    #[fuchsia::test]
    fn persisted_run() {
        let inspector = Inspector::new();
        let root_node = RootInspectNode::new(inspector.root());
        let run = root_node.new_run("run_1");
        assert_data_tree!(
            inspector,
            root: {
                executing: {
                    run_1: {
                        controller_state: "AwaitingRequest",
                        execution_state: "NotStarted",
                        debug_data_state: "PendingDebugDataProduced",
                        suites: {}
                    }
                },
                finished: {}
            }
        );

        run.set_execution_state(RunExecutionState::Executing);
        run.set_debug_data_state(DebugDataState::NoDebugData);
        assert_data_tree!(
            inspector,
            root: {
                executing: {
                    run_1: {
                        controller_state: "AwaitingRequest",
                        execution_state: "Executing",
                        debug_data_state: "NoDebugData",
                        suites: {}
                    }
                },
                finished: {}
            }
        );

        let suite = run.new_suite("suite_1", "suite-url");
        assert_data_tree!(
            inspector,
            root: {
                executing: {
                    run_1: {
                        controller_state: "AwaitingRequest",
                        execution_state: "Executing",
                        debug_data_state: "NoDebugData",
                        suites: {
                            suite_1: {
                                url: "suite-url",
                                execution_state: "Pending"
                            }
                        }
                    }
                },
                finished: {}
            }
        );
        drop(suite);

        run.persist();
        assert_data_tree!(
            inspector,
            root: {
                executing: {},
                finished: {
                    "0": {
                        run_1: {
                            controller_state: "AwaitingRequest",
                            execution_state: "Executing",
                            debug_data_state: "NoDebugData",
                            suites: {
                                suite_1: {
                                    url: "suite-url",
                                    execution_state: "Pending"
                                }
                            }
                        }
                    }
                },
            }
        );
    }

    #[fuchsia::test]
    fn persisted_run_buffer_overflow() {
        let inspector = Inspector::new();
        let root_node = RootInspectNode::new(inspector.root());

        for i in 0..RootInspectNode::MAX_PERSISTED_RUNS {
            root_node.new_run(&format!("run_{:?}", i)).persist();
        }

        root_node.new_run("run_overflow").persist();

        // hardcoded assumption here that MAX_PERSISTED_RUNS == 3
        assert_data_tree!(
            inspector,
            root: {
                executing: {},
                finished: {
                    "1": {
                        run_1: contains {}
                    },
                    "2": {
                        run_2: contains {}
                    },
                    "3": { run_overflow: contains {}}
                },
            }
        );
    }
}
