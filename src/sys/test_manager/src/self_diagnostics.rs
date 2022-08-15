// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_inspect::{
        types::{LazyNode, Node},
        Inspector,
    },
    fuchsia_inspect_contrib::nodes::BoundedListNode,
    futures::FutureExt,
    std::{
        fmt::{Debug, Error, Formatter},
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
    node: LazyNode,
    inner: Arc<Mutex<RunInspectNodeInner>>,
    finished_runs_node: Weak<Mutex<BoundedListNode>>,
}

#[derive(Debug)]
struct RunInspectNodeInner {
    execution_state: RunExecutionState,
    debug_data_state: DebugDataState,
    controller_state: RunControllerState,
    suites: Vec<Arc<SuiteInspectNode>>,
    used_parallel_scheduler: bool,
}

impl RunInspectNode {
    /// Create a new run under |executing_root|.
    fn new(
        executing_root: &Node,
        finished_runs_node: Weak<Mutex<BoundedListNode>>,
        node_name: &str,
    ) -> Self {
        let inner = Arc::new(Mutex::new(RunInspectNodeInner {
            execution_state: RunExecutionState::NotStarted,
            debug_data_state: DebugDataState::PendingDebugDataProduced,
            controller_state: RunControllerState::AwaitingRequest,
            suites: vec![],
            used_parallel_scheduler: false,
        }));
        let inner_clone = inner.clone();
        let node = executing_root.create_lazy_child(node_name, move || {
            let inspector = Inspector::new();
            let root = inspector.root();
            let lock = inner_clone.lock().unwrap();
            root.record_string("execution_state", format!("{:#?}", lock.execution_state));
            root.record_string("debug_data_state", format!("{:#?}", lock.debug_data_state));
            root.record_string("controller_state", format!("{:#?}", lock.controller_state));
            root.record_bool("used_parallel_scheduler", lock.used_parallel_scheduler);
            let suites = lock.suites.clone();
            drop(lock);
            let suite_node = root.create_child("suites");
            for suite in suites {
                suite.record(&suite_node);
            }
            root.record(suite_node);
            futures::future::ready(Ok(inspector)).boxed()
        });
        Self { inner, node, finished_runs_node }
    }

    pub fn set_execution_state(&self, state: RunExecutionState) {
        self.inner.lock().unwrap().execution_state = state;
    }

    pub fn set_debug_data_state(&self, state: DebugDataState) {
        self.inner.lock().unwrap().debug_data_state = state;
    }

    pub fn set_controller_state(&self, state: RunControllerState) {
        self.inner.lock().unwrap().controller_state = state;
    }

    pub fn set_used_parallel_scheduler(&self, used_parallel_scheduler: bool) {
        self.inner.lock().unwrap().used_parallel_scheduler = used_parallel_scheduler;
    }

    pub fn new_suite(&self, name: &str, url: &str) -> Arc<SuiteInspectNode> {
        let node = Arc::new(SuiteInspectNode::new(name, url));
        self.inner.lock().unwrap().suites.push(node.clone());
        node
    }

    pub fn persist(self) {
        if let Some(finished_runs) = self.finished_runs_node.upgrade() {
            let mut node_lock = finished_runs.lock().unwrap();
            let parent = node_lock.create_entry();
            let _ = parent.adopt(&self.node);
            parent.record(self.node);
        }
    }
}

impl Debug for RunInspectNode {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), Error> {
        self.inner.lock().unwrap().fmt(f)
    }
}

/// Inspect node containing state for a single test suite.
pub struct SuiteInspectNode {
    name: String,
    url: String,
    execution_state: Mutex<ExecutionState>,
}

impl SuiteInspectNode {
    fn new(name: &str, url: &str) -> Self {
        Self {
            name: name.into(),
            url: url.into(),
            execution_state: Mutex::new(ExecutionState::Pending),
        }
    }

    pub fn set_execution_state(&self, state: ExecutionState) {
        *self.execution_state.lock().unwrap() = state;
    }

    fn record(&self, parent_node: &Node) {
        let node = parent_node.create_child(&self.name);
        node.record_string("url", &self.url);
        node.record_string(
            "execution_state",
            format!("{:#?}", *self.execution_state.lock().unwrap()),
        );
        parent_node.record(node);
    }
}

impl Debug for SuiteInspectNode {
    fn fmt(&self, f: &mut Formatter<'_>) -> Result<(), Error> {
        f.debug_struct(&self.name)
            .field("url", &self.url)
            .field("execution_state", &*self.execution_state.lock().unwrap())
            .finish()
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
                        suites: {},
                        used_parallel_scheduler: false
                    }
                },
                finished: {}
            }
        );

        run.set_execution_state(RunExecutionState::Executing);
        run.set_debug_data_state(DebugDataState::NoDebugData);
        run.set_used_parallel_scheduler(true);
        assert_data_tree!(
            inspector,
            root: {
                executing: {
                    run_1: {
                        controller_state: "AwaitingRequest",
                        execution_state: "Executing",
                        debug_data_state: "NoDebugData",
                        suites: {},
                        used_parallel_scheduler: true
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
                        },
                        used_parallel_scheduler: true
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
                        used_parallel_scheduler: true
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
                        suites: {},
                        used_parallel_scheduler: false
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
                        suites: {},
                        used_parallel_scheduler: false
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
                        },
                        used_parallel_scheduler: false
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
                            },
                            used_parallel_scheduler: false
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
