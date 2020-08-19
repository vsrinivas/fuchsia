// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::update::{Config, EnvironmentConnector, RebootController, Updater},
    anyhow::anyhow,
    event_queue::{EventQueue, Notify},
    fidl_fuchsia_update_installer::UpdateNotStartedReason,
    fidl_fuchsia_update_installer_ext::State,
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    futures::{
        channel::{mpsc, oneshot},
        prelude::*,
        select,
        stream::FusedStream,
    },
};

/// Start a install manager task that:
///  * Runs an update attempt in a seperate task.
///  * Provides a control handle to forward FIDL requests to the update attempt task.
pub async fn start_install_manager<N, U, E>(
    updater: U,
) -> (InstallManagerControlHandle<N>, impl Future<Output = ()>)
where
    N: Notify<State> + Send + 'static,
    U: Updater,
    E: EnvironmentConnector,
{
    let (send, recv) = mpsc::channel(0);
    (InstallManagerControlHandle(send), run::<N, U, E>(recv, updater))
}

/// The install manager task.
async fn run<N, U, E>(mut recv: mpsc::Receiver<ControlRequest<N>>, mut updater: U)
where
    N: Notify<State> + Send + 'static,
    U: Updater,
    E: EnvironmentConnector,
{
    // Set up event queue to keep track of all the Monitors.
    let (monitor_queue_fut, mut monitor_queue) = EventQueue::new();
    let eq_task = fasync::Task::spawn(monitor_queue_fut);

    // Each iteration of this loop is one update attempt.
    loop {
        // There is no active update attempt, so let's wait for a start request.
        let StartRequestData { config, monitor, reboot_controller, responder } =
            match handle_idle_control_request(&mut recv).await {
                Some(start_data) => start_data,
                None => {
                    // Ensure all monitors get the remaining state updates.
                    drop(monitor_queue);
                    eq_task.await;
                    return;
                }
            };

        // We connect to FIDL services on each update attempt (rather than once at the
        // beginning) to prevent stale connections.
        let env = match E::connect() {
            Ok(env) => env,
            Err(e) => {
                fx_log_err!("Error connecting to services: {:#}", anyhow!(e));
                // This fails the update attempt because it drops the responder, which closes
                // the zx channel that we got the start request from.
                continue;
            }
        };

        // Now we can actually start the task that manages the update attempt.
        let update_url = &config.update_url.clone();
        let should_write_recovery = config.should_write_recovery;
        let (attempt_id, attempt_stream) = updater.update(config, env, reboot_controller).await;
        futures::pin_mut!(attempt_stream);

        // Don't forget to add the first monitor to the queue and respond to StartUpdate :)
        if let Err(e) = monitor_queue.add_client(monitor).await {
            fx_log_warn!("error adding client to monitor queue: {:#}", anyhow!(e));
        }
        let _ = responder.send(Ok(attempt_id.clone()));

        // For this update attempt, handle events both from the FIDL server and the update task.
        loop {
            // We use this enum to make the body of the select as short as possible. Otherwise,
            // we'd need to set the crate's recursion_limit to be higher.
            enum Op<N: Notify<State>> {
                Request(ControlRequest<N>),
                Status(Option<State>),
            };
            let op = select! {
                req = recv.select_next_some() => Op::Request(req),
                state = attempt_stream.next() => Op::Status(state)
            };
            match op {
                // We got a FIDL requests (via the mpsc::Receiver).
                Op::Request(req) => {
                    handle_active_control_request(
                        req,
                        &mut monitor_queue,
                        &attempt_id,
                        &update_url,
                        should_write_recovery,
                    )
                    .await
                }
                // The update task has given us a progress update, so let's forward
                // that to all the monitors.
                Op::Status(Some(state)) => {
                    if let Err(e) = monitor_queue.queue_event(state).await {
                        fx_log_warn!("error sending state to monitor_queue: {:#}", anyhow!(e));
                    }
                }
                // The update task tells us the update is over, so let's notify all monitors.
                Op::Status(None) => {
                    if let Err(e) = monitor_queue.clear().await {
                        fx_log_warn!("error clearing clients of monitor_queue: {:#}", anyhow!(e));
                    }
                    break;
                }
            }
        }
    }
}

