// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    api_metrics::{ApiEvent, ApiMetricsReporter},
    app_set::FuchsiaAppSet,
    inspect::{AppsNode, StateNode},
};
use anyhow::{anyhow, Context as _, Error};
use channel_config::ChannelConfigs;
use event_queue::{ClosedClient, ControlHandle, Event, EventQueue, Notify};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_hardware_power_statecontrol::RebootReason;
use fidl_fuchsia_update::{
    self as update, AttemptsMonitorMarker, CheckNotStartedReason, CheckingForUpdatesData,
    ErrorCheckingForUpdateData, Initiator, InstallationDeferralReason, InstallationDeferredData,
    InstallationErrorData, InstallationProgress, InstallingData, ManagerRequest,
    ManagerRequestStream, MonitorMarker, MonitorProxy, MonitorProxyInterface,
    NoUpdateAvailableData, UpdateInfo,
};
use fidl_fuchsia_update_channel::{ProviderRequest, ProviderRequestStream};
use fidl_fuchsia_update_channelcontrol::{ChannelControlRequest, ChannelControlRequestStream};
use fidl_fuchsia_update_ext::AttemptOptions;
use fuchsia_async as fasync;
use fuchsia_component::{
    client::connect_to_protocol,
    server::{ServiceFs, ServiceObjLocal},
};
use fuchsia_syslog::{fx_log_err, fx_log_warn};
use fuchsia_zircon as zx;
use futures::{future::BoxFuture, lock::Mutex, prelude::*};
use omaha_client::{
    app_set::{AppSet as _, AppSetExt as _},
    common::CheckOptions,
    protocol::request::InstallSource,
    state_machine::{self, StartUpdateCheckResponse, StateMachineGone},
    storage::{Storage, StorageExt},
};
use std::{cell::RefCell, rc::Rc, time::Duration};
use tracing::{error, info, warn};

#[derive(Debug, Clone, PartialEq)]
pub struct State {
    pub manager_state: state_machine::State,
    pub version_available: Option<String>,
    pub install_progress: Option<f32>,
}

impl From<State> for Option<update::State> {
    fn from(state: State) -> Self {
        let update = Some(UpdateInfo {
            version_available: state.version_available,
            download_size: None,
            ..UpdateInfo::EMPTY
        });
        let installation_progress = Some(InstallationProgress {
            fraction_completed: state.install_progress,
            ..InstallationProgress::EMPTY
        });
        match state.manager_state {
            state_machine::State::Idle => None,
            state_machine::State::CheckingForUpdates(_) => {
                Some(update::State::CheckingForUpdates(CheckingForUpdatesData::EMPTY))
            }
            state_machine::State::ErrorCheckingForUpdate => {
                Some(update::State::ErrorCheckingForUpdate(ErrorCheckingForUpdateData::EMPTY))
            }
            state_machine::State::NoUpdateAvailable => {
                Some(update::State::NoUpdateAvailable(NoUpdateAvailableData::EMPTY))
            }
            state_machine::State::InstallationDeferredByPolicy => {
                Some(update::State::InstallationDeferredByPolicy(InstallationDeferredData {
                    update,
                    // For now, we deliberately only support one deferral reason. When we simplify
                    // the StateMachine type parameters, consider modifying the binary to support
                    // multiple deferral reasons.
                    deferral_reason: Some(InstallationDeferralReason::CurrentSystemNotCommitted),
                    ..InstallationDeferredData::EMPTY
                }))
            }
            state_machine::State::InstallingUpdate => {
                Some(update::State::InstallingUpdate(InstallingData {
                    update,
                    installation_progress,
                    ..InstallingData::EMPTY
                }))
            }
            state_machine::State::WaitingForReboot => {
                Some(update::State::WaitingForReboot(InstallingData {
                    update,
                    installation_progress,
                    ..InstallingData::EMPTY
                }))
            }
            state_machine::State::InstallationError => {
                Some(update::State::InstallationError(InstallationErrorData {
                    update,
                    installation_progress,
                    ..InstallationErrorData::EMPTY
                }))
            }
        }
    }
}

#[derive(Clone, Debug)]
struct StateNotifier {
    proxy: MonitorProxy,
}

impl Notify for StateNotifier {
    type Event = State;
    type NotifyFuture = futures::future::Either<
        futures::future::Map<
            <MonitorProxy as MonitorProxyInterface>::OnStateResponseFut,
            fn(Result<(), fidl::Error>) -> Result<(), ClosedClient>,
        >,
        futures::future::Ready<Result<(), ClosedClient>>,
    >;

    fn notify(&self, state: State) -> Self::NotifyFuture {
        let map_fidl_err_to_closed: fn(Result<(), fidl::Error>) -> Result<(), ClosedClient> =
            |res| res.map_err(|_| ClosedClient);

        match state.into() {
            Some(mut state) => {
                self.proxy.on_state(&mut state).map(map_fidl_err_to_closed).left_future()
            }
            None => future::ready(Ok(())).right_future(),
        }
    }
}

impl Event for State {
    fn can_merge(&self, other: &State) -> bool {
        if self.manager_state != other.manager_state {
            return false;
        }
        if self.version_available != other.version_available {
            warn!("version_available mismatch between two states: {:?}, {:?}", self, other);
        }
        true
    }
}

#[derive(Clone, Debug)]
struct AttemptNotifier {
    proxy: fidl_fuchsia_update::AttemptsMonitorProxy,
    control_handle: ControlHandle<StateNotifier>,
}

impl Notify for AttemptNotifier {
    type Event = fidl_fuchsia_update_ext::AttemptOptions;
    type NotifyFuture = futures::future::BoxFuture<'static, Result<(), ClosedClient>>;

    fn notify(&self, options: fidl_fuchsia_update_ext::AttemptOptions) -> Self::NotifyFuture {
        let mut update_attempt_event_queue = self.control_handle.clone();
        let proxy = self.proxy.clone();

        async move {
            let (monitor_proxy, monitor_server_end) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_update::MonitorMarker>()
                    .map_err(|_| ClosedClient)?;
            update_attempt_event_queue
                .add_client(StateNotifier { proxy: monitor_proxy })
                .await
                .map_err(|_| ClosedClient)?;
            proxy.on_start(options.into(), monitor_server_end).await.map_err(|_| ClosedClient)
        }
        .boxed()
    }
}

pub trait StateMachineController: Clone {
    fn start_update_check(
        &mut self,
        options: CheckOptions,
    ) -> BoxFuture<'_, Result<StartUpdateCheckResponse, StateMachineGone>>;
}

impl StateMachineController for state_machine::ControlHandle {
    fn start_update_check(
        &mut self,
        options: CheckOptions,
    ) -> BoxFuture<'_, Result<StartUpdateCheckResponse, StateMachineGone>> {
        self.start_update_check(options).boxed()
    }
}

