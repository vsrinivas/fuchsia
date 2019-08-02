// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::apply::{apply_system_update, Initiator};
use crate::channel::TargetChannelManager;
use crate::check::{check_for_system_update, SystemUpdateStatus};
use crate::connect::ServiceConnect;
use crate::update_monitor::{State, StateChangeCallback, UpdateMonitor};
use failure::{Error, ResultExt};
use fidl_fuchsia_update::{CheckStartedResult, ManagerState};
use fuchsia_async as fasync;
use fuchsia_inspect as finspect;
use fuchsia_merkle::Hash;
use fuchsia_syslog::{fx_log_err, fx_log_info};
use futures::future::BoxFuture;
use futures::lock::Mutex as AsyncMutex;
use futures::prelude::*;
use parking_lot::Mutex;
use std::sync::Arc;

/// Manages the lifecycle of an update attempt and notifies interested clients.
//
// # Lock Order
//
// `updater` is locked for the duration of an update attempt, and an update attempt will
// periodically lock `monitor` to send status updates. Before an async task or thread can lock
// `updater`, it must release any locks on `monitor`.
//
pub struct UpdateManager<T, C, A, S>
where
    T: TargetChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
    S: StateChangeCallback,
{
    monitor: Arc<Mutex<UpdateMonitor<S>>>,
    updater: Arc<AsyncMutex<SystemInterface<T, C, A>>>,
}

struct SystemInterface<T, C, A>
where
    T: TargetChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
{
    channel_updater: T,
    update_checker: C,
    update_applier: A,
}

impl<T, S> UpdateManager<T, RealUpdateChecker, RealUpdateApplier, S>
where
    T: TargetChannelUpdater,
    S: StateChangeCallback,
{
    pub fn new(channel_updater: T, node: finspect::Node) -> Self {
        Self {
            monitor: Arc::new(Mutex::new(UpdateMonitor::from_inspect_node(node))),
            updater: Arc::new(AsyncMutex::new(SystemInterface::new(
                channel_updater,
                RealUpdateChecker,
                RealUpdateApplier,
            ))),
        }
    }
}

impl<T, C, A, S> UpdateManager<T, C, A, S>
where
    T: TargetChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
    S: StateChangeCallback,
{
    #[cfg(test)]
    pub fn from_checker_and_applier(
        channel_updater: T,
        update_checker: C,
        update_applier: A,
    ) -> Self {
        Self {
            monitor: Arc::new(Mutex::new(UpdateMonitor::new())),
            updater: Arc::new(AsyncMutex::new(SystemInterface::new(
                channel_updater,
                update_checker,
                update_applier,
            ))),
        }
    }

    /// A Fuchsia Executor must be active when this method is called, b/c it uses fuchsia_async::spawn
    pub fn try_start_update(
        &self,
        initiator: Initiator,
        callback: Option<S>,
    ) -> CheckStartedResult {
        let mut monitor = self.monitor.lock();
        callback.map(|cb| monitor.add_temporary_callback(cb));
        match monitor.manager_state() {
            ManagerState::Idle => {
                monitor.advance_manager_state(ManagerState::CheckingForUpdates);
                let updater = Arc::clone(&self.updater);
                let monitor = Arc::clone(&self.monitor);

                // Spawn so that callers of this method are not blocked
                fasync::spawn(async move {
                    // Lock the updater for the duration of the update attempt. Contention is not
                    // expected except in the case that a previous update attempt failed, has set
                    // manager_state back to Idle, but has not yet returned.
                    let mut updater = updater.lock().await;
                    updater.do_system_update_check_and_return_to_idle(monitor, initiator).await
                });
                CheckStartedResult::Started
            }
            _ => CheckStartedResult::InProgress,
        }
    }

    pub fn get_state(&self) -> State {
        let monitor = self.monitor.lock();
        monitor.state()
    }

    pub fn add_permanent_callback(&self, callback: S) {
        self.monitor.lock().add_permanent_callback(callback);
    }
}

