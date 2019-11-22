// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    channel::ChannelConfigs,
    inspect::{AppsNode, ProtocolStateNode, ScheduleNode, StateNode},
    observer::FuchsiaObserver,
};
use failure::{bail, Error, ResultExt};
use fidl_fuchsia_update::{
    CheckStartedResult, Initiator, ManagerRequest, ManagerRequestStream, ManagerState,
    MonitorControlHandle, State,
};
use fidl_fuchsia_update_channelcontrol::{ChannelControlRequest, ChannelControlRequestStream};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceObjLocal};
use futures::{lock::Mutex, prelude::*};
use log::{error, info, warn};
use omaha_client::{
    common::{AppSet, CheckOptions},
    http_request::HttpRequest,
    installer::Installer,
    metrics::MetricsReporter,
    policy::PolicyEngine,
    protocol::request::InstallSource,
    state_machine::{self, StateMachine, Timer},
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

pub struct FidlServer<PE, HR, IN, TM, MR, ST>
where
    PE: PolicyEngine,
    HR: HttpRequest,
    IN: Installer,
    TM: Timer,
    MR: MetricsReporter,
    ST: Storage,
{
    state_machine_ref: Rc<RefCell<StateMachine<PE, HR, IN, TM, MR, ST>>>,

    storage_ref: Rc<Mutex<ST>>,

    app_set: AppSet,

    apps_node: AppsNode,

    state_node: StateNode,

    channel_configs: Option<ChannelConfigs>,

    // The current State table, defined in fuchsia.update.fidl.
    state: State,

    /// The monitor handles for monitoring all updates.
    monitor_handles: Vec<MonitorControlHandle>,

    /// The monitor handles for monitoring the current update only, will be cleared after each
    /// update.
    current_monitor_handles: Vec<MonitorControlHandle>,
}

pub enum IncomingServices {
    Manager(ManagerRequestStream),
    ChannelControl(ChannelControlRequestStream),
}

impl<PE, HR, IN, TM, MR, ST> FidlServer<PE, HR, IN, TM, MR, ST>
where
    PE: PolicyEngine + 'static,
    HR: HttpRequest + 'static,
    IN: Installer + 'static,
    TM: Timer + 'static,
    MR: MetricsReporter + 'static,
    ST: Storage + 'static,
{
    pub fn new(
        state_machine_ref: Rc<RefCell<StateMachine<PE, HR, IN, TM, MR, ST>>>,
        storage_ref: Rc<Mutex<ST>>,
        app_set: AppSet,
        apps_node: AppsNode,
        state_node: StateNode,
        channel_configs: Option<ChannelConfigs>,
    ) -> Self {
        let state = State { state: Some(ManagerState::Idle), version_available: None };
        state_node.set(&state);
        FidlServer {
            state_machine_ref,
            storage_ref,
            app_set,
            apps_node,
            state_node,
            channel_configs,
            state,
            monitor_handles: vec![],
            current_monitor_handles: vec![],
        }
    }

    /// Starts the FIDL Server and the StateMachine.
    pub async fn start(
        self,
        mut fs: ServiceFs<ServiceObjLocal<'_, IncomingServices>>,
        schedule_node: ScheduleNode,
        protocol_state_node: ProtocolStateNode,
    ) {
        fs.dir("svc")
            .add_fidl_service(IncomingServices::Manager)
            .add_fidl_service(IncomingServices::ChannelControl);
        const MAX_CONCURRENT: usize = 1000;
        let server = Rc::new(RefCell::new(self));
        // Handle each client connection concurrently.
        let fs_fut = fs.for_each_concurrent(MAX_CONCURRENT, |stream| {
            Self::handle_client(server.clone(), stream).unwrap_or_else(|e| error!("{:?}", e))
        });
        Self::setup_observer(server.clone(), schedule_node, protocol_state_node);
        fs_fut.await;
    }

    /// Setup the observer from state machine.
    fn setup_observer(
        server: Rc<RefCell<Self>>,
        schedule_node: ScheduleNode,
        protocol_state_node: ProtocolStateNode,
    ) {
        let state_machine_ref = server.borrow().state_machine_ref.clone();
        let mut state_machine = state_machine_ref.borrow_mut();
        state_machine.set_observer(FuchsiaObserver::<PE, HR, IN, TM, MR, ST>::new(
            server,
            schedule_node,
            protocol_state_node,
        ));
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
                    Self::handle_manager_request(server.clone(), request)?;
                }
            }
            IncomingServices::ChannelControl(mut stream) => {
                while let Some(request) =
                    stream.try_next().await.context("error receiving ChannelControl request")?
                {
                    Self::handle_channel_control_request(server.clone(), request).await?;
                }
            }
        }
        Ok(())
    }

    /// Handle fuchsia.update.Manager requests.
    fn handle_manager_request(
        server: Rc<RefCell<Self>>,
        request: ManagerRequest,
    ) -> Result<(), Error> {
        let mut server = server.borrow_mut();
        match request {
            ManagerRequest::CheckNow { options, monitor, responder } => {
                info!("Received CheckNow request with {:?} and {:?}", options, monitor);

                // Attach the monitor if passed for current update.
                if let Some(monitor) = monitor {
                    let (_stream, handle) = monitor.into_stream_and_control_handle()?;
                    handle.send_on_state(clone(&server.state))?;
                    server.current_monitor_handles.push(handle);
                }

                match server.state.state {
                    Some(ManagerState::Idle) => {
                        let options = CheckOptions {
                            source: match options.initiator {
                                Some(Initiator::User) => InstallSource::OnDemand,
                                Some(Initiator::Service) => InstallSource::ScheduledTask,
                                None => bail!("Options.Initiator is required"),
                            },
                        };
                        let state_machine_ref = server.state_machine_ref.clone();
                        // Drop the borrowed server before starting update check because the state
                        // callback also need to borrow the server.
                        drop(server);
                        // TODO: Detect and return CheckStartedResult::Throttled.
                        fasync::spawn_local(async move {
                            let mut state_machine = state_machine_ref.borrow_mut();
                            state_machine.start_update_check(options).await;
                        });
                        responder
                            .send(CheckStartedResult::Started)
                            .context("error sending response")?;
                    }
                    _ => {
                        responder
                            .send(CheckStartedResult::InProgress)
                            .context("error sending response")?;
                    }
                }
            }
            ManagerRequest::GetState { responder } => {
                info!("Received GetState request");
                responder.send(clone(&server.state)).context("error sending response")?;
            }
            ManagerRequest::AddMonitor { monitor, control_handle: _ } => {
                info!("Received AddMonitor request with {:?}", monitor);
                let (_stream, handle) = monitor.into_stream_and_control_handle()?;
                handle.send_on_state(clone(&server.state))?;
                server.monitor_handles.push(handle);
            }
        }
        Ok(())
    }

    /// Handle fuchsia.update.channelcontrol.ChannelControl requests.
    async fn handle_channel_control_request(
        server: Rc<RefCell<Self>>,
        request: ChannelControlRequest,
    ) -> Result<(), Error> {
        let server = server.borrow();
        match request {
            ChannelControlRequest::SetTarget { channel, responder } => {
                info!("Received SetTarget request with {}", channel);
                // TODO: Verify that channel is valid.
                if channel.is_empty() {
                    // TODO: Remove this when fxb/36608 is fixed.
                    warn!(
                        "Empty channel passed to SetTarget, erasing all channel data in SysConfig."
                    );
                    write_partition(SysconfigPartition::Config, &[])?;
                    let target_channel = match &server.channel_configs {
                        Some(channel_configs) => channel_configs.default_channel.clone(),
                        None => None,
                    };
                    server.app_set.set_target_channel(target_channel).await;
                } else {
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

                    let mut storage = server.storage_ref.lock().await;
                    server.app_set.set_target_channel(Some(channel)).await;
                    server.app_set.persist(&mut *storage).await;
                    if let Err(e) = storage.commit().await {
                        error!("Unable to commit target channel change: {}", e);
                    }
                }
                server.apps_node.set(&server.app_set.to_vec().await);
                responder.send().context("error sending response")?;
            }
            ChannelControlRequest::GetTarget { responder } => {
                let channel = server.app_set.get_target_channel().await;
                responder.send(&channel).context("error sending response")?;
            }
            ChannelControlRequest::GetCurrent { responder } => {
                let channel = server.app_set.get_current_channel().await;
                responder.send(&channel).context("error sending response")?;
            }
            ChannelControlRequest::GetTargetList { responder } => {
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

    /// The state change callback from StateMachine.
    pub async fn on_state_change(&mut self, state: state_machine::State) {
        self.state.state = Some(match state {
            state_machine::State::Idle => ManagerState::Idle,
            state_machine::State::CheckingForUpdates => ManagerState::CheckingForUpdates,
            state_machine::State::UpdateAvailable => ManagerState::UpdateAvailable,
            state_machine::State::PerformingUpdate => ManagerState::PerformingUpdate,
            state_machine::State::WaitingForReboot => ManagerState::WaitingForReboot,
            state_machine::State::FinalizingUpdate => ManagerState::FinalizingUpdate,
            state_machine::State::EncounteredError => ManagerState::EncounteredError,
        });

        // Send the new state to all monitor handles and remove the handle if it fails.
        let state = clone(&self.state);
        let send_state_and_remove_failed =
            |handle: &MonitorControlHandle| match handle.send_on_state(clone(&state)) {
                Ok(()) => true,
                Err(e) => {
                    error!(
                        "Failed to send on_state callback to {:?}: {:?}, removing handle.",
                        handle, e
                    );
                    false
                }
            };
        self.current_monitor_handles.retain(send_state_and_remove_failed);
        self.monitor_handles.retain(send_state_and_remove_failed);

        // State is back to idle, clear the current update monitor handles.
        if self.state.state == Some(ManagerState::Idle) {
            self.current_monitor_handles.clear();
        }

        self.state_node.set(&self.state);

        // The state machine might make changes to apps only when state changes to `Idle` or
        // `WaitingForReboot`, update the apps node in inspect.
        if self.state.state == Some(ManagerState::Idle)
            || self.state.state == Some(ManagerState::WaitingForReboot)
        {
            self.apps_node.set(&self.app_set.to_vec().await);
        }
    }
}

/// Manually clone |State| because FIDL table doesn't derive clone.
fn clone(state: &State) -> State {
    State { state: state.state.clone(), version_available: state.version_available.clone() }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{channel::ChannelConfig, configuration};
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_update::{ManagerMarker, MonitorEvent, MonitorMarker, Options};
    use fidl_fuchsia_update_channelcontrol::ChannelControlMarker;
    use fuchsia_inspect::{assert_inspect_tree, Inspector};
    use omaha_client::{
        common::App, http_request::StubHttpRequest, installer::stub::StubInstaller,
        metrics::StubMetricsReporter, policy::StubPolicyEngine, protocol::Cohort,
        state_machine::StubTimer, storage::MemStorage,
    };

    type StubFidlServer = FidlServer<
        StubPolicyEngine,
        StubHttpRequest,
        StubInstaller,
        StubTimer,
        StubMetricsReporter,
        MemStorage,
    >;

    struct FidlServerBuilder {
        apps: Vec<App>,
        channel_configs: Option<ChannelConfigs>,
        apps_node: Option<AppsNode>,
        state_node: Option<StateNode>,
    }

    impl FidlServerBuilder {
        fn new() -> Self {
            Self { apps: Vec::new(), channel_configs: None, apps_node: None, state_node: None }
        }

        fn with_apps(mut self, mut apps: Vec<App>) -> Self {
            self.apps.append(&mut apps);
            self
        }

        fn with_apps_node(mut self, apps_node: AppsNode) -> Self {
            self.apps_node = Some(apps_node);
            self
        }

        fn with_state_node(mut self, state_node: StateNode) -> Self {
            self.state_node = Some(state_node);
            self
        }

        fn with_channel_configs(mut self, channel_configs: ChannelConfigs) -> Self {
            self.channel_configs = Some(channel_configs);
            self
        }

        async fn build(self) -> StubFidlServer {
            let config = configuration::get_config("0.1.2");
            let storage_ref = Rc::new(Mutex::new(MemStorage::new()));
            let app_set = if self.apps.is_empty() {
                AppSet::new(vec![App::new("id", [1, 0], Cohort::default())])
            } else {
                AppSet::new(self.apps)
            };
            let state_machine = StateMachine::new(
                StubPolicyEngine,
                StubHttpRequest,
                StubInstaller::default(),
                &config,
                StubTimer,
                StubMetricsReporter,
                storage_ref.clone(),
                app_set.clone(),
            )
            .await;
            let inspector = Inspector::new();
            let root = inspector.root();
            let apps_node = self.apps_node.unwrap_or(AppsNode::new(root.create_child("apps")));
            let state_node = self.state_node.unwrap_or(StateNode::new(root.create_child("state")));
            FidlServer::new(
                Rc::new(RefCell::new(state_machine)),
                storage_ref,
                app_set,
                apps_node,
                state_node,
                self.channel_configs,
            )
        }
    }

    fn spawn_fidl_server<M: fidl::endpoints::ServiceMarker>(
        fidl: Rc<RefCell<StubFidlServer>>,
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
        let mut fidl = FidlServerBuilder::new().build().await;
        fidl.on_state_change(state_machine::State::CheckingForUpdates).await;
        assert_eq!(Some(ManagerState::CheckingForUpdates), fidl.state.state);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_state() {
        let fidl = Rc::new(RefCell::new(FidlServerBuilder::new().build().await));
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl.clone(), IncomingServices::Manager);
        let state = proxy.get_state().await.unwrap();
        let fidl = fidl.borrow();
        assert_eq!(state, fidl.state);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_monitor() {
        let fidl = Rc::new(RefCell::new(FidlServerBuilder::new().build().await));
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl.clone(), IncomingServices::Manager);
        let (client_proxy, server_end) = create_proxy::<MonitorMarker>().unwrap();
        proxy.add_monitor(server_end).unwrap();
        let mut stream = client_proxy.take_event_stream();
        let event = stream.next().await.unwrap().unwrap();
        let fidl = fidl.borrow();
        match event {
            MonitorEvent::OnState { state } => {
                assert_eq!(state, fidl.state);
            }
        }
        assert_eq!(fidl.monitor_handles.len(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now() {
        let fidl = Rc::new(RefCell::new(FidlServerBuilder::new().build().await));
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl, IncomingServices::Manager);
        let options = Options { initiator: Some(Initiator::User) };
        let result = proxy.check_now(options, None).await.unwrap();
        assert_eq!(result, CheckStartedResult::Started);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_with_monitor() {
        let fidl = Rc::new(RefCell::new(FidlServerBuilder::new().build().await));
        let inspector = Inspector::new();
        let schedule_node = ScheduleNode::new(inspector.root().create_child("schedule"));
        let protocol_state_node =
            ProtocolStateNode::new(inspector.root().create_child("protocol_state"));
        FidlServer::setup_observer(fidl.clone(), schedule_node, protocol_state_node);
        let proxy = spawn_fidl_server::<ManagerMarker>(fidl.clone(), IncomingServices::Manager);
        let (client_proxy, server_end) = create_proxy::<MonitorMarker>().unwrap();
        let options = Options { initiator: Some(Initiator::User) };
        let result = proxy.check_now(options, Some(server_end)).await.unwrap();
        assert_eq!(result, CheckStartedResult::Started);
        let mut expected_states = [
            State { state: Some(ManagerState::Idle), version_available: None },
            State { state: Some(ManagerState::CheckingForUpdates), version_available: None },
            State { state: Some(ManagerState::EncounteredError), version_available: None },
            State { state: Some(ManagerState::Idle), version_available: None },
        ]
        .iter();
        let mut stream = client_proxy.take_event_stream();
        while let Some(event) = stream.try_next().await.unwrap() {
            match event {
                MonitorEvent::OnState { state } => {
                    assert_eq!(Some(&state), expected_states.next());
                }
            }
        }
        assert_eq!(None, expected_states.next());
        assert!(fidl.borrow().current_monitor_handles.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_channel() {
        let apps = vec![App::new(
            "id",
            [1, 0],
            Cohort { name: Some("current-channel".to_string()), ..Cohort::default() },
        )];
        let fidl = Rc::new(RefCell::new(FidlServerBuilder::new().with_apps(apps).build().await));

        let proxy =
            spawn_fidl_server::<ChannelControlMarker>(fidl, IncomingServices::ChannelControl);

        assert_eq!("current-channel", proxy.get_current().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_target() {
        let apps = vec![App::new("id", [1, 0], Cohort::from_hint("target-channel"))];
        let fidl = Rc::new(RefCell::new(FidlServerBuilder::new().with_apps(apps).build().await));

        let proxy =
            spawn_fidl_server::<ChannelControlMarker>(fidl, IncomingServices::ChannelControl);
        assert_eq!("target-channel", proxy.get_target().await.unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_set_target() {
        let fidl = Rc::new(RefCell::new(
            FidlServerBuilder::new()
                .with_channel_configs(ChannelConfigs {
                    default_channel: None,
                    known_channels: vec![
                        ChannelConfig::new("some-channel"),
                        ChannelConfig::new("target-channel"),
                    ],
                })
                .build()
                .await,
        ));

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            fidl.clone(),
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
        let fidl = Rc::new(RefCell::new(
            FidlServerBuilder::new()
                .with_channel_configs(ChannelConfigs {
                    default_channel: Some("default-channel".to_string()),
                    known_channels: vec![],
                })
                .build()
                .await,
        ));

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            fidl.clone(),
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
        let fidl = Rc::new(RefCell::new(
            FidlServerBuilder::new()
                .with_channel_configs(ChannelConfigs {
                    default_channel: None,
                    known_channels: vec![
                        ChannelConfig::new("some-channel"),
                        ChannelConfig::new("some-other-channel"),
                    ],
                })
                .build()
                .await,
        ));

        let proxy =
            spawn_fidl_server::<ChannelControlMarker>(fidl, IncomingServices::ChannelControl);
        let response = proxy.get_target_list().await.unwrap();

        assert_eq!(2, response.len());
        assert!(response.contains(&"some-channel".to_string()));
        assert!(response.contains(&"some-other-channel".to_string()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_target_list_when_no_channels_configured() {
        let fidl = Rc::new(RefCell::new(FidlServerBuilder::new().build().await));

        let proxy =
            spawn_fidl_server::<ChannelControlMarker>(fidl, IncomingServices::ChannelControl);
        let response = proxy.get_target_list().await.unwrap();

        assert!(response.is_empty());
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_apps_on_state_change() {
        let inspector = Inspector::new();
        let apps_node = AppsNode::new(inspector.root().create_child("apps"));
        let mut fidl = FidlServerBuilder::new().with_apps_node(apps_node).build().await;

        fidl.on_state_change(state_machine::State::Idle).await;

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
    async fn test_inspect_apps_on_channel_change() {
        let inspector = Inspector::new();
        let apps_node = AppsNode::new(inspector.root().create_child("apps"));
        let fidl = Rc::new(RefCell::new(
            FidlServerBuilder::new()
                .with_apps_node(apps_node)
                .with_channel_configs(ChannelConfigs {
                    default_channel: None,
                    known_channels: vec![ChannelConfig::new("target-channel")],
                })
                .build()
                .await,
        ));

        let proxy = spawn_fidl_server::<ChannelControlMarker>(
            fidl.clone(),
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
        let mut fidl = FidlServerBuilder::new().with_state_node(state_node).build().await;

        assert_inspect_tree!(
            inspector,
            root: {
                state: {
                    state: format!("{:?}", fidl.state),
                }
            }
        );

        fidl.on_state_change(state_machine::State::PerformingUpdate).await;

        assert_inspect_tree!(
            inspector,
            root: {
                state: {
                    state: format!("{:?}", fidl.state),
                }
            }
        );
    }
}
