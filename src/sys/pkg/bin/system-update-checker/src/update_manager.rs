// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::apply::{apply_system_update, Initiator};
use crate::channel::{CurrentChannelManager, TargetChannelManager};
use crate::check::{check_for_system_update, SystemUpdateStatus};
use crate::connect::ServiceConnect;
use crate::last_update_storage::{LastUpdateStorage, LastUpdateStorageFile};
use crate::update_monitor::{StateNotifier, UpdateMonitor};
use crate::update_service::RealStateNotifier;
use anyhow::{Context as _, Error};
use fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxyInterface};
use fidl_fuchsia_update::CheckNotStartedReason;
use fidl_fuchsia_update_ext::{InstallationErrorData, InstallingData, State, UpdateInfo};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_inspect as finspect;
use fuchsia_merkle::Hash;
use fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn};
use futures::future::BoxFuture;
use futures::lock::Mutex as AsyncMutex;
use futures::prelude::*;
use std::fs;
use std::path::Path;
use std::sync::Arc;

/// Manages the lifecycle of an update attempt and notifies interested clients.
//
// # Lock Order
//
// `updater` is locked for the duration of an update attempt, and an update attempt will
// periodically lock `monitor` to send status updates. Before an async task or thread can lock
// `updater`, it must release any locks on `monitor`.
//
pub struct UpdateManager<T, Ch, C, A, N>
where
    T: TargetChannelUpdater,
    Ch: CurrentChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
    N: StateNotifier,
{
    monitor: Arc<AsyncMutex<UpdateMonitor<N>>>,
    updater: Arc<AsyncMutex<SystemInterface<T, Ch, C, A>>>,
}

struct SystemInterface<T, Ch, C, A>
where
    T: TargetChannelUpdater,
    Ch: CurrentChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
{
    target_channel_updater: T,
    current_channel_updater: Ch,
    update_checker: C,
    update_applier: A,
    last_update_storage: Arc<dyn LastUpdateStorage + Send + Sync>,
    last_known_update_package: Option<Hash>,
}

impl<T, Ch> UpdateManager<T, Ch, RealUpdateChecker, RealUpdateApplier, RealStateNotifier>
where
    T: TargetChannelUpdater,
    Ch: CurrentChannelUpdater,
{
    pub async fn new(
        target_channel_updater: T,
        current_channel_updater: Ch,
        node: finspect::Node,
    ) -> Self {
        let (fut, update_monitor) = UpdateMonitor::from_inspect_node(node);
        fasync::spawn(fut);
        Self {
            monitor: Arc::new(AsyncMutex::new(update_monitor)),
            updater: Arc::new(AsyncMutex::new(
                SystemInterface::load(
                    target_channel_updater,
                    current_channel_updater,
                    RealUpdateChecker,
                    RealUpdateApplier,
                    Arc::new(LastUpdateStorageFile { data_dir: "/data".into() }),
                )
                .await,
            )),
        }
    }
}

