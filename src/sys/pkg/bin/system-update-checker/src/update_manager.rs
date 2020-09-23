// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::apply::apply_system_update;
use crate::channel::{CurrentChannelManager, TargetChannelManager};
use crate::check::{check_for_system_update, SystemUpdateStatus};
use crate::connect::ServiceConnect;
use crate::last_update_storage::{LastUpdateStorage, LastUpdateStorageFile};
use crate::update_monitor::{StateNotifier, UpdateMonitor};
use crate::update_service::RealStateNotifier;
use anyhow::{anyhow, Context as _, Error};
use async_generator::GeneratorState;
use fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxyInterface};
use fidl_fuchsia_update::CheckNotStartedReason;
use fidl_fuchsia_update_ext::{
    CheckOptions, Initiator, InstallationErrorData, InstallingData, State, UpdateInfo,
};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_hash::Hash;
use fuchsia_inspect as finspect;
use fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn};
use futures::channel::{mpsc, oneshot};
use futures::future::BoxFuture;
use futures::prelude::*;
use futures::{pin_mut, select};
use std::fs;
use std::path::Path;
use std::sync::Arc;

#[derive(Debug)]
pub struct UpdateManagerControlHandle<N>(mpsc::Sender<UpdateManagerRequest<N>>);

impl<N> UpdateManagerControlHandle<N>
where
    N: StateNotifier,
{
    /// Try to start an update with the given options and optional monitor, returning whether or
    /// not the attempt was started (or attached to, if the options allow it).
    pub async fn try_start_update(
        &mut self,
        options: CheckOptions,
        callback: Option<N>,
    ) -> Result<(), CheckNotStartedReason> {
        let (send, recv) = oneshot::channel();
        let () = self
            .0
            .send(UpdateManagerRequest::TryStartUpdate { options, callback, responder: send })
            .await
            .map_err(|_| CheckNotStartedReason::Internal)?;
        recv.await.map_err(|_| CheckNotStartedReason::Internal)?
    }

    #[cfg(test)]
    pub async fn get_state(&mut self) -> Option<State> {
        let (send, recv) = oneshot::channel();
        let () = self.0.send(UpdateManagerRequest::GetState { responder: send }).await.ok()?;
        recv.await.ok()?
    }
}

// Manually implement Clone as not all N impl Clone, so derive(Clone) won't always impl Clone.
// See https://github.com/rust-lang/rust/issues/26925 for more context.
impl<N> Clone for UpdateManagerControlHandle<N> {
    fn clone(&self) -> Self {
        Self(self.0.clone())
    }
}

#[derive(Debug)]
pub(crate) enum UpdateManagerRequest<N> {
    TryStartUpdate {
        options: CheckOptions,
        callback: Option<N>,
        responder: oneshot::Sender<Result<(), CheckNotStartedReason>>,
    },
    #[cfg_attr(not(test), allow(dead_code))]
    GetState { responder: oneshot::Sender<Option<State>> },
}

#[derive(Debug)]
enum StatusEvent {
    State(State),
    VersionAvailableKnown(String),
}

pub struct UpdateManager<T, Ch, C, A, N>
where
    T: TargetChannelUpdater,
    Ch: CurrentChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
    N: StateNotifier,
{
    monitor: UpdateMonitor<N>,
    updater: SystemInterface<T, Ch, C, A>,
}

