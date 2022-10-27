// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::apply::{apply_system_update, ApplyProgress, ApplyState};
use crate::channel::{CurrentChannelManager, TargetChannelManager};
use crate::check::{check_for_system_update, SystemUpdateStatus};
use crate::connect::ServiceConnect;
use crate::update_monitor::{AttemptNotifier, StateNotifier, UpdateMonitor};
use crate::update_service::{RealAttemptNotifier, RealStateNotifier};
use anyhow::{anyhow, Context as _, Error};
use async_generator::GeneratorState;
use event_queue::ControlHandle;
use fidl_fuchsia_update::{
    CheckNotStartedReason, CommitStatusProviderMarker, InstallationDeferralReason,
};
use fidl_fuchsia_update_ext::{
    query_commit_status, CheckOptions, CommitStatus, Initiator, InstallationDeferredData,
    InstallationErrorData, InstallationProgress, InstallingData, State, UpdateInfo,
};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_hash::Hash;
use fuchsia_inspect as finspect;
use futures::{
    channel::{mpsc, oneshot},
    future::BoxFuture,
    pin_mut,
    prelude::*,
    select,
    stream::BoxStream,
};
use std::sync::Arc;
use tracing::{error, info};

#[derive(Debug)]
pub struct UpdateManagerControlHandle<N: StateNotifier, A>(
    mpsc::Sender<UpdateManagerRequest<N, A>>,
);

impl<N, A> UpdateManagerControlHandle<N, A>
where
    N: StateNotifier,
    A: AttemptNotifier,
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

    pub async fn handle_all_these_updates(
        &mut self,
        callback: Box<dyn FnOnce(ControlHandle<N>) -> A + Send>,
    ) {
        let _ = self
            .0
            .send(UpdateManagerRequest::RegisterAttemptsMonitor { callback: NoDebug(callback) })
            .await;
    }

    #[cfg(test)]
    pub async fn get_state(&mut self) -> Option<State> {
        let (send, recv) = oneshot::channel();
        let () = self.0.send(UpdateManagerRequest::GetState { responder: send }).await.ok()?;
        recv.await.ok()?
    }

    #[cfg(test)]
    pub async fn get_last_known_update_package_hash(&mut self) -> Option<Hash> {
        let (send, recv) = oneshot::channel();
        let () = self
            .0
            .send(UpdateManagerRequest::GetLastKnownUpdatePackageHash { responder: send })
            .await
            .ok()?;
        recv.await.ok()?
    }

    #[cfg(test)]
    pub async fn try_start_update_then_wait_for_terminal_state(
        &mut self,
        options: CheckOptions,
        callback: Option<N>,
    ) -> Result<(), CheckNotStartedReason> {
        self.try_start_update(options, callback).await?;

        while !self.get_state().await.unwrap().is_terminal() {}
        Ok(())
    }
}

// Manually implement Clone as not all N impl Clone, so derive(Clone) won't always impl Clone.
// See https://github.com/rust-lang/rust/issues/26925 for more context.
impl<N: StateNotifier, A> Clone for UpdateManagerControlHandle<N, A> {
    fn clone(&self) -> Self {
        Self(self.0.clone())
    }
}

type MakeAttemptsMonitor<N, A> = Box<dyn FnOnce(ControlHandle<N>) -> A + Send>;
pub(crate) struct NoDebug<T>(T);

impl<T> std::fmt::Debug for NoDebug<T> {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_tuple("NoDebug").finish()
    }
}

#[derive(Debug)]
pub(crate) enum UpdateManagerRequest<N: StateNotifier, A> {
    TryStartUpdate {
        options: CheckOptions,
        callback: Option<N>,
        responder: oneshot::Sender<Result<(), CheckNotStartedReason>>,
    },
    RegisterAttemptsMonitor {
        callback: NoDebug<MakeAttemptsMonitor<N, A>>,
    },
    #[cfg_attr(not(test), allow(dead_code))]
    GetState {
        responder: oneshot::Sender<Option<State>>,
    },
    #[cfg_attr(not(test), allow(dead_code))]
    GetLastKnownUpdatePackageHash {
        responder: oneshot::Sender<Option<Hash>>,
    },
}

#[derive(Debug)]
enum StatusEvent {
    State(State),
    VersionAvailableKnown(String),
}