impl<T, Ch, C, A, N> UpdateManager<T, Ch, C, A, N>
where
    T: TargetChannelUpdater,
    Ch: CurrentChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
    N: StateNotifier,
{
    #[cfg(test)]
    pub async fn from_checker_and_applier(
        target_channel_updater: T,
        current_channel_updater: Ch,
        update_checker: C,
        update_applier: A,
        last_update_storage: Arc<impl LastUpdateStorage + Send + Sync + 'static>,
    ) -> Self {
        let (fut, update_monitor) = UpdateMonitor::new();
        fasync::spawn(fut);
        Self {
            monitor: Arc::new(AsyncMutex::new(update_monitor)),
            updater: Arc::new(AsyncMutex::new(SystemInterface::new(
                target_channel_updater,
                current_channel_updater,
                update_checker,
                update_applier,
                last_update_storage,
                None,
            ))),
        }
    }

    #[cfg(test)]
    pub async fn from_checker_and_applier_and_last_known_update_package(
        target_channel_updater: T,
        current_channel_updater: Ch,
        update_checker: C,
        update_applier: A,
        last_update_storage: Arc<impl LastUpdateStorage + Send + Sync + 'static>,
        last_known_update_package: Option<Hash>,
    ) -> Self {
        let (fut, update_monitor) = UpdateMonitor::new();
        fasync::spawn(fut);
        Self {
            monitor: Arc::new(AsyncMutex::new(update_monitor)),
            updater: Arc::new(AsyncMutex::new(SystemInterface::new(
                target_channel_updater,
                current_channel_updater,
                update_checker,
                update_applier,
                last_update_storage,
                last_known_update_package,
            ))),
        }
    }

    /// A Fuchsia Executor must be active when this method is called, b/c it uses fuchsia_async::spawn
    pub async fn try_start_update(
        &self,
        initiator: Initiator,
        callback: Option<N>,
        allow_attaching_to_existing_update_check: Option<bool>,
    ) -> Result<(), CheckNotStartedReason> {
        let mut monitor = self.monitor.lock().await;

        match monitor.update_state() {
            None => {
                if let Some(cb) = callback {
                    monitor.add_temporary_callback(cb).await;
                }
                monitor.advance_update_state(Some(State::CheckingForUpdates)).await;
                drop(monitor);
                let updater = Arc::clone(&self.updater);
                let monitor = Arc::clone(&self.monitor);

                // Spawn so that callers of this method are not blocked
                fasync::spawn(async move {
                    // Lock the updater for the duration of the update attempt. Contention is not
                    // expected except in the case that a previous update attempt failed, has set
                    // update_state back to None, but has not yet returned.
                    let mut updater = updater.lock().await;
                    updater.do_system_update_check_and_return_to_idle(monitor, initiator).await
                });
                Ok(())
            }
            _ => {
                if allow_attaching_to_existing_update_check == Some(true) {
                    if let Some(cb) = callback {
                        monitor.add_temporary_callback(cb).await;
                    }
                    Ok(())
                } else {
                    Err(CheckNotStartedReason::AlreadyInProgress)
                }
            }
        }
    }

    pub async fn get_state(&self) -> Option<State> {
        let monitor = self.monitor.lock().await;
        monitor.update_state()
    }

    #[cfg(test)]
    pub async fn add_temporary_callback(&self, callback: N) {
        self.monitor.lock().await.add_temporary_callback(callback).await;
    }
}

async fn load_last_update_package(
    last_update_storage: &(dyn LastUpdateStorage + Send + Sync),
    pkgfs_path: &Path,
    package_resolver: Result<impl PackageResolverProxyInterface, anyhow::Error>,
) -> Option<Hash> {
    if let Some(update) = last_update_storage.load() {
        return Some(update);
    }
    if let Some(update) = discover_last_update_package(pkgfs_path, package_resolver).await {
        last_update_storage.store(&update);
        return Some(update);
    }
    None
}

async fn discover_last_update_package(
    pkgfs_path: &Path,
    package_resolver: Result<impl PackageResolverProxyInterface, anyhow::Error>,
) -> Option<Hash> {
    fn check_dynamic_index(pkgfs_path: &Path) -> Result<Hash, anyhow::Error> {
        let bytes = fs::read(pkgfs_path.join("packages/update/0/meta"))?;
        let hex_str = std::str::from_utf8(&bytes)?;
        Ok(hex_str.parse()?)
    }
    match check_dynamic_index(pkgfs_path) {
        Ok(hash) => return Some(hash),
        Err(err) => fx_log_warn!("error finding update package in dynamic index: {:?}", err),
    }

    async fn fetch_update_merkle(
        package_resolver: Result<impl PackageResolverProxyInterface, anyhow::Error>,
    ) -> Result<Hash, anyhow::Error> {
        let package_resolver = package_resolver?;
        Ok(crate::check::latest_update_merkle(&package_resolver).await?)
    }
    match fetch_update_merkle(package_resolver).await {
        Ok(hash) => return Some(hash),
        Err(err) => fx_log_warn!("error resolving update package: {:?}", err),
    }

    None
}