/// Returns when we get a Start control request (i.e. a StartUpdate FIDL request forwarded
/// from the FIDL server).
async fn handle_idle_control_request<N>(
    recv: &mut mpsc::Receiver<ControlRequest<N>>,
) -> Option<StartRequestData<N>>
where
    N: Notify<State>,
{
    // If the stream of control requests terminated while an update attempt was running,
    // this stream has already yielded None, and so further calls to next() may panic.
    // Instead, check if the stream is terminated via its FusedStream implementation
    // before proceeding.
    if recv.is_terminated() {
        return None;
    }

    // Right now we are in a state where there is no update running.
    while let Some(control_request) = recv.next().await {
        match control_request {
            ControlRequest::Start(start_data) => {
                return Some(start_data);
            }
            ControlRequest::Monitor(MonitorRequestData { responder, .. }) => {
                let _ = responder.send(false);
            }
        }
    }
    None
}

/// Handle the logic for the install manager task will do when receiving a control request
/// while an update is underway.
async fn handle_active_control_request<N>(
    req: ControlRequest<N>,
    monitor_queue: &mut event_queue::ControlHandle<N, State>,
    attempt_id: &str,
    update_url: &fuchsia_url::pkg_url::PkgUrl,
    should_write_recovery: bool,
) where
    N: Notify<State>,
{
    match req {
        ControlRequest::Start(StartRequestData {
            responder,
            config,
            monitor,
            reboot_controller,
        }) => {
            // Only attach monitor if she's compatible with current update check.
            // Note: We can only attach a reboot controller during the FIRST start request.
            // Any subsequent request with a reboot controller should fail.
            if reboot_controller.is_none()
                && config.allow_attach_to_existing_attempt
                && &config.update_url == update_url
                && config.should_write_recovery == should_write_recovery
            {
                if let Err(e) = monitor_queue.add_client(monitor).await {
                    fx_log_warn!("error adding client to monitor queue: {:#}", anyhow!(e));
                }
                let _ = responder.send(Ok(attempt_id.to_string()));
            } else {
                let _ = responder.send(Err(UpdateNotStartedReason::AlreadyInProgress));
            }
        }
        ControlRequest::Monitor(MonitorRequestData { attempt_id: id, monitor, responder }) => {
            // If an attempt ID is provided, ensure it matches the current attempt.
            if let Some(id) = id {
                if &id != attempt_id {
                    let _ = responder.send(false);
                    return;
                }
            }

            if let Err(e) = monitor_queue.add_client(monitor).await {
                fx_log_warn!("error adding client to monitor queue: {:#}", anyhow!(e));
            }
            let _ = responder.send(true);
        }
    }
}

/// A handle to forward installer FIDL requests to the install manager task.
#[derive(Clone)]
pub struct InstallManagerControlHandle<N>(mpsc::Sender<ControlRequest<N>>)
where
    N: Notify<State>;

/// Error indicating that the install manager task no longer exists.
#[derive(Debug, Clone, thiserror::Error, PartialEq, Eq)]
#[error("install manager dropped before all its control handles")]
pub struct InstallManagerGone;

impl From<mpsc::SendError> for InstallManagerGone {
    fn from(_: mpsc::SendError) -> Self {
        InstallManagerGone
    }
}

impl From<oneshot::Canceled> for InstallManagerGone {
    fn from(_: oneshot::Canceled) -> Self {
        InstallManagerGone
    }
}

