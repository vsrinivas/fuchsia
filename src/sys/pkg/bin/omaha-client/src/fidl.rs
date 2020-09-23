// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    api_metrics::{ApiEvent, ApiMetricsReporter},
    channel::ChannelConfigs,
    inspect::{AppsNode, StateNode},
};
use anyhow::{Context as _, Error};
use event_queue::{ClosedClient, ControlHandle, Event, EventQueue, Notify};
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_hardware_power_statecontrol::RebootReason;
use fidl_fuchsia_update::{
    self as update, CheckNotStartedReason, CheckingForUpdatesData, ErrorCheckingForUpdateData,
    Initiator, InstallationDeferredData, InstallationErrorData, InstallationProgress,
    InstallingData, ManagerRequest, ManagerRequestStream, MonitorMarker, MonitorProxy,
    MonitorProxyInterface, NoUpdateAvailableData, UpdateInfo,
};
use fidl_fuchsia_update_channel::{ProviderRequest, ProviderRequestStream};
use fidl_fuchsia_update_channelcontrol::{ChannelControlRequest, ChannelControlRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::{
    client::connect_to_service,
    server::{ServiceFs, ServiceObjLocal},
};
use fuchsia_zircon as zx;
use futures::{future::BoxFuture, lock::Mutex, prelude::*};
use log::{error, info, warn};
use omaha_client::{
    common::{AppSet, CheckOptions},
    protocol::request::InstallSource,
    state_machine::{self, StartUpdateCheckResponse, StateMachineGone},
    storage::{Storage, StorageExt},
};
use std::cell::RefCell;
use std::rc::Rc;

#[derive(Debug, Clone, PartialEq)]
pub struct State {
    pub manager_state: state_machine::State,
    pub version_available: Option<String>,
    pub install_progress: Option<f32>,
}

impl From<State> for Option<update::State> {
    fn from(state: State) -> Self {
        let update =
            Some(UpdateInfo { version_available: state.version_available, download_size: None });
        let installation_progress =
            Some(InstallationProgress { fraction_completed: state.install_progress });
        match state.manager_state {
            state_machine::State::Idle => None,
            state_machine::State::CheckingForUpdates => {
                Some(update::State::CheckingForUpdates(CheckingForUpdatesData {}))
            }
            state_machine::State::ErrorCheckingForUpdate => {
                Some(update::State::ErrorCheckingForUpdate(ErrorCheckingForUpdateData {}))
            }
            state_machine::State::NoUpdateAvailable => {
                Some(update::State::NoUpdateAvailable(NoUpdateAvailableData {}))
            }
            state_machine::State::InstallationDeferredByPolicy => {
                Some(update::State::InstallationDeferredByPolicy(InstallationDeferredData {
                    update,
                }))
            }
            state_machine::State::InstallingUpdate => {
                Some(update::State::InstallingUpdate(InstallingData {
                    update,
                    installation_progress,
                }))
            }
            state_machine::State::WaitingForReboot => {
                Some(update::State::WaitingForReboot(InstallingData {
                    update,
                    installation_progress,
                }))
            }
            state_machine::State::InstallationError => {
                Some(update::State::InstallationError(InstallationErrorData {
                    update,
                    installation_progress,
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

    app_set: AppSet,

    apps_node: AppsNode,

    state_node: StateNode,

    channel_configs: Option<ChannelConfigs>,

    // The current State, this is the internal representation of the fuchsia.update/State.
    state: State,

    monitor_queue: ControlHandle<StateNotifier>,

    metrics_reporter: Box<dyn ApiMetricsReporter>,

    current_channel: Option<String>,
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
        app_set: AppSet,
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
        let (monitor_queue_fut, monitor_queue) = EventQueue::new();
        fasync::Task::local(monitor_queue_fut).detach();
        FidlServer {
            state_machine_control,
            storage_ref,
            app_set,
            apps_node,
            state_node,
            channel_configs,
            state,
            monitor_queue,
            metrics_reporter,
            current_channel,
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
                info!("Received PerformPendingRebootRequest");
                if server.borrow().state.manager_state == state_machine::State::WaitingForReboot {
                    connect_to_service::<fidl_fuchsia_hardware_power_statecontrol::AdminMarker>()?
                        .reboot(RebootReason::SystemUpdate)
                        .await?
                        .map_err(zx::Status::from_raw)
                        .context("reboot error")?;
                    responder.send(true)?;
                } else {
                    responder.send(false)?;
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
                let app_set = server.borrow().app_set.clone();
                let channel = app_set.get_target_channel().await;
                responder
                    .send(&channel)
                    .context("error sending GetTarget response from ChannelControl")?;
            }
            ChannelControlRequest::GetCurrent { responder } => {
                let (current_channel, app_set) = {
                    let server = server.borrow();
                    (server.current_channel.clone(), server.app_set.clone()) // server borrow is dropped
                };
                let channel = match current_channel {
                    Some(channel) => channel.to_string(),
                    None => app_set.get_current_channel().await,
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
                    .send(&mut channel_names.iter().copied())
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
                    (server.current_channel.clone(), server.app_set.clone()) // server borrow is dropped
                };
                let channel = match current_channel {
                    Some(channel) => channel.to_string(),
                    None => app_set.get_current_channel().await,
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

        // Attach the monitor if passed for current update.
        if let Some(monitor) = monitor {
            if options.allow_attaching_to_existing_update_check == Some(true)
                || server.borrow().state.manager_state == state_machine::State::Idle
            {
                let monitor_proxy = monitor.into_proxy().map_err(|e| {
                    error!("error getting proxy from monitor: {:?}", e);
                    CheckNotStartedReason::InvalidOptions
                })?;
                let mut monitor_queue = server.borrow().monitor_queue.clone();
                monitor_queue.add_client(StateNotifier { proxy: monitor_proxy }).await.map_err(
                    |e| {
                        error!("error adding client to monitor_queue: {:?}", e);
                        CheckNotStartedReason::Internal
                    },
                )?;
            }
        }

        let mut state_machine_control = server.borrow().state_machine_control.clone();

        let check_options = CheckOptions { source };

        match state_machine_control.start_update_check(check_options).await {
            Ok(StartUpdateCheckResponse::Started) => Ok(()),
            Ok(StartUpdateCheckResponse::AlreadyRunning) => {
                if options.allow_attaching_to_existing_update_check == Some(true) {
                    Ok(())
                } else {
                    Err(CheckNotStartedReason::AlreadyInProgress)
                }
            }
            Ok(StartUpdateCheckResponse::Throttled) => Err(CheckNotStartedReason::Throttled),
            Err(state_machine::StateMachineGone) => Err(CheckNotStartedReason::Internal),
        }
    }

    async fn handle_set_target(server: Rc<RefCell<Self>>, channel: String) {
        // TODO: Verify that channel is valid.
        let app_set = server.borrow().app_set.clone();
        let target_channel = app_set.get_target_channel().await;
        if channel.is_empty() {
            let default_channel_cfg = match &server.borrow().channel_configs {
                Some(cfgs) => cfgs.get_default_channel(),
                None => None,
            };
            let (channel_name, appid) = match default_channel_cfg {
                Some(cfg) => (Some(cfg.name), cfg.appid.clone()),
                None => (None, None),
            };
            if let Some(name) = &channel_name {
                // If the default channel is the same as the target channel, then this is a no-op.
                if name == &target_channel {
                    return;
                }
                warn!("setting device to default channel: '{}' with app id: '{:?}'", name, appid);
            }
            // TODO(fxb/58887): only OTA that follows can change the current channel.
            // Simplify this logic.
            app_set.set_target_channel(channel_name, appid).await;
        } else {
            // If the new target channel is the same as the existing target channel, then this is
            // a no-op.
            if channel == target_channel {
                return;
            }
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

            if let Some(id) = &appid {
                if id != &app_set.get_current_app_id().await {
                    warn!("Changing app id to: {}", id);
                }
            }

            app_set.set_target_channel(Some(channel), appid).await;
            app_set.persist(&mut *storage).await;
            storage.commit_or_log().await;
        }
        let app_vec = app_set.to_vec().await;
        server.borrow().apps_node.set(&app_vec);
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

        let s = server.borrow();
        s.state_node.set(&s.state);

        if state == state_machine::State::Idle {
            // State is back to idle, clear the current update monitor handles.
            let mut monitor_queue = s.monitor_queue.clone();
            drop(s);
            if let Err(e) = monitor_queue.clear().await {
                warn!("error clearing clients of monitor_queue: {:?}", e);
            }

            // The state machine might make changes to apps only when state changes to `Idle`,
            // update the apps node in inspect.
            let app_set = server.borrow().app_set.clone();
            let app_set = app_set.to_vec().await;
            server.borrow().apps_node.set(&app_set);
        }
    }

    async fn send_state_to_queue(server: Rc<RefCell<Self>>) {
        let server = server.borrow();
        let mut monitor_queue = server.monitor_queue.clone();
        let state = server.state.clone();
        drop(server);
        if let Err(e) = monitor_queue.queue_event(state).await {
            warn!("error sending state to monitor_queue: {:?}", e)
        }
    }

    pub async fn on_progress_change(
        server: Rc<RefCell<Self>>,
        progress: state_machine::InstallProgress,
    ) {
        server.borrow_mut().state.install_progress = Some(progress.progress);
        Self::send_state_to_queue(server).await;
    }
}

#[cfg(test)]
pub use stub::{
    FidlServerBuilder, MockOrRealStateMachineController, MockStateMachineController, StubFidlServer,
};

#[cfg(test)]
mod stub {
    use super::*;
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
        http_request::StubHttpRequest,
        installer::{stub::StubInstaller, Plan},
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
        apps: Vec<App>,
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
                apps: Vec::new(),
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
        pub fn with_apps(mut self, mut apps: Vec<App>) -> Self {
            self.apps.append(&mut apps);
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
            self.current_channel = current_channel.into();
            self
        }

        pub async fn build(self) -> Rc<RefCell<StubFidlServer>> {
            let config = configuration::get_config("0.1.2").await;
            let storage_ref = Rc::new(Mutex::new(MemStorage::new()));
            let app_set = if self.apps.is_empty() {
                AppSet::new(vec![App::builder("id", [1, 0]).build()])
            } else {
                AppSet::new(self.apps)
            };
            let time_source = self.time_source.unwrap_or(MockTimeSource::new_from_now());
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
                app_set.clone(),
            )
            .start()
            .await;
            let inspector = Inspector::new();
            let root = inspector.root();

            let apps_node = self.apps_node.unwrap_or(AppsNode::new(root.create_child("apps")));
            let state_node = self.state_node.unwrap_or(StateNode::new(root.create_child("state")));
            let state_machine_control = match self.state_machine_control {
                Some(mock) => MockOrRealStateMachineController::Mock(mock),
                None => MockOrRealStateMachineController::Real(state_machine_control),
            };
            let fidl = Rc::new(RefCell::new(FidlServer::new(
                state_machine_control,
                storage_ref,
                app_set.clone(),
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
                source: check_options.source.clone(),
                use_configured_proxies: true,
            }))
            .boxed()
        }

        fn update_can_start(
            &mut self,
            _proposed_install_plan: &impl Plan,
        ) -> BoxFuture<'_, UpdateDecision> {
            future::ready(UpdateDecision::Ok).boxed()
        }

        fn reboot_allowed(&mut self, _check_options: &CheckOptions) -> BoxFuture<'_, bool> {
            future::ready(true).boxed()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::channel::ChannelConfig;
    use fidl::endpoints::{create_proxy_and_stream, create_request_stream};
    use fidl_fuchsia_update::{self as update, ManagerMarker, MonitorMarker, MonitorRequest};
    use fidl_fuchsia_update_channel::ProviderMarker;
    use fidl_fuchsia_update_channelcontrol::ChannelControlMarker;
    use fuchsia_inspect::{assert_inspect_tree, Inspector};
    use matches::assert_matches;
    use omaha_client::{common::App, protocol::Cohort};

    fn spawn_fidl_server<M: fidl::endpoints::ServiceMarker>(
        fidl: Rc<RefCell<stub::StubFidlServer>>,
        service: fn(M::RequestStream) -> IncomingServices,
    ) -> M::Proxy {
        let (proxy, stream) = create_proxy_and_stream::<M>().unwrap();
        fasync::Task::local(
            FidlServer::handle_client(fidl, service(stream)).unwrap_or_else(|e| panic!(e)),
        )
        .detach();
        proxy
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_on_state_change() {
        let fidl = FidlServerBuilder::new().build().await;
        FidlServer::on_state_change(Rc::clone(&fidl), state_machine::State::CheckingForUpdates)
            .await;
        assert_eq!(state_machine::State::CheckingForUpdates, fidl.borrow().state.manager_state);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now() {
        let fidl = FidlServerBuilder::new().build().await;
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl, IncomingServices::Manager);
        let options = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: Some(false),
        };
        let result = proxy.check_now(options, None).await.unwrap();
        assert_matches!(result, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_invalid_options() {
        let fidl = FidlServerBuilder::new().build().await;
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl, IncomingServices::Manager);
        let (client_end, mut stream) = create_request_stream::<MonitorMarker>().unwrap();
        let options = update::CheckOptions {
            initiator: None,
            allow_attaching_to_existing_update_check: None,
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
        };
        let result = proxy.check_now(options, Some(client_end)).await.unwrap();
        assert_matches!(result, Ok(()));
        let mut expected_states = [
            update::State::CheckingForUpdates(CheckingForUpdatesData {}),
            update::State::ErrorCheckingForUpdate(ErrorCheckingForUpdateData {}),
        ]
        .iter();
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
    async fn test_check_now_with_closed_monitor() {
        let fidl = FidlServerBuilder::new().build().await;
        let proxy = spawn_fidl_server::<ManagerMarker>(Rc::clone(&fidl), IncomingServices::Manager);
        let (client_end, stream) = create_request_stream::<MonitorMarker>().unwrap();
        drop(stream);
        let options = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: Some(true),
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
        let apps = vec![App::builder("id", [1, 0])
            .with_cohort(Cohort { name: "current-channel".to_string().into(), ..Cohort::default() })
            .build()];
        let fidl = FidlServerBuilder::new().with_apps(apps).build().await;

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

        let fidl = fidl.borrow();
        fidl.app_set.set_target_channel(None, None).await;

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
        let apps = vec![App::builder("id", [1, 0])
            .with_cohort(Cohort { name: "current-channel".to_string().into(), ..Cohort::default() })
            .build()];
        let fidl = FidlServerBuilder::new().with_apps(apps).build().await;

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

        let fidl = fidl.borrow();
        fidl.app_set.set_target_channel(None, None).await;

        assert_eq!("current-channel", proxy.get_current().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_target() {
        let apps = vec![App::builder("id", [1, 0])
            .with_cohort(Cohort::from_hint("target-channel"))
            .build()];
        let fidl = FidlServerBuilder::new().with_apps(apps).build().await;

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
                    ChannelConfig::new("some-channel"),
                    ChannelConfig::with_appid("target-channel", "target-id"),
                ],
            })
            .build()
            .await;

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            Rc::clone(&fidl),
            IncomingServices::ChannelControl,
        );
        proxy.set_target("target-channel").await.unwrap();
        let fidl = fidl.borrow();
        let apps = fidl.app_set.to_vec().await;
        assert_eq!("target-channel", apps[0].get_target_channel());
        assert_eq!("target-id", apps[0].id);
        let storage = fidl.storage_ref.lock().await;
        storage.get_string(&apps[0].id).await.unwrap();
        assert!(storage.committed());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_target_empty() {
        let fidl = FidlServerBuilder::new()
            .with_channel_configs(ChannelConfigs {
                default_channel: "default-channel".to_string().into(),
                known_channels: vec![ChannelConfig::with_appid("default-channel", "default-app")],
            })
            .build()
            .await;

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            Rc::clone(&fidl),
            IncomingServices::ChannelControl,
        );
        proxy.set_target("").await.unwrap();
        let fidl = fidl.borrow();
        let apps = fidl.app_set.to_vec().await;
        assert_eq!("default-channel", apps[0].get_target_channel());
        assert_eq!("default-app", apps[0].id);
        let storage = fidl.storage_ref.lock().await;
        // Default channel should not be persisted to storage.
        assert_eq!(None, storage.get_string(&apps[0].id).await);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_target_no_op() {
        let apps = vec![App::builder("id", [1, 0])
            .with_cohort(Cohort::from_hint("target-channel"))
            .build()];
        let fidl = FidlServerBuilder::new().with_apps(apps).build().await;

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            Rc::clone(&fidl),
            IncomingServices::ChannelControl,
        );
        proxy.set_target("target-channel").await.unwrap();
        let fidl = fidl.borrow();
        let apps = fidl.app_set.to_vec().await;
        assert_eq!("target-channel", apps[0].get_target_channel());
        let storage = fidl.storage_ref.lock().await;
        // Verify that app is not persisted to storage.
        assert_eq!(storage.get_string(&apps[0].id).await, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_target_list() {
        let fidl = FidlServerBuilder::new()
            .with_channel_configs(ChannelConfigs {
                default_channel: None,
                known_channels: vec![
                    ChannelConfig::new("some-channel"),
                    ChannelConfig::new("some-other-channel"),
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
        let inspector = Inspector::new();
        let apps_node = AppsNode::new(inspector.root().create_child("apps"));
        let fidl = FidlServerBuilder::new().with_apps_node(apps_node).build().await;

        StubFidlServer::on_state_change(Rc::clone(&fidl), state_machine::State::Idle).await;

        let app_set = fidl.borrow().app_set.clone();
        assert_inspect_tree!(
            inspector,
            root: {
                apps: {
                    apps: format!("{:?}", app_set.to_vec().await),
                }
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_apps_on_channel_change() {
        let inspector = Inspector::new();
        let apps_node = AppsNode::new(inspector.root().create_child("apps"));
        let fidl = FidlServerBuilder::new()
            .with_apps_node(apps_node)
            .with_channel_configs(ChannelConfigs {
                default_channel: None,
                known_channels: vec![ChannelConfig::new("target-channel")],
            })
            .build()
            .await;

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            Rc::clone(&fidl),
            IncomingServices::ChannelControl,
        );
        proxy.set_target("target-channel").await.unwrap();
        let fidl = fidl.borrow();

        assert_inspect_tree!(
            inspector,
            root: {
                apps: {
                    apps: format!("{:?}", fidl.app_set.to_vec().await),
                }
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_state() {
        let inspector = Inspector::new();
        let state_node = StateNode::new(inspector.root().create_child("state"));
        let fidl = FidlServerBuilder::new().with_state_node(state_node).build().await;

        assert_inspect_tree!(
            inspector,
            root: {
                state: {
                    state: format!("{:?}", fidl.borrow().state),
                }
            }
        );

        StubFidlServer::on_state_change(Rc::clone(&fidl), state_machine::State::InstallingUpdate)
            .await;

        assert_inspect_tree!(
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
        assert_eq!(result, false);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_perform_pending_reboot_waiting_for_reboot() {
        let fidl = FidlServerBuilder::new().build().await;
        fidl.borrow_mut().state = State {
            manager_state: state_machine::State::WaitingForReboot,
            version_available: None,
            install_progress: None,
        };
        let (proxy, stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
        // It will fail because unit test can't access the Admin FIDL.
        let _ = fasync::Task::local(async move {
            assert_matches!(
                FidlServer::handle_client(fidl, IncomingServices::Manager(stream)).await,
                Err(_)
            );
        });
        assert_matches!(proxy.perform_pending_reboot().await, Err(_));
    }
}