pub struct UpdateManager<T, C, A, N, Cq, Att>
where
    T: TargetChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
    N: StateNotifier,
    Cq: CommitQuerier,
    Att: AttemptNotifier,
{
    monitor: UpdateMonitor<N, Att>,
    updater: SystemInterface<T, C, A, Cq>,
}

struct SystemInterface<T, C, A, Cq>
where
    T: TargetChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
    Cq: CommitQuerier,
{
    target_channel_updater: Arc<T>,
    update_checker: C,
    update_applier: A,
    last_known_update_package: Option<Hash>,
    commit_status: Option<CommitStatus>,
    commit_querier: Cq,
}

impl<T>
    UpdateManager<
        T,
        RealUpdateChecker,
        RealUpdateApplier,
        RealStateNotifier,
        RealCommitQuerier,
        RealAttemptNotifier,
    >
where
    T: TargetChannelUpdater,
{
    pub async fn new(target_channel_updater: Arc<T>, node: finspect::Node) -> Self {
        let (fut, attempt_fut, update_monitor) = UpdateMonitor::from_inspect_node(node);
        fasync::Task::spawn(fut).detach();
        fasync::Task::spawn(attempt_fut).detach();
        Self {
            monitor: update_monitor,
            updater: SystemInterface::new(
                target_channel_updater,
                RealUpdateChecker,
                RealUpdateApplier,
                None,
                RealCommitQuerier,
                None,
            ),
        }
    }
}

impl<T, C, A, N, Cq, Att> UpdateManager<T, C, A, N, Cq, Att>
where
    T: TargetChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
    N: StateNotifier,
    Cq: CommitQuerier,
    Att: AttemptNotifier,
{
    #[cfg(test)]
    pub async fn from_checker_and_applier(
        target_channel_updater: Arc<T>,
        update_checker: C,
        update_applier: A,
        commit_querier: Cq,
    ) -> Self {
        let (fut, attempt_fut, update_monitor) = UpdateMonitor::new();
        fasync::Task::spawn(fut).detach();
        fasync::Task::spawn(attempt_fut).detach();
        Self {
            monitor: update_monitor,
            updater: SystemInterface::new(
                target_channel_updater,
                update_checker,
                update_applier,
                None,
                commit_querier,
                None,
            ),
        }
    }

    #[cfg(test)]
    async fn from_checker_and_applier_with_commit_status(
        target_channel_updater: Arc<T>,
        update_checker: C,
        update_applier: A,
        commit_querier: Cq,
        commit_status: Option<CommitStatus>,
    ) -> Self {
        let last_known_update_package = None;
        let (fut, attempt_fut, update_monitor) = UpdateMonitor::new();
        fasync::Task::spawn(fut).detach();
        fasync::Task::spawn(attempt_fut).detach();
        Self {
            monitor: update_monitor,
            updater: SystemInterface::new(
                target_channel_updater,
                update_checker,
                update_applier,
                last_known_update_package,
                commit_querier,
                commit_status,
            ),
        }
    }

    /// Builds and returns the update manager async task, along with a control handle to interact
    /// with the task. The returned future must be polled for the update manager task to make
    /// forward progress.
    pub fn start(self) -> (UpdateManagerControlHandle<N, Att>, impl Future<Output = ()>) {
        let (send, recv) = mpsc::channel(0);
        (UpdateManagerControlHandle(send), self.run(recv))
    }

    #[cfg(test)]
    pub fn spawn(self) -> UpdateManagerControlHandle<N, Att> {
        let (ctl, fut) = self.start();
        fasync::Task::spawn(fut).detach();
        ctl
    }

    async fn run(self, requests: mpsc::Receiver<UpdateManagerRequest<N, Att>>) {
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
                    UpdateManagerRequest::RegisterAttemptsMonitor { callback } => {
                        monitor.add_all_the_callbacks(callback.0).await;
                    }
                    UpdateManagerRequest::GetState { responder } => {
                        let _ = responder.send(None);
                    }
                    UpdateManagerRequest::GetLastKnownUpdatePackageHash { responder } => {
                        let _ = responder.send(updater.last_known_update_package);
                    }
                }
            };

            // Start the update check with the requested options, configuring a monitor if
            // requested.
            if let Some(callback) = callback {
                monitor.add_temporary_callback(callback).await;
            }

            monitor.tell_global_monitors_about_the_update(options.clone().into()).await;

            // Used for testing: it's ok to be slightly stale.
            let last_known_update_package = updater.last_known_update_package;

            let update_check = async_generator::generate(|mut co| {
                let updater = &mut updater;
                async move { updater.do_system_update_check(&mut co, options.initiator).await }
            });
            pin_mut!(update_check);
            let mut current_state = None;

            // Run the update check, forwarding status updates to monitors, responding to requests
            // to monitor the attempt and blocking requests to start a new update attempt.
            let update_check_res = loop {
                enum Op<N: StateNotifier, Att> {
                    Request(UpdateManagerRequest<N, Att>),
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
                    Op::Request(UpdateManagerRequest::RegisterAttemptsMonitor { callback }) => {
                        monitor.add_all_the_callbacks(callback.0).await;
                    }
                    Op::Request(UpdateManagerRequest::GetState { responder }) => {
                        let _ = responder.send(current_state.clone());
                    }
                    Op::Request(UpdateManagerRequest::GetLastKnownUpdatePackageHash {
                        responder,
                    }) => {
                        let _ = responder.send(last_known_update_package);
                    }
                    Op::Status(StatusEvent::State(state)) => {
                        current_state = Some(state.clone());
                        let should_flush = matches!(state, State::WaitingForReboot(_));
                        monitor.advance_update_state(state).await;
                        if should_flush {
                            monitor.try_flush().await;
                        }
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
                    error!("update attempt failed: {:#}", anyhow!(e));
                }
            }
            monitor.clear().await;
        }
    }
}

