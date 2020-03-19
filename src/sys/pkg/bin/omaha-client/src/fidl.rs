// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    channel::ChannelConfigs,
    inspect::{AppsNode, StateNode},
};
use anyhow::{Context as _, Error};
use event_queue::{ClosedClient, ControlHandle, Event, EventQueue, Notify};
use fidl_fuchsia_update::{
    self as update, CheckNotStartedReason, CheckingForUpdatesData, ErrorCheckingForUpdateData,
    Initiator, InstallationDeferredData, InstallationErrorData, InstallationProgress,
    InstallingData, ManagerRequest, ManagerRequestStream, MonitorProxy, NoUpdateAvailableData,
    UpdateInfo,
};
use fidl_fuchsia_update_channel::{ProviderRequest, ProviderRequestStream};
use fidl_fuchsia_update_channelcontrol::{ChannelControlRequest, ChannelControlRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceObjLocal};
use futures::{future::BoxFuture, lock::Mutex, prelude::*};
use log::{error, info, warn};
use omaha_client::{
    common::{AppSet, CheckOptions},
    protocol::request::InstallSource,
    state_machine::{self, StartUpdateCheckResponse, StateMachineGone},
    storage::Storage,
};
use std::cell::RefCell;
use std::rc::Rc;
use sysconfig_client::{channel::OtaUpdateChannelConfig, SysconfigPartition};

#[cfg(not(test))]
use sysconfig_client::channel::write_channel_config;

#[cfg(test)]
fn write_channel_config(config: &OtaUpdateChannelConfig) -> Result<(), Error> {
    assert_eq!(config.channel_name(), "target-channel");
    assert_eq!(config.tuf_config_name(), "target-channel-repo");
    Ok(())
}

#[cfg(not(test))]
use sysconfig_client::write_partition;

#[cfg(test)]
fn write_partition(partition: SysconfigPartition, data: &[u8]) -> Result<(), Error> {
    assert_eq!(partition, SysconfigPartition::Config);
    assert_eq!(data, &[] as &[u8]);
    Ok(())
}

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