impl<T, Ch, C, A> SystemInterface<T, Ch, C, A>
where
    T: TargetChannelUpdater,
    Ch: CurrentChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
{
    fn new(
        target_channel_updater: T,
        current_channel_updater: Ch,
        update_checker: C,
        update_applier: A,
        last_update_storage: Arc<dyn LastUpdateStorage + Send + Sync>,
        last_known_update_package: Option<Hash>,
    ) -> Self {
        Self {
            target_channel_updater,
            current_channel_updater,
            update_checker,
            update_applier,
            last_known_update_package,
            last_update_storage,
        }
    }

    pub async fn load(
        target_channel_updater: T,
        current_channel_updater: Ch,
        update_checker: C,
        update_applier: A,
        last_update_storage: Arc<dyn LastUpdateStorage + Send + Sync>,
    ) -> Self {
        let package_resolver = connect_to_service::<PackageResolverMarker>();

        let last_known_update_package = load_last_update_package(
            last_update_storage.as_ref(),
            Path::new("/pkgfs"),
            package_resolver,
        )
        .await;
        Self::new(
            target_channel_updater,
            current_channel_updater,
            update_checker,
            update_applier,
            last_update_storage,
            last_known_update_package,
        )
    }

    async fn do_system_update_check_and_return_to_idle<N: StateNotifier>(
        &mut self,
        monitor: Arc<AsyncMutex<UpdateMonitor<N>>>,
        initiator: Initiator,
    ) {
        if let Err(e) = self.do_system_update_check(monitor.clone(), initiator).await {
            fx_log_err!("update attempt failed: {:?}", e);
        }
        let mut monitor = monitor.lock().await;
        match monitor.update_state() {
            Some(State::WaitingForReboot(_)) => fx_log_err!(
                "system-update-checker is in the WaitingForReboot state. \
                 This should not have happened, because the sytem-updater should \
                 have rebooted the device before it returned."
            ),
            _ => {
                monitor.advance_update_state(None).await;
            }
        }
    }

    async fn do_system_update_check<N: StateNotifier>(
        &mut self,
        monitor: Arc<AsyncMutex<UpdateMonitor<N>>>,
        initiator: Initiator,
    ) -> Result<(), Error> {
        fx_log_info!(
            "starting update check (requested by {})",
            match initiator {
                Initiator::Automatic => "service",
                Initiator::Manual => "user",
            }
        );

        self.target_channel_updater.update().await;

        match self
            .update_checker
            .check(self.last_known_update_package.as_ref())
            .await
            .context("check_for_system_update failed")
        {
            Err(e) => {
                monitor
                    .lock()
                    .await
                    .advance_update_state(Some(State::ErrorCheckingForUpdate))
                    .await;
                return Err(e);
            }
            Ok(SystemUpdateStatus::UpToDate { system_image, update_package }) => {
                fx_log_info!("current system_image merkle: {}", system_image);
                fx_log_info!("system_image is already up-to-date");

                if self.last_known_update_package.is_none() {
                    self.last_known_update_package = Some(update_package);
                    self.last_update_storage.store(&update_package);
                }

                self.current_channel_updater.update().await;
                monitor.lock().await.advance_update_state(Some(State::NoUpdateAvailable)).await;

                return Ok(());
            }
            Ok(SystemUpdateStatus::UpdateAvailable {
                current_system_image,
                latest_system_image,
                latest_update_package,
            }) => {
                fx_log_info!("current system_image merkle: {}", current_system_image);
                fx_log_info!("new system_image available: {}", latest_system_image);
                {
                    let mut monitor = monitor.lock().await;
                    monitor.set_version_available(latest_system_image.to_string());
                    monitor
                        .advance_update_state(Some(State::InstallingUpdate(InstallingData {
                            update: Some(UpdateInfo {
                                version_available: Some(latest_system_image.to_string()),
                                download_size: None,
                            }),
                            installation_progress: None,
                        })))
                        .await;
                }

                self.last_update_storage.store(&latest_update_package);

                if let Err(e) = self
                    .update_applier
                    .apply(current_system_image, latest_system_image, initiator)
                    .await
                    .context("apply_system_update failed")
                {
                    monitor
                        .lock()
                        .await
                        .advance_update_state(Some(State::InstallationError(
                            InstallationErrorData {
                                update: Some(UpdateInfo {
                                    version_available: Some(latest_system_image.to_string()),
                                    download_size: None,
                                }),
                                installation_progress: None,
                            },
                        )))
                        .await;
                    return Err(e);
                };
                // On success, system-updater reboots the system before returning, so this code
                // should never run. The only way to leave WaitingForReboot state is to restart
                // the component
                monitor
                    .lock()
                    .await
                    .advance_update_state(Some(State::WaitingForReboot(InstallingData {
                        update: Some(UpdateInfo {
                            version_available: Some(latest_system_image.to_string()),
                            download_size: None,
                        }),
                        installation_progress: None,
                    })))
                    .await;
            }
        }
        Ok(())
    }
}