impl<T, C, A, Cq> SystemInterface<T, C, A, Cq>
where
    T: TargetChannelUpdater,
    C: UpdateChecker,
    A: UpdateApplier,
    Cq: CommitQuerier,
{
    fn new(
        target_channel_updater: Arc<T>,
        update_checker: C,
        update_applier: A,
        last_known_update_package: Option<Hash>,
        commit_querier: Cq,
        commit_status: Option<CommitStatus>,
    ) -> Self {
        Self {
            target_channel_updater,
            update_checker,
            update_applier,
            last_known_update_package,
            commit_querier,
            commit_status,
        }
    }

    async fn do_system_update_check(
        &mut self,
        co: &mut async_generator::Yield<StatusEvent>,
        initiator: Initiator,
    ) -> Result<(), Error> {
        co.yield_(StatusEvent::State(State::CheckingForUpdates)).await;
        info!(
            "starting update check (requested by {})",
            match initiator {
                Initiator::Service => "service",
                Initiator::User => "user",
            }
        );

        match self
            .update_checker
            .check(self.last_known_update_package.as_ref(), self.target_channel_updater.as_ref())
            .await
            .context("check_for_system_update failed")
        {
            Err(e) => {
                co.yield_(StatusEvent::State(State::ErrorCheckingForUpdate)).await;
                return Err(e);
            }
            Ok(SystemUpdateStatus::UpToDate { system_image, update_package }) => {
                info!("current system_image hash: {}", system_image);
                info!("system_image is already up-to-date");

                self.last_known_update_package = Some(update_package);

                co.yield_(StatusEvent::State(State::NoUpdateAvailable)).await;

                return Ok(());
            }
            Ok(SystemUpdateStatus::UpdateAvailable {
                current_system_image,
                latest_system_image,
            }) => {
                info!("current system_image hash: {}", current_system_image);
                info!("new system_image available: {}", latest_system_image);
                let version_available = latest_system_image.to_string();

                let status = match self.commit_status {
                    Some(CommitStatus::Committed) => Ok(CommitStatus::Committed),
                    Some(CommitStatus::Pending) | None => self
                        .commit_querier
                        .query_commit_status()
                        .await
                        .context("while querying commit status"),
                };

                match status {
                    Ok(CommitStatus::Committed) => {
                        self.commit_status = Some(CommitStatus::Committed);
                    }
                    Ok(CommitStatus::Pending) => {
                        self.commit_status = Some(CommitStatus::Pending);
                        co.yield_(StatusEvent::State(State::InstallationDeferredByPolicy(
                            InstallationDeferredData {
                                update: Some(UpdateInfo {
                                    version_available: Some(version_available.clone()),
                                    download_size: None,
                                }),
                                deferral_reason: Some(
                                    InstallationDeferralReason::CurrentSystemNotCommitted,
                                ),
                            },
                        )))
                        .await;
                        return Ok(());
                    }
                    Err(e) => return Err(e),
                }

                {
                    co.yield_(StatusEvent::VersionAvailableKnown(version_available.clone())).await;
                    co.yield_(StatusEvent::State(State::InstallingUpdate(InstallingData {
                        update: Some(UpdateInfo {
                            version_available: Some(version_available.clone()),
                            download_size: None,
                        }),
                        installation_progress: None,
                    })))
                    .await;
                }

                match self
                    .update_applier
                    .apply(initiator, self.target_channel_updater.as_ref())
                    .await
                    .context("apply_system_update failed")
                {
                    Ok(mut stream) => {
                        let mut waiting_for_reboot = false;
                        while let Some(result) = stream.next().await {
                            match result {
                                Ok(apply_state) => {
                                    let state = match apply_state {
                                        ApplyState::InstallingUpdate(ApplyProgress {
                                            download_size,
                                            fraction_completed,
                                        }) => State::InstallingUpdate(InstallingData {
                                            update: Some(UpdateInfo {
                                                version_available: Some(version_available.clone()),
                                                download_size,
                                            }),
                                            installation_progress: Some(InstallationProgress {
                                                fraction_completed,
                                            }),
                                        }),
                                        ApplyState::WaitingForReboot(ApplyProgress {
                                            download_size,
                                            fraction_completed,
                                        }) => {
                                            waiting_for_reboot = true;
                                            State::WaitingForReboot(InstallingData {
                                                update: Some(UpdateInfo {
                                                    version_available: Some(
                                                        version_available.clone(),
                                                    ),
                                                    download_size,
                                                }),
                                                installation_progress: Some(InstallationProgress {
                                                    fraction_completed,
                                                }),
                                            })
                                        }
                                    };
                                    co.yield_(StatusEvent::State(state)).await;
                                }
                                Err((ApplyProgress { download_size, fraction_completed }, e)) => {
                                    // If we failed to unblock reboot, it will ends up here and we
                                    // should not go back to InstallationError.
                                    if !waiting_for_reboot {
                                        co.yield_(StatusEvent::State(State::InstallationError(
                                            InstallationErrorData {
                                                update: Some(UpdateInfo {
                                                    version_available: Some(version_available),
                                                    download_size,
                                                }),
                                                installation_progress: Some(InstallationProgress {
                                                    fraction_completed,
                                                }),
                                            },
                                        )))
                                        .await;
                                    }
                                    return Err(e);
                                }
                            }
                        }
                    }
                    Err(e) => {
                        co.yield_(StatusEvent::State(State::InstallationError(
                            InstallationErrorData {
                                update: Some(UpdateInfo {
                                    version_available: Some(version_available),
                                    download_size: None,
                                }),
                                installation_progress: None,
                            },
                        )))
                        .await;
                        return Err(e);
                    }
                }
            }
        }
        Ok(())
    }
}