impl Notify<State> for StateNotifier {
    fn notify(&self, state: State) -> BoxFuture<'static, Result<(), ClosedClient>> {
        match state.into() {
            Some(mut state) => self
                .proxy
                .on_state(&mut state)
                .map(|result| result.map_err(|_| ClosedClient))
                .boxed(),
            None => future::ready(Ok(())).boxed(),
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

    monitor_queue: ControlHandle<StateNotifier, State>,
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
    ) -> Self {
        let state = State {
            manager_state: state_machine::State::Idle,
            version_available: None,
            install_progress: None,
        };
        state_node.set(&state);
        let (monitor_queue_fut, monitor_queue) = EventQueue::new();
        fasync::spawn_local(monitor_queue_fut);
        FidlServer {
            state_machine_control,
            storage_ref,
            app_set,
            apps_node,
            state_node,
            channel_configs,
            state,
            monitor_queue,
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
                info!("Received CheckNow request with {:?} and {:?}", options, monitor);

                let source = match options.initiator {
                    Some(Initiator::User) => InstallSource::OnDemand,
                    Some(Initiator::Service) => InstallSource::ScheduledTask,
                    None => {
                        responder
                            .send(&mut Err(CheckNotStartedReason::InvalidOptions))
                            .context("error sending response")?;
                        return Ok(());
                    }
                };

                // Attach the monitor if passed for current update.
                if let Some(monitor) = monitor {
                    if options.allow_attaching_to_existing_update_check == Some(true)
                        || server.borrow().state.manager_state == state_machine::State::Idle
                    {
                        let monitor_proxy = monitor.into_proxy()?;
                        let mut monitor_queue = server.borrow().monitor_queue.clone();
                        monitor_queue.add_client(StateNotifier { proxy: monitor_proxy }).await?;
                    }
                }

                let mut state_machine_control = server.borrow().state_machine_control.clone();

                let check_options = CheckOptions { source };

                let mut res = match state_machine_control.start_update_check(check_options).await {
                    Ok(StartUpdateCheckResponse::Started) => Ok(()),
                    Ok(StartUpdateCheckResponse::AlreadyRunning) => {
                        if options.allow_attaching_to_existing_update_check == Some(true) {
                            Ok(())
                        } else {
                            Err(CheckNotStartedReason::AlreadyInProgress)
                        }
                    }
                    Err(state_machine::StateMachineGone) => Err(CheckNotStartedReason::Internal),
                };

                responder.send(&mut res).context("error sending response")?;
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
                // TODO: Verify that channel is valid.
                let app_set = server.borrow().app_set.clone();
                if channel.is_empty() {
                    // TODO: Remove this when fxb/36608 is fixed.
                    warn!(
                        "Empty channel passed to SetTarget, erasing all channel data in SysConfig."
                    );
                    write_partition(SysconfigPartition::Config, &[])?;
                    let target_channel = match &server.borrow().channel_configs {
                        Some(channel_configs) => channel_configs.default_channel.clone(),
                        None => None,
                    };
                    app_set.set_target_channel(target_channel).await;
                } else {
                    let server = server.borrow();
                    let tuf_repo = if let Some(channel_configs) = &server.channel_configs {
                        if let Some(channel_config) = channel_configs
                            .known_channels
                            .iter()
                            .find(|channel_config| channel_config.name == channel)
                        {
                            &channel_config.repo
                        } else {
                            error!(
                                "Channel {} not found in known channels, using channel name as \
                                 TUF repo name.",
                                &channel
                            );
                            &channel
                        }
                    } else {
                        warn!("No channel configs found, using channel name as TUF repo name.");
                        &channel
                    };
                    let config = OtaUpdateChannelConfig::new(&channel, tuf_repo)?;
                    write_channel_config(&config)?;

                    let storage_ref = Rc::clone(&server.storage_ref);
                    // Don't borrow server across await.
                    drop(server);
                    let mut storage = storage_ref.lock().await;
                    app_set.set_target_channel(Some(channel)).await;
                    app_set.persist(&mut *storage).await;
                    if let Err(e) = storage.commit().await {
                        error!("Unable to commit target channel change: {}", e);
                    }
                }
                let app_vec = app_set.to_vec().await;
                server.borrow().apps_node.set(&app_vec);
                responder.send().context("error sending response")?;
            }
            ChannelControlRequest::GetTarget { responder } => {
                let app_set = server.borrow().app_set.clone();
                let channel = app_set.get_target_channel().await;
                responder.send(&channel).context("error sending response")?;
            }
            ChannelControlRequest::GetCurrent { responder } => {
                let app_set = server.borrow().app_set.clone();
                let channel = app_set.get_current_channel().await;
                responder.send(&channel).context("error sending response")?;
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
                    .context("error sending channel list response")?;
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
                let app_set = server.borrow().app_set.clone();
                let channel = app_set.get_current_channel().await;
                responder.send(&channel).context("error sending response")?;
            }
        }
        Ok(())
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