// For mocking
pub trait UpdateChecker: Send + Sync + 'static {
    fn check<'a>(
        &self,
        last_known_update_merkle: Option<&'a Hash>,
    ) -> BoxFuture<'a, Result<SystemUpdateStatus, crate::errors::Error>>;
}

pub struct RealUpdateChecker;

impl UpdateChecker for RealUpdateChecker {
    fn check<'a>(
        &self,
        last_known_update_merkle: Option<&'a Hash>,
    ) -> BoxFuture<'a, Result<SystemUpdateStatus, crate::errors::Error>> {
        check_for_system_update(last_known_update_merkle).boxed()
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
pub trait CurrentChannelUpdater: Send + Sync + 'static {
    fn update(&mut self) -> BoxFuture<'_, ()>;
}

impl CurrentChannelUpdater for CurrentChannelManager {
    fn update(&mut self) -> BoxFuture<'_, ()> {
        CurrentChannelManager::update(self)
            .unwrap_or_else(|e| fx_log_err!("while updating current channel: {:?}", e))
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
    ) -> BoxFuture<'_, Result<(), anyhow::Error>>;
}

pub struct RealUpdateApplier;

impl UpdateApplier for RealUpdateApplier {
    fn apply(
        &self,
        current_system_image: Hash,
        latest_system_image: Hash,
        initiator: Initiator,
    ) -> BoxFuture<'_, Result<(), anyhow::Error>> {
        apply_system_update(current_system_image, latest_system_image, initiator).boxed()
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use event_queue::{ClosedClient, Notify};
    use futures::channel::mpsc::{channel, Receiver, Sender};
    use futures::channel::oneshot;
    use futures::future::BoxFuture;
    use matches::assert_matches;
    use parking_lot::Mutex;
    use std::sync::atomic::{AtomicU64, Ordering};

    pub const CALLBACK_CHANNEL_SIZE: usize = 20;
    pub const CURRENT_SYSTEM_IMAGE: &str =
        "0000000000000000000000000000000000000000000000000000000000000000";
    pub const LATEST_SYSTEM_IMAGE: &str =
        "1111111111111111111111111111111111111111111111111111111111111111";
    pub const CURRENT_UPDATE_PACKAGE: &str =
        "2222222222222222222222222222222222222222222222222222222222222222";
    pub const LATEST_UPDATE_PACKAGE: &str =
        "3333333333333333333333333333333333333333333333333333333333333333";