impl<T, C, A> SystemInterface<T, C, A>
where
    T: TargetChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
{
    pub fn new(channel_updater: T, update_checker: C, update_applier: A) -> Self {
        Self { channel_updater, update_checker, update_applier }
    }

    async fn do_system_update_check_and_return_to_idle<S: StateChangeCallback>(
        &mut self,
        monitor: Arc<Mutex<UpdateMonitor<S>>>,
        initiator: Initiator,
    ) {
        if let Err(e) = self.do_system_update_check(monitor.clone(), initiator).await {
            fx_log_err!("update attempt failed: {:?}", e);
            monitor.lock().advance_manager_state(ManagerState::EncounteredError);
        }
        let mut monitor = monitor.lock();
        match monitor.manager_state() {
            ManagerState::WaitingForReboot => fx_log_err!(
                "system-update-checker is in the WaitingForReboot state. \
                 This should not have happened, because the sytem-updater should \
                 have rebooted the device before it returned."
            ),
            _ => {
                monitor.advance_manager_state(ManagerState::Idle);
            }
        }
    }

    async fn do_system_update_check<S: StateChangeCallback>(
        &mut self,
        monitor: Arc<Mutex<UpdateMonitor<S>>>,
        initiator: Initiator,
    ) -> Result<(), Error> {
        fx_log_info!(
            "starting update check (requested by {})",
            match initiator {
                Initiator::Automatic => "service",
                Initiator::Manual => "user",
            }
        );

        self.channel_updater.update().await;

        match self.update_checker.check().await.context("check_for_system_update failed")? {
            SystemUpdateStatus::UpToDate { system_image } => {
                fx_log_info!("current system_image merkle: {}", system_image);
                fx_log_info!("system_image is already up-to-date");
                return Ok(());
            }
            SystemUpdateStatus::UpdateAvailable { current_system_image, latest_system_image } => {
                fx_log_info!("current system_image merkle: {}", current_system_image);
                fx_log_info!("new system_image available: {}", latest_system_image);
                {
                    let mut monitor = monitor.lock();
                    monitor.set_version_available(latest_system_image.to_string());
                    monitor.advance_manager_state(ManagerState::PerformingUpdate);
                }
                self.update_applier
                    .apply(current_system_image, latest_system_image, initiator)
                    .await
                    .context("apply_system_update failed")?;
                // On success, system-updater reboots the system before returning, so this code
                // should never run. The only way to leave WaitingForReboot state is to restart
                // the component
                monitor.lock().advance_manager_state(ManagerState::WaitingForReboot);
            }
        }
        Ok(())
    }
}

// For mocking
pub trait UpdateChecker: Send + Sync + 'static {
    fn check(&self) -> BoxFuture<'_, Result<SystemUpdateStatus, crate::errors::Error>>;
}

pub struct RealUpdateChecker;

impl UpdateChecker for RealUpdateChecker {
    fn check(&self) -> BoxFuture<'_, Result<SystemUpdateStatus, crate::errors::Error>> {
        check_for_system_update().boxed()
    }
}

// For mocking
pub trait TargetChannelUpdater: Send + Sync + 'static {
    fn update(&mut self) -> BoxFuture<'_, ()>;
}

impl<S: ServiceConnect + 'static> TargetChannelUpdater for TargetChannelManager<S> {
    fn update(&mut self) -> BoxFuture<'_, ()> {
        TargetChannelManager::update(self)
            .unwrap_or_else(|e| fx_log_err!("while updating target channel: {:?}", e))
            .boxed()
    }
}

// For mocking
pub trait UpdateApplier: Send + Sync + 'static {
    fn apply(
        &self,
        current_system_image: Hash,
        latest_system_image: Hash,
        initiator: Initiator,
    ) -> BoxFuture<'_, Result<(), crate::errors::Error>>;
}

pub struct RealUpdateApplier;