/// This can be used to forward requests to the install manager task.
impl<N> InstallManagerControlHandle<N>
where
    N: Notify<State>,
{
    /// Forward StartUpdate requests to the install manager task.
    pub async fn start_update(
        &mut self,
        config: Config,
        monitor: N,
        reboot_controller: Option<RebootController>,
    ) -> Result<Result<String, UpdateNotStartedReason>, InstallManagerGone> {
        let (responder, receive_response) = oneshot::channel();
        self.0
            .send(ControlRequest::Start(StartRequestData {
                config,
                monitor,
                reboot_controller,
                responder,
            }))
            .await?;
        Ok(receive_response.await?)
    }

    /// Forward MonitorUpdate requests to the install manager task.
    pub async fn monitor_update(
        &mut self,
        attempt_id: Option<String>,
        monitor: N,
    ) -> Result<bool, InstallManagerGone> {
        let (responder, receive_response) = oneshot::channel();
        self.0
            .send(ControlRequest::Monitor(MonitorRequestData { attempt_id, monitor, responder }))
            .await?;
        Ok(receive_response.await?)
    }
}

/// Requests that can be forwarded to the sinstall manager task.
enum ControlRequest<N>
where
    N: Notify<State>,
{
    Start(StartRequestData<N>),
    Monitor(MonitorRequestData<N>),
}

struct StartRequestData<N>
where
    N: Notify<State>,
{
    config: Config,
    monitor: N,
    reboot_controller: Option<RebootController>,
    responder: oneshot::Sender<Result<String, UpdateNotStartedReason>>,
}