    #[derive(Clone)]
    pub struct FakeUpdateChecker {
        result: Result<SystemUpdateStatus, crate::errors::Error>,
        call_count: Arc<AtomicU64>,
        // Taking this mutex blocks update checker.
        check_blocked: Arc<AsyncMutex<()>>,
    }
    impl FakeUpdateChecker {
        fn new(result: Result<SystemUpdateStatus, crate::errors::Error>) -> Self {
            Self {
                result,
                call_count: Arc::new(AtomicU64::new(0)),
                check_blocked: Arc::new(AsyncMutex::new(())),
            }
        }
        pub fn new_up_to_date() -> Self {
            Self::new(Ok(SystemUpdateStatus::UpToDate {
                system_image: CURRENT_SYSTEM_IMAGE.parse().expect("valid merkle"),
                update_package: CURRENT_UPDATE_PACKAGE.parse().expect("valid merkle"),
            }))
        }
        pub fn new_update_available() -> Self {
            Self::new(Ok(SystemUpdateStatus::UpdateAvailable {
                current_system_image: CURRENT_SYSTEM_IMAGE.parse().expect("valid merkle"),
                latest_system_image: LATEST_SYSTEM_IMAGE.parse().expect("valid merkle"),
                latest_update_package: LATEST_UPDATE_PACKAGE.parse().expect("valid merkle"),
            }))
        }
        pub fn new_error() -> Self {
            Self::new(Err(crate::errors::Error::ResolveUpdatePackage))
        }
        pub fn block(&self) -> Option<futures::lock::MutexGuard<'_, ()>> {
            self.check_blocked.try_lock()
        }
        pub fn call_count(&self) -> u64 {
            self.call_count.load(Ordering::SeqCst)
        }
    }
    impl UpdateChecker for FakeUpdateChecker {
        fn check<'a>(
            &self,
            _last_known_update_merkle: Option<&'a Hash>,
        ) -> BoxFuture<'a, Result<SystemUpdateStatus, crate::errors::Error>> {
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
    pub struct FakeCurrentChannelUpdater {
        call_count: Arc<AtomicU64>,
    }
    impl FakeCurrentChannelUpdater {
        pub fn new() -> Self {
            Self { call_count: Arc::new(AtomicU64::new(0)) }
        }
        pub fn call_count(&self) -> u64 {
            self.call_count.load(Ordering::SeqCst)
        }
    }
    impl CurrentChannelUpdater for FakeCurrentChannelUpdater {
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
        ) -> BoxFuture<'_, Result<(), anyhow::Error>> {
            unreachable!();
        }
    }

    #[derive(Clone)]
    pub struct FakeUpdateApplier {
        result: Result<(), crate::errors::Error>,
        call_count: Arc<AtomicU64>,
    }
    impl FakeUpdateApplier {
        pub fn new_success() -> Self {
            Self { result: Ok(()), call_count: Arc::new(AtomicU64::new(0)) }
        }
        pub fn new_error() -> Self {
            Self {
                result: Err(crate::errors::Error::SystemUpdaterFailed),
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
        ) -> BoxFuture<'_, Result<(), anyhow::Error>> {
            self.call_count.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
            future::ready(self.result.clone().map_err(|e| e.into())).boxed()
        }
    }

    #[derive(Clone)]
    pub struct UnreachableNotifier;
    impl Notify<State> for UnreachableNotifier {
        fn notify(&self, _state: State) -> BoxFuture<'static, Result<(), ClosedClient>> {
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
    impl Notify<State> for StateChangeCollector {
        fn notify(&self, state: State) -> BoxFuture<'static, Result<(), ClosedClient>> {
            self.states.lock().push(state);
            future::ready(Ok(())).boxed()
        }
    }

    #[derive(Clone)]
    struct FakeStateNotifier {
        sender: Arc<Mutex<Sender<State>>>,
    }
    impl FakeStateNotifier {
        fn new_callback_and_receiver() -> (Self, Receiver<State>) {
            let (sender, receiver) = channel(CALLBACK_CHANNEL_SIZE);
            (Self { sender: Arc::new(Mutex::new(sender)) }, receiver)
        }
    }
    impl Notify<State> for FakeStateNotifier {
        fn notify(&self, state: State) -> BoxFuture<'static, Result<(), ClosedClient>> {
            self.sender.lock().try_send(state).expect("FakeStateNotifier failed to send state");
            future::ready(Ok(())).boxed()
        }
    }

    #[derive(Default)]
    pub struct FakeLastUpdateStorage {
        store: Mutex<Option<Hash>>,
    }
    impl FakeLastUpdateStorage {
        pub fn new() -> Arc<Self> {
            Arc::new(Self::default())
        }
    }
    impl LastUpdateStorage for FakeLastUpdateStorage {
        fn load(&self) -> Option<Hash> {
            *self.store.lock()
        }
        fn store(&self, value: &Hash) {
            *self.store.lock() = Some(*value);
        }
    }

    type FakeUpdateManager = UpdateManager<
        FakeTargetChannelUpdater,
        FakeCurrentChannelUpdater,
        FakeUpdateChecker,
        FakeUpdateApplier,
        FakeStateNotifier,
    >;

    type BlockingManagerManager = UpdateManager<
        FakeTargetChannelUpdater,
        FakeCurrentChannelUpdater,
        BlockingUpdateChecker,
        FakeUpdateApplier,
        FakeStateNotifier,
    >;

    async fn next_n_states(receiver: &mut Receiver<State>, n: usize) -> Vec<State> {
        let mut v = Vec::with_capacity(n);
        for _ in 0..n {
            v.push(receiver.next().await.expect("next_n_states stream empty"));
        }
        v
    }

    async fn wait_until_update_state_n(
        receiver: &mut Receiver<State>,
        update_state: State,
        mut seen_count: u64,
    ) {
        if seen_count == 0 {
            return;
        }
        while let Some(new_state) = receiver.next().await {
            if new_state == update_state {
                seen_count -= 1;
                if seen_count == 0 {
                    return;
                }
            }
        }
        panic!("wait_until_state_n emptied stream: {}", seen_count);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_correct_initial_state() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeLastUpdateStorage::new(),
        )
        .await;

        assert_eq!(manager.get_state().await, Default::default());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_returns_started() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeLastUpdateStorage::new(),
        )
        .await;

        assert_eq!(manager.try_start_update(Initiator::Manual, None, None).await, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_temporary_callbacks_dropped_after_update_attempt() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeLastUpdateStorage::new(),
        )
        .await;
        let (callback0, mut receiver0) = FakeStateNotifier::new_callback_and_receiver();
        let (callback1, mut receiver1) = FakeStateNotifier::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback0), None).await.unwrap();

        // Wait for first update attempt to complete, to guarantee that the second
        // try_start_update() call starts a new attempt (and generates more callback calls).
        wait_until_update_state_n(&mut receiver0, State::NoUpdateAvailable, 1).await;

        manager.try_start_update(Initiator::Manual, Some(callback1), None).await.unwrap();

        // Wait for the second update attempt to complete, to guarantee the callbacks
        // have been called with more states.
        wait_until_update_state_n(&mut receiver1, State::NoUpdateAvailable, 1).await;

        // The first callback should have been dropped after the first attempt completed,
        // so it should still be empty.
        assert_matches!(receiver0.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_callback_when_up_to_date() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeLastUpdateStorage::new(),
        )
        .await;
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback), None).await.unwrap();

        assert_eq!(
            receiver.collect::<Vec<State>>().await,
            vec![State::CheckingForUpdates, State::NoUpdateAvailable,]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_callback_when_update_available_and_apply_errors() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
            FakeLastUpdateStorage::new(),
        )
        .await;
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();
        let expected_update_info = Some(UpdateInfo {
            version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
            download_size: None,
        });

        manager.try_start_update(Initiator::Manual, Some(callback), None).await.unwrap();

        assert_eq!(
            receiver.collect::<Vec<State>>().await,
            vec![
                State::CheckingForUpdates,
                State::InstallingUpdate(InstallingData {
                    update: expected_update_info.clone(),
                    installation_progress: None,
                }),
                State::InstallationError(InstallationErrorData {
                    update: expected_update_info,
                    installation_progress: None,
                }),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_callback_when_update_available_and_apply_succeeds() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_success(),
            FakeLastUpdateStorage::new(),
        )
        .await;
        let (callback, mut receiver) = FakeStateNotifier::new_callback_and_receiver();
        let expected_installing_data = InstallingData {
            update: Some(UpdateInfo {
                version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
                download_size: None,
            }),
            installation_progress: None,
        };

        manager.try_start_update(Initiator::Manual, Some(callback), None).await.unwrap();

        assert_eq!(
            next_n_states(&mut receiver, 3).await,
            vec![
                State::CheckingForUpdates,
                State::InstallingUpdate(expected_installing_data.clone()),
                State::WaitingForReboot(expected_installing_data),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_channel_updater_called() {
        let channel_updater = FakeTargetChannelUpdater::new();
        let manager = UpdateManager::from_checker_and_applier(
            channel_updater.clone(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            UnreachableUpdateApplier,
            FakeLastUpdateStorage::new(),
        )
        .await;
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback), None).await.unwrap();
        receiver.collect::<Vec<State>>().await;

        assert_eq!(channel_updater.call_count(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_applier_called_if_update_available() {
        let update_applier = FakeUpdateApplier::new_error();
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_update_available(),
            update_applier.clone(),
            FakeLastUpdateStorage::new(),
        )
        .await;
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback), None).await.unwrap();
        receiver.collect::<Vec<State>>().await;

        assert_eq!(update_applier.call_count(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_last_update_channel_stored_when_update_applied() {
        let last_update_storage = FakeLastUpdateStorage::new();

        let manager = FakeUpdateManager::from_checker_and_applier_and_last_known_update_package(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
            last_update_storage.clone(),
            Some(CURRENT_UPDATE_PACKAGE.parse().expect("valid merkle")),
        )
        .await;
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback), None).await.unwrap();
        receiver.collect::<Vec<State>>().await;

        assert_eq!(
            last_update_storage.load(),
            Some(LATEST_UPDATE_PACKAGE.parse().expect("valid merkle"))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_applier_not_called_if_up_to_date() {
        let update_applier = FakeUpdateApplier::new_error();
        let current_channel_updater = FakeCurrentChannelUpdater::new();
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            current_channel_updater.clone(),
            FakeUpdateChecker::new_up_to_date(),
            update_applier.clone(),
            FakeLastUpdateStorage::new(),
        )
        .await;
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback), None).await.unwrap();
        receiver.collect::<Vec<State>>().await;

        assert_eq!(update_applier.call_count(), 0);
        assert_eq!(current_channel_updater.call_count(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_last_update_channel_stored_during_check_when_unknown() {
        let last_update_storage = FakeLastUpdateStorage::new();

        let manager = FakeUpdateManager::from_checker_and_applier_and_last_known_update_package(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_error(),
            last_update_storage.clone(),
            None,
        )
        .await;
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback), None).await.unwrap();
        receiver.collect::<Vec<State>>().await;

        assert_eq!(
            last_update_storage.load(),
            Some(CURRENT_UPDATE_PACKAGE.parse().expect("valid merkle"))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_last_update_channel_not_stored_during_check_when_known() {
        let last_update_storage = FakeLastUpdateStorage::new();

        let manager = FakeUpdateManager::from_checker_and_applier_and_last_known_update_package(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_error(),
            last_update_storage.clone(),
            Some(CURRENT_UPDATE_PACKAGE.parse().expect("valid merkle")),
        )
        .await;
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback), None).await.unwrap();
        receiver.collect::<Vec<State>>().await;

        assert_eq!(last_update_storage.load(), None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_return_to_initial_state_on_update_check_error() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_error(),
            FakeUpdateApplier::new_error(),
            FakeLastUpdateStorage::new(),
        )
        .await;
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback), None).await.unwrap();
        receiver.collect::<Vec<State>>().await;

        assert_eq!(manager.get_state().await, Default::default());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_return_to_initial_state_on_update_apply_error() {
        let manager = FakeUpdateManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
            FakeLastUpdateStorage::new(),
        )
        .await;
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        manager.try_start_update(Initiator::Manual, Some(callback), None).await.unwrap();
        receiver.collect::<Vec<State>>().await;

        assert_eq!(manager.get_state().await, Default::default());
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
        fn check<'a>(
            &self,
            _last_known_update_merkle: Option<&'a Hash>,
        ) -> BoxFuture<'a, Result<SystemUpdateStatus, crate::errors::Error>> {
            let blocker = self.blocker.clone();
            async move {
                assert!(blocker.await.is_ok(), "blocking future cancelled");
                Ok(SystemUpdateStatus::UpdateAvailable {
                    current_system_image: CURRENT_SYSTEM_IMAGE.parse().expect("valid merkle"),
                    latest_system_image: LATEST_SYSTEM_IMAGE.parse().expect("valid merkle"),
                    latest_update_package: LATEST_SYSTEM_IMAGE.parse().expect("valid merkle"),
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
            FakeCurrentChannelUpdater::new(),
            blocking_update_checker,
            FakeUpdateApplier::new_error(),
            FakeLastUpdateStorage::new(),
        )
        .await;

        manager.try_start_update(Initiator::Manual, None, None).await.unwrap();

        assert_eq!(manager.get_state().await, Some(State::CheckingForUpdates));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_concurrent_update_attempts() {
        let (blocking_update_checker, sender) = BlockingUpdateChecker::new_checker_and_sender();
        let update_applier = FakeUpdateApplier::new_error();
        let manager = BlockingManagerManager::from_checker_and_applier(
            FakeTargetChannelUpdater::new(),
            FakeCurrentChannelUpdater::new(),
            blocking_update_checker,
            update_applier.clone(),
            FakeLastUpdateStorage::new(),
        )
        .await;
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let res0 = manager.try_start_update(Initiator::Manual, Some(callback), None).await;
        // try_start_update advances state to CheckingForUpdates before returning
        // and the blocking_update_checker keeps it there
        let res1 = manager.try_start_update(Initiator::Manual, None, None).await;
        assert_matches!(sender.send(()), Ok(()));
        receiver.collect::<Vec<State>>().await;

        assert_eq!(res0, Ok(()));
        assert_eq!(res1, Err(CheckNotStartedReason::AlreadyInProgress));
        assert_eq!(update_applier.call_count(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_last_update_merkle() {
        let storage_merkle = Hash::from([0x11; 32]);
        let index_merkle = Hash::from([0x22; 32]);
        let resolver_merkle = Hash::from([0x33; 32]);

        let last_update_storage = FakeLastUpdateStorage::default();
        last_update_storage.store(&storage_merkle);

        let pkgfs_dir = tempfile::tempdir().expect("create temp dir");
        fs::create_dir_all(pkgfs_dir.path().join("packages/update/0")).expect("mkdir");
        fs::write(pkgfs_dir.path().join("packages/update/0/meta"), index_merkle.to_string())
            .expect("write meta");

        let package_resolver = Ok(
            crate::check::test_check_for_system_update_impl::PackageResolverProxyTempDir::new_with_merkle(&resolver_merkle));

        let result =
            load_last_update_package(&last_update_storage, pkgfs_dir.path(), package_resolver)
                .await;
        assert_eq!(result, Some(storage_merkle))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_last_update_merkle_from_dynamic_index() {
        let index_merkle = Hash::from([0x22; 32]);
        let resolver_merkle = Hash::from([0x33; 32]);

        let last_update_storage = FakeLastUpdateStorage::default();

        let pkgfs_dir = tempfile::tempdir().expect("create temp dir");
        fs::create_dir_all(pkgfs_dir.path().join("packages/update/0")).expect("mkdir");
        fs::write(pkgfs_dir.path().join("packages/update/0/meta"), index_merkle.to_string())
            .expect("write meta");

        let package_resolver = Ok(
            crate::check::test_check_for_system_update_impl::PackageResolverProxyTempDir::new_with_merkle(&resolver_merkle));

        let result =
            load_last_update_package(&last_update_storage, pkgfs_dir.path(), package_resolver)
                .await;
        assert_eq!(result, Some(index_merkle))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_load_last_update_merkle_from_package_resolver() {
        let resolver_merkle = Hash::from([0x33; 32]);

        let last_update_storage = FakeLastUpdateStorage::default();
        let pkgfs_dir = tempfile::tempdir().expect("create temp dir");

        let package_resolver = Ok(
            crate::check::test_check_for_system_update_impl::PackageResolverProxyTempDir::new_with_merkle(&resolver_merkle));

        let result =
            load_last_update_package(&last_update_storage, pkgfs_dir.path(), package_resolver)
                .await;
        assert_eq!(result, Some(resolver_merkle))
    }
}