pub struct FidlServer<ST, SM>
where
    ST: Storage,
    SM: StateMachineController,
{
    state_machine_control: SM,

    storage_ref: Rc<Mutex<ST>>,

    app_set: Rc<Mutex<FuchsiaAppSet>>,

    apps_node: AppsNode,

    state_node: StateNode,

    channel_configs: Option<ChannelConfigs>,

    // The current State, this is the internal representation of the fuchsia.update/State.
    state: State,

    single_monitor_queue: ControlHandle<StateNotifier>,

    attempt_monitor_queue: ControlHandle<AttemptNotifier>,

    metrics_reporter: Box<dyn ApiMetricsReporter>,

    current_channel: Option<String>,

    previous_out_of_space_failure: bool,
}

pub enum IncomingServices {
    Manager(ManagerRequestStream),
    ChannelControl(ChannelControlRequestStream),
    ChannelProvider(ProviderRequestStream),
}

impl<ST, SM> FidlServer<ST, SM>
where
    ST: Storage + 'static,
    SM: StateMachineController,
{
    pub fn new(
        state_machine_control: SM,
        storage_ref: Rc<Mutex<ST>>,
        app_set: Rc<Mutex<FuchsiaAppSet>>,
        apps_node: AppsNode,
        state_node: StateNode,
        channel_configs: Option<ChannelConfigs>,
        metrics_reporter: Box<dyn ApiMetricsReporter>,
        current_channel: Option<String>,
    ) -> Self {
        let state = State {
            manager_state: state_machine::State::Idle,
            version_available: None,
            install_progress: None,
        };
        state_node.set(&state);
        let (single_monitor_queue_fut, single_monitor_queue) = EventQueue::new();
        let (attempt_monitor_queue_fut, attempt_monitor_queue) = EventQueue::new();
        fasync::Task::local(single_monitor_queue_fut).detach();
        fasync::Task::local(attempt_monitor_queue_fut).detach();
        FidlServer {
            state_machine_control,
            storage_ref,
            app_set,
            apps_node,
            state_node,
            channel_configs,
            state,
            single_monitor_queue,
            attempt_monitor_queue,
            metrics_reporter,
            current_channel,
            previous_out_of_space_failure: false,
        }
    }

    /// Runs the FIDL Server and the StateMachine.
    pub async fn run(
        server: Rc<RefCell<Self>>,
        mut fs: ServiceFs<ServiceObjLocal<'_, IncomingServices>>,
    ) {
        fs.dir("svc")
            .add_fidl_service(IncomingServices::Manager)
            .add_fidl_service(IncomingServices::ChannelControl)
            .add_fidl_service(IncomingServices::ChannelProvider);
        const MAX_CONCURRENT: usize = 1000;
        // Handle each client connection concurrently.
        fs.for_each_concurrent(MAX_CONCURRENT, |stream| {
            Self::handle_client(Rc::clone(&server), stream).unwrap_or_else(|e| error!("{:?}", e))
        })
        .await
    }

    /// Handle an incoming FIDL connection from a client.
    async fn handle_client(
        server: Rc<RefCell<Self>>,
        stream: IncomingServices,
    ) -> Result<(), Error> {
        match stream {
            IncomingServices::Manager(mut stream) => {
                server.borrow_mut().metrics_reporter.emit_event(ApiEvent::UpdateManagerConnection);

                while let Some(request) =
                    stream.try_next().await.context("error receiving Manager request")?
                {
                    Self::handle_manager_request(Rc::clone(&server), request).await?;
                }
            }
            IncomingServices::ChannelControl(mut stream) => {
                while let Some(request) =
                    stream.try_next().await.context("error receiving ChannelControl request")?
                {
                    Self::handle_channel_control_request(Rc::clone(&server), request).await?;
                }
            }
            IncomingServices::ChannelProvider(mut stream) => {
                while let Some(request) =
                    stream.try_next().await.context("error receiving Provider request")?
                {
                    Self::handle_channel_provider_request(Rc::clone(&server), request).await?;
                }
            }
        }
        Ok(())
    }

    /// Handle fuchsia.update.Manager requests.
    async fn handle_manager_request(
        server: Rc<RefCell<Self>>,
        request: ManagerRequest,
    ) -> Result<(), Error> {
        match request {
            ManagerRequest::CheckNow { options, monitor, responder } => {
                let mut res = Self::handle_check_now(Rc::clone(&server), options, monitor).await;

                server
                    .borrow_mut()
                    .metrics_reporter
                    .emit_event(ApiEvent::UpdateManagerCheckNowResult(res));

                responder.send(&mut res).context("error sending CheckNow response")?;
            }

            ManagerRequest::PerformPendingReboot { responder } => {
                // We should reboot if either we're in a WaitingForReboot state or we've previously
                // received an error for OUT_OF_SPACE. In the second condition, a reboot will clear
                // the dynamic index in pkgfs and allow subsequent OTAs to continue after a garbage
                // collection.
                //
                // TODO(fxbug.dev/65571): remove previous_out_of_space_failure and this
                // rebooting behavior when pkg-cache can clear previous OTA packages on its own
                // TODO: this variable triggered the `must_not_suspend` lint and may be held across an await
                // If this is the case, it is an error. See fxbug.dev/87757 for more details
                let server_ref = server.borrow();
                let state_machine_state = server_ref.state.manager_state;
                let previous_out_of_space_failure = server_ref.previous_out_of_space_failure;

                // Drop to prevent holding the borrowed ref across an await.
                drop(server_ref);

                info!("Received PerformPendingRebootRequest");
                if previous_out_of_space_failure {
                    error!(
                        "Received request for PerformPendingReboot, and have OUT_OF_SPACE from \
                        a previous install attempt. Rebooting immediately."
                    )
                }

                if state_machine_state == state_machine::State::WaitingForReboot
                    || previous_out_of_space_failure
                {
                    connect_to_protocol::<fidl_fuchsia_hardware_power_statecontrol::AdminMarker>()?
                        .reboot(RebootReason::SystemUpdate)
                        .await?
                        .map_err(zx::Status::from_raw)
                        .context("reboot error")?;

                    responder.send(true)?;
                } else {
                    responder.send(false)?;
                }
            }

            ManagerRequest::MonitorAllUpdateChecks { attempts_monitor, control_handle: _ } => {
                if let Err(e) = Self::handle_monitor_all_updates(server, attempts_monitor).await {
                    fx_log_err!("error monitoring all update checks: {:#}", anyhow!(e))
                }
            }
        }
        Ok(())
    }

    /// Handle fuchsia.update.channelcontrol.ChannelControl requests.
    async fn handle_channel_control_request(
        server: Rc<RefCell<Self>>,
        request: ChannelControlRequest,
    ) -> Result<(), Error> {
        match request {
            ChannelControlRequest::SetTarget { channel, responder } => {
                info!("Received SetTarget request with {}", channel);

                server
                    .borrow_mut()
                    .metrics_reporter
                    .emit_event(ApiEvent::UpdateChannelControlSetTarget);

                Self::handle_set_target(server, channel).await;

                responder.send().context("error sending SetTarget response from ChannelControl")?;
            }
            ChannelControlRequest::GetTarget { responder } => {
                let app_set = Rc::clone(&server.borrow().app_set);
                let app_set = app_set.lock().await;
                let channel = app_set.get_system_target_channel();
                responder
                    .send(channel)
                    .context("error sending GetTarget response from ChannelControl")?;
            }
            ChannelControlRequest::GetCurrent { responder } => {
                let (current_channel, app_set) = {
                    let server = server.borrow();
                    (server.current_channel.clone(), Rc::clone(&server.app_set))
                };
                let channel = match current_channel {
                    Some(channel) => channel,
                    None => app_set.lock().await.get_system_current_channel().to_owned(),
                };

                responder
                    .send(&channel)
                    .context("error sending GetCurrent response from ChannelControl")?;
            }
            ChannelControlRequest::GetTargetList { responder } => {
                let server = server.borrow();
                let channel_names: Vec<&str> = match &server.channel_configs {
                    Some(channel_configs) => {
                        channel_configs.known_channels.iter().map(|cfg| cfg.name.as_ref()).collect()
                    }
                    None => Vec::new(),
                };
                responder
                    .send(&mut channel_names.into_iter())
                    .context("error sending channel list response from ChannelControl")?;
            }
        }
        Ok(())
    }

    async fn handle_channel_provider_request(
        server: Rc<RefCell<Self>>,
        request: ProviderRequest,
    ) -> Result<(), Error> {
        match request {
            ProviderRequest::GetCurrent { responder } => {
                let (current_channel, app_set) = {
                    let server = server.borrow();
                    (server.current_channel.clone(), Rc::clone(&server.app_set))
                };
                let channel = match current_channel {
                    Some(channel) => channel,
                    None => app_set.lock().await.get_system_current_channel().to_owned(),
                };
                responder
                    .send(&channel)
                    .context("error sending GetCurrent response from Provider")?;
            }
        }
        Ok(())
    }

    async fn handle_check_now(
        server: Rc<RefCell<Self>>,
        options: fidl_fuchsia_update::CheckOptions,
        monitor: Option<ClientEnd<MonitorMarker>>,
    ) -> Result<(), CheckNotStartedReason> {
        info!("Received CheckNow request with {:?} and {:?}", options, monitor);

        let source = match options.initiator {
            Some(Initiator::User) => InstallSource::OnDemand,
            Some(Initiator::Service) => InstallSource::ScheduledTask,
            None => {
                return Err(CheckNotStartedReason::InvalidOptions);
            }
        };

        let mut state_machine_control = server.borrow().state_machine_control.clone();

        let check_options = CheckOptions { source };

        match state_machine_control.start_update_check(check_options).await {
            Ok(StartUpdateCheckResponse::Started) => {}
            Ok(StartUpdateCheckResponse::AlreadyRunning) => {
                if options.allow_attaching_to_existing_update_check != Some(true) {
                    return Err(CheckNotStartedReason::AlreadyInProgress);
                }
            }
            Ok(StartUpdateCheckResponse::Throttled) => {
                return Err(CheckNotStartedReason::Throttled)
            }
            Err(state_machine::StateMachineGone) => return Err(CheckNotStartedReason::Internal),
        }

        // Attach the monitor if passed for current update.
        if let Some(monitor) = monitor {
            let monitor_proxy = monitor.into_proxy().map_err(|e| {
                error!("error getting proxy from monitor: {:?}", e);
                CheckNotStartedReason::InvalidOptions
            })?;
            let mut single_monitor_queue = server.borrow().single_monitor_queue.clone();
            single_monitor_queue.add_client(StateNotifier { proxy: monitor_proxy }).await.map_err(
                |e| {
                    error!("error adding client to single_monitor_queue: {:?}", e);
                    CheckNotStartedReason::Internal
                },
            )?;
        }

        Ok(())
    }

    async fn handle_monitor_all_updates(
        server: Rc<RefCell<Self>>,
        attempts_monitor: ClientEnd<AttemptsMonitorMarker>,
    ) -> Result<(), Error> {
        let proxy = attempts_monitor.into_proxy()?;
        let mut attempt_monitor_queue = server.borrow().attempt_monitor_queue.clone();
        let control_handle = server.borrow().single_monitor_queue.clone();
        attempt_monitor_queue.add_client(AttemptNotifier { proxy, control_handle }).await?;
        Ok(())
    }

    async fn handle_set_target(server: Rc<RefCell<Self>>, channel: String) {
        // TODO: Verify that channel is valid.
        let app_set = Rc::clone(&server.borrow().app_set);
        let target_channel = app_set.lock().await.get_system_target_channel().to_owned();
        if channel.is_empty() {
            let default_channel_cfg = match &server.borrow().channel_configs {
                Some(cfgs) => cfgs.get_default_channel(),
                None => None,
            };
            let (channel_name, appid) = match default_channel_cfg {
                Some(cfg) => (Some(cfg.name), cfg.appid),
                None => (None, None),
            };
            if let Some(name) = &channel_name {
                // If the default channel is the same as the target channel, then this is a no-op.
                if name == &target_channel {
                    return;
                }
                warn!("setting device to default channel: '{}' with app id: '{:?}'", name, appid);
            }
            // TODO(fxbug.dev/58887): only OTA that follows can change the current channel.
            // Simplify this logic.
            app_set.lock().await.set_system_target_channel(channel_name, appid);
        } else {
            // If the new target channel is the same as the existing target channel, then this is
            // a no-op.
            if channel == target_channel {
                return;
            }
            // TODO: this variable triggered the `must_not_suspend` lint and may be held across an await
            // If this is the case, it is an error. See fxbug.dev/87757 for more details
            let server = server.borrow();
            let channel_cfg = match &server.channel_configs {
                Some(cfgs) => cfgs.get_channel(&channel),
                None => None,
            };
            if channel_cfg.is_none() {
                warn!("Channel {} not found in known channels", &channel);
            }
            let appid = match channel_cfg {
                Some(cfg) => cfg.appid.clone(),
                None => None,
            };

            let storage_ref = Rc::clone(&server.storage_ref);
            // Don't borrow server across await.
            drop(server);
            let mut storage = storage_ref.lock().await;
            {
                let mut app_set = app_set.lock().await;
                if let Some(id) = &appid {
                    if id != app_set.get_system_app_id() {
                        warn!("Changing app id to: {}", id);
                    }
                }

                app_set.set_system_target_channel(Some(channel), appid);
                app_set.persist(&mut *storage).await;
            }
            storage.commit_or_log().await;
        }
        server.borrow().apps_node.set(&*app_set.lock().await);
    }

    /// The state change callback from StateMachine.
    pub async fn on_state_change(server: Rc<RefCell<Self>>, state: state_machine::State) {
        server.borrow_mut().state.manager_state = state;

        match state {
            state_machine::State::Idle => {
                server.borrow_mut().state.install_progress = None;
            }
            state_machine::State::WaitingForReboot => {
                server.borrow_mut().state.install_progress = Some(1.);
            }
            _ => {}
        }

        Self::send_state_to_queue(Rc::clone(&server)).await;

        // TODO: this variable triggered the `must_not_suspend` lint and may be held across an await
        // If this is the case, it is an error. See fxbug.dev/87757 for more details
        let s = server.borrow();
        s.state_node.set(&s.state);

        match state {
            state_machine::State::Idle | state_machine::State::WaitingForReboot => {
                let mut single_monitor_queue = s.single_monitor_queue.clone();
                let app_set = Rc::clone(&s.app_set);
                drop(s);

                // Try to flush the states before starting to reboot.
                if state == state_machine::State::WaitingForReboot {
                    match single_monitor_queue.try_flush(Duration::from_secs(5)).await {
                        Ok(flush_future) => {
                            if let Err(e) = flush_future.await {
                                warn!("Timed out flushing single_monitor_queue: {:#}", anyhow!(e));
                            }
                        }
                        Err(e) => {
                            warn!("error trying to flush single_monitor_queue: {:#}", anyhow!(e))
                        }
                    }
                }

                if let Err(e) = single_monitor_queue.clear().await {
                    warn!("error clearing clients of single_monitor_queue: {:?}", e);
                }

                // The state machine might make changes to apps only at the end of an update,
                // update the apps node in inspect.
                server.borrow().apps_node.set(&*app_set.lock().await);
            }
            state_machine::State::CheckingForUpdates(install_source) => {
                let attempt_options = match install_source {
                    InstallSource::OnDemand => AttemptOptions { initiator: Initiator::User.into() },
                    InstallSource::ScheduledTask => {
                        AttemptOptions { initiator: Initiator::Service.into() }
                    }
                };
                let mut attempt_monitor_queue = s.attempt_monitor_queue.clone();
                drop(s);
                if let Err(e) = attempt_monitor_queue.queue_event(attempt_options).await {
                    fx_log_warn!("error sending update to attempt queue: {:#}", anyhow!(e))
                }
            }
            _ => {}
        }
    }

    async fn send_state_to_queue(server: Rc<RefCell<Self>>) {
        // TODO: this variable triggered the `must_not_suspend` lint and may be held across an await
        // If this is the case, it is an error. See fxbug.dev/87757 for more details
        let server = server.borrow();
        let mut single_monitor_queue = server.single_monitor_queue.clone();
        let state = server.state.clone();
        drop(server);
        if let Err(e) = single_monitor_queue.queue_event(state).await {
            warn!("error sending state to single_monitor_queue: {:?}", e)
        }
    }

    pub async fn on_progress_change(
        server: Rc<RefCell<Self>>,
        progress: state_machine::InstallProgress,
    ) {
        server.borrow_mut().state.install_progress = Some(progress.progress);
        Self::send_state_to_queue(server).await;
    }

    /// Alert the `FidlServer` that a previous update attempt on this boot failed with an
    /// OUT_OF_SPACE error.
    pub fn set_previous_out_of_space_failure(server: Rc<RefCell<Self>>) {
        server.borrow_mut().previous_out_of_space_failure = true;
    }

    /// Get the state of the `previous_out_of_space_failure` latch.
    #[cfg(test)]
    pub fn previous_out_of_space_failure(server: Rc<RefCell<Self>>) -> bool {
        server.borrow().previous_out_of_space_failure
    }
}