        if state == state_machine::State::Idle || state == state_machine::State::WaitingForReboot {
            // State is back to idle or waiting for reboot, clear the current update monitor handles.
            let mut monitor_queue = s.monitor_queue.clone();
            drop(s);
            if let Err(e) = monitor_queue.clear().await {
                warn!("error clearing clients of monitor_queue: {:?}", e);
            }

            // The state machine might make changes to apps only when state changes to `Idle` or
            // `WaitingForReboot`, update the apps node in inspect.
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
pub use stub::{FidlServerBuilder, StubFidlServer, StubStateMachineController};

#[cfg(test)]
mod stub {
    use super::*;
    use crate::{
        configuration,
        inspect::{LastResultsNode, ProtocolStateNode, ScheduleNode},
        observer::FuchsiaObserver,
    };
    use fuchsia_inspect::Inspector;
    use futures::future::BoxFuture;
    use omaha_client::{
        clock,
        common::{App, ProtocolState, UpdateCheckSchedule},
        http_request::StubHttpRequest,
        installer::{stub::StubInstaller, Plan},
        metrics::StubMetricsReporter,
        policy::{CheckDecision, PolicyEngine, UpdateDecision},
        protocol::Cohort,
        request_builder::RequestParams,
        state_machine::{timer::InfiniteTimer, StateMachineBuilder},
        storage::MemStorage,
    };
    use std::time::Duration;

    #[derive(Clone)]
    pub struct StubStateMachineController;

    impl StateMachineController for StubStateMachineController {
        fn start_update_check(
            &mut self,
            _options: CheckOptions,
        ) -> BoxFuture<'_, Result<StartUpdateCheckResponse, StateMachineGone>> {
            future::ready(Ok(StartUpdateCheckResponse::Started)).boxed()
        }
    }

    #[derive(Clone)]
    pub enum StubOrRealStateMachineController {
        Stub(StubStateMachineController),
        Real(state_machine::ControlHandle),
    }

    impl StateMachineController for StubOrRealStateMachineController {
        fn start_update_check(
            &mut self,
            options: CheckOptions,
        ) -> BoxFuture<'_, Result<StartUpdateCheckResponse, StateMachineGone>> {
            match self {
                Self::Stub(stub) => stub.start_update_check(options),
                Self::Real(real) => real.start_update_check(options).boxed(),
            }
        }
    }

    pub type StubFidlServer = FidlServer<MemStorage, StubOrRealStateMachineController>;

    pub struct FidlServerBuilder {
        apps: Vec<App>,
        channel_configs: Option<ChannelConfigs>,
        apps_node: Option<AppsNode>,
        state_node: Option<StateNode>,
        allow_update_check: bool,
        state_machine_control: Option<StubStateMachineController>,
    }

    impl FidlServerBuilder {
        pub fn new() -> Self {
            Self {
                apps: Vec::new(),
                channel_configs: None,
                apps_node: None,
                state_node: None,
                allow_update_check: true,
                state_machine_control: None,
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

        pub fn allow_update_check(mut self, allow_update_check: bool) -> Self {
            self.allow_update_check = allow_update_check;
            self
        }

        pub fn state_machine_control(
            mut self,
            state_machine_control: StubStateMachineController,
        ) -> Self {
            self.state_machine_control = Some(state_machine_control);
            self
        }

        pub async fn build(self) -> Rc<RefCell<StubFidlServer>> {
            let config = configuration::get_config("0.1.2");
            let storage_ref = Rc::new(Mutex::new(MemStorage::new()));
            let app_set = if self.apps.is_empty() {
                AppSet::new(vec![App::new("id", [1, 0], Cohort::default())])
            } else {
                AppSet::new(self.apps)
            };
            // A state machine with only stub implementations never yields from a poll.
            // Configure the state machine to schedule automatic update checks in the future and
            // block timers forever so we can control when update checks happen.
            let (state_machine_control, state_machine) = StateMachineBuilder::new(
                MockPolicyEngine { allow_update_check: self.allow_update_check },
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
                Some(stub) => StubOrRealStateMachineController::Stub(stub),
                None => StubOrRealStateMachineController::Real(state_machine_control),
            };
            let fidl = Rc::new(RefCell::new(FidlServer::new(
                state_machine_control,
                storage_ref,
                app_set.clone(),
                apps_node,
                state_node,
                self.channel_configs,
            )));

            let schedule_node = ScheduleNode::new(root.create_child("schedule"));
            let protocol_state_node = ProtocolStateNode::new(root.create_child("protocol_state"));
            let last_results_node = LastResultsNode::new(root.create_child("last_results"));

            let mut observer = FuchsiaObserver::new(
                Rc::clone(&fidl),
                schedule_node,
                protocol_state_node,
                last_results_node,
                app_set,
                true,
            );
            fasync::spawn_local(async move {
                futures::pin_mut!(state_machine);

                while let Some(event) = state_machine.next().await {
                    observer.on_event(event).await;
                }
            });

            fidl
        }
    }

    /// A mock PolicyEngine implementation that allows update checks with an interval of a few
    /// seconds.
    #[derive(Debug)]
    pub struct MockPolicyEngine {
        allow_update_check: bool,
    }

    impl PolicyEngine for MockPolicyEngine {
        fn compute_next_update_time(
            &mut self,
            _apps: &[App],
            scheduling: &UpdateCheckSchedule,
            _protocol_state: &ProtocolState,
        ) -> BoxFuture<'_, UpdateCheckSchedule> {
            let schedule = UpdateCheckSchedule {
                last_update_time: scheduling.last_update_time,
                next_update_window_start: clock::now() + Duration::from_secs(3),
                next_update_time: clock::now() + Duration::from_secs(3),
            };
            future::ready(schedule).boxed()
        }

        fn update_check_allowed(
            &mut self,
            _apps: &[App],
            _scheduling: &UpdateCheckSchedule,
            _protocol_state: &ProtocolState,
            check_options: &CheckOptions,
        ) -> BoxFuture<'_, CheckDecision> {
            if self.allow_update_check {
                future::ready(CheckDecision::Ok(RequestParams {
                    source: check_options.source.clone(),
                    use_configured_proxies: true,
                }))
                .boxed()
            } else {
                future::pending().boxed()
            }
        }

