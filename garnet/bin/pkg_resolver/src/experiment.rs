// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl_fuchsia_pkg::{
        ExperimentToggle, PackageResolverAdminRequest, PackageResolverAdminRequestStream,
    },
    fuchsia_inspect as inspect,
    futures::prelude::*,
    inspect::Property,
    parking_lot::RwLock,
    std::collections::{hash_map, HashMap, HashSet},
    std::sync::Arc,
};

pub(crate) struct State {
    state: HashSet<ExperimentToggle>,
    inspect: inspect::Node,
    inspect_states: HashMap<ExperimentToggle, inspect::IntProperty>,
}

impl State {
    pub fn new(inspect: inspect::Node) -> Self {
        Self { state: HashSet::new(), inspect, inspect_states: HashMap::new() }
    }
    pub fn set_state(&mut self, experiment: ExperimentToggle, state: bool) {
        if state {
            self.state.insert(experiment);
        } else {
            self.state.remove(&experiment);
        }
        let inspect_value = if state { 1 } else { 0 };
        match self.inspect_states.entry(experiment) {
            hash_map::Entry::Occupied(entry) => entry.get().set(inspect_value),
            hash_map::Entry::Vacant(entry) => {
                entry.insert(self.inspect.create_int(format!("{:?}", &experiment), inspect_value));
            }
        };
    }

    // allow dead_code here to allow for future experiment use
    #[allow(dead_code)]
    pub fn get_state(&self, experiment: ExperimentToggle) -> bool {
        self.state.contains(&experiment)
    }
}

pub(crate) async fn run_admin_service(
    experiment_state: Arc<RwLock<State>>,
    mut stream: PackageResolverAdminRequestStream,
) -> Result<(), Error> {
    while let Some(event) = await!(stream.try_next())? {
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
    use fuchsia_inspect::assert_inspect_tree;

    #[test]
    fn test_experiments_start_disabled() {
        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("experiments");
        let state = State::new(node);
        assert_eq!(state.get_state(ExperimentToggle::Lightbulb), false);
        assert_inspect_tree!(
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
        state.set_state(ExperimentToggle::Lightbulb, true);
        assert_eq!(state.get_state(ExperimentToggle::Lightbulb), true);
        assert_inspect_tree!(
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
        state.set_state(ExperimentToggle::Lightbulb, true);
        state.set_state(ExperimentToggle::Lightbulb, false);
        assert_eq!(state.get_state(ExperimentToggle::Lightbulb), false);
        assert_inspect_tree!(
            inspector,
            root: {
                experiments: {
                    Lightbulb: 0i64
                }
            }
        );
    }
}
