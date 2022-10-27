// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub use fidl_fuchsia_pkg::ExperimentToggle as Experiment;
use {
    anyhow::Error,
    fidl_fuchsia_pkg::{PackageResolverAdminRequest, PackageResolverAdminRequestStream},
    fuchsia_inspect as inspect,
    futures::prelude::*,
    inspect::Property,
    parking_lot::RwLock,
    std::collections::{hash_map, HashMap, HashSet},
    std::sync::Arc,
};

// A read-only view of current experiment states.
#[derive(Clone, Debug)]
pub struct Experiments(Arc<RwLock<State>>);

impl Experiments {
    #[allow(unused)]
    pub fn get(&self, experiment: Experiment) -> bool {
        self.0.read().get_state(experiment)
    }

    #[cfg(test)]
    pub fn none() -> Self {
        Self(Arc::new(RwLock::new(State::new_test())))
    }
}

impl From<Arc<RwLock<State>>> for Experiments {
    fn from(x: Arc<RwLock<State>>) -> Self {
        Self(x)
    }
}

#[derive(Debug)]
pub struct State {
    state: HashSet<Experiment>,
    inspect: inspect::Node,
    inspect_states: HashMap<Experiment, inspect::IntProperty>,
}

impl State {
    pub fn new(inspect: inspect::Node) -> Self {
        Self { state: HashSet::new(), inspect, inspect_states: HashMap::new() }
    }

    #[cfg(test)]
    pub fn new_test() -> Self {
        let null_node = inspect::Inspector::new().root().create_child("test");
        Self::new(null_node)
    }

    pub fn set_state(&mut self, experiment: Experiment, state: bool) {
        if state {
            self.state.insert(experiment);
        } else {
            self.state.remove(&experiment);
        }
        let inspect_value = i64::from(state);
        match self.inspect_states.entry(experiment) {
            hash_map::Entry::Occupied(entry) => entry.get().set(inspect_value),
            hash_map::Entry::Vacant(entry) => {
                entry.insert(self.inspect.create_int(format!("{:?}", &experiment), inspect_value));
            }
        };
    }

    pub fn get_state(&self, experiment: Experiment) -> bool {
        self.state.contains(&experiment)
    }
}

#[allow(unused)]
// Disabling the service because there are no active experiments
// and we don't want nor need to continuously audit it for security.
pub(crate) async fn run_admin_service(
    experiment_state: Arc<RwLock<State>>,
    mut stream: PackageResolverAdminRequestStream,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        let PackageResolverAdminRequest::SetExperimentState { experiment_id, state, responder } =
            event;
        experiment_state.write().set_state(experiment_id, state);
        responder.send()?;
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_inspect::assert_data_tree;

    #[test]
    fn test_experiments_start_disabled() {
        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("experiments");
        let state = State::new(node);
        assert!(!state.get_state(Experiment::Lightbulb));
        assert_data_tree!(
            inspector,
            root: {
                experiments: {}
            }
        );
    }

    #[test]
    fn test_experiments_can_be_enabled() {
        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("experiments");
        let mut state = State::new(node);
        state.set_state(Experiment::Lightbulb, true);
        assert!(state.get_state(Experiment::Lightbulb));
        assert_data_tree!(
            inspector,
            root: {
                experiments: {
                    Lightbulb: 1i64
                }
            }
        );
    }

    #[test]
    fn test_experiments_can_be_disabled() {
        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("experiments");
        let mut state = State::new(node);
        state.set_state(Experiment::Lightbulb, true);
        state.set_state(Experiment::Lightbulb, false);
        assert!(!state.get_state(Experiment::Lightbulb));
        assert_data_tree!(
            inspector,
            root: {
                experiments: {
                    Lightbulb: 0i64
                }
            }
        );
    }
}
