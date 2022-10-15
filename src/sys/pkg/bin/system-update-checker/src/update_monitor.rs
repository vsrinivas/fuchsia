// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    event_queue::{ControlHandle, EventQueue, Notify},
    fidl_fuchsia_update_ext::{AttemptOptions, State},
    fuchsia_inspect_contrib::inspectable::InspectableDebugString,
    futures::prelude::*,
    std::time::Duration,
    tracing::{error, warn},
};

pub trait StateNotifier: Notify<Event = State> + Send + Sync + 'static {}

impl<T> StateNotifier for T where T: Notify<Event = State> + Send + Sync + 'static {}

pub trait AttemptNotifier: Notify<Event = AttemptOptions> + Send + Sync + 'static {}

impl<T> AttemptNotifier for T where T: Notify<Event = AttemptOptions> + Send + Sync + 'static {}

#[derive(Debug)]
pub struct UpdateMonitor<N, A>
where
    N: StateNotifier,
    A: AttemptNotifier,
{
    temporary_queue: ControlHandle<N>,
    attempt_queue: ControlHandle<A>,
    update_state: InspectableDebugString<Option<State>>,
    version_available: InspectableDebugString<Option<String>>,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    inspect_node: fuchsia_inspect::Node,
}

impl<N, A> UpdateMonitor<N, A>
where
    N: StateNotifier,
    A: AttemptNotifier,
{
    pub fn from_inspect_node(
        node: fuchsia_inspect::Node,
    ) -> (impl Future<Output = ()>, impl Future<Output = ()>, Self) {
        let (temporary_fut, temporary_queue) = EventQueue::new();
        let (attempt_fut, attempt_queue) = EventQueue::new();
        (
            temporary_fut,
            attempt_fut,
            UpdateMonitor {
                temporary_queue,
                attempt_queue,
                update_state: InspectableDebugString::new(None, &node, "update-state"),
                version_available: InspectableDebugString::new(None, &node, "version-available"),
                inspect_node: node,
            },
        )
    }

    #[cfg(test)]
    pub fn new() -> (impl Future<Output = ()>, impl Future<Output = ()>, Self) {
        Self::from_inspect_node(
            fuchsia_inspect::Inspector::new().root().create_child("test-update-monitor-root-node"),
        )
    }

    pub async fn add_all_the_callbacks(
        &mut self,
        callback: Box<dyn FnOnce(ControlHandle<N>) -> A + Send>,
    ) {
        let attempt_notifier = callback(self.temporary_queue.clone());
        if let Err(e) = self.attempt_queue.add_client(attempt_notifier).await {
            error!("error adding client to global queue: {:#}", anyhow!(e))
        }
    }

    pub async fn tell_global_monitors_about_the_update(&mut self, attempt_options: AttemptOptions) {
        if let Err(e) = self.attempt_queue.queue_event(attempt_options).await {
            warn!("error sending update to attempt queue: {:#}", anyhow!(e))
        }
    }

    pub async fn add_temporary_callback(&mut self, callback: N) {
        if let Err(e) = self.temporary_queue.add_client(callback).await {
            error!("error adding client to temporary queue: {:#}", anyhow!(e))
        }
    }

    pub async fn advance_update_state(&mut self, next_update_state: State) {
        *self.update_state.get_mut() = Some(next_update_state.clone());
        if let Err(e) = self.temporary_queue.queue_event(next_update_state).await {
            warn!("error sending state to temporary queue: {:#}", anyhow!(e))
        }
    }

    pub async fn clear(&mut self) {
        *self.version_available.get_mut() = None;
        if let Err(e) = self.temporary_queue.clear().await {
            warn!("error clearing clients of temporary queue: {:#}", anyhow!(e))
        }
    }

    pub async fn try_flush(&mut self) {
        match self.temporary_queue.try_flush(Duration::from_secs(5)).await {
            Ok(flush_future) => {
                if let Err(e) = flush_future.await {
                    warn!("Timed out flushing temporary queue: {:#}", anyhow!(e));
                }
            }
            Err(e) => {
                warn!("error trying to flush temporary queue: {:#}", anyhow!(e));
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
}

#[cfg(test)]
mod test {
    use super::*;
    use event_queue::{ClosedClient, Event, Notify};
    use fidl_fuchsia_update_ext::random_version_available;
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use futures::{channel::mpsc, future::BoxFuture, pin_mut, task::Poll};
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
    impl Notify for FakeStateNotifier {
        type Event = State;
        type NotifyFuture = future::Ready<Result<(), ClosedClient>>;
        fn notify(&self, state: State) -> Self::NotifyFuture {
            self.states.lock().push(state);
            future::ready(Ok(()))
        }
    }

    #[derive(Clone, Debug)]
    struct FakeAttemptNotifier;

    impl Notify for FakeAttemptNotifier {
        type Event = AttemptOptions;
        type NotifyFuture = future::Ready<Result<(), ClosedClient>>;
        fn notify(&self, _options: AttemptOptions) -> Self::NotifyFuture {
            future::ready(Ok(()))
        }
    }

    struct MpscNotifier<T> {
        sender: mpsc::Sender<T>,
    }

    impl<T> Notify for MpscNotifier<T>
    where
        T: Event + Send + 'static,
    {
        type Event = T;
        type NotifyFuture = BoxFuture<'static, Result<(), ClosedClient>>;

        fn notify(&self, event: T) -> BoxFuture<'static, Result<(), ClosedClient>> {
            let mut sender = self.sender.clone();
            async move { sender.send(event).map(|result| result.map_err(|_| ClosedClient)).await }
                .boxed()
        }
    }

    async fn random_update_monitor<N: StateNotifier, A: AttemptNotifier>(
        update_state: Option<State>,
        version_available: Option<String>,
    ) -> UpdateMonitor<N, A> {
        let (fut, attempt_fut, mut mms) = UpdateMonitor::<N, A>::new();
        fasync::Task::spawn(fut).detach();
        fasync::Task::spawn(attempt_fut).detach();
        version_available.map(|s| mms.set_version_available(s));
        if let Some(update_state) = update_state {
            mms.advance_update_state(update_state).await;
        }
        mms
    }

    async fn wait_for_states(callback: &FakeStateNotifier, len: usize) {
        while callback.states.lock().len() != len {
            fasync::Timer::new(Duration::from_millis(10)).await;
        }
    }

    proptest! {
        #[test]
        fn test_adding_temporary_callback_sends_current_state(
                update_state: Option<State>,
                version_available in random_version_available(),
        ) {
            fasync::TestExecutor::new().unwrap().run_singlethreaded(async {
                let mut update_monitor = random_update_monitor::<FakeStateNotifier, FakeAttemptNotifier>(update_state.clone(), version_available).await;
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
                initial_state: Option<State>,
                version_available in random_version_available(),
                next_states in prop::collection::vec(any::<State>(), 0..4),
        ) {
            fasync::TestExecutor::new().unwrap().run_singlethreaded(async {
                let mut update_monitor = random_update_monitor::<FakeStateNotifier, FakeAttemptNotifier>(initial_state.clone(), version_available).await;
                let temporary_callback = FakeStateNotifier::new();
                let expected_states: Vec<_> = initial_state.clone().into_iter().chain(next_states.clone().into_iter()).collect();

                update_monitor.add_temporary_callback(temporary_callback.clone()).await;

                for state in next_states {
                    update_monitor.advance_update_state(state).await;
                }

                wait_for_states(&temporary_callback, expected_states.len()).await;
                prop_assert_eq!(
                    &temporary_callback.states.lock().clone(),
                    &expected_states
                );
                Ok(())
            }).unwrap();
        }

        #[test]
        fn test_clear_clears_version_available(
            update_state: Option<State>,
            version_available in random_version_available(),
        ) {
            fasync::TestExecutor::new().unwrap().run_singlethreaded(async {
                let mut update_monitor = random_update_monitor::<FakeStateNotifier, FakeAttemptNotifier>(update_state, version_available).await;
                update_monitor.set_version_available(VERSION_AVAILABLE.to_string());

                update_monitor.clear().await;

                prop_assert_eq!(
                    update_monitor.get_version_available(),
                    None
                );
                Ok(())
            }).unwrap();
        }

        #[test]
        fn test_clear_clears_temporary_callbacks(
            update_state: Option<State>,
            version_available in random_version_available(),
        ) {
            fasync::TestExecutor::new().unwrap().run_singlethreaded(async {
                let mut update_monitor = random_update_monitor::<FakeStateNotifier, FakeAttemptNotifier>(update_state, version_available).await;
                let temporary_callback = FakeStateNotifier::new();

                update_monitor.add_temporary_callback(temporary_callback.clone()).await;
                update_monitor.clear().await;
                temporary_callback.states.lock().clear();
                update_monitor.advance_update_state(State::CheckingForUpdates).await;

                prop_assert_eq!(temporary_callback.states.lock().clone(), vec![]);
                Ok(())
            }).unwrap();
        }

        #[test]
        fn test_try_flush_flushes_temporary_callbacks(
            update_state: State,
            version_available in random_version_available(),
        ) {
            let mut executor = fasync::TestExecutor::new().unwrap();
            let mut update_monitor = executor.run_singlethreaded(random_update_monitor::<_, FakeAttemptNotifier>(Some(update_state.clone()), version_available));

            let (sender, mut receiver) = mpsc::channel(0);
            let temporary_callback = MpscNotifier { sender };
            executor.run_singlethreaded(update_monitor.add_temporary_callback(temporary_callback));

            let flush = update_monitor.try_flush();
            pin_mut!(flush);
            prop_assert_eq!(executor.run_until_stalled(&mut flush), Poll::Pending);
            prop_assert_eq!(executor.run_until_stalled(&mut receiver.next()), Poll::Ready(Some(update_state)));
            prop_assert_eq!(executor.run_until_stalled(&mut flush), Poll::Ready(()));
        }

        #[test]
        fn test_try_flush_timeout(
            update_state: State,
            version_available in random_version_available(),
        ) {
            let mut executor = fasync::TestExecutor::new_with_fake_time().unwrap();
            // Can't use run_singlethreaded on executor with fake time.
            let update_monitor_fut = random_update_monitor::<_, FakeAttemptNotifier>(Some(update_state), version_available);
            pin_mut!(update_monitor_fut);
            let mut update_monitor = match executor.run_until_stalled(&mut update_monitor_fut) {
                Poll::Ready(monitor) => monitor,
                Poll::Pending => panic!("random_update_monitor blocked"),
            };

            let (sender, _receiver) = mpsc::channel(0);
            {
                let temporary_callback = MpscNotifier { sender };
                let add_temporary_callback = update_monitor.add_temporary_callback(temporary_callback);
                pin_mut!(add_temporary_callback);
                prop_assert_eq!(executor.run_until_stalled(&mut add_temporary_callback), Poll::Ready(()));
            }

            let flush = update_monitor.try_flush();
            pin_mut!(flush);
            prop_assert_eq!(executor.run_until_stalled(&mut flush), Poll::Pending);
            let expected_deadline = executor.now() + zx::Duration::from_seconds(5);
            prop_assert_eq!(executor.wake_next_timer(), Some(expected_deadline));
            prop_assert_eq!(executor.run_until_stalled(&mut flush), Poll::Ready(()));
        }
    }
}

#[cfg(test)]
mod test_inspect {
    use super::*;
    use event_queue::{ClosedClient, Notify};
    use fuchsia_async as fasync;
    use fuchsia_inspect::assert_data_tree;

    #[derive(Clone, Debug)]
    struct FakeStateNotifier;
    impl Notify for FakeStateNotifier {
        type Event = State;
        type NotifyFuture = future::Ready<Result<(), ClosedClient>>;
        fn notify(&self, _state: State) -> Self::NotifyFuture {
            future::ready(Ok(()))
        }
    }

    #[derive(Clone, Debug)]
    struct FakeAttemptNotifier;
    impl Notify for FakeAttemptNotifier {
        type Event = AttemptOptions;
        type NotifyFuture = future::Ready<Result<(), ClosedClient>>;
        fn notify(&self, _options: AttemptOptions) -> Self::NotifyFuture {
            future::ready(Ok(()))
        }
    }

    #[test]
    fn test_inspect_initial_state() {
        let inspector = fuchsia_inspect::Inspector::new();
        let _update_monitor =
            UpdateMonitor::<FakeStateNotifier, FakeAttemptNotifier>::from_inspect_node(
                inspector.root().create_child("update-monitor"),
            );

        assert_data_tree!(
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
        let (fut, att_fut, mut update_monitor) =
            UpdateMonitor::<FakeStateNotifier, FakeAttemptNotifier>::from_inspect_node(
                inspector.root().create_child("update-monitor"),
            );
        fasync::Task::spawn(fut).detach();
        fasync::Task::spawn(att_fut).detach();

        update_monitor.advance_update_state(State::CheckingForUpdates).await;

        assert_data_tree!(
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
        let (_fut, _att_fut, mut update_monitor) =
            UpdateMonitor::<FakeStateNotifier, FakeAttemptNotifier>::from_inspect_node(
                inspector.root().create_child("update-monitor"),
            );
        let version_available = "fake-version-available";

        update_monitor.set_version_available(version_available.to_string());

        assert_data_tree!(
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