        fn update_can_start(
            &mut self,
            _proposed_install_plan: &impl Plan,
        ) -> BoxFuture<'_, UpdateDecision> {
            future::ready(UpdateDecision::Ok).boxed()
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
        fasync::spawn_local(
            FidlServer::handle_client(fidl, service(stream)).unwrap_or_else(|e| panic!(e)),
        );
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
        let fidl = FidlServerBuilder::new().allow_update_check(false).build().await;
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl, IncomingServices::Manager);
        let options = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: None,
        };
        let result = proxy.check_now(options, None).await.unwrap();
        assert_matches!(result, Ok(()));
        let options = update::CheckOptions {
            initiator: Some(Initiator::User),
            allow_attaching_to_existing_update_check: None,
        };
        let result = proxy.check_now(options, None).await.unwrap();
        assert_matches!(result, Err(CheckNotStartedReason::AlreadyInProgress));
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
    async fn test_monitor_progress() {
        let fidl = FidlServerBuilder::new()
            .state_machine_control(StubStateMachineController)
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
    async fn test_get_channel() {
        let apps = vec![App::new(
            "id",
            [1, 0],
            Cohort { name: Some("current-channel".to_string()), ..Cohort::default() },
        )];
        let fidl = FidlServerBuilder::new().with_apps(apps).build().await;

        let proxy =
            spawn_fidl_server::<ChannelControlMarker>(fidl, IncomingServices::ChannelControl);

        assert_eq!("current-channel", proxy.get_current().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_provider_get_channel() {
        let apps = vec![App::new(
            "id",
            [1, 0],
            Cohort { name: Some("current-channel".to_string()), ..Cohort::default() },
        )];
        let fidl = FidlServerBuilder::new().with_apps(apps).build().await;

        let proxy = spawn_fidl_server::<ProviderMarker>(fidl, IncomingServices::ChannelProvider);

        assert_eq!("current-channel", proxy.get_current().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_target() {
        let apps = vec![App::new("id", [1, 0], Cohort::from_hint("target-channel"))];
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
                    ChannelConfig::new("target-channel"),
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
        let storage = fidl.storage_ref.lock().await;
        storage.get_string(&apps[0].id).await.unwrap();
        assert!(storage.committed());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_target_empty() {
        let fidl = FidlServerBuilder::new()
            .with_channel_configs(ChannelConfigs {
                default_channel: Some("default-channel".to_string()),
                known_channels: vec![],
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
        let storage = fidl.storage_ref.lock().await;
        // Default channel should not be persisted to storage.
        assert_eq!(None, storage.get_string(&apps[0].id).await);
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
}