#[cfg(test)]
pub use stub::{
    FidlServerBuilder, MockOrRealStateMachineController, MockStateMachineController, StubFidlServer,
};

#[cfg(test)]
mod stub {
    use super::*;
    use crate::app_set::{AppIdSource, AppMetadata};
    use crate::{
        api_metrics::StubApiMetricsReporter,
        configuration,
        inspect::{LastResultsNode, ProtocolStateNode, ScheduleNode},
        observer::FuchsiaObserver,
    };
    use fuchsia_inspect::Inspector;
    use futures::future::BoxFuture;
    use omaha_client::{
        common::{App, CheckTiming, ProtocolState, UpdateCheckSchedule},
        cup_ecdsa::StandardCupv2Handler,
        http_request::StubHttpRequest,
        installer::stub::{StubInstaller, StubPlan},
        metrics::StubMetricsReporter,
        policy::{CheckDecision, PolicyEngine, UpdateDecision},
        request_builder::RequestParams,
        state_machine::StateMachineBuilder,
        storage::MemStorage,
        time::{timers::InfiniteTimer, MockTimeSource, TimeSource},
    };
    use std::time::Duration;

    #[derive(Clone)]
    pub struct MockStateMachineController {
        result: Result<StartUpdateCheckResponse, StateMachineGone>,
    }

