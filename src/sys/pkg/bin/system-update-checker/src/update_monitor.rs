// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    event_queue::{ControlHandle, EventQueue, Notify},
    fidl_fuchsia_update_ext::State,
    fuchsia_inspect_contrib::inspectable::InspectableDebugString,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    futures::prelude::*,
};

pub trait StateNotifier: Notify<State> + Send + Sync + 'static {}

impl<T> StateNotifier for T where T: Notify<State> + Send + Sync + 'static {}

#[derive(Debug)]
pub struct UpdateMonitor<N>
where
    N: StateNotifier,
{
    temporary_queue: ControlHandle<N, State>,
    update_state: InspectableDebugString<Option<State>>,
    version_available: InspectableDebugString<Option<String>>,
    inspect_node: fuchsia_inspect::Node,
}

impl<N> UpdateMonitor<N>
where
    N: StateNotifier,
{
    pub fn from_inspect_node(node: fuchsia_inspect::Node) -> (impl Future<Output = ()>, Self) {
        let (temporary_fut, temporary_queue) = EventQueue::new();
        (
            temporary_fut,
            UpdateMonitor {
                temporary_queue,
                update_state: InspectableDebugString::new(None, &node, "update-state"),
                version_available: InspectableDebugString::new(None, &node, "version-available"),
                inspect_node: node,
            },
        )
    }

    #[cfg(test)]
    pub fn new() -> (impl Future<Output = ()>, Self) {
        Self::from_inspect_node(
            fuchsia_inspect::Inspector::new().root().create_child("test-update-monitor-root-node"),
        )
    }

    pub async fn add_temporary_callback(&mut self, callback: N) {
        if let Err(e) = self.temporary_queue.add_client(callback).await {
            fx_log_err!("error adding client to temporary queue: {:?}", e)
        }
    }

    pub async fn advance_update_state(&mut self, next_update_state: Option<State>) {
        *self.update_state.get_mut() = next_update_state;
        if *self.update_state == None {
            *self.version_available.get_mut() = None;
        }
        self.send_on_state().await;
        if *self.update_state == None {
            if let Err(e) = self.temporary_queue.clear().await {
                fx_log_warn!("error clearing clients of temporary queue: {:?}", e)
            }
        }
    }

    pub fn set_version_available(&mut self, version_available: String) {
        *self.version_available.get_mut() = Some(version_available);
    }

    #[cfg(test)]
    pub fn get_version_available(&self) -> Option<String> {
        self.version_available.clone()
    }

    pub fn update_state(&self) -> Option<State> {
        self.update_state.clone()
    }

    pub async fn send_on_state(&mut self) {
        if let Some(state) = self.update_state() {
            if let Err(e) = self.temporary_queue.queue_event(state).await {
                fx_log_warn!("error sending state to temporary queue: {:?}", e)
            }
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use event_queue::{ClosedClient, Notify};
    use fidl_fuchsia_update_ext::{
        random_installation_deferred_data, random_installation_error_data, random_installing_data,
        random_version_available,
    };
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::future::BoxFuture;
    use parking_lot::Mutex;
    use proptest::prelude::*;
    use std::sync::Arc;

    const VERSION_AVAILABLE: &str = "fake-version-available";

    #[derive(Clone, Debug)]
    struct FakeStateNotifier {
        states: Arc<Mutex<Vec<State>>>,
    }
    impl FakeStateNotifier {
        fn new() -> Self {
            Self { states: Arc::new(Mutex::new(vec![])) }
        }
    }
    impl Notify<State> for FakeStateNotifier {
        fn notify(&self, state: State) -> BoxFuture<'static, Result<(), ClosedClient>> {
            self.states.lock().push(state);
            future::ready(Ok(())).boxed()
        }
    }

    prop_compose! {
        fn random_update_state()
        (
            installation_deferred_data in random_installation_deferred_data(),
            installing_data in random_installing_data(),
            installation_error_data in random_installation_error_data()
        )
        (
            state in prop_oneof![
                Just(Some(State::CheckingForUpdates)),
                Just(Some(State::ErrorCheckingForUpdate)),
                Just(Some(State::NoUpdateAvailable)),
                Just(Some(State::InstallationDeferredByPolicy(installation_deferred_data))),
                Just(Some(State::InstallingUpdate(installing_data.clone()))),
                Just(Some(State::WaitingForReboot(installing_data))),
                Just(Some(State::InstallationError(installation_error_data))),
                Just(None),
            ]) -> Option<State>
        {
            state
        }
    }

    async fn random_update_monitor(
        update_state: Option<State>,
        version_available: Option<String>,
    ) -> UpdateMonitor<FakeStateNotifier> {
        let (fut, mut mms) = UpdateMonitor::<FakeStateNotifier>::new();
        fasync::spawn(fut);
        version_available.map(|s| mms.set_version_available(s));
        mms.advance_update_state(update_state).await;
        mms
    }

    async fn wait_for_states(callback: &FakeStateNotifier, len: usize) {
        while callback.states.lock().len() != len {
            fasync::Timer::new(fasync::Time::after(zx::Duration::from_millis(10))).await;
        }
    }

    proptest! {
        #[test]
        fn test_adding_callback_sends_current_state(
                update_state in random_update_state(),
                version_available in random_version_available(),
        ) {
            fasync::Executor::new().unwrap().run_singlethreaded(async {
                let mut update_monitor = random_update_monitor(update_state.clone(), version_available).await;
                let expected_states: Vec<_> = update_state.into_iter().collect();
                let temporary_callback = FakeStateNotifier::new();

                update_monitor.add_temporary_callback(temporary_callback.clone()).await;

                wait_for_states(&temporary_callback, expected_states.len()).await;
                prop_assert_eq!(
                    &temporary_callback.states.lock().clone(),
                    &expected_states
                );
                Ok(())
            }).unwrap();
        }

        #[test]
        fn test_advance_update_state_calls_callbacks(
                initial_state in random_update_state(),
                version_available in random_version_available(),
                next_state in random_update_state(),
        ) {
            fasync::Executor::new().unwrap().run_singlethreaded(async {
                let mut update_monitor = random_update_monitor(initial_state.clone(), version_available).await;
                let temporary_callback = FakeStateNotifier::new();
                let expected_states: Vec<_> = vec![initial_state, next_state.clone()].into_iter().filter_map(|o|o).collect();

                update_monitor.add_temporary_callback(temporary_callback.clone()).await;

                update_monitor.advance_update_state(next_state).await;

                wait_for_states(&temporary_callback, expected_states.len()).await;
                prop_assert_eq!(
                    &temporary_callback.states.lock().clone(),
                    &expected_states
                );
                Ok(())
            }).unwrap();
        }

        #[test]
        fn test_advance_updater_state_to_none_clears_version_available(
            update_state in random_update_state(),
            version_available in random_version_available(),
        ) {
            fasync::Executor::new().unwrap().run_singlethreaded(async {
                let mut update_monitor = random_update_monitor(update_state, version_available).await;
                update_monitor.set_version_available(VERSION_AVAILABLE.to_string());

                update_monitor.advance_update_state(None).await;

                prop_assert_eq!(
                    update_monitor.get_version_available(),
                    None
                );
                Ok(())
            }).unwrap();
        }

        #[test]
        fn test_advance_update_state_to_idle_clears_temporary_callbacks(
            update_state in random_update_state(),
            version_available in random_version_available(),
        ) {
            fasync::Executor::new().unwrap().run_singlethreaded(async {
                let mut update_monitor = random_update_monitor(update_state, version_available).await;
                let temporary_callback = FakeStateNotifier::new();

                update_monitor.add_temporary_callback(temporary_callback.clone()).await;
                update_monitor.advance_update_state(None).await;
                temporary_callback.states.lock().clear();
                update_monitor.advance_update_state(Some(State::CheckingForUpdates)).await;

                prop_assert_eq!(temporary_callback.states.lock().clone(), vec![]);
                Ok(())
            }).unwrap();
        }
    }
}

#[cfg(test)]
mod test_inspect {
    use super::*;
    use event_queue::{ClosedClient, Notify};
    use fuchsia_async as fasync;
    use fuchsia_inspect::assert_inspect_tree;
    use futures::future::BoxFuture;

    #[derive(Clone, Debug)]
    struct FakeStateNotifier;
    impl Notify<State> for FakeStateNotifier {
        fn notify(&self, _state: State) -> BoxFuture<'static, Result<(), ClosedClient>> {
            future::ready(Ok(())).boxed()
        }
    }

    #[test]
    fn test_inspect_initial_state() {
        let inspector = fuchsia_inspect::Inspector::new();
        let _update_monitor = UpdateMonitor::<FakeStateNotifier>::from_inspect_node(
            inspector.root().create_child("update-monitor"),
        );

        assert_inspect_tree!(
            inspector,
            root: {
                "update-monitor": {
                    "update-state": "None",
                    "version-available": "None",
                }
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_update_state() {
        let inspector = fuchsia_inspect::Inspector::new();
        let (fut, mut update_monitor) = UpdateMonitor::<FakeStateNotifier>::from_inspect_node(
            inspector.root().create_child("update-monitor"),
        );
        fasync::spawn(fut);

        update_monitor.advance_update_state(Some(State::CheckingForUpdates)).await;

        assert_inspect_tree!(
            inspector,
            root: {
                "update-monitor": {
                    "update-state": format!("{:?}", Some(State::CheckingForUpdates)),
                    "version-available": "None",
                }
            }
        );
    }

    #[test]
    fn test_inspect_version_available() {
        let inspector = fuchsia_inspect::Inspector::new();
        let (_fut, mut update_monitor) = UpdateMonitor::<FakeStateNotifier>::from_inspect_node(
            inspector.root().create_child("update-monitor"),
        );
        let version_available = "fake-version-available";

        update_monitor.set_version_available(version_available.to_string());

        assert_inspect_tree!(
            inspector,
            root: {
                "update-monitor": {
                    "update-state": "None",
                    "version-available": format!("{:?}", Some(version_available)),
                }
            }
        );
    }
}