impl UpdateApplier for RealUpdateApplier {
    fn apply(
        &self,
        current_system_image: Hash,
        latest_system_image: Hash,
        initiator: Initiator,
    ) -> BoxFuture<'_, Result<(), crate::errors::Error>> {
        apply_system_update(current_system_image, latest_system_image, initiator).boxed()
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use futures::channel::mpsc::{channel, Receiver, Sender};
    use futures::channel::oneshot;
    use matches::assert_matches;
    use std::sync::atomic::{AtomicU64, Ordering};

    pub const CALLBACK_CHANNEL_SIZE: usize = 20;
    pub const CURRENT_SYSTEM_IMAGE: &str =
        "0000000000000000000000000000000000000000000000000000000000000000";
    pub const LATEST_SYSTEM_IMAGE: &str =
        "1111111111111111111111111111111111111111111111111111111111111111";

    #[derive(Clone)]
    pub struct FakeUpdateChecker {
        result: Result<SystemUpdateStatus, crate::errors::ErrorKind>,
        call_count: Arc<AtomicU64>,
        // Taking this mutex blocks update checker.
        check_blocked: Arc<AsyncMutex<()>>,
    }
    impl FakeUpdateChecker {
        fn new(result: Result<SystemUpdateStatus, crate::errors::ErrorKind>) -> Self {
            Self {
                result,
                call_count: Arc::new(AtomicU64::new(0)),
                check_blocked: Arc::new(AsyncMutex::new(())),
            }
        }
        pub fn new_up_to_date() -> Self {
            Self::new(Ok(SystemUpdateStatus::UpToDate {
                system_image: CURRENT_SYSTEM_IMAGE.parse().expect("valid merkle"),
            }))
        }
        pub fn new_update_available() -> Self {
            Self::new(Ok(SystemUpdateStatus::UpdateAvailable {
                current_system_image: CURRENT_SYSTEM_IMAGE.parse().expect("valid merkle"),
                latest_system_image: LATEST_SYSTEM_IMAGE.parse().expect("valid merkle"),
            }))
        }
        pub fn new_error() -> Self {
            Self::new(Err(crate::errors::ErrorKind::ResolveUpdatePackage))
        }
        pub fn block(&self) -> Option<futures::lock::MutexGuard<'_, ()>> {
            self.check_blocked.try_lock()
        }
        pub fn call_count(&self) -> u64 {
            self.call_count.load(Ordering::SeqCst)
        }
    }
    impl UpdateChecker for FakeUpdateChecker {
        fn check(&self) -> BoxFuture<'_, Result<SystemUpdateStatus, crate::errors::Error>> {
            let check_blocked = Arc::clone(&self.check_blocked);
            let result = self.result.clone();
            self.call_count.fetch_add(1, Ordering::SeqCst);
            async move {
                check_blocked.lock().await;
                result.map_err(|e| e.into())
            }
                .boxed()
        }
    }

    #[derive(Clone)]
    pub struct FakeTargetChannelUpdater {
        call_count: Arc<AtomicU64>,
    }
    impl FakeTargetChannelUpdater {
        pub fn new() -> Self {
            Self { call_count: Arc::new(AtomicU64::new(0)) }
        }
        pub fn call_count(&self) -> u64 {
            self.call_count.load(Ordering::SeqCst)
        }
    }
    impl TargetChannelUpdater for FakeTargetChannelUpdater {
        fn update(&mut self) -> BoxFuture<'_, ()> {
            let call_count = self.call_count.clone();
            async move {
                call_count.fetch_add(1, Ordering::SeqCst);
            }
                .boxed()
        }
    }

    #[derive(Clone)]
    pub struct UnreachableUpdateApplier;
    impl UpdateApplier for UnreachableUpdateApplier {
        fn apply(
            &self,
            _current_system_image: Hash,
            _latest_system_image: Hash,
            _initiator: Initiator,
        ) -> BoxFuture<'_, Result<(), crate::errors::Error>> {
            unreachable!();
        }
    }

    #[derive(Clone)]
    pub struct FakeUpdateApplier {
        result: Result<(), crate::errors::ErrorKind>,
        call_count: Arc<AtomicU64>,
    }
    impl FakeUpdateApplier {
        pub fn new_success() -> Self {
            Self { result: Ok(()), call_count: Arc::new(AtomicU64::new(0)) }
        }
        pub fn new_error() -> Self {
            Self {
                result: Err(crate::errors::ErrorKind::SystemUpdaterFailed),
                call_count: Arc::new(AtomicU64::new(0)),
            }
        }
        pub fn call_count(&self) -> u64 {
            self.call_count.load(std::sync::atomic::Ordering::Relaxed)
        }
    }
    impl UpdateApplier for FakeUpdateApplier {
        fn apply(
            &self,
            _current_system_image: Hash,
            _latest_system_image: Hash,
            _initiator: Initiator,
        ) -> BoxFuture<'_, Result<(), crate::errors::Error>> {
            self.call_count.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
            future::ready(self.result.clone().map_err(|e| e.into())).boxed()
        }
    }

    #[derive(Clone)]
    pub struct UnreachableStateChangeCallback;
    impl StateChangeCallback for UnreachableStateChangeCallback {
        fn on_state_change(&self, _new_state: State) -> Result<(), Error> {
            unreachable!();
        }
    }

    #[derive(Clone)]
    pub struct StateChangeCollector {
        states: Arc<Mutex<Vec<State>>>,
    }
    impl StateChangeCollector {
        pub fn new() -> Self {
            Self { states: Arc::new(Mutex::new(vec![])) }
        }
        pub fn take_states(&self) -> Vec<State> {
            std::mem::replace(&mut self.states.lock(), vec![])
        }
    }
    impl StateChangeCallback for StateChangeCollector {
        fn on_state_change(&self, new_state: State) -> Result<(), Error> {
            self.states.lock().push(new_state);
            Ok(())
        }
    }

    #[derive(Clone)]
    struct FakeStateChangeCallback {
        sender: Arc<Mutex<Sender<State>>>,
    }
    impl FakeStateChangeCallback {
        fn new_callback_and_receiver() -> (Self, Receiver<State>) {
            let (sender, receiver) = channel(CALLBACK_CHANNEL_SIZE);
            (Self { sender: Arc::new(Mutex::new(sender)) }, receiver)
        }
    }
    impl StateChangeCallback for FakeStateChangeCallback {
        fn on_state_change(&self, new_state: State) -> Result<(), Error> {
            self.sender
                .lock()
                .try_send(new_state)
                .expect("FakeStateChangeCallback failed to send state");
            Ok(())
        }
    }

    type FakeUpdateManager = UpdateManager<
        FakeTargetChannelUpdater,
        FakeUpdateChecker,
        FakeUpdateApplier,
        FakeStateChangeCallback,
    >;

    type BlockingManagerManager = UpdateManager<
        FakeTargetChannelUpdater,
        BlockingUpdateChecker,
        FakeUpdateApplier,
        FakeStateChangeCallback,
    >;

    async fn next_n_states(receiver: &mut Receiver<State>, n: usize) -> Vec<State> {
        let mut v = Vec::with_capacity(n);
        for _ in 0..n {
            v.push(receiver.next().await.expect("next_n_states stream empty"));
        }
        v
    }

    async fn wait_until_manager_state_n(
        receiver: &mut Receiver<State>,
        manager_state: ManagerState,
        mut seen_count: u64,
    ) {
        if seen_count == 0 {
            return;
        }
        while let Some(new_state) = receiver.next().await {
            if new_state.manager_state == manager_state {
                seen_count -= 1;
                if seen_count == 0 {
                    return;
                }
            }
        }
        panic!("wait_until_state_n emptied stream: {}", seen_count);
    }

    impl From<ManagerState> for State {
        fn from(manager_state: ManagerState) -> Self {
            match manager_state {
                ManagerState::Idle | ManagerState::CheckingForUpdates => {
                    State { manager_state, version_available: None }
                }
                manager_state => State {
                    manager_state,
                    version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
                },
            }
        }
    }

    #[test]
    fn test_correct_initial_state() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
        );

        assert_eq!(manager.get_state(), Default::default());
    }

    #[test]
    fn test_try_start_update_returns_started() {
        let _executor = fasync::Executor::new().expect("create test executor");
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
        );

        assert_eq!(manager.try_start_update(Initiator::Manual, None), CheckStartedResult::Started);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_temporary_callbacks_dropped_after_update_attempt() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
        );
        let (callback0, mut receiver0) = FakeStateChangeCallback::new_callback_and_receiver();
        let (callback1, mut receiver1) = FakeStateChangeCallback::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback0));

        // Wait for first update attempt to complete, to guarantee that the second
        // try_start_update() call starts a new attempt (and generates more callback calls).
        wait_until_manager_state_n(&mut receiver0, ManagerState::Idle, 2).await;

        manager.try_start_update(Initiator::Manual, Some(callback1));

        // Wait for the second update attempt to complete, to guarantee the callbacks
        // have been called with more states.
        wait_until_manager_state_n(&mut receiver1, ManagerState::Idle, 2).await;

        // The first callback should have been dropped after the first attempt completed,
        // so it should still be empty.
        assert_matches!(receiver0.try_next(), Ok(None));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_callback_when_up_to_date() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
        );
        let (callback, receiver) = FakeStateChangeCallback::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback));

        assert_eq!(
            receiver.collect::<Vec<State>>().await,
            vec![
                ManagerState::Idle.into(),
                ManagerState::CheckingForUpdates.into(),
                ManagerState::Idle.into()
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_callback_when_update_available_and_apply_errors() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
        );
        let (callback, receiver) = FakeStateChangeCallback::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback));

        assert_eq!(
            receiver.collect::<Vec<State>>().await,
            vec![
                ManagerState::Idle.into(),
                ManagerState::CheckingForUpdates.into(),
                ManagerState::PerformingUpdate.into(),
                ManagerState::EncounteredError.into(),
                ManagerState::Idle.into()
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_callback_when_update_available_and_apply_succeeds() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_success(),
        );
        let (callback, mut receiver) = FakeStateChangeCallback::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback));

        assert_eq!(
            next_n_states(&mut receiver, 4).await,
            vec![
                ManagerState::Idle.into(),
                ManagerState::CheckingForUpdates.into(),
                ManagerState::PerformingUpdate.into(),
                ManagerState::WaitingForReboot.into(),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_permanent_callback_is_called() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
        );
        let (callback, mut receiver) = FakeStateChangeCallback::new_callback_and_receiver();

        manager.add_permanent_callback(callback);
        manager.try_start_update(Initiator::Manual, None);

        assert_eq!(
            next_n_states(&mut receiver, 5).await,
            vec![
                ManagerState::Idle.into(),
                ManagerState::CheckingForUpdates.into(),
                ManagerState::PerformingUpdate.into(),
                ManagerState::EncounteredError.into(),
                ManagerState::Idle.into(),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_permanent_callback_persists_across_attempts() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
        );
        let (callback, mut receiver) = FakeStateChangeCallback::new_callback_and_receiver();

        manager.add_permanent_callback(callback);
        manager.try_start_update(Initiator::Manual, None);

        // waiting for Idle state guarantees second try_start_update call
        // starts a new attempt
        assert_eq!(
            next_n_states(&mut receiver, 5).await,
            vec![
                ManagerState::Idle.into(),
                ManagerState::CheckingForUpdates.into(),
                ManagerState::PerformingUpdate.into(),
                ManagerState::EncounteredError.into(),
                ManagerState::Idle.into(),
            ]
        );

        manager.try_start_update(Initiator::Manual, None);

        assert_eq!(
            next_n_states(&mut receiver, 4).await,
            vec![
                ManagerState::CheckingForUpdates.into(),
                ManagerState::PerformingUpdate.into(),
                ManagerState::EncounteredError.into(),
                ManagerState::Idle.into(),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_channel_updater_called() {
        let channel_updater = FakeTargetChannelUpdater::new();
        let manager = UpdateManager::from_checker_and_applier(
            channel_updater.clone(),
            FakeUpdateChecker::new_up_to_date(),
            UnreachableUpdateApplier,
        );
        let (callback, receiver) = FakeStateChangeCallback::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback));
        receiver.collect::<Vec<State>>().await;

        assert_eq!(channel_updater.call_count(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_applier_called_if_update_available() {
        let update_applier = FakeUpdateApplier::new_error();
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_update_available(),
            update_applier.clone(),
        );
        let (callback, receiver) = FakeStateChangeCallback::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback));
        receiver.collect::<Vec<State>>().await;

        assert_eq!(update_applier.call_count(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_applier_not_called_if_up_to_date() {
        let update_applier = FakeUpdateApplier::new_error();
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            update_applier.clone(),
        );
        let (callback, receiver) = FakeStateChangeCallback::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback));
        receiver.collect::<Vec<State>>().await;

        assert_eq!(update_applier.call_count(), 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_return_to_initial_state_on_update_check_error() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_error(),
            FakeUpdateApplier::new_error(),
        );
        let (callback, receiver) = FakeStateChangeCallback::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback));
        receiver.collect::<Vec<State>>().await;

        assert_eq!(manager.get_state(), Default::default());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_return_to_initial_state_on_update_apply_error() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
        );
        let (callback, receiver) = FakeStateChangeCallback::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback));
        receiver.collect::<Vec<State>>().await;

        assert_eq!(manager.get_state(), Default::default());
    }

    #[derive(Clone)]
    pub struct BlockingUpdateChecker {
        blocker: future::Shared<oneshot::Receiver<()>>,
    }
    impl BlockingUpdateChecker {
        pub fn new_checker_and_sender() -> (Self, oneshot::Sender<()>) {
            let (sender, receiver) = oneshot::channel();
            let blocking_update_checker = BlockingUpdateChecker { blocker: receiver.shared() };
            (blocking_update_checker, sender)
        }
    }
    impl UpdateChecker for BlockingUpdateChecker {
        fn check(&self) -> BoxFuture<'_, Result<SystemUpdateStatus, crate::errors::Error>> {
            let blocker = self.blocker.clone();
            async move {
                assert!(blocker.await.is_ok(), "blocking future cancelled");
                Ok(SystemUpdateStatus::UpdateAvailable {
                    current_system_image: CURRENT_SYSTEM_IMAGE.parse().expect("valid merkle"),
                    latest_system_image: LATEST_SYSTEM_IMAGE.parse().expect("valid merkle"),
                })
            }
                .boxed()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_state_in_checking_for_updates() {
        let (blocking_update_checker, _sender) = BlockingUpdateChecker::new_checker_and_sender();
        let manager = BlockingManagerManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            blocking_update_checker,
            FakeUpdateApplier::new_error(),
        );

        manager.try_start_update(Initiator::Manual, None);

        assert_eq!(manager.get_state().manager_state, ManagerState::CheckingForUpdates);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_concurrent_update_attempts() {
        let (blocking_update_checker, sender) = BlockingUpdateChecker::new_checker_and_sender();
        let update_applier = FakeUpdateApplier::new_error();
        let manager = BlockingManagerManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            blocking_update_checker,
            update_applier.clone(),
        );
        let (callback, receiver) = FakeStateChangeCallback::new_callback_and_receiver();

        let res0 = manager.try_start_update(Initiator::Manual, Some(callback));
        // try_start_update advances state to CheckingForUpdates before returning
        // and the blocking_update_checker keeps it there
        let res1 = manager.try_start_update(Initiator::Manual, None);
        assert_matches!(sender.send(()), Ok(()));
        receiver.collect::<Vec<State>>().await;

        assert_eq!(res0, CheckStartedResult::Started);
        assert_eq!(res1, CheckStartedResult::InProgress);
        assert_eq!(update_applier.call_count(), 1);
    }
}
