// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl_fuchsia_update::ManagerState,
    fuchsia_inspect_contrib::inspectable::{InspectableDebugString, InspectableVectorSize},
};

pub trait StateChangeCallback: Clone + Send + Sync + 'static {
    fn on_state_change(&self, new_state: State) -> Result<(), Error>;
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct State {
    pub manager_state: ManagerState,
    pub version_available: Option<String>,
}

impl Default for State {
    fn default() -> Self {
        Self { manager_state: ManagerState::Idle, version_available: None }
    }
}

#[derive(Debug)]
pub struct UpdateMonitor<S>
where
    S: StateChangeCallback,
{
    permanent_callbacks: InspectableVectorSize<S>,
    temporary_callbacks: InspectableVectorSize<S>,
    manager_state: InspectableDebugString<ManagerState>,
    version_available: InspectableDebugString<Option<String>>,
    inspect_node: fuchsia_inspect::Node,
}

impl<S> UpdateMonitor<S>
where
    S: StateChangeCallback,
{
    pub fn from_inspect_node(node: fuchsia_inspect::Node) -> Self {
        UpdateMonitor {
            permanent_callbacks: InspectableVectorSize::new(
                vec![],
                &node,
                "permanent-callbacks-count",
            ),
            temporary_callbacks: InspectableVectorSize::new(
                vec![],
                &node,
                "temporary-callbacks-count",
            ),
            manager_state: InspectableDebugString::new(ManagerState::Idle, &node, "manager-state"),
            version_available: InspectableDebugString::new(None, &node, "version-available"),
            inspect_node: node,
        }
    }

    #[cfg(test)]
    pub fn new() -> Self {
        Self::from_inspect_node(
            fuchsia_inspect::Inspector::new().root().create_child("test-update-monitor-root-node"),
        )
    }

    pub fn add_temporary_callback(&mut self, callback: S) {
        if callback.on_state_change(self.state()).is_ok() {
            self.temporary_callbacks.get_mut().push(callback);
        }
    }

    pub fn add_permanent_callback(&mut self, callback: S) {
        if callback.on_state_change(self.state()).is_ok() {
            self.permanent_callbacks.get_mut().push(callback);
        }
    }

    pub fn advance_manager_state(&mut self, next_manager_state: ManagerState) {
        *self.manager_state.get_mut() = next_manager_state;
        if *self.manager_state == ManagerState::Idle {
            *self.version_available.get_mut() = None;
        }
        self.send_on_state();
        if *self.manager_state == ManagerState::Idle {
            self.temporary_callbacks.get_mut().clear();
        }
    }

    pub fn set_version_available(&mut self, version_available: String) {
        *self.version_available.get_mut() = Some(version_available);
    }

    pub fn state(&self) -> State {
        State {
            manager_state: *self.manager_state,
            version_available: self.version_available.clone(),
        }
    }

    pub fn manager_state(&self) -> ManagerState {
        *self.manager_state
    }

    fn send_on_state(&mut self) {
        let state = self.state();
        self.permanent_callbacks.get_mut().retain(|cb| cb.on_state_change(state.clone()).is_ok());
        self.temporary_callbacks.get_mut().retain(|cb| cb.on_state_change(state.clone()).is_ok());
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use parking_lot::Mutex;
    use proptest::prelude::*;
    use std::sync::Arc;

    const VERSION_AVAILABLE: &str = "fake-version-available";

    #[derive(Clone, Debug)]
    struct FakeStateChangeCallback {
        states: Arc<Mutex<Vec<State>>>,
    }
    impl FakeStateChangeCallback {
        fn new() -> Self {
            Self { states: Arc::new(Mutex::new(vec![])) }
        }
    }
    impl StateChangeCallback for FakeStateChangeCallback {
        fn on_state_change(&self, new_state: State) -> Result<(), Error> {
            self.states.lock().push(new_state);
            Ok(())
        }
    }

    prop_compose! {
        fn random_manager_state() (
            manager_state in prop_oneof![
                Just(ManagerState::Idle),
                Just(ManagerState::CheckingForUpdates),
                Just(ManagerState::UpdateAvailable),
                Just(ManagerState::PerformingUpdate),
                Just(ManagerState::WaitingForReboot),
                Just(ManagerState::FinalizingUpdate),
                Just(ManagerState::EncounteredError),
            ]) -> ManagerState
        {
            manager_state
        }
    }

    prop_compose! {
        fn random_version_available() (
            version_available in prop_oneof![
                Just(Some(VERSION_AVAILABLE.to_string())),
                Just(None),]
        ) -> Option<String> {
            version_available
        }
    }

    prop_compose! {
        fn random_update_monitor()(
            manager_state in random_manager_state(),
            version_available in random_version_available(),
        ) -> UpdateMonitor<FakeStateChangeCallback> {
            let mut mms = UpdateMonitor::<FakeStateChangeCallback>::new();
            mms.advance_manager_state(manager_state);
            version_available.map(|s| mms.set_version_available(s));
            mms
        }
    }

    proptest! {
        #[test]
        fn test_adding_callback_sends_current_state(
            mut update_monitor in random_update_monitor()
        ) {
            let expected_states = vec![update_monitor.state()];
            let temporary_callback = FakeStateChangeCallback::new();
            let permanent_callback = FakeStateChangeCallback::new();

            update_monitor.add_temporary_callback(temporary_callback.clone());
            update_monitor.add_permanent_callback(permanent_callback.clone());

            prop_assert_eq!(
                &temporary_callback.states.lock().clone(),
                &expected_states
            );
            prop_assert_eq!(
                &permanent_callback.states.lock().clone(),
                &expected_states
            );
        }

        #[test]
        fn test_advance_manager_state_calls_callbacks(
            mut update_monitor in random_update_monitor(),
            next_state in random_manager_state(),
        ) {
            let expected_initial_state = update_monitor.state();
            let temporary_callback = FakeStateChangeCallback::new();
            let permanent_callback = FakeStateChangeCallback::new();

            update_monitor.add_temporary_callback(temporary_callback.clone());
            update_monitor.add_permanent_callback(permanent_callback.clone());
            update_monitor.advance_manager_state(next_state);

            let expected_final_state = State {
                manager_state: next_state,
                version_available: match next_state {
                    ManagerState::Idle => None,
                    _ => expected_initial_state.version_available.clone()
                }
            };
            let expected_states = vec![expected_initial_state, expected_final_state];
            prop_assert_eq!(
                &temporary_callback.states.lock().clone(),
                &expected_states
            );
            prop_assert_eq!(
                &permanent_callback.states.lock().clone(),
                &expected_states
            );
        }

        #[test]
        fn test_advance_manager_state_to_idle_clears_version_available(
            mut update_monitor in random_update_monitor()
        ) {
            update_monitor.set_version_available(VERSION_AVAILABLE.to_string());

            update_monitor.advance_manager_state(ManagerState::Idle);

            prop_assert_eq!(
                update_monitor.state().version_available,
                None
            );
        }

        #[test]
        fn test_advance_manager_state_to_idle_clears_temporary_callbacks(
            mut update_monitor in random_update_monitor()
        ) {
            let temporary_callback = FakeStateChangeCallback::new();

            update_monitor.add_temporary_callback(temporary_callback.clone());
            update_monitor.advance_manager_state(ManagerState::Idle);
            temporary_callback.states.lock().clear();
            update_monitor.advance_manager_state(ManagerState::CheckingForUpdates);

            prop_assert_eq!(temporary_callback.states.lock().clone(), vec![]);
        }

        #[test]
        fn test_advance_manager_state_to_idle_retains_permanent_callbacks(
            mut update_monitor in random_update_monitor()
        ) {
            let permanent_callback = FakeStateChangeCallback::new();

            update_monitor.add_permanent_callback(permanent_callback.clone());
            update_monitor.advance_manager_state(ManagerState::Idle);
            permanent_callback.states.lock().clear();
            update_monitor.advance_manager_state(ManagerState::CheckingForUpdates);

            prop_assert_eq!(
                permanent_callback.states.lock().clone(),
                vec![
                    State {
                        manager_state: ManagerState::CheckingForUpdates,
                        version_available: None
                    }
                ]
            );
        }
    }
}

#[cfg(test)]
mod test_inspect {
    use super::*;
    use fuchsia_inspect::assert_inspect_tree;

    #[derive(Clone, Debug)]
    struct FakeStateChangeCallback;
    impl StateChangeCallback for FakeStateChangeCallback {
        fn on_state_change(&self, _new_state: State) -> Result<(), Error> {
            Ok(())
        }
    }

    #[test]
    fn test_inspect_initial_state() {
        let inspector = fuchsia_inspect::Inspector::new();
        let _update_monitor = UpdateMonitor::<FakeStateChangeCallback>::from_inspect_node(
            inspector.root().create_child("update-monitor"),
        );

        assert_inspect_tree!(
            inspector,
            root: {
                "update-monitor": {
                    "permanent-callbacks-count": 0u64,
                    "temporary-callbacks-count": 0u64,
                    "manager-state": "Idle",
                    "version-available": "None",
                }
            }
        );
    }

    #[test]
    fn test_inspect_callback_counts() {
        let inspector = fuchsia_inspect::Inspector::new();
        let mut update_monitor =
            UpdateMonitor::from_inspect_node(inspector.root().create_child("update-monitor"));

        update_monitor.add_permanent_callback(FakeStateChangeCallback);
        update_monitor.add_temporary_callback(FakeStateChangeCallback);

        assert_inspect_tree!(
            inspector,
            root: {
                "update-monitor": {
                    "permanent-callbacks-count": 1u64,
                    "temporary-callbacks-count": 1u64,
                    "manager-state": "Idle",
                    "version-available": "None",
                }
            }
        );
    }

    #[test]
    fn test_inspect_manager_state() {
        let inspector = fuchsia_inspect::Inspector::new();
        let mut update_monitor = UpdateMonitor::<FakeStateChangeCallback>::from_inspect_node(
            inspector.root().create_child("update-monitor"),
        );

        update_monitor.advance_manager_state(ManagerState::CheckingForUpdates);

        assert_inspect_tree!(
            inspector,
            root: {
                "update-monitor": {
                    "permanent-callbacks-count": 0u64,
                    "temporary-callbacks-count": 0u64,
                    "manager-state": "CheckingForUpdates",
                    "version-available": "None",
                }
            }
        );
    }

    #[test]
    fn test_inspect_version_available() {
        let inspector = fuchsia_inspect::Inspector::new();
        let mut update_monitor = UpdateMonitor::<FakeStateChangeCallback>::from_inspect_node(
            inspector.root().create_child("update-monitor"),
        );
        let version_available = "fake-version-available";

        update_monitor.set_version_available(version_available.to_string());

        assert_inspect_tree!(
            inspector,
            root: {
                "update-monitor": {
                    "permanent-callbacks-count": 0u64,
                    "temporary-callbacks-count": 0u64,
                    "manager-state": "Idle",
                    "version-available": format!("{:?}", Some(version_available)),
                }
            }
        );
    }
}
