// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, Error, ResultExt};
use fidl_fuchsia_omaha_client::{
    OmahaClientConfigurationRequest, OmahaClientConfigurationRequestStream,
};
use fidl_fuchsia_update::{
    CheckStartedResult, Initiator, ManagerRequest, ManagerRequestStream, ManagerState,
    MonitorControlHandle, State,
};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceObjLocal};
use fuchsia_zircon as zx;
use futures::prelude::*;
use log::{error, info};
use omaha_client::{
    common::CheckOptions,
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
    OmahaClientConfiguration(OmahaClientConfigurationRequestStream),
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
    pub fn new(state_machine_ref: Rc<RefCell<StateMachine<PE, HR, IN, TM, MR, ST>>>) -> Self {
        FidlServer {
            state_machine_ref,
            state: State { state: Some(ManagerState::Idle), version_available: None },
            monitor_handles: vec![],
            current_monitor_handles: vec![],
        }
    }

    /// Starts the FIDL Server and the StateMachine.
    pub async fn start(self, mut fs: ServiceFs<ServiceObjLocal<'_, IncomingServices>>) {
        fs.dir("svc")
            .add_fidl_service(IncomingServices::Manager)
            .add_fidl_service(IncomingServices::OmahaClientConfiguration);
        const MAX_CONCURRENT: usize = 1000;
        let server = Rc::new(RefCell::new(self));
        // Handle each client connection concurrently.
        let fs_fut = fs.for_each_concurrent(MAX_CONCURRENT, |stream| {
            Self::handle_client(server.clone(), stream).unwrap_or_else(|e| error!("{:?}", e))
        });
        Self::setup_state_callback(server.clone());
        fs_fut.await;
    }

    /// Setup the state callback from state machine.
    fn setup_state_callback(server: Rc<RefCell<Self>>) {
        let state_machine_ref = server.borrow().state_machine_ref.clone();
        let mut state_machine = state_machine_ref.borrow_mut();
        state_machine.set_state_callback(move |state| {
            let mut server = server.borrow_mut();
            server.on_state_change(state);
        });
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
            IncomingServices::OmahaClientConfiguration(mut stream) => {
                while let Some(request) = stream
                    .try_next()
                    .await
                    .context("error receiving OmahaClientConfiguration request")?
                {
                    Self::handle_omaha_client_configuration_request(server.clone(), request)?;
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

    /// Handle fuchsia.update.OmahaClientConfiguration requests.
    fn handle_omaha_client_configuration_request(
        _server: Rc<RefCell<Self>>,
        request: OmahaClientConfigurationRequest,
    ) -> Result<(), Error> {
        match request {
            OmahaClientConfigurationRequest::SetChannel {
                channel,
                allow_factory_reset,
                responder,
            } => {
                info!(
                    "Received SetChannel request with {:?} and {:?}",
                    channel, allow_factory_reset
                );
                // TODO: Set the channel in state machine.
                responder.send(zx::Status::OK.into_raw()).context("error sending response")?;
            }
            OmahaClientConfigurationRequest::GetChannel { responder } => {
                info!("Received GetChannel request");
                // TODO: Get the channel from state machine.
                responder.send("stable-channel").context("error sending response")?;
            }
        }
        Ok(())
    }

    /// The state change callback from StateMachine.
    fn on_state_change(&mut self, state: state_machine::State) {
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
    }
}

/// Manually clone |State| because FIDL table doesn't derive clone.
fn clone(state: &State) -> State {
    State { state: state.state.clone(), version_available: state.version_available.clone() }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::configuration;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_update::{ManagerMarker, MonitorEvent, MonitorMarker, Options};
    use omaha_client::{
        http_request::StubHttpRequest, installer::stub::StubInstaller,
        metrics::StubMetricsReporter, policy::StubPolicyEngine, state_machine::StubTimer,
        storage::MemStorage,
    };

    async fn new_fidl_server() -> FidlServer<
        StubPolicyEngine,
        StubHttpRequest,
        StubInstaller,
        StubTimer,
        StubMetricsReporter,
        MemStorage,
    > {
        let config = configuration::get_config();
        let state_machine = StateMachine::new_stub(&config).await;

        FidlServer::new(Rc::new(RefCell::new(state_machine)))
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_on_state_change() {
        let mut fidl = new_fidl_server().await;
        fidl.on_state_change(state_machine::State::CheckingForUpdates);
        assert_eq!(Some(ManagerState::CheckingForUpdates), fidl.state.state);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_state() {
        let fidl = Rc::new(RefCell::new(new_fidl_server().await));
        let (proxy, stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
        fasync::spawn_local(
            FidlServer::handle_client(fidl.clone(), IncomingServices::Manager(stream))
                .unwrap_or_else(|e| panic!(e)),
        );
        let state = proxy.get_state().await.unwrap();
        let fidl = fidl.borrow();
        assert_eq!(state, fidl.state);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_monitor() {
        let fidl = Rc::new(RefCell::new(new_fidl_server().await));
        let (proxy, stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
        fasync::spawn_local(
            FidlServer::handle_client(fidl.clone(), IncomingServices::Manager(stream))
                .unwrap_or_else(|e| panic!(e)),
        );
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
        let fidl = Rc::new(RefCell::new(new_fidl_server().await));
        let (proxy, stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
        fasync::spawn_local(
            FidlServer::handle_client(fidl.clone(), IncomingServices::Manager(stream))
                .unwrap_or_else(|e| panic!(e)),
        );
        let options = Options { initiator: Some(Initiator::User) };
        let result = proxy.check_now(options, None).await.unwrap();
        assert_eq!(result, CheckStartedResult::Started);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_check_now_with_monitor() {
        let fidl = Rc::new(RefCell::new(new_fidl_server().await));
        FidlServer::setup_state_callback(fidl.clone());
        let (proxy, stream) = create_proxy_and_stream::<ManagerMarker>().unwrap();
        fasync::spawn_local(
            FidlServer::handle_client(fidl.clone(), IncomingServices::Manager(stream))
                .unwrap_or_else(|e| panic!(e)),
        );
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
}