struct MonitorRequestData<N>
where
    N: Notify<State>,
{
    attempt_id: Option<String>,
    monitor: N,
    responder: oneshot::Sender<bool>,
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::update::{ConfigBuilder, Environment, NamespaceBuildInfo, NamespaceCobaltConnector},
        async_trait::async_trait,
        event_queue::ClosedClient,
        fidl_fuchsia_hardware_power_statecontrol::AdminMarker as PowerStateControlMarker,
        fidl_fuchsia_paver::{BootManagerMarker, DataSinkMarker},
        fidl_fuchsia_pkg::{PackageCacheMarker, PackageResolverMarker},
        fidl_fuchsia_space::ManagerMarker as SpaceManagerMarker,
        futures::future::BoxFuture,
        mpsc::{Receiver, Sender},
        parking_lot::Mutex,
        std::sync::Arc,
    };

    const CALLBACK_CHANNEL_SIZE: usize = 20;

    struct FakeStateNotifier {
        sender: Arc<Mutex<Sender<State>>>,
    }
    impl FakeStateNotifier {
        fn new_callback_and_receiver() -> (Self, Receiver<State>) {
            let (sender, receiver) = mpsc::channel(CALLBACK_CHANNEL_SIZE);
            (Self { sender: Arc::new(Mutex::new(sender)) }, receiver)
        }
    }
    impl Notify<State> for FakeStateNotifier {
        fn notify(&self, state: State) -> BoxFuture<'static, Result<(), ClosedClient>> {
            self.sender.lock().try_send(state).expect("FakeStateNotifier failed to send state");
            future::ready(Ok(())).boxed()
        }
    }

    struct StubEnvironmentConnector;
    impl EnvironmentConnector for StubEnvironmentConnector {
        fn connect() -> Result<Environment, anyhow::Error> {
            let (data_sink, _) = fidl::endpoints::create_proxy::<DataSinkMarker>().unwrap();
            let (boot_manager, _) = fidl::endpoints::create_proxy::<BootManagerMarker>().unwrap();
            let (pkg_resolver, _) =
                fidl::endpoints::create_proxy::<PackageResolverMarker>().unwrap();
            let (pkg_cache, _) = fidl::endpoints::create_proxy::<PackageCacheMarker>().unwrap();
            let (space_manager, _) = fidl::endpoints::create_proxy::<SpaceManagerMarker>().unwrap();
            let (power_state_control, _) =
                fidl::endpoints::create_proxy::<PowerStateControlMarker>().unwrap();

            Ok(Environment {
                data_sink,
                boot_manager,
                pkg_resolver,
                pkg_cache,
                space_manager,
                power_state_control,
                build_info: NamespaceBuildInfo,
                cobalt_connector: NamespaceCobaltConnector,
                pkgfs_system: None,
            })
        }
    }

    struct FakeUpdater(mpsc::Receiver<(String, mpsc::Receiver<State>)>);
    impl FakeUpdater {
        fn new(receiver: mpsc::Receiver<(String, mpsc::Receiver<State>)>) -> Self {
            Self(receiver)
        }
    }

    #[async_trait(?Send)]
    impl Updater for FakeUpdater {
        type UpdateStream = mpsc::Receiver<State>;

        async fn update(
            &mut self,
            _config: Config,
            _env: Environment,
            _reboot_controller: Option<RebootController>,
        ) -> (String, Self::UpdateStream) {
            self.0.next().await.unwrap()
        }
    }

    async fn start_install_manager_with_update_id(
        id: &str,
    ) -> (
        InstallManagerControlHandle<FakeStateNotifier>,
        fasync::Task<()>,
        mpsc::Sender<(String, mpsc::Receiver<State>)>,
        mpsc::Sender<State>,
    ) {
        // We use this channel to send the attempt id and state receiver to the update task, for
        // each update attempt. This allows tests to control when an update attempt ends -- all they
        // need to do is drop the state sender.
        let (mut updater_sender, updater_receiver) = mpsc::channel(0);
        let updater = FakeUpdater::new(updater_receiver);
        let (state_sender, state_receiver) = mpsc::channel(0);
        let (install_manager_ch, fut) =
            start_install_manager::<FakeStateNotifier, FakeUpdater, StubEnvironmentConnector>(
                updater,
            )
            .await;
        let install_manager_task = fasync::Task::local(fut);

        // We just use try_send because send calls are blocked on the receiver receiving the
        // event... and the receiver won't receive the event until we do a start_update request.
        updater_sender.try_send((id.to_string(), state_receiver)).unwrap();

        (install_manager_ch, install_manager_task, updater_sender, state_sender)
    }

    #[fasync::run_singlethreaded(test)]
    async fn monitor_update_fails_when_no_update_running() {
        let (mut install_manager_ch, _install_manager_task, _updater_sender, _state_sender) =
            start_install_manager_with_update_id("my-attempt").await;
        let (notifier0, _state_receiver0) = FakeStateNotifier::new_callback_and_receiver();
        let (notifier1, _state_receiver1) = FakeStateNotifier::new_callback_and_receiver();

        assert_eq!(install_manager_ch.monitor_update(None, notifier0).await, Ok(false));
        assert_eq!(
            install_manager_ch.monitor_update(Some("my-attempt".to_string()), notifier1).await,
            Ok(false)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn monitor_update_fails_with_wrong_id() {
        let (mut install_manager_ch, _install_manager_task, _updater_sender, _state_sender) =
            start_install_manager_with_update_id("my-attempt").await;
        let (notifier0, _state_receiver0) = FakeStateNotifier::new_callback_and_receiver();
        let (notifier1, _state_receiver1) = FakeStateNotifier::new_callback_and_receiver();

        assert_eq!(
            install_manager_ch
                .start_update(ConfigBuilder::new().build().unwrap(), notifier0, None)
                .await,
            Ok(Ok("my-attempt".to_string()))
        );

        assert_eq!(
            install_manager_ch.monitor_update(Some("unknown id".to_string()), notifier1).await,
            Ok(false)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn monitor_connects_via_start_update() {
        let (mut install_manager_ch, _install_manager_task, _updater_sender, mut state_sender) =
            start_install_manager_with_update_id("my-attempt").await;
        let (notifier, state_receiver) = FakeStateNotifier::new_callback_and_receiver();
        assert_eq!(
            install_manager_ch
                .start_update(ConfigBuilder::new().build().unwrap(), notifier, None)
                .await,
            Ok(Ok("my-attempt".to_string()))
        );

        let () = state_sender.send(State::Prepare).await.unwrap();
        let () = state_sender.send(State::FailPrepare).await.unwrap();
        drop(state_sender);

        assert_eq!(
            state_receiver.collect::<Vec<State>>().await,
            vec![State::Prepare, State::FailPrepare]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn monitor_update_succeeds() {
        let (mut install_manager_ch, _install_manager_task, _updater_sender, mut state_sender) =
            start_install_manager_with_update_id("my-attempt").await;
        let (notifier0, state_receiver0) = FakeStateNotifier::new_callback_and_receiver();
        let (notifier1, state_receiver1) = FakeStateNotifier::new_callback_and_receiver();
        let (notifier2, state_receiver2) = FakeStateNotifier::new_callback_and_receiver();

        // Show we can successfuly add each notifier via either start_update or monitor_update.
        assert_eq!(
            install_manager_ch
                .start_update(ConfigBuilder::new().build().unwrap(), notifier0, None)
                .await,
            Ok(Ok("my-attempt".to_string()))
        );
        assert_eq!(install_manager_ch.monitor_update(None, notifier1).await, Ok(true));
        assert_eq!(
            install_manager_ch.monitor_update(Some("my-attempt".to_string()), notifier2).await,
            Ok(true)
        );

        // Send state updates to the update task.
        let () = state_sender.send(State::Prepare).await.unwrap();
        let () = state_sender.send(State::FailPrepare).await.unwrap();
        drop(state_sender);

        // Each monitor should get these events.
        assert_eq!(
            state_receiver0.collect::<Vec<State>>().await,
            vec![State::Prepare, State::FailPrepare]
        );
        assert_eq!(
            state_receiver1.collect::<Vec<State>>().await,
            vec![State::Prepare, State::FailPrepare]
        );
        assert_eq!(
            state_receiver2.collect::<Vec<State>>().await,
            vec![State::Prepare, State::FailPrepare]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn succeed_additional_start_requests_when_compatible() {
        let (mut install_manager_ch, _install_manager_task, _updater_sender, mut state_sender) =
            start_install_manager_with_update_id("my-attempt").await;
        let (notifier0, state_receiver0) = FakeStateNotifier::new_callback_and_receiver();
        let (notifier1, state_receiver1) = FakeStateNotifier::new_callback_and_receiver();
        assert_eq!(
            install_manager_ch
                .start_update(ConfigBuilder::new().build().unwrap(), notifier0, None)
                .await,
            Ok(Ok("my-attempt".to_string()))
        );

        // The second start_update request is acceptable because the config is compatible.
        assert_eq!(
            install_manager_ch
                .start_update(
                    ConfigBuilder::new().allow_attach_to_existing_attempt(true).build().unwrap(),
                    notifier1,
                    None
                )
                .await,
            Ok(Ok("my-attempt".to_string()))
        );

        // Send state updates to the update task, then end the update by dropping the sender.
        let () = state_sender.send(State::Prepare).await.unwrap();
        let () = state_sender.send(State::FailPrepare).await.unwrap();
        drop(state_sender);

        // Each monitor should get these events.
        assert_eq!(
            state_receiver0.collect::<Vec<State>>().await,
            vec![State::Prepare, State::FailPrepare]
        );
        assert_eq!(
            state_receiver1.collect::<Vec<State>>().await,
            vec![State::Prepare, State::FailPrepare]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn fail_additional_start_requests_when_config_incompatible() {
        let (mut install_manager_ch, _install_manager_task, mut updater_sender, state_sender0) =
            start_install_manager_with_update_id("first-attempt-id").await;
        let (notifier0, mut state_receiver0) = FakeStateNotifier::new_callback_and_receiver();
        let (notifier1, _state_receiver1) = FakeStateNotifier::new_callback_and_receiver();
        let (notifier2, _state_receiver2) = FakeStateNotifier::new_callback_and_receiver();
        let (notifier3, _state_receiver3) = FakeStateNotifier::new_callback_and_receiver();
        let (notifier4, _state_receiver4) = FakeStateNotifier::new_callback_and_receiver();

        assert_eq!(
            install_manager_ch
                .start_update(ConfigBuilder::new().build().unwrap(), notifier0, None)
                .await,
            Ok(Ok("first-attempt-id".to_string()))
        );

        // Fails because allow_attach_to_existing_attempt is false.
        assert_eq!(
            install_manager_ch
                .start_update(ConfigBuilder::new().build().unwrap(), notifier1, None)
                .await,
            Ok(Err(UpdateNotStartedReason::AlreadyInProgress))
        );

        // Fails because different update URL.
        assert_eq!(
            install_manager_ch
                .start_update(
                    ConfigBuilder::new()
                        .update_url("fuchsia-pkg://fuchsia.com/different-url")
                        .allow_attach_to_existing_attempt(true)
                        .build()
                        .unwrap(),
                    notifier2,
                    None
                )
                .await,
            Ok(Err(UpdateNotStartedReason::AlreadyInProgress))
        );

        // Fails because incompatible configs (i.e. should_write_recovery is different).
        assert_eq!(
            install_manager_ch
                .start_update(
                    ConfigBuilder::new()
                        .allow_attach_to_existing_attempt(true)
                        .should_write_recovery(false)
                        .build()
                        .unwrap(),
                    notifier3,
                    None
                )
                .await,
            Ok(Err(UpdateNotStartedReason::AlreadyInProgress))
        );

        // Fails because we can't attach reboot controller in second start request.
        let (_, receiver) = mpsc::channel(0);
        assert_eq!(
            install_manager_ch
                .start_update(
                    ConfigBuilder::new().allow_attach_to_existing_attempt(true).build().unwrap(),
                    notifier4,
                    Some(RebootController::new(receiver))
                )
                .await,
            Ok(Err(UpdateNotStartedReason::AlreadyInProgress))
        );

        // End the current update attempt.
        drop(state_sender0);
        assert_eq!(state_receiver0.next().await, None);

        // Set what update() should return in the second attempt.
        let (_state_sender1, recv) = mpsc::channel(0);
        updater_sender.try_send(("second-attempt-id".to_string(), recv)).unwrap();

        // Now that there's no update in progress, start_update should work regardless of config.
        let (notifier5, _state_receiver5) = FakeStateNotifier::new_callback_and_receiver();
        assert_eq!(
            install_manager_ch
                .start_update(ConfigBuilder::new().build().unwrap(), notifier5, None)
                .await,
            Ok(Ok("second-attempt-id".to_string()))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn update_attempt_finishes_after_dropping_control_handle() {
        let (mut install_manager_ch, install_manager_task, _updater_sender, mut state_sender) =
            start_install_manager_with_update_id("my-attempt").await;
        let (notifier, state_receiver) = FakeStateNotifier::new_callback_and_receiver();

        assert_eq!(
            install_manager_ch
                .start_update(ConfigBuilder::new().build().unwrap(), notifier, None)
                .await,
            Ok(Ok("my-attempt".to_string()))
        );

        // Close the channel that sends ControlRequests to the update manager task.
        drop(install_manager_ch);

        // Even though the ControlRequest channel was dropped, the update attempt should still
        // be running it should be able to receive state events from the monitor stream.
        let () = state_sender.send(State::Prepare).await.unwrap();
        let () = state_sender.send(State::FailPrepare).await.unwrap();

        // Even if we drop the sender (which ends the update attempt), the state receivers
        // should still receive all the events we've sent up until this point.
        drop(state_sender);
        assert_eq!(
            state_receiver.collect::<Vec<State>>().await,
            vec![State::Prepare, State::FailPrepare]
        );

        // Ensures the update manager task stops after it sends the buffered state events to monitors.
        install_manager_task.await;
    }
}