struct SystemInterface<T, Ch, C, A>
where
    T: TargetChannelUpdater,
    Ch: CurrentChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
{
    target_channel_updater: Arc<T>,
    current_channel_updater: Arc<Ch>,
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
        target_channel_updater: Arc<T>,
        current_channel_updater: Arc<Ch>,
        node: finspect::Node,
    ) -> Self {
        let (fut, update_monitor) = UpdateMonitor::from_inspect_node(node);
        fasync::Task::spawn(fut).detach();
        Self {
            monitor: update_monitor,
            updater: SystemInterface::load(
                target_channel_updater,
                current_channel_updater,
                RealUpdateChecker,
                RealUpdateApplier,
                Arc::new(LastUpdateStorageFile { data_dir: "/data".into() }),
            )
            .await,
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
        target_channel_updater: Arc<T>,
        current_channel_updater: Arc<Ch>,
        update_checker: C,
        update_applier: A,
        last_update_storage: Arc<impl LastUpdateStorage + Send + Sync + 'static>,
    ) -> Self {
        let (fut, update_monitor) = UpdateMonitor::new();
        fasync::Task::spawn(fut).detach();
        Self {
            monitor: update_monitor,
            updater: SystemInterface::new(
                target_channel_updater,
                current_channel_updater,
                update_checker,
                update_applier,
                last_update_storage,
                None,
            ),
        }
    }

    #[cfg(test)]
    pub async fn from_checker_and_applier_and_last_known_update_package(
        target_channel_updater: Arc<T>,
        current_channel_updater: Arc<Ch>,
        update_checker: C,
        update_applier: A,
        last_update_storage: Arc<impl LastUpdateStorage + Send + Sync + 'static>,
        last_known_update_package: Option<Hash>,
    ) -> Self {
        let (fut, update_monitor) = UpdateMonitor::new();
        fasync::Task::spawn(fut).detach();
        Self {
            monitor: update_monitor,
            updater: SystemInterface::new(
                target_channel_updater,
                current_channel_updater,
                update_checker,
                update_applier,
                last_update_storage,
                last_known_update_package,
            ),
        }
    }

    /// Builds and returns the update manager async task, along with a control handle to interact
    /// with the task. The returned future must be polled for the update manager task to make
    /// forward progress.
    pub fn start(self) -> (UpdateManagerControlHandle<N>, impl Future<Output = ()>) {
        let (send, recv) = mpsc::channel(0);
        (UpdateManagerControlHandle(send), self.run(recv))
    }

    #[cfg(test)]
    pub fn spawn(self) -> UpdateManagerControlHandle<N> {
        let (ctl, fut) = self.start();
        fasync::Task::spawn(fut).detach();
        ctl
    }

    async fn run(self, requests: mpsc::Receiver<UpdateManagerRequest<N>>) {
        let Self { mut monitor, mut updater } = self;
        pin_mut!(requests);

        loop {
            // Get the next request to start an update attempt, responding to other requests with
            // the appropriate defaults when no update attempt is in progress.
            let (options, callback) = loop {
                let request = match requests.next().await {
                    Some(request) => request,
                    None => return,
                };

                match request {
                    UpdateManagerRequest::TryStartUpdate { options, callback, responder } => {
                        let _ = responder.send(Ok(()));
                        break (options, callback);
                    }
                    UpdateManagerRequest::GetState { responder } => {
                        let _ = responder.send(None);
                    }
                }
            };

            // Start the update check with the requested options, configuring a monitor if
            // requested.
            if let Some(callback) = callback {
                monitor.add_temporary_callback(callback).await;
            }
            let update_check = async_generator::generate(|mut co| {
                let updater = &mut updater;
                async move { updater.do_system_update_check(&mut co, options.initiator).await }
            });
            pin_mut!(update_check);
            let mut current_state = None;

            // Run the update check, forwarding status updates to monitors, responding to requests
            // to monitor the attempt and blocking requests to start a new update attempt.
            let update_check_res = loop {
                enum Op<N> {
                    Request(UpdateManagerRequest<N>),
                    Status(StatusEvent),
                }
                let op = select! {
                    request = requests.select_next_some() => Op::Request(request),
                    status = update_check.select_next_some() => match status {
                        GeneratorState::Yielded(status) => Op::Status(status),
                        GeneratorState::Complete(res) => break res,
                    },
                };
                match op {
                    Op::Request(UpdateManagerRequest::TryStartUpdate {
                        options,
                        callback,
                        responder,
                    }) => {
                        let _ =
                            responder.send(if !options.allow_attaching_to_existing_update_check {
                                Err(CheckNotStartedReason::AlreadyInProgress)
                            } else {
                                if let Some(callback) = callback {
                                    monitor.add_temporary_callback(callback).await;
                                }
                                Ok(())
                            });
                    }
                    Op::Request(UpdateManagerRequest::GetState { responder }) => {
                        let _ = responder.send(current_state.clone());
                    }
                    Op::Status(StatusEvent::State(state)) => {
                        current_state = Some(state.clone());
                        monitor.advance_update_state(state).await
                    }
                    Op::Status(StatusEvent::VersionAvailableKnown(version)) => {
                        monitor.set_version_available(version);
                    }
                }
            };

            // Log the result of the update check and reset the monitor queue/inspect state for the
            // attempt.
            match update_check_res {
                Ok(()) => {}
                Err(e) => {
                    fx_log_err!("update attempt failed: {:#}", anyhow!(e));
                }
            }
            monitor.clear().await;
        }
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
        Err(err) => {
            fx_log_warn!("error finding update package in dynamic index: {:#}", anyhow!(err))
        }
    }

    async fn fetch_update_merkle(
        package_resolver: Result<impl PackageResolverProxyInterface, anyhow::Error>,
    ) -> Result<Hash, anyhow::Error> {
        let package_resolver = package_resolver?;
        Ok(crate::check::latest_update_merkle(&package_resolver).await?)
    }
    match fetch_update_merkle(package_resolver).await {
        Ok(hash) => return Some(hash),
        Err(err) => fx_log_warn!("error resolving update package: {:#}", anyhow!(err)),
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
        target_channel_updater: Arc<T>,
        current_channel_updater: Arc<Ch>,
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
        target_channel_updater: Arc<T>,
        current_channel_updater: Arc<Ch>,
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

    async fn do_system_update_check(
        &mut self,
        co: &mut async_generator::Yield<StatusEvent>,
        initiator: Initiator,
    ) -> Result<(), Error> {
        co.yield_(StatusEvent::State(State::CheckingForUpdates)).await;
        fx_log_info!(
            "starting update check (requested by {})",
            match initiator {
                Initiator::Service => "service",
                Initiator::User => "user",
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
                co.yield_(StatusEvent::State(State::ErrorCheckingForUpdate)).await;
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
                co.yield_(StatusEvent::State(State::NoUpdateAvailable)).await;

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
                    co.yield_(StatusEvent::VersionAvailableKnown(latest_system_image.to_string()))
                        .await;
                    co.yield_(StatusEvent::State(State::InstallingUpdate(InstallingData {
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
                    co.yield_(StatusEvent::State(State::InstallationError(
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
                co.yield_(StatusEvent::State(State::WaitingForReboot(InstallingData {
                    update: Some(UpdateInfo {
                        version_available: Some(latest_system_image.to_string()),
                        download_size: None,
                    }),
                    installation_progress: None,
                })))
                .await;
                let () = future::pending().await;
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
    fn update(&self) -> BoxFuture<'_, ()>;
}

impl<S: ServiceConnect + 'static> TargetChannelUpdater for TargetChannelManager<S> {
    fn update(&self) -> BoxFuture<'_, ()> {
        TargetChannelManager::update(self)
            .unwrap_or_else(|e| fx_log_err!("while updating target channel: {:#}", anyhow!(e)))
            .boxed()
    }
}

// For mocking
pub trait CurrentChannelUpdater: Send + Sync + 'static {
    fn update(&self) -> BoxFuture<'_, ()>;
}

impl CurrentChannelUpdater for CurrentChannelManager {
    fn update(&self) -> BoxFuture<'_, ()> {
        CurrentChannelManager::update(self)
            .unwrap_or_else(|e| fx_log_err!("while updating current channel: {:#}", anyhow!(e)))
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
    use crate::errors;
    use event_queue::{ClosedClient, Notify};
    use fuchsia_async::{DurationExt, TimeoutExt};
    use fuchsia_zircon as zx;
    use fuchsia_zircon::prelude::*;
    use futures::channel::mpsc::{channel, Receiver, Sender};
    use futures::channel::oneshot;
    use futures::future::BoxFuture;
    use futures::lock::Mutex as AsyncMutex;
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

    pub(crate) struct FakeUpdateManagerControlHandle<N> {
        requests: mpsc::Receiver<UpdateManagerRequest<N>>,
    }

    impl<N> FakeUpdateManagerControlHandle<N> {
        pub(crate) fn new() -> (UpdateManagerControlHandle<N>, Self) {
            let (send, recv) = mpsc::channel(0);

            (UpdateManagerControlHandle(send), Self { requests: recv })
        }

        pub(crate) fn next(&mut self) -> Option<UpdateManagerRequest<N>> {
            self.requests.next().now_or_never().flatten()
        }
    }

    type CheckResultFactory = fn() -> Result<SystemUpdateStatus, crate::errors::Error>;

    #[derive(Clone)]
    pub struct FakeUpdateChecker {
        result: CheckResultFactory,
        call_count: Arc<AtomicU64>,
        // Taking this mutex blocks update checker.
        check_blocked: Arc<AsyncMutex<()>>,
    }
    impl FakeUpdateChecker {
        fn new(result: CheckResultFactory) -> Self {
            Self {
                result,
                call_count: Arc::new(AtomicU64::new(0)),
                check_blocked: Arc::new(AsyncMutex::new(())),
            }
        }
        pub fn new_up_to_date() -> Self {
            Self::new(|| {
                Ok(SystemUpdateStatus::UpToDate {
                    system_image: CURRENT_SYSTEM_IMAGE.parse().expect("valid merkle"),
                    update_package: CURRENT_UPDATE_PACKAGE.parse().expect("valid merkle"),
                })
            })
        }
        pub fn new_update_available() -> Self {
            Self::new(|| {
                Ok(SystemUpdateStatus::UpdateAvailable {
                    current_system_image: CURRENT_SYSTEM_IMAGE.parse().expect("valid merkle"),
                    latest_system_image: LATEST_SYSTEM_IMAGE.parse().expect("valid merkle"),
                    latest_update_package: LATEST_UPDATE_PACKAGE.parse().expect("valid merkle"),
                })
            })
        }
        pub fn new_error() -> Self {
            Self::new(|| {
                Err(errors::Error::UpdatePackage(errors::UpdatePackage::Resolve(
                    zx::Status::INTERNAL,
                )))
            })
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
            let result = (self.result)();
            self.call_count.fetch_add(1, Ordering::SeqCst);
            async move {
                check_blocked.lock().await;
                result
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
        fn update(&self) -> BoxFuture<'_, ()> {
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
        fn update(&self) -> BoxFuture<'_, ()> {
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

    type ApplyResultFactory = fn() -> Result<(), crate::errors::Error>;

    #[derive(Clone)]
    pub struct FakeUpdateApplier {
        result: ApplyResultFactory,
        call_count: Arc<AtomicU64>,
    }
    impl FakeUpdateApplier {
        pub fn new_success() -> Self {
            Self { result: || Ok(()), call_count: Arc::new(AtomicU64::new(0)) }
        }
        pub fn new_error() -> Self {
            Self {
                result: || Err(crate::errors::Error::SystemUpdaterFailed),
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
            future::ready((self.result)().map_err(|e| e.into())).boxed()
        }
    }

    #[derive(Clone)]
    pub struct UnreachableNotifier;
    impl Notify for UnreachableNotifier {
        type Event = State;
        type NotifyFuture = BoxFuture<'static, Result<(), ClosedClient>>;
        fn notify(&self, _state: State) -> Self::NotifyFuture {
            unreachable!();
        }
    }

    #[derive(Clone, Debug)]
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
    impl Notify for StateChangeCollector {
        type Event = State;
        type NotifyFuture = future::Ready<Result<(), ClosedClient>>;
        fn notify(&self, state: State) -> Self::NotifyFuture {
            self.states.lock().push(state);
            future::ready(Ok(()))
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
    impl Notify for FakeStateNotifier {
        type Event = State;
        type NotifyFuture = future::Ready<Result<(), ClosedClient>>;
        fn notify(&self, state: State) -> Self::NotifyFuture {
            self.sender.lock().try_send(state).expect("FakeStateNotifier failed to send state");
            future::ready(Ok(()))
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

    #[fasync::run_singlethreaded(test)]
    async fn test_correct_initial_state() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();

        assert_eq!(manager.get_state().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_returns_started() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        assert_eq!(manager.try_start_update(options, None).await, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_temporary_callbacks_dropped_after_update_attempt() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options.clone(), Some(callback)).await.unwrap();

        // Drain the stream of status updates, which is only closed when the update attempt
        // completes and the callbacks are dropped, so this would hang if the callback is not
        // dropped after the update attempt.
        assert_eq!(
            receiver.collect::<Vec<State>>().await,
            vec![State::CheckingForUpdates, State::NoUpdateAvailable,]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_callback_when_up_to_date() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();

        assert_eq!(
            receiver.collect::<Vec<State>>().await,
            vec![State::CheckingForUpdates, State::NoUpdateAvailable,]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_callback_when_update_available_and_apply_errors() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();
        let expected_update_info = Some(UpdateInfo {
            version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
            download_size: None,
        });

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();

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
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_success(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();
        let (callback, mut receiver) = FakeStateNotifier::new_callback_and_receiver();
        let expected_installing_data = InstallingData {
            update: Some(UpdateInfo {
                version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
                download_size: None,
            }),
            installation_progress: None,
        };

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();

        assert_eq!(
            next_n_states(&mut receiver, 3).await,
            vec![
                State::CheckingForUpdates,
                State::InstallingUpdate(expected_installing_data.clone()),
                State::WaitingForReboot(expected_installing_data),
            ]
        );

        // The update attempt will never leave the WaitingForReboot state.
        assert_eq!(
            receiver.next().map(Some).on_timeout(100.millis().after_now(), || None).await,
            None
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_channel_updater_called() {
        let channel_updater = Arc::new(FakeTargetChannelUpdater::new());
        let mut manager = UpdateManager::from_checker_and_applier(
            Arc::clone(&channel_updater),
            Arc::new(FakeCurrentChannelUpdater::new()),
            FakeUpdateChecker::new_up_to_date(),
            UnreachableUpdateApplier,
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();
        let _ = receiver.collect::<Vec<State>>().await;

        assert_eq!(channel_updater.call_count(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_applier_called_if_update_available() {
        let update_applier = FakeUpdateApplier::new_error();
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            FakeUpdateChecker::new_update_available(),
            update_applier.clone(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();
        let _ = receiver.collect::<Vec<State>>().await;

        assert_eq!(update_applier.call_count(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_last_update_channel_stored_when_update_applied() {
        let last_update_storage = FakeLastUpdateStorage::new();

        let mut manager =
            FakeUpdateManager::from_checker_and_applier_and_last_known_update_package(
                Arc::new(FakeTargetChannelUpdater::new()),
                Arc::new(FakeCurrentChannelUpdater::new()),
                FakeUpdateChecker::new_update_available(),
                FakeUpdateApplier::new_error(),
                last_update_storage.clone(),
                Some(CURRENT_UPDATE_PACKAGE.parse().expect("valid merkle")),
            )
            .await
            .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();
        let _ = receiver.collect::<Vec<State>>().await;

        assert_eq!(
            last_update_storage.load(),
            Some(LATEST_UPDATE_PACKAGE.parse().expect("valid merkle"))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_applier_not_called_if_up_to_date() {
        let update_applier = FakeUpdateApplier::new_error();
        let current_channel_updater = Arc::new(FakeCurrentChannelUpdater::new());
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::clone(&current_channel_updater),
            FakeUpdateChecker::new_up_to_date(),
            update_applier.clone(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();
        let _ = receiver.collect::<Vec<State>>().await;

        assert_eq!(update_applier.call_count(), 0);
        assert_eq!(current_channel_updater.call_count(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_last_update_channel_stored_during_check_when_unknown() {
        let last_update_storage = FakeLastUpdateStorage::new();

        let mut manager =
            FakeUpdateManager::from_checker_and_applier_and_last_known_update_package(
                Arc::new(FakeTargetChannelUpdater::new()),
                Arc::new(FakeCurrentChannelUpdater::new()),
                FakeUpdateChecker::new_up_to_date(),
                FakeUpdateApplier::new_error(),
                last_update_storage.clone(),
                None,
            )
            .await
            .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();
        let _ = receiver.collect::<Vec<State>>().await;

        assert_eq!(
            last_update_storage.load(),
            Some(CURRENT_UPDATE_PACKAGE.parse().expect("valid merkle"))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_last_update_channel_not_stored_during_check_when_known() {
        let last_update_storage = FakeLastUpdateStorage::new();

        let mut manager =
            FakeUpdateManager::from_checker_and_applier_and_last_known_update_package(
                Arc::new(FakeTargetChannelUpdater::new()),
                Arc::new(FakeCurrentChannelUpdater::new()),
                FakeUpdateChecker::new_up_to_date(),
                FakeUpdateApplier::new_error(),
                last_update_storage.clone(),
                Some(CURRENT_UPDATE_PACKAGE.parse().expect("valid merkle")),
            )
            .await
            .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();
        let _ = receiver.collect::<Vec<State>>().await;

        assert_eq!(last_update_storage.load(), None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_return_to_initial_state_on_update_check_error() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            FakeUpdateChecker::new_error(),
            FakeUpdateApplier::new_error(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();
        let _ = receiver.collect::<Vec<State>>().await;

        assert_eq!(manager.get_state().await, Default::default());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_return_to_initial_state_on_update_apply_error() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();
        let _ = receiver.collect::<Vec<State>>().await;

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
        let (blocking_update_checker, sender) = BlockingUpdateChecker::new_checker_and_sender();
        let mut manager = BlockingManagerManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            blocking_update_checker,
            FakeUpdateApplier::new_error(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, None).await.unwrap();

        // Wait for the update attempt to enter the CheckingForUpdates state, panicing if it
        // completes prematurely.
        loop {
            let state = manager.get_state().await.unwrap();
            if state == State::CheckingForUpdates {
                break;
            }
        }

        // Unblock the update attempt and verify that it eventually enters the idle state.
        sender.send(()).unwrap();
        while let Some(_) = manager.get_state().await {}
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_no_concurrent_update_attempts_if_attach_not_requested() {
        let (blocking_update_checker, sender) = BlockingUpdateChecker::new_checker_and_sender();
        let update_applier = FakeUpdateApplier::new_error();
        let mut manager = BlockingManagerManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            blocking_update_checker,
            update_applier.clone(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        let res0 = manager.try_start_update(options.clone(), Some(callback)).await;
        let res1 = manager.try_start_update(options, None).await;
        assert_matches!(sender.send(()), Ok(()));
        let _ = receiver.collect::<Vec<State>>().await;

        assert_eq!(res0, Ok(()));
        assert_eq!(res1, Err(CheckNotStartedReason::AlreadyInProgress));
        assert_eq!(update_applier.call_count(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_merge_update_attempt_monitors_if_attach_requested() {
        let (blocking_update_checker, sender) = BlockingUpdateChecker::new_checker_and_sender();
        let update_applier = FakeUpdateApplier::new_error();
        let mut manager = BlockingManagerManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            Arc::new(FakeCurrentChannelUpdater::new()),
            blocking_update_checker,
            update_applier.clone(),
            FakeLastUpdateStorage::new(),
        )
        .await
        .spawn();
        let (callback0, receiver0) = FakeStateNotifier::new_callback_and_receiver();
        let (callback1, receiver1) = FakeStateNotifier::new_callback_and_receiver();
        let options = CheckOptions::builder().initiator(Initiator::User);

        let res0 = manager.try_start_update(options.clone().build(), Some(callback0)).await;
        let res1 = manager
            .try_start_update(
                options.allow_attaching_to_existing_update_check(true).build(),
                Some(callback1),
            )
            .await;
        assert_matches!(sender.send(()), Ok(()));
        let states0 = receiver0.collect::<Vec<State>>().await;
        let states1 = receiver1.collect::<Vec<State>>().await;

        assert_eq!(res0, Ok(()));
        assert_eq!(res1, Ok(()));
        assert_eq!(update_applier.call_count(), 1);
        assert_eq!(states0, states1);
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