// For mocking
pub trait UpdateChecker: Send + Sync + 'static {
    fn check<'a>(
        &self,
        last_known_update_hash: Option<&'a Hash>,
        target_channel_manager: &'a dyn TargetChannelUpdater,
    ) -> BoxFuture<'a, Result<SystemUpdateStatus, crate::errors::Error>>;
}

pub struct RealUpdateChecker;

impl UpdateChecker for RealUpdateChecker {
    fn check<'a>(
        &self,
        last_known_update_hash: Option<&'a Hash>,
        target_channel_manager: &'a dyn TargetChannelUpdater,
    ) -> BoxFuture<'a, Result<SystemUpdateStatus, crate::errors::Error>> {
        check_for_system_update(last_known_update_hash, target_channel_manager).boxed()
    }
}

// For mocking
pub trait TargetChannelUpdater: Send + Sync + 'static {
    fn get_target_channel_update_url(&self) -> Option<String>;
}

impl<S: ServiceConnect + 'static> TargetChannelUpdater for TargetChannelManager<S> {
    fn get_target_channel_update_url(&self) -> Option<String> {
        TargetChannelManager::get_target_channel_update_url(self)
    }
}

// For mocking
pub trait CurrentChannelUpdater: Send + Sync + 'static {}
impl CurrentChannelUpdater for CurrentChannelManager {}