    impl MockStateMachineController {
        pub fn new(result: Result<StartUpdateCheckResponse, StateMachineGone>) -> Self {
            Self { result }
        }
    }

    impl StateMachineController for MockStateMachineController {
        fn start_update_check(
            &mut self,
            _options: CheckOptions,
        ) -> BoxFuture<'_, Result<StartUpdateCheckResponse, StateMachineGone>> {
            future::ready(self.result.clone()).boxed()
        }
    }

    #[derive(Clone)]
    pub enum MockOrRealStateMachineController {
        Mock(MockStateMachineController),
        Real(state_machine::ControlHandle),
    }

    impl StateMachineController for MockOrRealStateMachineController {
        fn start_update_check(
            &mut self,
            options: CheckOptions,
        ) -> BoxFuture<'_, Result<StartUpdateCheckResponse, StateMachineGone>> {
            match self {
                Self::Mock(mock) => mock.start_update_check(options),
                Self::Real(real) => real.start_update_check(options).boxed(),
            }
        }
    }

    pub type StubFidlServer = FidlServer<MemStorage, MockOrRealStateMachineController>;

    pub struct FidlServerBuilder {
        app_set: Option<FuchsiaAppSet>,
        channel_configs: Option<ChannelConfigs>,
        apps_node: Option<AppsNode>,
        state_node: Option<StateNode>,
        state_machine_control: Option<MockStateMachineController>,
        time_source: Option<MockTimeSource>,
        current_channel: Option<String>,
    }

    impl FidlServerBuilder {
        pub fn new() -> Self {
            Self {
                app_set: None,
                channel_configs: None,
                apps_node: None,
                state_node: None,
                state_machine_control: None,
                time_source: None,
                current_channel: None,
            }
        }
    }

    impl FidlServerBuilder {
        pub fn with_app_set(mut self, app_set: FuchsiaAppSet) -> Self {
            self.app_set = Some(app_set);
            self
        }

        pub fn with_apps_node(mut self, apps_node: AppsNode) -> Self {
            self.apps_node = Some(apps_node);
            self
        }

        pub fn with_state_node(mut self, state_node: StateNode) -> Self {
            self.state_node = Some(state_node);
            self
        }

        pub fn with_channel_configs(mut self, channel_configs: ChannelConfigs) -> Self {
            self.channel_configs = Some(channel_configs);
            self
        }

        pub fn state_machine_control(
            mut self,
            state_machine_control: MockStateMachineController,
        ) -> Self {
            self.state_machine_control = Some(state_machine_control);
            self
        }

        #[allow(dead_code)]
        pub fn time_source(mut self, time_source: MockTimeSource) -> Self {
            self.time_source = Some(time_source);
            self
        }

        pub fn with_current_channel(mut self, current_channel: Option<String>) -> Self {
            self.current_channel = current_channel;
            self
        }

        pub async fn build(self) -> Rc<RefCell<StubFidlServer>> {
            let config = configuration::get_config("0.1.2", None, None);
            let storage_ref = Rc::new(Mutex::new(MemStorage::new()));

            let cup_handler: Option<StandardCupv2Handler> =
                config.omaha_public_keys.as_ref().map(StandardCupv2Handler::new);

            let app_set = self.app_set.unwrap_or_else(|| {
                FuchsiaAppSet::new(
                    App::builder().id("id").version([1, 0]).build(),
                    AppMetadata { appid_source: AppIdSource::VbMetadata },
                )
            });
            let app_set = Rc::new(Mutex::new(app_set));
            let time_source = self.time_source.unwrap_or_else(MockTimeSource::new_from_now);
            // A state machine with only stub implementations never yields from a poll.
            // Configure the state machine to schedule automatic update checks in the future and
            // block timers forever so we can control when update checks happen.
            let (state_machine_control, state_machine) = StateMachineBuilder::new(
                MockPolicyEngine { time_source },
                StubHttpRequest,
                StubInstaller::default(),
                InfiniteTimer,
                StubMetricsReporter,
                Rc::clone(&storage_ref),
                config,
                Rc::clone(&app_set),
                cup_handler,
            )
            .start()
            .await;
            let inspector = Inspector::new();
            let root = inspector.root();

            let apps_node =
                self.apps_node.unwrap_or_else(|| AppsNode::new(root.create_child("apps")));
            let state_node =
                self.state_node.unwrap_or_else(|| StateNode::new(root.create_child("state")));
            let state_machine_control = match self.state_machine_control {
                Some(mock) => MockOrRealStateMachineController::Mock(mock),
                None => MockOrRealStateMachineController::Real(state_machine_control),
            };
            let fidl = Rc::new(RefCell::new(FidlServer::new(
                state_machine_control,
                storage_ref,
                Rc::clone(&app_set),
                apps_node,
                state_node,
                self.channel_configs,
                Box::new(StubApiMetricsReporter),
                self.current_channel,
            )));

            let schedule_node = ScheduleNode::new(root.create_child("schedule"));
            let protocol_state_node = ProtocolStateNode::new(root.create_child("protocol_state"));
            let last_results_node = LastResultsNode::new(root.create_child("last_results"));
            let platform_metrics_node = root.create_child("platform_metrics");

            let mut observer = FuchsiaObserver::new(
                Rc::clone(&fidl),
                schedule_node,
                protocol_state_node,
                last_results_node,
                app_set,
                true,
                platform_metrics_node,
            );
            fasync::Task::local(async move {
                futures::pin_mut!(state_machine);

                while let Some(event) = state_machine.next().await {
                    observer.on_event(event).await;
                }
            })
            .detach();

            fidl
        }
    }

    /// A mock PolicyEngine implementation that allows update checks with an interval of a few
    /// seconds.
    #[derive(Debug)]
    pub struct MockPolicyEngine {
        time_source: MockTimeSource,
    }

    impl PolicyEngine for MockPolicyEngine {
        type TimeSource = MockTimeSource;
        type InstallResult = ();
        type InstallPlan = StubPlan;

        fn time_source(&self) -> &Self::TimeSource {
            &self.time_source
        }

        fn compute_next_update_time(
            &mut self,
            _apps: &[App],
            _scheduling: &UpdateCheckSchedule,
            _protocol_state: &ProtocolState,
        ) -> BoxFuture<'_, CheckTiming> {
            let timing = CheckTiming::builder()
                .time(self.time_source.now() + Duration::from_secs(3))
                .build();
            future::ready(timing).boxed()
        }

        fn update_check_allowed(
            &mut self,
            _apps: &[App],
            _scheduling: &UpdateCheckSchedule,
            _protocol_state: &ProtocolState,
            check_options: &CheckOptions,
        ) -> BoxFuture<'_, CheckDecision> {
            future::ready(CheckDecision::Ok(RequestParams {
                source: check_options.source,
                use_configured_proxies: true,
                ..RequestParams::default()
            }))
            .boxed()
        }

        fn update_can_start<'p>(
            &mut self,
            _proposed_install_plan: &'p Self::InstallPlan,
        ) -> BoxFuture<'p, UpdateDecision> {
            future::ready(UpdateDecision::Ok).boxed()
        }

        fn reboot_allowed(
            &mut self,
            _check_options: &CheckOptions,
            _install_result: &Self::InstallResult,
        ) -> BoxFuture<'_, bool> {
            future::ready(true).boxed()
        }

        fn reboot_needed(&mut self, _install_plan: &Self::InstallPlan) -> BoxFuture<'_, bool> {
            future::ready(true).boxed()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::app_set::{AppIdSource, AppMetadata};
    use assert_matches::assert_matches;
    use channel_config::ChannelConfig;
    use fidl::endpoints::{create_proxy_and_stream, create_request_stream};
    use fidl_fuchsia_update::{
        self as update, AttemptsMonitorRequest, ManagerMarker, MonitorMarker, MonitorRequest,
        MonitorRequestStream,
    };
    use fidl_fuchsia_update_channel::ProviderMarker;
    use fidl_fuchsia_update_channelcontrol::ChannelControlMarker;
    use fuchsia_inspect::{assert_data_tree, Inspector};
    use omaha_client::{common::App, protocol::Cohort};

    fn spawn_fidl_server<M: fidl::endpoints::ProtocolMarker>(
        fidl: Rc<RefCell<stub::StubFidlServer>>,
        service: fn(M::RequestStream) -> IncomingServices,
    ) -> M::Proxy {
        let (proxy, stream) = create_proxy_and_stream::<M>().unwrap();
        fasync::Task::local(
            FidlServer::handle_client(fidl, service(stream)).unwrap_or_else(|e| panic!("{}", e)),
        )
        .detach();
        proxy
    }

    async fn next_n_on_state_events(
        mut request_stream: MonitorRequestStream,
        n: usize,
    ) -> Vec<update::State> {
        let mut v = Vec::with_capacity(n);
        for _ in 0..n {
            let MonitorRequest::OnState { state, responder } =
                request_stream.next().await.unwrap().unwrap();
            responder.send().unwrap();
            v.push(state);
        }
        v
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_on_state_change() {
        let fidl = FidlServerBuilder::new().build().await;
        FidlServer::on_state_change(
            Rc::clone(&fidl),
            state_machine::State::CheckingForUpdates(InstallSource::OnDemand),
        )
        .await;
        assert_eq!(
            state_machine::State::CheckingForUpdates(InstallSource::OnDemand),
            fidl.borrow().state.manager_state
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now() {
        let fidl = FidlServerBuilder::new().build().await;
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl, IncomingServices::Manager);
        let options = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: Some(false),
            ..update::CheckOptions::EMPTY
        };
        let result = proxy.check_now(options, None).await.unwrap();
        assert_matches!(result, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_attempts_monitor() {
        let fidl = FidlServerBuilder::new().build().await;
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl, IncomingServices::Manager);
        let options = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: Some(false),
            ..update::CheckOptions::EMPTY
        };
        let (client_end, mut request_stream) =
            fidl::endpoints::create_request_stream().expect("create_request_stream");
        assert_matches!(proxy.monitor_all_update_checks(client_end), Ok(()));
        assert_matches!(proxy.check_now(options, None).await.unwrap(), Ok(()));

        let AttemptsMonitorRequest::OnStart { options, monitor, responder } =
            request_stream.next().await.unwrap().unwrap();

        assert_matches!(responder.send(), Ok(()));
        assert_matches!(options.initiator, Some(fidl_fuchsia_update::Initiator::User));

        let events = next_n_on_state_events(monitor.into_stream().unwrap(), 2).await;
        assert_eq!(
            events,
            [
                update::State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
                update::State::ErrorCheckingForUpdate(ErrorCheckingForUpdateData::EMPTY),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_invalid_options() {
        let fidl = FidlServerBuilder::new().build().await;
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl, IncomingServices::Manager);
        let (client_end, mut stream) = create_request_stream::<MonitorMarker>().unwrap();
        let options = update::CheckOptions {
            initiator: None,
            allow_attaching_to_existing_update_check: None,
            ..update::CheckOptions::EMPTY
        };
        let result = proxy.check_now(options, Some(client_end)).await.unwrap();
        assert_matches!(result, Err(CheckNotStartedReason::InvalidOptions));
        assert_matches!(stream.next().await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_already_in_progress() {
        let fidl = FidlServerBuilder::new()
            .state_machine_control(MockStateMachineController::new(Ok(
                StartUpdateCheckResponse::AlreadyRunning,
            )))
            .build()
            .await;
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl, IncomingServices::Manager);
        let options = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: None,
            ..update::CheckOptions::EMPTY
        };
        let result = proxy.check_now(options, None).await.unwrap();
        assert_matches!(result, Err(CheckNotStartedReason::AlreadyInProgress));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_throttled() {
        let fidl = FidlServerBuilder::new()
            .state_machine_control(MockStateMachineController::new(Ok(
                StartUpdateCheckResponse::Throttled,
            )))
            .build()
            .await;
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl, IncomingServices::Manager);
        let options = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: None,
            ..update::CheckOptions::EMPTY
        };
        let result = proxy.check_now(options, None).await.unwrap();
        assert_matches!(result, Err(CheckNotStartedReason::Throttled));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_with_monitor() {
        let fidl = FidlServerBuilder::new().build().await;
        let proxy = spawn_fidl_server::<ManagerMarker>(Rc::clone(&fidl), IncomingServices::Manager);
        let (client_end, mut stream) = create_request_stream::<MonitorMarker>().unwrap();
        let options = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: Some(true),
            ..update::CheckOptions::EMPTY
        };
        let result = proxy.check_now(options, Some(client_end)).await.unwrap();
        assert_matches!(result, Ok(()));
        let expected_states = [
            update::State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
            update::State::ErrorCheckingForUpdate(ErrorCheckingForUpdateData::EMPTY),
        ];
        let mut expected_states = expected_states.iter();
        while let Some(event) = stream.try_next().await.unwrap() {
            match event {
                MonitorRequest::OnState { state, responder } => {
                    assert_eq!(Some(&state), expected_states.next());
                    responder.send().unwrap();
                }
            }
        }
        assert_eq!(None, expected_states.next());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_attempts_monitor_two_updates() {
        let fidl = FidlServerBuilder::new().build().await;
        let proxy = spawn_fidl_server::<ManagerMarker>(Rc::clone(&fidl), IncomingServices::Manager);

        let check_options_1 = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: Some(true),
            ..update::CheckOptions::EMPTY
        };

        let (attempt_client_end, mut attempt_request_stream) =
            fidl::endpoints::create_request_stream().expect("create_request_stream");
        assert_matches!(proxy.monitor_all_update_checks(attempt_client_end), Ok(()));
        assert_matches!(proxy.check_now(check_options_1, None).await.unwrap(), Ok(()));

        let AttemptsMonitorRequest::OnStart { options, monitor, responder } =
            attempt_request_stream.next().await.unwrap().unwrap();

        assert_matches!(responder.send(), Ok(()));
        assert_matches!(options.initiator, Some(fidl_fuchsia_update::Initiator::User));

        let events = next_n_on_state_events(monitor.into_stream().unwrap(), 2).await;
        assert_eq!(
            events,
            [
                update::State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
                update::State::ErrorCheckingForUpdate(ErrorCheckingForUpdateData::EMPTY),
            ]
        );

        // Check for a second update and see the results on the same attempts_monitor.
        let check_options_2 = update::CheckOptions {
            initiator: Some(Initiator::Service),
            allow_attaching_to_existing_update_check: Some(true),
            ..update::CheckOptions::EMPTY
        };
        assert_matches!(proxy.check_now(check_options_2, None).await.unwrap(), Ok(()));
        let AttemptsMonitorRequest::OnStart { options, monitor, responder } =
            attempt_request_stream.next().await.unwrap().unwrap();

        assert_matches!(responder.send(), Ok(()));
        assert_matches!(options.initiator, Some(fidl_fuchsia_update::Initiator::Service));

        let events = next_n_on_state_events(monitor.into_stream().unwrap(), 2).await;
        assert_eq!(
            events,
            [
                update::State::CheckingForUpdates(CheckingForUpdatesData::EMPTY),
                update::State::ErrorCheckingForUpdate(ErrorCheckingForUpdateData::EMPTY),
            ]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_with_closed_monitor() {
        let fidl = FidlServerBuilder::new().build().await;
        let proxy = spawn_fidl_server::<ManagerMarker>(Rc::clone(&fidl), IncomingServices::Manager);
        let (client_end, stream) = create_request_stream::<MonitorMarker>().unwrap();
        drop(stream);
        let options = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: Some(true),
            ..update::CheckOptions::EMPTY
        };
        let result = proxy.check_now(options, Some(client_end)).await.unwrap();
        assert_matches!(result, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_monitor_progress() {
        let fidl = FidlServerBuilder::new()
            .state_machine_control(MockStateMachineController::new(Ok(
                StartUpdateCheckResponse::Started,
            )))
            .build()
            .await;
        let proxy = spawn_fidl_server::<ManagerMarker>(Rc::clone(&fidl), IncomingServices::Manager);
        let (client_end, mut stream) = create_request_stream::<MonitorMarker>().unwrap();
        let options = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: Some(true),
            ..update::CheckOptions::EMPTY
        };
        let result = proxy.check_now(options, Some(client_end)).await.unwrap();
        assert_matches!(result, Ok(()));
        FidlServer::on_state_change(Rc::clone(&fidl), state_machine::State::InstallingUpdate).await;
        // Ignore the first InstallingUpdate state with no progress.
        let MonitorRequest::OnState { state: _, responder } =
            stream.try_next().await.unwrap().unwrap();
        responder.send().unwrap();

        let progresses = vec![0.0, 0.3, 0.9, 1.0];
        for &progress in &progresses {
            FidlServer::on_progress_change(
                Rc::clone(&fidl),
                state_machine::InstallProgress { progress },
            )
            .await;
            let MonitorRequest::OnState { state, responder } =
                stream.try_next().await.unwrap().unwrap();
            match state {
                update::State::InstallingUpdate(InstallingData {
                    update: _,
                    installation_progress,
                    ..
                }) => {
                    assert_eq!(installation_progress.unwrap().fraction_completed.unwrap(), progress)
                }
                state => panic!("unexpected state: {:?}", state),
            }
            responder.send().unwrap();
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_channel_from_app() {
        let app_set = FuchsiaAppSet::new(
            App::builder()
                .id("id")
                .version([1, 0])
                .cohort(Cohort { name: "current-channel".to_string().into(), ..Cohort::default() })
                .build(),
            AppMetadata { appid_source: AppIdSource::VbMetadata },
        );
        let fidl = FidlServerBuilder::new().with_app_set(app_set).build().await;

        let proxy =
            spawn_fidl_server::<ChannelControlMarker>(fidl, IncomingServices::ChannelControl);

        assert_eq!("current-channel", proxy.get_current().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_current_channel_from_constructor() {
        let fidl = FidlServerBuilder::new()
            .with_current_channel("current-channel".to_string().into())
            .build()
            .await;

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            Rc::clone(&fidl),
            IncomingServices::ChannelControl,
        );
        assert_eq!("current-channel", proxy.get_current().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_changing_target_doesnt_change_current_channel() {
        let fidl = FidlServerBuilder::new()
            .with_current_channel("current-channel".to_string().into())
            .build()
            .await;

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            Rc::clone(&fidl),
            IncomingServices::ChannelControl,
        );
        assert_eq!("current-channel", proxy.get_current().await.unwrap());

        let app_set = Rc::clone(&fidl.borrow().app_set);
        app_set.lock().await.set_system_target_channel(None, None);

        assert_eq!("current-channel", proxy.get_current().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_provider_get_channel_from_constructor() {
        let fidl = FidlServerBuilder::new()
            .with_current_channel("current-channel".to_string().into())
            .build()
            .await;

        let proxy = spawn_fidl_server::<ProviderMarker>(fidl, IncomingServices::ChannelProvider);

        assert_eq!("current-channel", proxy.get_current().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_provider_get_current_channel_from_app() {
        let app_set = FuchsiaAppSet::new(
            App::builder()
                .id("id")
                .version([1, 0])
                .cohort(Cohort { name: "current-channel".to_string().into(), ..Cohort::default() })
                .build(),
            AppMetadata { appid_source: AppIdSource::VbMetadata },
        );
        let fidl = FidlServerBuilder::new().with_app_set(app_set).build().await;

        let proxy = spawn_fidl_server::<ProviderMarker>(fidl, IncomingServices::ChannelProvider);

        assert_eq!("current-channel", proxy.get_current().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_changing_target_doesnt_change_current_channel_provider() {
        let fidl = FidlServerBuilder::new()
            .with_current_channel("current-channel".to_string().into())
            .build()
            .await;

        let proxy = spawn_fidl_server::<ProviderMarker>(
            Rc::clone(&fidl),
            IncomingServices::ChannelProvider,
        );

        assert_eq!("current-channel", proxy.get_current().await.unwrap());

        let app_set = Rc::clone(&fidl.borrow().app_set);
        app_set.lock().await.set_system_target_channel(None, None);

        assert_eq!("current-channel", proxy.get_current().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_target() {
        let app_metadata = AppMetadata { appid_source: AppIdSource::VbMetadata };
        let app_set = FuchsiaAppSet::new(
            App::builder()
                .id("id")
                .version([1, 0])
                .cohort(Cohort::from_hint("target-channel"))
                .build(),
            app_metadata,
        );
        let fidl = FidlServerBuilder::new().with_app_set(app_set).build().await;

        let proxy =
            spawn_fidl_server::<ChannelControlMarker>(fidl, IncomingServices::ChannelControl);
        assert_eq!("target-channel", proxy.get_target().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_target() {
        let fidl = FidlServerBuilder::new()
            .with_channel_configs(ChannelConfigs {
                default_channel: None,
                known_channels: vec![
                    ChannelConfig::new_for_test("some-channel"),
                    ChannelConfig::with_appid_for_test("target-channel", "target-id"),
                ],
            })
            .build()
            .await;

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            Rc::clone(&fidl),
            IncomingServices::ChannelControl,
        );
        proxy.set_target("target-channel").await.unwrap();

        let app_set = Rc::clone(&fidl.borrow().app_set);
        let apps = app_set.lock().await.get_apps();
        assert_eq!("target-channel", apps[0].get_target_channel());
        assert_eq!("target-id", apps[0].id);
        let storage = Rc::clone(&fidl.borrow().storage_ref);
        let storage = storage.lock().await;
        storage.get_string(&apps[0].id).await.unwrap();
        assert!(storage.committed());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_target_empty() {
        let fidl = FidlServerBuilder::new()
            .with_channel_configs(ChannelConfigs {
                default_channel: "default-channel".to_string().into(),
                known_channels: vec![ChannelConfig::with_appid_for_test(
                    "default-channel",
                    "default-app",
                )],
            })
            .build()
            .await;

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            Rc::clone(&fidl),
            IncomingServices::ChannelControl,
        );
        proxy.set_target("").await.unwrap();

        let app_set = Rc::clone(&fidl.borrow().app_set);
        let apps = app_set.lock().await.get_apps();
        assert_eq!("default-channel", apps[0].get_target_channel());
        assert_eq!("default-app", apps[0].id);
        let storage = Rc::clone(&fidl.borrow().storage_ref);
        let storage = storage.lock().await;
        // Default channel should not be persisted to storage.
        assert_eq!(None, storage.get_string(&apps[0].id).await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_target_no_op() {
        let app_metadata = AppMetadata { appid_source: AppIdSource::VbMetadata };
        let app_set = FuchsiaAppSet::new(
            App::builder()
                .id("id")
                .version([1, 0])
                .cohort(Cohort::from_hint("target-channel"))
                .build(),
            app_metadata,
        );
        let fidl = FidlServerBuilder::new().with_app_set(app_set).build().await;

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            Rc::clone(&fidl),
            IncomingServices::ChannelControl,
        );
        proxy.set_target("target-channel").await.unwrap();

        let app_set = Rc::clone(&fidl.borrow().app_set);
        let apps = app_set.lock().await.get_apps();
        assert_eq!("target-channel", apps[0].get_target_channel());
        let storage = Rc::clone(&fidl.borrow().storage_ref);
        let storage = storage.lock().await;
        // Verify that app is not persisted to storage.
        assert_eq!(storage.get_string(&apps[0].id).await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_target_list() {
        let fidl = FidlServerBuilder::new()
            .with_channel_configs(ChannelConfigs {
                default_channel: None,
                known_channels: vec![
                    ChannelConfig::new_for_test("some-channel"),
                    ChannelConfig::new_for_test("some-other-channel"),
                ],
            })
            .build()
            .await;

        let proxy =
            spawn_fidl_server::<ChannelControlMarker>(fidl, IncomingServices::ChannelControl);
        let response = proxy.get_target_list().await.unwrap();

        assert_eq!(2, response.len());
        assert!(response.contains(&"some-channel".to_string()));
        assert!(response.contains(&"some-other-channel".to_string()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_target_list_when_no_channels_configured() {
        let fidl = FidlServerBuilder::new().build().await;

        let proxy =
            spawn_fidl_server::<ChannelControlMarker>(fidl, IncomingServices::ChannelControl);
        let response = proxy.get_target_list().await.unwrap();

        assert!(response.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_apps_on_state_change() {
        for &state in &[state_machine::State::Idle, state_machine::State::WaitingForReboot] {
            let inspector = Inspector::new();
            let apps_node = AppsNode::new(inspector.root().create_child("apps"));
            let fidl = FidlServerBuilder::new().with_apps_node(apps_node).build().await;

            StubFidlServer::on_state_change(Rc::clone(&fidl), state).await;

            let app_set = Rc::clone(&fidl.borrow().app_set);
            assert_data_tree!(
                inspector,
                root: {
                    apps: {
                        apps: format!("{:?}", app_set.lock().await.get_apps()),
                        apps_metadata: "[(\"id\", AppMetadata { appid_source: VbMetadata })]".to_string(),
                    }
                }
            );
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_apps_on_channel_change() {
        let inspector = Inspector::new();
        let apps_node = AppsNode::new(inspector.root().create_child("apps"));
        let fidl = FidlServerBuilder::new()
            .with_apps_node(apps_node)
            .with_channel_configs(ChannelConfigs {
                default_channel: None,
                known_channels: vec![ChannelConfig::new_for_test("target-channel")],
            })
            .build()
            .await;

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            Rc::clone(&fidl),
            IncomingServices::ChannelControl,
        );
        proxy.set_target("target-channel").await.unwrap();

        let app_set = Rc::clone(&fidl.borrow().app_set);
        assert_data_tree!(
            inspector,
            root: {
                apps: {
                    apps: format!("{:?}", app_set.lock().await.get_apps()),
                    apps_metadata: "[(\"id\", AppMetadata { appid_source: VbMetadata })]".to_string(),
                }
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_state() {
        let inspector = Inspector::new();
        let state_node = StateNode::new(inspector.root().create_child("state"));
        let fidl = FidlServerBuilder::new().with_state_node(state_node).build().await;

        assert_data_tree!(
            inspector,
            root: {
                state: {
                    state: format!("{:?}", fidl.borrow().state),
                }
            }
        );

        StubFidlServer::on_state_change(Rc::clone(&fidl), state_machine::State::InstallingUpdate)
            .await;

        assert_data_tree!(
            inspector,
            root: {
                state: {
                    state: format!("{:?}", fidl.borrow().state),
                }
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_perform_pending_reboot_returns_false() {
        let fidl = FidlServerBuilder::new().build().await;
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl, IncomingServices::Manager);
        let result = proxy.perform_pending_reboot().await.unwrap();
        assert!(!result);
    }

    async fn assert_fidl_server_calls_reboot(
        fidl: Rc<
            RefCell<
                FidlServer<omaha_client::storage::MemStorage, MockOrRealStateMachineController>,
            >,
        >,
    ) {
        let (proxy, stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
        // Handling this request should fail because unit test can't access the Admin FIDL.
        // Don't use spawn_fidl_server to run this task, since that will panic on any errors.

        // Also, be very careful to assign the result of Task::local to a named variable
        // (i.e. not `_`). Results assigned to `_` are immediately dropped. In the case of a task,
        // that means the task might never run or might be canceled.
        let _task = fasync::Task::local(async move {
            let error = FidlServer::handle_client(fidl, IncomingServices::Manager(stream))
                .await
                .unwrap_err();

            // The only reason OMCL should have attempted to talk to this FIDL service was for a
            // reboot call, so this shows we actually attempted to call reboot.
            assert_matches!(
                error.downcast::<fidl::Error>().unwrap(),
                fidl::Error::ClientChannelClosed {
                    status: zx::Status::PEER_CLOSED,
                    protocol_name: "fuchsia.hardware.power.statecontrol.Admin"
                }
            );
        });
        assert_matches!(
            proxy.perform_pending_reboot().await,
            Err(fidl::Error::ClientChannelClosed {
                status: zx::Status::PEER_CLOSED,
                protocol_name: "fuchsia.update.Manager"
            })
        );
    }

    // When the state machine is in WaitingForReboot, a call to PerformPendingReboot should call
    // reboot
    #[fasync::run_singlethreaded(test)]
    async fn test_perform_pending_reboot_waiting_for_reboot() {
        let fidl = FidlServerBuilder::new().build().await;
        fidl.borrow_mut().state = State {
            manager_state: state_machine::State::WaitingForReboot,
            version_available: None,
            install_progress: None,
        };

        assert_fidl_server_calls_reboot(fidl).await;
    }

    // When the FidlServer has previous_out_of_space_error set to true, a call to
    // PerformPendingReboot should call reboot, even if StateMachine state is not WaitingForReboot
    #[fasync::run_singlethreaded(test)]
    async fn test_perform_pending_reboot_with_previous_out_of_space_error() {
        let fidl = FidlServerBuilder::new().build().await;
        fidl.borrow_mut().state = State {
            manager_state: state_machine::State::Idle,
            version_available: None,
            install_progress: None,
        };
        fidl.borrow_mut().previous_out_of_space_failure = true;

        assert_fidl_server_calls_reboot(fidl).await;
    }
}
