// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::{
        types::{Node, StringProperty},
        Property,
    },
    std::fmt::Debug,
};

/// Top level inspect node for test_manager.
pub struct RootInspectNode {
    /// Node under which inspect for currently running test runs is stored.
    executing_runs_node: Node,
}

impl RootInspectNode {
    pub fn new(root: &Node) -> Self {
        Self { executing_runs_node: root.create_child("executing") }
    }

    /// Create an inspect node for a new test run.
    pub fn new_run(&self, run_name: &str) -> RunInspectNode {
        RunInspectNode::new(&self.executing_runs_node, run_name)
    }
}

/// Inspect node containing diagnostics for a single test run.
pub struct RunInspectNode {
    _node: Node,
    suites_node: Node,
    execution_state: DebugStringProperty<RunExecutionState>,
    debug_data_state: DebugStringProperty<DebugDataState>,
    controller_state: DebugStringProperty<RunControllerState>,
}

impl RunInspectNode {
    /// Create a new run under |executing_root|.
    fn new(executing_root: &Node, node_name: &str) -> Self {
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
            suites_node: node.create_child("executing_suites"),
            _node: node,
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
}

/// Inspect node containing state for a single test suite.
pub struct SuiteInspectNode {
    _node: Node,
    execution_state: DebugStringProperty<ExecutionState>,
}

impl SuiteInspectNode {
    fn new(name: &str, url: &str, suites_node: &Node) -> Self {
        let node = suites_node.create_child(name);
        node.record_string("url", url);
        Self {
            execution_state: DebugStringProperty::new(
                &node,
                "execution_state",
                ExecutionState::Pending,
            ),
            _node: node,
        }
    }

    pub fn set_execution_state(&self, state: ExecutionState) {
        self.execution_state.set(state);
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
                        executing_suites: {},
                    }
                },
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
                        executing_suites: {},
                    }
                },
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
                        executing_suites: {
                            suite_1: {
                                url: "suite-url",
                                execution_state: "Pending"
                            }
                        },
                    }
                },
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
                        executing_suites: {
                        },
                    }
                },
            }
        );

        drop(run);
        assert_data_tree!(
            inspector,
            root: {
                executing: {},
            }
        );
    }
}