// For mocking
pub trait UpdateApplier: Send + Sync + 'static {
    fn apply<'a>(
        &self,
        initiator: Initiator,
        target_channel_updater: &'a dyn TargetChannelUpdater,
    ) -> BoxFuture<
        'a,
        Result<BoxStream<'a, Result<ApplyState, (ApplyProgress, anyhow::Error)>>, anyhow::Error>,
    >;
}

pub struct RealUpdateApplier;

impl UpdateApplier for RealUpdateApplier {
    fn apply<'a>(
        &self,
        initiator: Initiator,
        target_channel_updater: &'a dyn TargetChannelUpdater,
    ) -> BoxFuture<
        'a,
        Result<BoxStream<'a, Result<ApplyState, (ApplyProgress, anyhow::Error)>>, anyhow::Error>,
    > {
        apply_system_update(initiator, target_channel_updater).boxed()
    }
}

// For mocking.
pub trait CommitQuerier: Send + Sync + 'static {
    fn query_commit_status<'a>(&self) -> BoxFuture<'a, Result<CommitStatus, anyhow::Error>>;
}

pub struct RealCommitQuerier;

impl CommitQuerier for RealCommitQuerier {
    fn query_commit_status<'a>(&self) -> BoxFuture<'a, Result<CommitStatus, anyhow::Error>> {
        async {
            let provider = connect_to_protocol::<CommitStatusProviderMarker>()
                .context("while connecting to commit status provider")?;
            query_commit_status(&provider).await
        }
        .boxed()
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;
    use crate::errors;
    use assert_matches::assert_matches;
    use event_queue::{ClosedClient, Notify};
    use fidl_fuchsia_update_ext::AttemptOptions;
    use fuchsia_async::{DurationExt, TimeoutExt};
    use fuchsia_zircon::prelude::*;
    use futures::channel::mpsc::{channel, Receiver, Sender};
    use futures::channel::oneshot;
    use futures::future::BoxFuture;
    use futures::lock::Mutex as AsyncMutex;
    use parking_lot::Mutex;
    use std::sync::atomic::{AtomicU64, Ordering};

    pub const CALLBACK_CHANNEL_SIZE: usize = 20;
    pub const CURRENT_SYSTEM_IMAGE: &str =
        "0000000000000000000000000000000000000000000000000000000000000000";
    pub const LATEST_SYSTEM_IMAGE: &str =
        "1111111111111111111111111111111111111111111111111111111111111111";
    pub const CURRENT_UPDATE_PACKAGE: &str =
        "2222222222222222222222222222222222222222222222222222222222222222";

    pub(crate) struct FakeUpdateManagerControlHandle<N: StateNotifier, Att> {
        requests: mpsc::Receiver<UpdateManagerRequest<N, Att>>,
    }

    impl<N: StateNotifier, Att> FakeUpdateManagerControlHandle<N, Att> {
        pub(crate) fn new() -> (UpdateManagerControlHandle<N, Att>, Self) {
            let (send, recv) = mpsc::channel(0);

            (UpdateManagerControlHandle(send), Self { requests: recv })
        }

        pub(crate) fn next(&mut self) -> Option<UpdateManagerRequest<N, Att>> {
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
                    system_image: CURRENT_SYSTEM_IMAGE.parse().expect("valid hash"),
                    update_package: CURRENT_UPDATE_PACKAGE.parse().expect("valid hash"),
                })
            })
        }
        pub fn new_update_available() -> Self {
            Self::new(|| {
                Ok(SystemUpdateStatus::UpdateAvailable {
                    current_system_image: CURRENT_SYSTEM_IMAGE.parse().expect("valid hash"),
                    latest_system_image: LATEST_SYSTEM_IMAGE.parse().expect("valid hash"),
                })
            })
        }
        pub fn new_error() -> Self {
            Self::new(|| {
                Err(errors::Error::UpdatePackage(errors::UpdatePackage::Resolve(
                    fidl_fuchsia_pkg_ext::ResolveError::Internal,
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
            _last_known_update_hash: Option<&'a Hash>,
            _target_channel_updater: &'a dyn TargetChannelUpdater,
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

    const UPDATE_URL: &str = "fuchsia-pkg://fuchsia.test/update";
    #[derive(Clone)]
    pub struct FakeTargetChannelUpdater {
        update_url: String,
    }
    impl FakeTargetChannelUpdater {
        pub fn new() -> Self {
            Self::new_with_update_url(UPDATE_URL)
        }

        pub fn new_with_update_url(url: &str) -> Self {
            Self { update_url: url.to_owned() }
        }
    }
    impl TargetChannelUpdater for FakeTargetChannelUpdater {
        fn get_target_channel_update_url(&self) -> Option<String> {
            Some(self.update_url.clone())
        }
    }

    #[derive(Clone)]
    pub struct UnreachableUpdateApplier;
    impl UpdateApplier for UnreachableUpdateApplier {
        fn apply<'a>(
            &self,
            _initiator: Initiator,
            _target_channel_updater: &'a dyn TargetChannelUpdater,
        ) -> BoxFuture<
            'a,
            Result<
                BoxStream<'a, Result<ApplyState, (ApplyProgress, anyhow::Error)>>,
                anyhow::Error,
            >,
        > {
            unreachable!();
        }
    }

    type ApplyResultFactory = fn() -> Result<
        BoxStream<'static, Result<ApplyState, (ApplyProgress, anyhow::Error)>>,
        crate::errors::Error,
    >;

    #[derive(Clone)]
    pub struct FakeUpdateApplier {
        result: ApplyResultFactory,
        call_count: Arc<AtomicU64>,
    }
    impl FakeUpdateApplier {
        pub fn new_success() -> Self {
            Self {
                result: || {
                    Ok(futures::stream::iter(vec![
                        Ok(ApplyState::InstallingUpdate(ApplyProgress::new(1000, 0.42))),
                        Ok(ApplyState::WaitingForReboot(ApplyProgress::new(1000, 1.0))),
                    ])
                    .chain(futures::stream::pending())
                    .boxed())
                },
                call_count: Arc::new(AtomicU64::new(0)),
            }
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
        fn apply<'a>(
            &self,
            _initiator: Initiator,
            _target_channel_updater: &'a dyn TargetChannelUpdater,
        ) -> BoxFuture<
            'a,
            Result<
                BoxStream<'a, Result<ApplyState, (ApplyProgress, anyhow::Error)>>,
                anyhow::Error,
            >,
        > {
            self.call_count.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
            future::ready((self.result)().map_err(|e| e.into())).boxed()
        }
    }

    #[derive(Clone)]
    pub struct FakeCommitQuerier {
        call_count: Arc<AtomicU64>,
        committed: bool,
    }

    impl FakeCommitQuerier {
        pub fn new() -> Self {
            Self { call_count: Arc::new(AtomicU64::new(0)), committed: true }
        }

        pub fn new_pending() -> Self {
            Self { call_count: Arc::new(AtomicU64::new(0)), committed: false }
        }
    }

    impl CommitQuerier for FakeCommitQuerier {
        fn query_commit_status<'a>(&self) -> BoxFuture<'a, Result<CommitStatus, anyhow::Error>> {
            self.call_count.fetch_add(1, std::sync::atomic::Ordering::Relaxed);
            if self.committed {
                future::ready(Ok(CommitStatus::Committed)).boxed()
            } else {
                future::ready(Ok(CommitStatus::Pending)).boxed()
            }
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

    #[derive(Clone, Debug)]
    pub struct FakeAttemptNotifier;

    impl Notify for FakeAttemptNotifier {
        type Event = AttemptOptions;
        type NotifyFuture = future::Ready<Result<(), ClosedClient>>;
        fn notify(&self, _options: AttemptOptions) -> Self::NotifyFuture {
            future::ready(Ok(()))
        }
    }

    type FakeUpdateManager = UpdateManager<
        FakeTargetChannelUpdater,
        FakeUpdateChecker,
        FakeUpdateApplier,
        FakeStateNotifier,
        FakeCommitQuerier,
        FakeAttemptNotifier,
    >;

    type BlockingManagerManager = UpdateManager<
        FakeTargetChannelUpdater,
        BlockingUpdateChecker,
        FakeUpdateApplier,
        FakeStateNotifier,
        FakeCommitQuerier,
        FakeAttemptNotifier,
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
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeCommitQuerier::new(),
        )
        .await
        .spawn();

        assert_eq!(manager.get_state().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_last_update_package_changed_when_no_update_available() {
        let fake_update_checker = FakeUpdateChecker::new_up_to_date();

        let mut manager = FakeUpdateManager::from_checker_and_applier_with_commit_status(
            Arc::new(FakeTargetChannelUpdater::new()),
            fake_update_checker,
            FakeUpdateApplier::new_success(),
            FakeCommitQuerier::new(),
            None,
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        assert_eq!(manager.try_start_update(options, Some(callback)).await, Ok(()));

        assert_eq!(
            receiver.collect::<Vec<State>>().await,
            vec![State::CheckingForUpdates, State::NoUpdateAvailable]
        );

        assert_eq!(
            manager.get_last_known_update_package_hash().await,
            Some(CURRENT_UPDATE_PACKAGE.parse().unwrap())
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_last_update_package_unchanged_when_update_available() {
        let fake_update_checker = FakeUpdateChecker::new_update_available();

        let mut manager = FakeUpdateManager::from_checker_and_applier_with_commit_status(
            Arc::new(FakeTargetChannelUpdater::new()),
            fake_update_checker,
            FakeUpdateApplier::new_success(),
            FakeCommitQuerier::new(),
            None,
        )
        .await
        .spawn();
        let (callback, mut receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        assert_eq!(manager.try_start_update(options, Some(callback)).await, Ok(()));

        assert_eq!(
            next_n_states(&mut receiver, 4).await,
            vec![
                State::CheckingForUpdates,
                State::InstallingUpdate(InstallingData {
                    update: Some(UpdateInfo {
                        version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
                        download_size: None,
                    }),
                    installation_progress: None,
                }),
                State::InstallingUpdate(InstallingData {
                    update: Some(UpdateInfo {
                        version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
                        download_size: Some(1000),
                    }),
                    installation_progress: Some(InstallationProgress {
                        fraction_completed: Some(0.42)
                    })
                }),
                State::WaitingForReboot(InstallingData {
                    update: Some(UpdateInfo {
                        version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
                        download_size: Some(1000),
                    }),
                    installation_progress: Some(InstallationProgress {
                        fraction_completed: Some(1.0)
                    })
                }),
            ]
        );

        assert_eq!(manager.get_last_known_update_package_hash().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_is_current_status_committed_called_when_none() {
        let fake_commit_querier = FakeCommitQuerier::new();
        let fidl_call_count = Arc::clone(&fake_commit_querier.call_count);

        let mut manager = FakeUpdateManager::from_checker_and_applier_with_commit_status(
            Arc::new(FakeTargetChannelUpdater::new()),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_success(),
            fake_commit_querier,
            None,
        )
        .await
        .spawn();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        assert_eq!(
            manager.try_start_update_then_wait_for_terminal_state(options, None).await,
            Ok(())
        );

        assert_eq!(fidl_call_count.load(Ordering::SeqCst), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_is_current_status_committed_called_when_pending() {
        let fake_commit_querier = FakeCommitQuerier::new();
        let fidl_call_count = Arc::clone(&fake_commit_querier.call_count);

        let mut manager = FakeUpdateManager::from_checker_and_applier_with_commit_status(
            Arc::new(FakeTargetChannelUpdater::new()),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_success(),
            fake_commit_querier,
            Some(CommitStatus::Pending),
        )
        .await
        .spawn();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        assert_eq!(
            manager.try_start_update_then_wait_for_terminal_state(options, None).await,
            Ok(())
        );

        assert_eq!(fidl_call_count.load(Ordering::SeqCst), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_is_current_status_committed_not_called_when_committed() {
        let fake_commit_querier = FakeCommitQuerier::new();
        let fidl_call_count = Arc::clone(&fake_commit_querier.call_count);

        let mut manager = FakeUpdateManager::from_checker_and_applier_with_commit_status(
            Arc::new(FakeTargetChannelUpdater::new()),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_success(),
            fake_commit_querier,
            Some(CommitStatus::Committed),
        )
        .await
        .spawn();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        assert_eq!(
            manager.try_start_update_then_wait_for_terminal_state(options, None).await,
            Ok(())
        );

        assert_eq!(fidl_call_count.load(Ordering::SeqCst), 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_returns_started() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeCommitQuerier::new(),
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
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeCommitQuerier::new(),
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
            vec![State::CheckingForUpdates, State::NoUpdateAvailable]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_callback_when_up_to_date() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            FakeUpdateChecker::new_up_to_date(),
            FakeUpdateApplier::new_success(),
            FakeCommitQuerier::new(),
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();

        assert_eq!(
            receiver.collect::<Vec<State>>().await,
            vec![State::CheckingForUpdates, State::NoUpdateAvailable]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_try_start_update_callback_when_update_available_and_apply_errors() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
            FakeCommitQuerier::new(),
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
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_success(),
            FakeCommitQuerier::new(),
        )
        .await
        .spawn();
        let (callback, mut receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();

        assert_eq!(
            next_n_states(&mut receiver, 4).await,
            vec![
                State::CheckingForUpdates,
                State::InstallingUpdate(InstallingData {
                    update: Some(UpdateInfo {
                        version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
                        download_size: None,
                    }),
                    installation_progress: None
                }),
                State::InstallingUpdate(InstallingData {
                    update: Some(UpdateInfo {
                        version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
                        download_size: Some(1000),
                    }),
                    installation_progress: Some(InstallationProgress {
                        fraction_completed: Some(0.42)
                    })
                }),
                State::WaitingForReboot(InstallingData {
                    update: Some(UpdateInfo {
                        version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
                        download_size: Some(1000),
                    }),
                    installation_progress: Some(InstallationProgress {
                        fraction_completed: Some(1.0)
                    })
                }),
            ]
        );

        // The update attempt will never leave the WaitingForReboot state.
        assert_eq!(
            receiver.next().map(Some).on_timeout(100.millis().after_now(), || None).await,
            None
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_start_update_callback_when_update_available_and_pending() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_success(),
            FakeCommitQuerier::new_pending(),
        )
        .await
        .spawn();
        let (callback, mut receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();

        assert_eq!(
            next_n_states(&mut receiver, 2).await,
            vec![
                State::CheckingForUpdates,
                State::InstallationDeferredByPolicy(InstallationDeferredData {
                    update: Some(UpdateInfo {
                        version_available: Some(LATEST_SYSTEM_IMAGE.to_string()),
                        download_size: None
                    }),
                    deferral_reason: Some(InstallationDeferralReason::CurrentSystemNotCommitted)
                }),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_update_applier_called_if_update_available() {
        let update_applier = FakeUpdateApplier::new_error();
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            FakeUpdateChecker::new_update_available(),
            update_applier.clone(),
            FakeCommitQuerier::new(),
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
    async fn test_update_applier_not_called_if_up_to_date() {
        let update_applier = FakeUpdateApplier::new_error();
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            FakeUpdateChecker::new_up_to_date(),
            update_applier.clone(),
            FakeCommitQuerier::new(),
        )
        .await
        .spawn();
        let (callback, receiver) = FakeStateNotifier::new_callback_and_receiver();

        let options = CheckOptions::builder().initiator(Initiator::User).build();
        manager.try_start_update(options, Some(callback)).await.unwrap();
        let _ = receiver.collect::<Vec<State>>().await;

        assert_eq!(update_applier.call_count(), 0);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_return_to_initial_state_on_update_check_error() {
        let mut manager = FakeUpdateManager::from_checker_and_applier(
            Arc::new(FakeTargetChannelUpdater::new()),
            FakeUpdateChecker::new_error(),
            FakeUpdateApplier::new_error(),
            FakeCommitQuerier::new(),
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
            FakeUpdateChecker::new_update_available(),
            FakeUpdateApplier::new_error(),
            FakeCommitQuerier::new(),
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
            _last_known_update_hash: Option<&'a Hash>,
            _target_channel_updater: &'a dyn TargetChannelUpdater,
        ) -> BoxFuture<'a, Result<SystemUpdateStatus, crate::errors::Error>> {
            let blocker = self.blocker.clone();
            async move {
                assert!(blocker.await.is_ok(), "blocking future cancelled");
                Ok(SystemUpdateStatus::UpdateAvailable {
                    current_system_image: CURRENT_SYSTEM_IMAGE.parse().expect("valid hash"),
                    latest_system_image: LATEST_SYSTEM_IMAGE.parse().expect("valid hash"),
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
            blocking_update_checker,
            FakeUpdateApplier::new_error(),
            FakeCommitQuerier::new(),
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
            blocking_update_checker,
            update_applier.clone(),
            FakeCommitQuerier::new(),
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
            blocking_update_checker,
            update_applier.clone(),
            FakeCommitQuerier::new(),
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
}
