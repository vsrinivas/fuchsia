// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        access_point::{state_machine as ap_fsm, state_machine::AccessPointApi, types as ap_types},
        client::state_machine as client_fsm,
        config_management::SavedNetworksManager,
        mode_management::iface_manager_types::*,
        mode_management::phy_manager::PhyManagerApi,
        util::{future_with_metadata, listener},
    },
    anyhow::{format_err, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_wlan_device, fidl_fuchsia_wlan_policy, fidl_fuchsia_wlan_sme,
    futures::{
        channel::{mpsc, oneshot},
        future::{ready, BoxFuture},
        lock::Mutex,
        select,
        stream::FuturesUnordered,
        FutureExt, StreamExt,
    },
    log::{error, info},
    std::sync::Arc,
    void::Void,
};

/// Wraps around vital information associated with a WLAN client interface.  In all cases, a client
/// interface will have an ID and a ClientSmeProxy to make requests of the interface.  If a client
/// is configured to connect to a WLAN network, it will store the network configuration information
/// associated with that network as well as a communcation channel to make requests of the state
/// machine that maintains client connectivity.
struct ClientIfaceContainer {
    iface_id: u16,
    sme_proxy: fidl_fuchsia_wlan_sme::ClientSmeProxy,
    config: Option<ap_types::NetworkIdentifier>,
    client_state_machine: Box<dyn client_fsm::ClientApi + Send>,
}

pub(crate) struct ApIfaceContainer {
    pub iface_id: u16,
    pub config: Option<ap_fsm::ApConfig>,
    pub ap_state_machine: Box<dyn AccessPointApi + Send + Sync>,
}

#[derive(Clone, Debug)]
pub struct StateMachineMetadata {
    pub iface_id: u16,
    pub role: fidl_fuchsia_wlan_device::MacRole,
}

/// Accounts for WLAN interfaces that are present and utilizes them to service requests that are
/// made of the policy layer.
pub(crate) struct IfaceManagerService {
    phy_manager: Arc<Mutex<dyn PhyManagerApi + Send>>,
    client_update_sender: listener::ClientListenerMessageSender,
    ap_update_sender: listener::ApListenerMessageSender,
    dev_svc_proxy: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
    clients: Vec<ClientIfaceContainer>,
    aps: Vec<ApIfaceContainer>,
    saved_networks: Arc<SavedNetworksManager>,
    client_event_sender: mpsc::Sender<client_fsm::ClientStateMachineNotification>,
    fsm_futures:
        FuturesUnordered<future_with_metadata::FutureWithMetadata<(), StateMachineMetadata>>,
}

impl IfaceManagerService {
    pub fn new(
        phy_manager: Arc<Mutex<dyn PhyManagerApi + Send>>,
        client_update_sender: listener::ClientListenerMessageSender,
        ap_update_sender: listener::ApListenerMessageSender,
        dev_svc_proxy: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
        saved_networks: Arc<SavedNetworksManager>,
        client_event_sender: mpsc::Sender<client_fsm::ClientStateMachineNotification>,
    ) -> Self {
        IfaceManagerService {
            phy_manager: phy_manager.clone(),
            client_update_sender,
            ap_update_sender,
            dev_svc_proxy,
            clients: Vec::new(),
            aps: Vec::new(),
            saved_networks: saved_networks,
            client_event_sender,
            fsm_futures: FuturesUnordered::new(),
        }
    }

    /// Checks for any known, unconfigured clients.  If one exists, its ClientIfaceContainer is
    /// returned to the caller.
    ///
    /// If all ifaces are configured, asks the PhyManager for an iface ID.  If the returned iface
    /// ID is an already-configured iface, that iface is removed from the configured clients list
    /// and returned to the caller.
    ///
    /// If it is not, a new ClientIfaceContainer is created from the returned iface ID and returned
    /// to the caller.
    async fn get_client(&mut self, iface_id: Option<u16>) -> Result<ClientIfaceContainer, Error> {
        let iface_id = match iface_id {
            Some(iface_id) => iface_id,
            None => {
                // If no iface_id is specified and if there there are any unconfigured client
                // ifaces, use the first available unconfigured iface.
                if let Some(removal_index) = self
                    .clients
                    .iter()
                    .position(|client_container| client_container.config.is_none())
                {
                    return Ok(self.clients.remove(removal_index));
                }

                // If all of the known client ifaces are configured, ask the PhyManager for a
                // client iface.
                match self.phy_manager.lock().await.get_client() {
                    None => return Err(format_err!("no client ifaces available")),
                    Some(id) => id,
                }
            }
        };

        // See if the selected iface ID is among the configured clients.
        if let Some(removal_index) =
            self.clients.iter().position(|client_container| client_container.iface_id == iface_id)
        {
            return Ok(self.clients.remove(removal_index));
        }

        // If the iface ID is not among configured clients, create a new ClientIfaceContainer for
        // the iface ID.
        let (sme_proxy, remote) = create_proxy()?;
        let status = self.dev_svc_proxy.get_client_sme(iface_id, remote).await?;
        fuchsia_zircon::ok(status)?;

        // Create a client state machine for the newly discovered interface.
        let (sender, receiver) = mpsc::channel(1);
        let new_client = client_fsm::Client::new(sender);
        let fut = client_fsm::serve(
            iface_id,
            sme_proxy.clone(),
            sme_proxy.take_event_stream(),
            receiver,
            self.client_update_sender.clone(),
            self.client_event_sender.clone(),
            self.saved_networks.clone(),
        )
        .boxed();

        // Begin running and monitoring the client state machine future.
        let metadata = StateMachineMetadata {
            iface_id: iface_id,
            role: fidl_fuchsia_wlan_device::MacRole::Client,
        };
        let fut = future_with_metadata::FutureWithMetadata::new(metadata, fut);
        self.fsm_futures.push(fut);

        Ok(ClientIfaceContainer {
            iface_id: iface_id,
            sme_proxy,
            config: None,
            client_state_machine: Box::new(new_client),
        })
    }

    /// Queries to PhyManager to determine if there are any interfaces that can be used as AP's.
    ///
    /// If the PhyManager indicates that there is an existing interface that should be used for the
    /// AP request, return the existing AP interface.
    ///
    /// If the indicated AP interface has not been used before, spawn a new AP state machine for
    /// the interface and return the new interface.
    async fn get_ap(&mut self, iface_id: Option<u16>) -> Result<ApIfaceContainer, Error> {
        let iface_id = match iface_id {
            Some(iface_id) => iface_id,
            None => {
                // If no iface ID is specified, ask the PhyManager for an AP iface ID.
                let mut phy_manager = self.phy_manager.lock().await;
                match phy_manager.create_or_get_ap_iface().await {
                    Ok(Some(iface_id)) => iface_id,
                    Ok(None) => return Err(format_err!("no available PHYs can support AP ifaces")),
                    phy_manager_error => {
                        return Err(format_err!("could not get AP {:?}", phy_manager_error));
                    }
                }
            }
        };

        // Check if this iface ID is already accounted for.
        if let Some(removal_index) =
            self.aps.iter().position(|ap_container| ap_container.iface_id == iface_id)
        {
            return Ok(self.aps.remove(removal_index));
        }

        // If this iface ID is not yet accounted for, create a new ApIfaceContainer.
        let (sme_proxy, remote) = create_proxy()?;
        let status = self.dev_svc_proxy.get_ap_sme(iface_id, remote).await?;
        fuchsia_zircon::ok(status)?;

        // Spawn the AP state machine.
        let (state_machine_start_sender, _) = oneshot::channel();
        let (sender, receiver) = mpsc::channel(1);
        let state_machine = ap_fsm::AccessPoint::new(sender);

        let event_stream = sme_proxy.take_event_stream();
        let state_machine_fut = ap_fsm::serve(
            iface_id,
            sme_proxy,
            event_stream,
            receiver,
            self.ap_update_sender.clone(),
            state_machine_start_sender,
        )
        .boxed();

        // Begin running and monitoring the AP state machine future.
        let metadata = StateMachineMetadata {
            iface_id: iface_id,
            role: fidl_fuchsia_wlan_device::MacRole::Ap,
        };
        let fut = future_with_metadata::FutureWithMetadata::new(metadata, state_machine_fut);
        self.fsm_futures.push(fut);

        Ok(ApIfaceContainer {
            iface_id: iface_id,
            config: None,
            ap_state_machine: Box::new(state_machine),
        })
    }

    /// Attempts to stop the AP and then exit the AP state machine.
    async fn stop_and_exit_ap_state_machine(
        mut ap_state_machine: Box<dyn AccessPointApi + Send + Sync>,
    ) -> Result<(), Error> {
        let (sender, receiver) = oneshot::channel();
        ap_state_machine.stop(sender)?;
        receiver.await?;

        let (sender, receiver) = oneshot::channel();
        ap_state_machine.exit(sender)?;
        receiver.await?;

        Ok(())
    }
    fn disconnect(
        &mut self,
        network_id: ap_types::NetworkIdentifier,
    ) -> BoxFuture<'static, Result<(), Error>> {
        // Find the client interface associated with the given network config and disconnect from
        // the network.
        let mut fsm_ack_receiver = None;
        let mut iface_id = None;

        for client in self.clients.iter_mut() {
            if client.config.as_ref() == Some(&network_id) {
                let (responder, receiver) = oneshot::channel();
                match client.client_state_machine.disconnect(responder) {
                    Ok(()) => {}
                    Err(e) => {
                        return Box::pin(async move {
                            return Err(format_err!("failed to send disconnect: {:?}", e));
                        });
                    }
                }
                client.config = None;
                fsm_ack_receiver = Some(receiver);
                iface_id = Some(client.iface_id);
                break;
            }
        }

        let receiver = match fsm_ack_receiver {
            Some(receiver) => receiver,
            None => return ready(Ok(())).boxed(),
        };
        let iface_id = match iface_id {
            Some(iface_id) => iface_id,
            None => return ready(Ok(())).boxed(),
        };

        let fut = async move {
            match receiver.await {
                Ok(()) => return Ok(()),
                error => {
                    Err(format_err!("failed to disconnect client iface {}: {:?}", iface_id, error))
                }
            }
        };
        return fut.boxed();
    }

    async fn connect(
        &mut self,
        connect_req: client_fsm::ConnectRequest,
    ) -> Result<oneshot::Receiver<()>, Error> {
        // Get a ClientIfaceContainer.  Ensure that the Client is populated.
        let mut client_iface = self.get_client(None).await?;

        // Create necessary components to make a connect request.
        let (responder, receiver) = oneshot::channel();

        client_iface.config = Some(connect_req.network.clone());

        // Call connect.
        let connect_request_result =
            client_iface.client_state_machine.as_mut().connect(connect_req, responder);

        // If it is possible to communicate with the client state machine, preserve it in the list
        // of clients.
        match connect_request_result {
            Ok(()) => {
                self.clients.push(client_iface);
                Ok(receiver)
            }
            Err(e) => Err(format_err!("error while requesting connect {:?}", e)),
        }
    }

    fn record_idle_client(&mut self, iface_id: u16) {
        for client in self.clients.iter_mut() {
            if client.iface_id == iface_id {
                client.config = None;
                return;
            }
        }
    }

    fn has_idle_client(&self) -> bool {
        for client in self.clients.iter() {
            if client.config.is_none() {
                return true;
            }
        }

        false
    }

    async fn handle_added_iface(&mut self, iface_id: u16) {
        let iface_info = if let Ok((status, Some(iface_info))) =
            self.dev_svc_proxy.query_iface(iface_id).await
        {
            if fuchsia_zircon::ok(status).is_err() {
                return;
            }
            iface_info
        } else {
            return;
        };

        match iface_info.role {
            fidl_fuchsia_wlan_device::MacRole::Client => {
                if let Ok(client_iface) = self.get_client(Some(iface_id)).await {
                    self.clients.push(client_iface);
                }
            }
            fidl_fuchsia_wlan_device::MacRole::Ap => {
                if let Ok(ap_iface) = self.get_ap(Some(iface_id)).await {
                    self.aps.push(ap_iface);
                }
            }
            fidl_fuchsia_wlan_device::MacRole::Mesh => {
                // Mesh roles are not currently supported.
            }
        }
    }

    async fn handle_removed_iface(&mut self, iface_id: u16) {
        self.phy_manager.lock().await.on_iface_removed(iface_id);

        self.clients.retain(|client_container| client_container.iface_id != iface_id);
        self.aps.retain(|ap_container| ap_container.iface_id != iface_id);
    }

    async fn scan(
        &mut self,
        timeout: u8,
        scan_type: fidl_fuchsia_wlan_common::ScanType,
    ) -> Result<fidl_fuchsia_wlan_sme::ScanTransactionProxy, Error> {
        let client_iface = self.get_client(None).await?;

        let (local, remote) = fidl::endpoints::create_proxy()?;
        let mut request =
            fidl_fuchsia_wlan_sme::ScanRequest { timeout: timeout, scan_type: scan_type };

        let scan_result = client_iface.sme_proxy.scan(&mut request, remote);

        self.clients.push(client_iface);

        match scan_result {
            Ok(()) => Ok(local),
            Err(e) => Err(format_err!("failed to scan: {:?}", e)),
        }
    }

    fn stop_client_connections(&mut self) -> BoxFuture<'static, Result<(), Error>> {
        let client_ifaces: Vec<ClientIfaceContainer> = self.clients.drain(..).collect();
        let phy_manager = self.phy_manager.clone();
        let update_sender = self.client_update_sender.clone();

        let fut = async move {
            // Disconnect and discard all of the configured client ifaces.
            for client_iface in client_ifaces {
                let mut client = client_iface.client_state_machine;
                let (responder, receiver) = oneshot::channel();
                match client.disconnect(responder) {
                    Ok(()) => {}
                    Err(e) => error!("failed to issue disconnect: {:?}", e),
                }
                match receiver.await {
                    Ok(()) => {}
                    Err(e) => error!("failed to disconnect: {:?}", e),
                }

                let (responder, receiver) = oneshot::channel();
                match client.exit(responder) {
                    Ok(()) => {}
                    Err(e) => error!("failed to issue exit: {:?}", e),
                }
                match receiver.await {
                    Ok(()) => {}
                    Err(e) => error!("failed to exit: {:?}", e),
                }
            }

            // Signal to the update listener that client connections have been disabled.
            let update = listener::ClientStateUpdate {
                state: Some(fidl_fuchsia_wlan_policy::WlanClientState::ConnectionsDisabled),
                networks: vec![],
            };
            if let Err(e) = update_sender.unbounded_send(listener::Message::NotifyListeners(update))
            {
                error!("Failed to send state update: {:?}", e)
            };

            // Tell the PhyManager to stop all client connections.
            let mut phy_manager = phy_manager.lock().await;
            phy_manager.destroy_all_client_ifaces().await?;

            Ok(())
        };

        fut.boxed()
    }

    async fn start_client_connections(&mut self) -> Result<(), Error> {
        let mut phy_manager = self.phy_manager.lock().await;
        match phy_manager.create_all_client_ifaces().await {
            Ok(()) => {
                // Send an update to the update listener indicating that client connections are now
                // enabled.
                let update = listener::ClientStateUpdate {
                    state: Some(fidl_fuchsia_wlan_policy::WlanClientState::ConnectionsEnabled),
                    networks: vec![],
                };
                if let Err(e) = self
                    .client_update_sender
                    .unbounded_send(listener::Message::NotifyListeners(update))
                {
                    error!("Failed to send state update: {:?}", e)
                };
            }
            phy_manager_error => {
                return Err(format_err!(
                    "could not start client connection {:?}",
                    phy_manager_error
                ));
            }
        }
        Ok(())
    }

    async fn start_ap(
        &mut self,
        config: ap_fsm::ApConfig,
    ) -> Result<oneshot::Receiver<fidl_fuchsia_wlan_sme::StartApResultCode>, Error> {
        let mut ap_iface_container = self.get_ap(None).await?;

        let (sender, receiver) = oneshot::channel();
        ap_iface_container.config = Some(config.clone());
        match ap_iface_container.ap_state_machine.start(config, sender) {
            Ok(()) => self.aps.push(ap_iface_container),
            Err(e) => {
                let mut phy_manager = self.phy_manager.lock().await;
                phy_manager.destroy_ap_iface(ap_iface_container.iface_id).await?;
                return Err(format_err!("could not start ap: {}", e));
            }
        }

        Ok(receiver)
    }

    fn stop_ap(
        &mut self,
        ssid: Vec<u8>,
        credential: Vec<u8>,
    ) -> BoxFuture<'static, Result<(), Error>> {
        if let Some(removal_index) =
            self.aps.iter().position(|ap_container| match ap_container.config.as_ref() {
                Some(config) => config.id.ssid == ssid && config.credential == credential,
                None => false,
            })
        {
            let phy_manager = self.phy_manager.clone();
            let ap_container = self.aps.remove(removal_index);

            let fut = async move {
                let stop_result =
                    Self::stop_and_exit_ap_state_machine(ap_container.ap_state_machine).await;

                let mut phy_manager = phy_manager.lock().await;
                phy_manager.destroy_ap_iface(ap_container.iface_id).await?;

                stop_result?;
                Ok(())
            };

            return fut.boxed();
        }

        return ready(Ok(())).boxed();
    }

    // Stop all APs, exit all of the state machines, and destroy all AP ifaces.
    fn stop_all_aps(&mut self) -> BoxFuture<'static, Result<(), Error>> {
        let mut aps: Vec<ApIfaceContainer> = self.aps.drain(..).collect();
        let phy_manager = self.phy_manager.clone();

        let fut = async move {
            let mut failed_iface_deletions: u8 = 0;
            for iface in aps.drain(..) {
                match IfaceManagerService::stop_and_exit_ap_state_machine(iface.ap_state_machine)
                    .await
                {
                    Ok(()) => {}
                    Err(e) => {
                        failed_iface_deletions += 1;
                        error!("failed to stop AP: {}", e);
                    }
                }

                let mut phy_manager = phy_manager.lock().await;
                match phy_manager.destroy_ap_iface(iface.iface_id).await {
                    Ok(()) => {}
                    Err(e) => {
                        failed_iface_deletions += 1;
                        error!("failed to delete AP {}: {}", iface.iface_id, e);
                    }
                }
            }

            if failed_iface_deletions == 0 {
                return Ok(());
            } else {
                return Err(format_err!("failed to delete {} ifaces", failed_iface_deletions));
            }
        };

        fut.boxed()
    }
}

pub(crate) async fn serve_iface_manager_requests(
    mut iface_manager: IfaceManagerService,
    mut requests: mpsc::Receiver<IfaceManagerRequest>,
) -> Result<Void, Error> {
    // Client and AP state machines need to be allowed to run in order for several operations to
    // complete.  In such cases, futures can be added to this list to progress them once the state
    // machines have the opportunity to run.
    let mut operation_futures = FuturesUnordered::new();

    loop {
        select! {
            terminated_fsm = iface_manager.fsm_futures.select_next_some() => {
                info!("state machine exited: {:?}", terminated_fsm.1);

                // TODO(57140): The client state machine will be modified to exit on idle.  The
                // reconnect process should be initiated here.
            },
            operation_result = operation_futures.select_next_some() => {},
            req = requests.select_next_some() => {
                match req {
                    IfaceManagerRequest::Disconnect(DisconnectRequest { network_id, responder }) => {
                        let fut = iface_manager.disconnect(network_id);
                        let disconnect_fut = async move {
                            if responder.send(fut.await).is_err() {
                                error!("could not respond to DisconnectRequest");
                            }
                        };
                        operation_futures.push(disconnect_fut.boxed());
                    }
                    IfaceManagerRequest::Connect(ConnectRequest { request, responder }) => {
                        if responder.send(iface_manager.connect(request).await).is_err() {
                            error!("could not respond to ConnectRequest");
                        }
                    }
                    IfaceManagerRequest::RecordIdleIface(RecordIdleIfaceRequest { iface_id, responder } ) => {
                        if responder.send(iface_manager.record_idle_client(iface_id)).is_err() {
                            error!("could not respond to RecordIdleIfaceRequest");
                        }
                    }
                    IfaceManagerRequest::HasIdleIface(HasIdleIfaceRequest { responder }) => {
                        if responder.send(iface_manager.has_idle_client()).is_err() {
                            error!("could not respond to  HasIdleIfaceRequest");
                        }
                    }
                    IfaceManagerRequest::AddIface(AddIfaceRequest { iface_id, responder } ) => {
                        if responder.send(iface_manager.handle_added_iface(iface_id).await).is_err() {
                            error!("could not respond to AddIfaceRequest");
                        }
                    }
                    IfaceManagerRequest::RemoveIface(RemoveIfaceRequest { iface_id, responder }) => {
                        if responder.send(iface_manager.handle_removed_iface(iface_id).await).is_err() {
                            error!("could not respond to RemoveIfaceRequest");
                        }
                    }
                    IfaceManagerRequest::Scan(ScanRequest { timeout, scan_type, responder }) => {
                        if responder.send(iface_manager.scan(timeout, scan_type).await).is_err() {
                            error!("could not respond to ScanRequest");
                        }
                    }
                    IfaceManagerRequest::StopClientConnections(StopClientConnectionsRequest { responder }) => {
                        let fut = iface_manager.stop_client_connections();
                        let stop_client_connections_fut = async move {
                            if responder.send(fut.await).is_err() {
                                error!("could not respond to StopClientConnectionsRequest");
                            }
                        };
                        operation_futures.push(stop_client_connections_fut.boxed());
                    }
                    IfaceManagerRequest::StartClientConnections(StartClientConnectionsRequest { responder }) => {
                        if responder.send(iface_manager.start_client_connections().await).is_err() {
                            error!("could not respond to StartClientConnectionRequest");
                        }
                    }
                    IfaceManagerRequest::StartAp(StartApRequest { config, responder }) => {
                        if responder.send(iface_manager.start_ap(config).await).is_err() {
                            error!("could not respond to StartApRequest");
                        }
                    }
                    IfaceManagerRequest::StopAp(StopApRequest { ssid, password, responder }) => {
                        let stop_ap_fut = iface_manager.stop_ap(ssid, password);
                        let stop_ap_fut = async move {
                            if responder.send(stop_ap_fut.await).is_err() {
                                error!("could not respond to StopApRequest");
                            }
                        };
                        operation_futures.push(stop_ap_fut.boxed());
                    }
                    IfaceManagerRequest::StopAllAps(StopAllApsRequest { responder }) => {
                        let stop_all_aps_fut = iface_manager.stop_all_aps();
                        let stop_all_aps_fut = async move {
                            if responder.send(stop_all_aps_fut.await).is_err() {
                                error!("could not respond to StopAllApsRequest");
                            }
                        };
                        operation_futures.push(stop_all_aps_fut.boxed());
                    }
                };
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            access_point::types,
            config_management::{Credential, NetworkIdentifier, SecurityType},
            mode_management::phy_manager::{self, PhyManagerError},
            util::logger::set_logger_for_test,
        },
        async_trait::async_trait,
        eui48::MacAddress,
        fidl::endpoints::create_proxy,
        fuchsia_async::Executor,
        futures::{
            channel::mpsc,
            stream::{StreamExt, StreamFuture},
            task::Poll,
            TryFutureExt,
        },
        pin_utils::pin_mut,
        test_case::test_case,
        wlan_common::{
            assert_variant,
            channel::{Cbw, Phy},
            RadioConfig,
        },
    };

    // Responses that FakePhyManager will provide
    pub const TEST_CLIENT_IFACE_ID: u16 = 0;
    pub const TEST_AP_IFACE_ID: u16 = 1;

    // Fake WLAN network that tests will scan for and connect to.
    pub static TEST_SSID: &str = "test_ssid";
    pub static TEST_PASSWORD: &str = "test_password";

    /// Produces wlan network configuration objects to be used in tests.
    pub fn create_connect_request(ssid: &str, password: &str) -> client_fsm::ConnectRequest {
        let network = ap_types::NetworkIdentifier {
            ssid: ssid.as_bytes().to_vec(),
            type_: fidl_fuchsia_wlan_policy::SecurityType::Wpa,
        };
        let credential = Credential::Password(password.as_bytes().to_vec());

        client_fsm::ConnectRequest { network, credential }
    }

    /// Holds all of the boilerplate required for testing IfaceManager.
    /// * DeviceServiceProxy and DeviceServiceRequestStream
    ///   * Allow for the construction of Clients and ClientIfaceContainers and the ability to send
    ///     responses to their requests.
    /// * ClientListenerMessageSender and MessageStream
    ///   * Allow for the construction of ClientIfaceContainers and the absorption of
    ///     ClientStateUpdates.
    /// * ApListenerMessageSender and MessageStream
    ///   * Allow for the construction of ApIfaceContainers and the absorption of
    ///     ApStateUpdates.
    /// * KnownEssStore, SavedNetworksManager, TempDir
    ///   * Allow for the querying of network credentials and storage of connection history.
    pub struct TestValues {
        pub device_service_proxy: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
        pub device_service_stream: fidl_fuchsia_wlan_device_service::DeviceServiceRequestStream,
        pub client_update_sender: listener::ClientListenerMessageSender,
        pub client_update_receiver: mpsc::UnboundedReceiver<listener::ClientListenerMessage>,
        pub ap_update_sender: listener::ApListenerMessageSender,
        pub ap_update_receiver: mpsc::UnboundedReceiver<listener::ApMessage>,
        pub client_event_sender: mpsc::Sender<client_fsm::ClientStateMachineNotification>,
        pub client_event_receiver: mpsc::Receiver<client_fsm::ClientStateMachineNotification>,
        pub saved_networks: Arc<SavedNetworksManager>,
    }

    /// Create a TestValues for a unit test.
    pub fn test_setup(exec: &mut Executor) -> TestValues {
        set_logger_for_test();
        let (proxy, requests) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceServiceMarker>()
                .expect("failed to create SeviceService proxy");
        let stream = requests.into_stream().expect("failed to create stream");

        let (client_sender, client_receiver) = mpsc::unbounded();
        let (ap_sender, ap_receiver) = mpsc::unbounded();
        let (fsm_sender, fsm_receiver) = mpsc::channel(0);

        let saved_networks = exec
            .run_singlethreaded(SavedNetworksManager::new_for_test())
            .expect("failed to create saved networks manager.");
        let saved_networks = Arc::new(saved_networks);

        TestValues {
            device_service_proxy: proxy,
            device_service_stream: stream,
            client_update_sender: client_sender,
            client_update_receiver: client_receiver,
            ap_update_sender: ap_sender,
            ap_update_receiver: ap_receiver,
            client_event_sender: fsm_sender,
            client_event_receiver: fsm_receiver,
            saved_networks: saved_networks,
        }
    }

    /// Creates a new PhyManagerPtr for tests.
    fn create_empty_phy_manager(
        device_service: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
    ) -> Arc<Mutex<dyn PhyManagerApi + Send>> {
        Arc::new(Mutex::new(phy_manager::PhyManager::new(device_service)))
    }

    struct FakePhyManager {
        create_iface_ok: bool,
        destroy_iface_ok: bool,
    }

    #[async_trait]
    impl PhyManagerApi for FakePhyManager {
        async fn add_phy(&mut self, _phy_id: u16) -> Result<(), PhyManagerError> {
            unimplemented!()
        }

        fn remove_phy(&mut self, _phy_id: u16) {
            unimplemented!()
        }

        async fn on_iface_added(&mut self, _iface_id: u16) -> Result<(), PhyManagerError> {
            unimplemented!()
        }

        fn on_iface_removed(&mut self, _iface_id: u16) {}

        async fn create_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
            if self.create_iface_ok {
                Ok(())
            } else {
                Err(PhyManagerError::IfaceCreateFailure)
            }
        }

        async fn destroy_all_client_ifaces(&mut self) -> Result<(), PhyManagerError> {
            if self.destroy_iface_ok {
                Ok(())
            } else {
                Err(PhyManagerError::IfaceDestroyFailure)
            }
        }

        fn get_client(&mut self) -> Option<u16> {
            Some(TEST_CLIENT_IFACE_ID)
        }

        async fn create_or_get_ap_iface(&mut self) -> Result<Option<u16>, PhyManagerError> {
            if self.create_iface_ok {
                Ok(Some(TEST_AP_IFACE_ID))
            } else {
                Err(PhyManagerError::IfaceCreateFailure)
            }
        }

        async fn destroy_ap_iface(&mut self, _iface_id: u16) -> Result<(), PhyManagerError> {
            if self.destroy_iface_ok {
                Ok(())
            } else {
                Err(PhyManagerError::IfaceDestroyFailure)
            }
        }

        async fn destroy_all_ap_ifaces(&mut self) -> Result<(), PhyManagerError> {
            if self.destroy_iface_ok {
                Ok(())
            } else {
                Err(PhyManagerError::IfaceDestroyFailure)
            }
        }

        fn suggest_ap_mac(&mut self, _mac: MacAddress) {
            unimplemented!()
        }

        fn get_phy_ids(&self) -> Vec<u16> {
            unimplemented!()
        }
    }

    struct FakeClient {
        connect_ok: bool,
        disconnect_ok: bool,
        exit_ok: bool,
    }

    impl FakeClient {
        fn new() -> Self {
            FakeClient { connect_ok: true, disconnect_ok: true, exit_ok: true }
        }
    }

    #[async_trait]
    impl client_fsm::ClientApi for FakeClient {
        fn connect(
            &mut self,
            _request: client_fsm::ConnectRequest,
            responder: oneshot::Sender<()>,
        ) -> Result<(), Error> {
            if self.connect_ok {
                let _ = responder.send(());
                Ok(())
            } else {
                Err(format_err!("fake failed to connect"))
            }
        }
        fn disconnect(&mut self, responder: oneshot::Sender<()>) -> Result<(), Error> {
            if self.disconnect_ok {
                let _ = responder.send(());
                Ok(())
            } else {
                Err(format_err!("fake failed to disconnect"))
            }
        }
        fn exit(&mut self, responder: oneshot::Sender<()>) -> Result<(), Error> {
            if self.exit_ok {
                let _ = responder.send(());
                Ok(())
            } else {
                Err(format_err!("fake failed to exit"))
            }
        }
    }

    struct FakeAp {
        start_succeeds: bool,
        stop_succeeds: bool,
        exit_succeeds: bool,
    }

    #[async_trait]
    impl AccessPointApi for FakeAp {
        fn start(
            &mut self,
            _request: ap_fsm::ApConfig,
            responder: ap_fsm::StartResponder,
        ) -> Result<(), anyhow::Error> {
            if self.start_succeeds {
                let _ = responder.send(fidl_fuchsia_wlan_sme::StartApResultCode::Success);
                Ok(())
            } else {
                Err(format_err!("start failed"))
            }
        }

        fn stop(&mut self, responder: oneshot::Sender<()>) -> Result<(), anyhow::Error> {
            if self.stop_succeeds {
                let _ = responder.send(());
                Ok(())
            } else {
                Err(format_err!("stop failed"))
            }
        }

        fn exit(&mut self, responder: oneshot::Sender<()>) -> Result<(), anyhow::Error> {
            if self.exit_succeeds {
                let _ = responder.send(());
                Ok(())
            } else {
                Err(format_err!("exit failed"))
            }
        }
    }

    fn create_iface_manager_with_client(
        test_values: &TestValues,
        configured: bool,
    ) -> (IfaceManagerService, StreamFuture<fidl_fuchsia_wlan_sme::ClientSmeRequestStream>) {
        let (sme_proxy, server) = create_proxy::<fidl_fuchsia_wlan_sme::ClientSmeMarker>()
            .expect("failed to create an sme channel");
        let mut client_container = ClientIfaceContainer {
            iface_id: TEST_CLIENT_IFACE_ID,
            sme_proxy,
            config: None,
            client_state_machine: Box::new(FakeClient::new()),
        };
        let phy_manager = FakePhyManager { create_iface_ok: true, destroy_iface_ok: true };
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender.clone(),
            test_values.ap_update_sender.clone(),
            test_values.device_service_proxy.clone(),
            test_values.saved_networks.clone(),
            test_values.client_event_sender.clone(),
        );

        if configured {
            client_container.config = Some(ap_types::NetworkIdentifier {
                ssid: TEST_SSID.as_bytes().to_vec(),
                type_: fidl_fuchsia_wlan_policy::SecurityType::Wpa,
            });
        }
        iface_manager.clients.push(client_container);

        (iface_manager, server.into_stream().unwrap().into_future())
    }

    fn create_ap_config(ssid: &str, password: &str) -> ap_fsm::ApConfig {
        let radio_config = RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6);
        ap_fsm::ApConfig {
            id: ap_types::NetworkIdentifier {
                ssid: ssid.as_bytes().to_vec(),
                type_: fidl_fuchsia_wlan_policy::SecurityType::None,
            },
            credential: password.as_bytes().to_vec(),
            radio_config,
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        }
    }

    fn create_iface_manager_with_ap(
        test_values: &TestValues,
        fake_ap: FakeAp,
    ) -> IfaceManagerService {
        let ap_container = ApIfaceContainer {
            iface_id: TEST_AP_IFACE_ID,
            config: None,
            ap_state_machine: Box::new(fake_ap),
        };
        let phy_manager = FakePhyManager { create_iface_ok: true, destroy_iface_ok: true };
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender.clone(),
            test_values.ap_update_sender.clone(),
            test_values.device_service_proxy.clone(),
            test_values.saved_networks.clone(),
            test_values.client_event_sender.clone(),
        );

        iface_manager.aps.push(ap_container);
        iface_manager
    }

    /// Tests the case where the only available client iface is one that has been configured.  The
    /// Client SME proxy associated with the configured iface should be used for scanning.
    #[test]
    fn test_scan_with_configured_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _next_sme_req) =
            create_iface_manager_with_client(&test_values, true);

        // Scan and ensure that the response is handled properly
        let scan_fut = iface_manager.scan(1, fidl_fuchsia_wlan_common::ScanType::Passive);

        pin_mut!(scan_fut);
        let _scan_proxy = match exec.run_until_stalled(&mut scan_fut) {
            Poll::Ready(proxy) => proxy,
            Poll::Pending => panic!("no scan was requested"),
        };
    }

    /// Tests the case where the only available client iface is one that has is unconfigured.  The
    /// Client SME proxy associated with the unconfigured iface should be used for scanning.
    #[test]
    fn test_scan_with_unconfigured_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create an unconfigured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _next_sme_req) =
            create_iface_manager_with_client(&test_values, false);

        // Scan and ensure that the response is handled properly
        let scan_fut = iface_manager.scan(1, fidl_fuchsia_wlan_common::ScanType::Passive);

        pin_mut!(scan_fut);
        let _scan_proxy = match exec.run_until_stalled(&mut scan_fut) {
            Poll::Ready(proxy) => proxy,
            Poll::Pending => panic!("no scan was requested"),
        };
    }

    /// Tests the scan behavior in the case where no client ifaces are present.  Ensures that the
    /// scan call results in an error.
    #[test]
    fn test_scan_with_no_ifaces() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a PhyManager with no knowledge of any client ifaces.
        let test_values = test_setup(&mut exec);
        let phy_manager = create_empty_phy_manager(test_values.device_service_proxy.clone());

        // Create and IfaceManager and issue a scan request.
        let mut iface_manager = IfaceManagerService::new(
            phy_manager,
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );
        let scan_fut = iface_manager.scan(1, fidl_fuchsia_wlan_common::ScanType::Passive);

        // Ensure that the scan request results in an error.
        pin_mut!(scan_fut);
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where a scan request cannot be made of the SME proxy.
    #[test]
    fn test_scan_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, next_sme_req) =
            create_iface_manager_with_client(&test_values, true);

        // Drop the SME's serving end so that the request to scan will fail.
        drop(next_sme_req);

        // Scan and ensure that an error is returned.
        let scan_fut = iface_manager.scan(1, fidl_fuchsia_wlan_common::ScanType::Passive);

        pin_mut!(scan_fut);
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where a scan is requested, the PhyManager provides a client iface ID, but
    /// when the IfaceManager attempts to create an SME proxy, the creation fails.
    #[test]
    fn test_scan_sme_creation_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create an IfaceManager and drop its client.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _next_sme_req) =
            create_iface_manager_with_client(&test_values, true);
        let _ = iface_manager.clients.pop();

        // Drop the serving end of our device service proxy so that the request to create an SME
        // proxy fails.
        drop(test_values.device_service_stream);

        // Scan and ensure that an error is returned.
        let scan_fut = iface_manager.scan(1, fidl_fuchsia_wlan_common::ScanType::Passive);

        pin_mut!(scan_fut);
        assert_variant!(exec.run_until_stalled(&mut scan_fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where connect is called and the only available client interface is already
    /// configured.
    #[test]
    fn test_connect_with_configured_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Update the saved networks with knowledge of the test SSID and credentials.
        let network_id = NetworkIdentifier::new(TEST_SSID.as_bytes().to_vec(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        exec.run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password");

        // Ask the IfaceManager to connect.
        let config = create_connect_request(TEST_SSID, TEST_PASSWORD);
        let connect_fut = iface_manager.connect(config);

        pin_mut!(connect_fut);
        let connect_response_fut = match exec.run_until_stalled(&mut connect_fut) {
            Poll::Ready(connect_result) => match connect_result {
                Ok(receiver) => receiver.into_future(),
                Err(e) => panic!("failed to connect with {}", e),
            },
            Poll::Pending => panic!("expected the connect request to finish"),
        };
        pin_mut!(connect_response_fut);

        assert_variant!(exec.run_until_stalled(&mut connect_response_fut), Poll::Ready(Ok(())));
    }

    /// Tests the case where connect is called while the only available interface is currently
    /// unconfigured.
    #[test]
    fn test_connect_with_unconfigured_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, false);

        // Add credentials for the test network to the saved networks.
        let network_id = NetworkIdentifier::new(TEST_SSID.as_bytes().to_vec(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        exec.run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password");

        {
            let config = create_connect_request(TEST_SSID, TEST_PASSWORD);
            let connect_fut = iface_manager.connect(config);

            pin_mut!(connect_fut);
            let connect_response_fut = match exec.run_until_stalled(&mut connect_fut) {
                Poll::Ready(connect_result) => match connect_result {
                    Ok(receiver) => receiver.into_future(),
                    Err(e) => panic!("failed to connect with {}", e),
                },
                Poll::Pending => panic!("expected the connect request to finish"),
            };
            pin_mut!(connect_response_fut);

            assert_variant!(exec.run_until_stalled(&mut connect_response_fut), Poll::Ready(Ok(())));
        }

        // Verify that the ClientIfaceContainer has been moved from unconfigured to configured.
        assert_eq!(iface_manager.clients.len(), 1);
        assert!(iface_manager.clients[0].config.is_some());
    }

    /// Tests the case where connect is called, but no client ifaces exist.
    #[test]
    fn test_connect_with_no_ifaces() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a PhyManager with no knowledge of any client ifaces.
        let test_values = test_setup(&mut exec);
        let phy_manager = create_empty_phy_manager(test_values.device_service_proxy.clone());

        let mut iface_manager = IfaceManagerService::new(
            phy_manager,
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        // Call connect on the IfaceManager
        let config = create_connect_request(TEST_SSID, TEST_PASSWORD);
        let connect_fut = iface_manager.connect(config);

        // Verify that the request to connect results in an error.
        pin_mut!(connect_fut);
        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where the request to connect fails.
    #[test]
    fn test_connect_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Make the client state machine's connect call fail.
        iface_manager.clients[0].client_state_machine =
            Box::new(FakeClient { connect_ok: false, disconnect_ok: true, exit_ok: false });

        // Update the saved networks with knowledge of the test SSID and credentials.
        let network_id = NetworkIdentifier::new(TEST_SSID.as_bytes().to_vec(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        exec.run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password");

        // Ask the IfaceManager to connect and make sure that it fails.
        let config = create_connect_request(TEST_SSID, TEST_PASSWORD);
        let connect_fut = iface_manager.connect(config);

        pin_mut!(connect_fut);
        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where the PhyManager knows of a client iface, but the IfaceManager is not
    /// able to create an SME proxy for it.
    #[test]
    fn test_connect_sme_creation_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create an IfaceManager and drop its client
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        let _ = iface_manager.clients.pop();

        // Drop the serving end of our device service proxy so that the request to create an SME
        // proxy fails.
        drop(test_values.device_service_stream);

        // Update the saved networks with knowledge of the test SSID and credentials.
        let network_id = NetworkIdentifier::new(TEST_SSID.as_bytes().to_vec(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        exec.run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password");

        // Ask the IfaceManager to connect and make sure that it fails.
        let config = create_connect_request(TEST_SSID, TEST_PASSWORD);
        let connect_fut = iface_manager.connect(config);

        pin_mut!(connect_fut);
        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where disconnect is called on a configured client.
    #[test]
    fn test_disconnect_configured_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        let network_id = NetworkIdentifier::new(TEST_SSID.as_bytes().to_vec(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        exec.run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password");

        {
            // Issue a call to disconnect from the network.
            let network_id = ap_types::NetworkIdentifier {
                ssid: TEST_SSID.as_bytes().to_vec(),
                type_: fidl_fuchsia_wlan_policy::SecurityType::Wpa,
            };
            let disconnect_fut = iface_manager.disconnect(network_id);

            // Ensure that disconnect returns a successful response.
            pin_mut!(disconnect_fut);
            assert_variant!(exec.run_until_stalled(&mut disconnect_fut), Poll::Ready(Ok(_)));
        }

        // Verify that the ClientIfaceContainer has been moved from configured to unconfigured.
        assert_eq!(iface_manager.clients.len(), 1);
        assert!(iface_manager.clients[0].config.is_none());
    }

    /// Tests the case where disconnect is called for a network for which the IfaceManager is not
    /// configured.  Verifies that the configured client is unaffected.
    #[test]
    fn test_disconnect_nonexistent_config() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a ClientIfaceContainer with a valid client.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Create a PhyManager with knowledge of a single client iface.
        let network_id = NetworkIdentifier::new(TEST_SSID.as_bytes().to_vec(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        exec.run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password");

        {
            // Issue a disconnect request for a bogus network configuration.
            let network_id = ap_types::NetworkIdentifier {
                ssid: "nonexistent_ssid".as_bytes().to_vec(),
                type_: fidl_fuchsia_wlan_policy::SecurityType::Wpa,
            };
            let disconnect_fut = iface_manager.disconnect(network_id);

            // Ensure that the request returns immediately.
            pin_mut!(disconnect_fut);
            assert!(exec.run_until_stalled(&mut disconnect_fut).is_ready());
        }

        // Verify that the configured client has not been affected.
        assert_eq!(iface_manager.clients.len(), 1);
        assert!(iface_manager.clients[0].config.is_some());
    }

    /// Tests the case where disconnect is called and no client ifaces are present.
    #[test]
    fn test_disconnect_no_clients() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        // Call disconnect on the IfaceManager
        let network_id = ap_types::NetworkIdentifier {
            ssid: "nonexistent_ssid".as_bytes().to_vec(),
            type_: fidl_fuchsia_wlan_policy::SecurityType::Wpa,
        };
        let disconnect_fut = iface_manager.disconnect(network_id);

        // Verify that disconnect returns immediately.
        pin_mut!(disconnect_fut);
        assert_variant!(exec.run_until_stalled(&mut disconnect_fut), Poll::Ready(Ok(_)));
    }

    /// Tests the case where the call to disconnect the client fails.
    #[test]
    fn test_disconnect_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Make the client state machine's connect call fail.
        iface_manager.clients[0].client_state_machine =
            Box::new(FakeClient { connect_ok: true, disconnect_ok: false, exit_ok: false });

        // Call disconnect on the IfaceManager
        let network_id = ap_types::NetworkIdentifier {
            ssid: TEST_SSID.as_bytes().to_vec(),
            type_: fidl_fuchsia_wlan_policy::SecurityType::Wpa,
        };
        let disconnect_fut = iface_manager.disconnect(network_id);

        pin_mut!(disconnect_fut);
        assert_variant!(exec.run_until_stalled(&mut disconnect_fut), Poll::Ready(Err(_)));
    }

    /// Tests stop_client_connections when there is a client that is connected.
    #[test]
    fn test_stop_connected_client() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let mut test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Create a PhyManager with a single, known client iface.
        let network_id = NetworkIdentifier::new(TEST_SSID.as_bytes().to_vec(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        exec.run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password");

        {
            // Stop all client connections.
            let stop_fut = iface_manager.stop_client_connections();
            pin_mut!(stop_fut);
            assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
        }

        // Ensure that no client interfaces are accounted for.
        assert!(iface_manager.clients.is_empty());

        // Ensure an update was sent
        let client_state_update = listener::ClientStateUpdate {
            state: Some(fidl_fuchsia_wlan_policy::WlanClientState::ConnectionsDisabled),
            networks: vec![],
        };
        assert_variant!(
            test_values.client_update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });
    }

    /// Call stop_client_connections when the only available client is unconfigured.
    #[test]
    fn test_stop_unconfigured_client() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Create a PhyManager with one known client.
        let network_id = NetworkIdentifier::new(TEST_SSID.as_bytes().to_vec(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        exec.run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password");

        {
            // Call stop_client_connections.
            let stop_fut = iface_manager.stop_client_connections();
            pin_mut!(stop_fut);
            assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
        }

        // Ensure there are no remaining client ifaces.
        assert!(iface_manager.clients.is_empty());
    }

    /// Tests the case where stop_client_connections is called, but there are no client ifaces.
    #[test]
    fn test_stop_no_clients() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create and empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        // Call stop_client_connections.
        let stop_fut = iface_manager.stop_client_connections();

        // Ensure stop_client_connections returns immediately and is successful.
        pin_mut!(stop_fut);
        assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
    }

    /// Tests the case where client connections are stopped, but stopping one of the client state
    /// machines fails.
    #[test]
    fn test_stop_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        iface_manager.clients[0].client_state_machine =
            Box::new(FakeClient { connect_ok: true, disconnect_ok: false, exit_ok: true });

        // Create a PhyManager with a single, known client iface.
        let network_id = NetworkIdentifier::new(TEST_SSID.as_bytes().to_vec(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        exec.run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password");

        {
            // Stop all client connections.
            let stop_fut = iface_manager.stop_client_connections();
            pin_mut!(stop_fut);
            assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
        }

        // Ensure that no client interfaces are accounted for.
        assert!(iface_manager.clients.is_empty());
    }

    /// Tests the case where client connections are stopped, but exiting one of the client state
    /// machines fails.
    #[test]
    fn test_stop_exit_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        iface_manager.clients[0].client_state_machine =
            Box::new(FakeClient { connect_ok: true, disconnect_ok: true, exit_ok: false });

        // Create a PhyManager with a single, known client iface.
        let network_id = NetworkIdentifier::new(TEST_SSID.as_bytes().to_vec(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        exec.run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password");

        {
            // Stop all client connections.
            let stop_fut = iface_manager.stop_client_connections();
            pin_mut!(stop_fut);
            assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
        }

        // Ensure that no client interfaces are accounted for.
        assert!(iface_manager.clients.is_empty());
    }

    /// Tests the case where the IfaceManager fails to tear down all of the client ifaces.
    #[test]
    fn test_stop_iface_destruction_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        iface_manager.phy_manager =
            Arc::new(Mutex::new(FakePhyManager { create_iface_ok: true, destroy_iface_ok: false }));

        // Create a PhyManager with a single, known client iface.
        let network_id = NetworkIdentifier::new(TEST_SSID.as_bytes().to_vec(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        exec.run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password");

        {
            // Stop all client connections.
            let stop_fut = iface_manager.stop_client_connections();
            pin_mut!(stop_fut);
            assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Err(_)));
        }

        // Ensure that no client interfaces are accounted for.
        assert!(iface_manager.clients.is_empty());
    }

    /// Tests the case where an existing iface is marked as idle.
    #[test]
    fn test_mark_iface_idle() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        assert!(iface_manager.clients[0].config.is_some());
        iface_manager.record_idle_client(TEST_CLIENT_IFACE_ID);
        assert!(iface_manager.clients[0].config.is_none());
    }

    /// Tests the case where a non-existent iface is marked as idle.
    #[test]
    fn test_mark_nonexistent_iface_idle() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        assert!(iface_manager.clients[0].config.is_some());
        iface_manager.record_idle_client(123);
        assert!(iface_manager.clients[0].config.is_some());
    }

    /// Tests the case where an iface is not configured and has_idle_client is called.
    #[test]
    fn test_unconfigured_iface_idle_check() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (iface_manager, _) = create_iface_manager_with_client(&test_values, false);
        assert!(iface_manager.has_idle_client());
    }

    /// Tests the case where an iface is not configured and has_idle_client is called.
    #[test]
    fn test_configured_iface_idle_check() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        assert!(!iface_manager.has_idle_client());
    }

    /// Tests the case where not ifaces are present and has_idle_client is called.
    #[test]
    fn test_no_ifaces_idle_check() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        let _ = iface_manager.clients.pop();
        assert!(!iface_manager.has_idle_client());
    }

    /// Tests the case where starting client connections succeeds.
    #[test]
    fn test_start_clients_succeeds() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        let start_fut = iface_manager.start_client_connections();

        // Ensure stop_client_connections returns immediately and is successful.
        pin_mut!(start_fut);
        assert_variant!(exec.run_until_stalled(&mut start_fut), Poll::Ready(Ok(_)));

        // Ensure an update was sent
        let client_state_update = listener::ClientStateUpdate {
            state: Some(fidl_fuchsia_wlan_policy::WlanClientState::ConnectionsEnabled),
            networks: vec![],
        };
        assert_variant!(
            test_values.client_update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });
    }

    /// Tests the case where starting client connections fails.
    #[test]
    fn test_start_clients_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        iface_manager.phy_manager =
            Arc::new(Mutex::new(FakePhyManager { create_iface_ok: false, destroy_iface_ok: true }));

        let start_fut = iface_manager.start_client_connections();
        pin_mut!(start_fut);
        assert_variant!(exec.run_until_stalled(&mut start_fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where the IfaceManager is able to request that the AP state machine start
    /// the access point.
    #[test]
    fn test_start_ap_succeeds() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(TEST_SSID, TEST_PASSWORD);

        let fut = iface_manager.start_ap(config);

        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(_)));
    }

    /// Tests the case where the IfaceManager is not able to request that the AP state machine start
    /// the access point.
    #[test]
    fn test_start_ap_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: false, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(TEST_SSID, TEST_PASSWORD);

        let fut = iface_manager.start_ap(config);

        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where start is called on the IfaceManager, but there are no AP ifaces.
    #[test]
    fn test_start_ap_no_ifaces() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        // Call start_ap.
        let config = create_ap_config(TEST_SSID, TEST_PASSWORD);
        let fut = iface_manager.start_ap(config);

        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where stop_ap is called for a config that is accounted for by the
    /// IfaceManager.
    #[test]
    fn test_stop_ap_succeeds() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(TEST_SSID, TEST_PASSWORD);
        iface_manager.aps[0].config = Some(config);

        {
            let fut = iface_manager
                .stop_ap(TEST_SSID.as_bytes().to_vec(), TEST_PASSWORD.as_bytes().to_vec());
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
        }
        assert!(iface_manager.aps.is_empty());
    }

    /// Tests the case where IfaceManager is requested to stop a config that is not accounted for.
    #[test]
    fn test_stop_ap_invalid_config() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);

        {
            let fut = iface_manager
                .stop_ap(TEST_SSID.as_bytes().to_vec(), TEST_PASSWORD.as_bytes().to_vec());
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
        }
        assert!(!iface_manager.aps.is_empty());
    }

    /// Tests the case where IfaceManager attempts to stop the AP state machine, but the request
    /// fails.
    #[test]
    fn test_stop_ap_stop_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: false, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(TEST_SSID, TEST_PASSWORD);
        iface_manager.aps[0].config = Some(config);

        {
            let fut = iface_manager
                .stop_ap(TEST_SSID.as_bytes().to_vec(), TEST_PASSWORD.as_bytes().to_vec());
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
        }

        assert!(iface_manager.aps.is_empty());
    }

    /// Tests the case where IfaceManager stops the AP state machine, but the request to exit
    /// fails.
    #[test]
    fn test_stop_ap_exit_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: false };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(TEST_SSID, TEST_PASSWORD);
        iface_manager.aps[0].config = Some(config);

        {
            let fut = iface_manager
                .stop_ap(TEST_SSID.as_bytes().to_vec(), TEST_PASSWORD.as_bytes().to_vec());
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
        }

        assert!(iface_manager.aps.is_empty());
    }

    /// Tests the case where stop is called on the IfaceManager, but there are no AP ifaces.
    #[test]
    fn test_stop_ap_no_ifaces() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );
        let fut =
            iface_manager.stop_ap(TEST_SSID.as_bytes().to_vec(), TEST_PASSWORD.as_bytes().to_vec());
        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    /// Tests the case where stop_all_aps is called and it succeeds.
    #[test]
    fn test_stop_all_aps_succeeds() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);

        // Insert a second iface and add it to the list of APs.
        let second_iface = ApIfaceContainer {
            iface_id: 2,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
        };
        iface_manager.aps.push(second_iface);

        {
            let fut = iface_manager.stop_all_aps();
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
        }
        assert!(iface_manager.aps.is_empty());
    }

    /// Tests the case where stop_all_aps is called and the request to stop fails for an iface.
    #[test]
    fn test_stop_all_aps_stop_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: false, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);

        // Insert a second iface and add it to the list of APs.
        let second_iface = ApIfaceContainer {
            iface_id: 2,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
        };
        iface_manager.aps.push(second_iface);

        {
            let fut = iface_manager.stop_all_aps();
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
        }
        assert!(iface_manager.aps.is_empty());
    }

    /// Tests the case where stop_all_aps is called and the request to stop fails for an iface.
    #[test]
    fn test_stop_all_aps_exit_fails() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: false };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);

        // Insert a second iface and add it to the list of APs.
        let second_iface = ApIfaceContainer {
            iface_id: 2,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
        };
        iface_manager.aps.push(second_iface);

        {
            let fut = iface_manager.stop_all_aps();
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
        }
        assert!(iface_manager.aps.is_empty());
    }

    /// Tests the case where stop_all_aps is called on the IfaceManager, but there are no AP
    /// ifaces.
    #[test]
    fn test_stop_all_aps_no_ifaces() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        let fut = iface_manager.stop_all_aps();
        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    #[test]
    fn test_remove_client_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create an IfaceManager with a client and an AP.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _next_sme_req) =
            create_iface_manager_with_client(&test_values, true);

        let ap_iface = ApIfaceContainer {
            iface_id: TEST_AP_IFACE_ID,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
        };
        iface_manager.aps.push(ap_iface);

        // Notify the IfaceManager that the client interface has been removed.
        {
            let fut = iface_manager.handle_removed_iface(TEST_CLIENT_IFACE_ID);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        assert!(iface_manager.clients.is_empty());
        assert!(!iface_manager.aps.is_empty());
    }

    #[test]
    fn test_remove_ap_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create an IfaceManager with a client and an AP.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _next_sme_req) =
            create_iface_manager_with_client(&test_values, true);

        let ap_iface = ApIfaceContainer {
            iface_id: TEST_AP_IFACE_ID,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
        };
        iface_manager.aps.push(ap_iface);

        // Notify the IfaceManager that the AP interface has been removed.
        {
            let fut = iface_manager.handle_removed_iface(TEST_AP_IFACE_ID);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        assert!(!iface_manager.clients.is_empty());
        assert!(iface_manager.aps.is_empty());
    }

    #[test]
    fn test_remove_nonexistent_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create an IfaceManager with a client and an AP.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _next_sme_req) =
            create_iface_manager_with_client(&test_values, true);

        let ap_iface = ApIfaceContainer {
            iface_id: TEST_AP_IFACE_ID,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
        };
        iface_manager.aps.push(ap_iface);

        // Notify the IfaceManager that an interface that has not been accounted for has been
        // removed.
        {
            let fut = iface_manager.handle_removed_iface(1234);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        assert!(!iface_manager.clients.is_empty());
        assert!(!iface_manager.aps.is_empty());
    }

    fn poll_device_service_req(
        exec: &mut fuchsia_async::Executor,
        next_req: &mut StreamFuture<fidl_fuchsia_wlan_device_service::DeviceServiceRequestStream>,
    ) -> Poll<fidl_fuchsia_wlan_device_service::DeviceServiceRequest> {
        exec.run_until_stalled(next_req).map(|(req, stream)| {
            *next_req = stream.into_future();
            req.expect("did not expect the SME request stream to end")
                .expect("error polling SME request stream")
        })
    }

    #[test]
    fn test_add_client_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        {
            // Notify the IfaceManager of a new interface.
            let fut = iface_manager.handle_added_iface(TEST_CLIENT_IFACE_ID);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

            // Expect and interface query and notify that this is a client interface.
            let mut device_service_fut = test_values.device_service_stream.into_future();
            assert_variant!(
                poll_device_service_req(&mut exec, &mut device_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceServiceRequest::QueryIface {
                    iface_id: TEST_CLIENT_IFACE_ID, responder
                }) => {
                    let mut response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                        role: fidl_fuchsia_wlan_device::MacRole::Client,
                        id: TEST_CLIENT_IFACE_ID,
                        phy_id: 0,
                        phy_assigned_id: 0,
                        mac_addr: [0; 6]
                    };
                    responder
                        .send(fuchsia_zircon::sys::ZX_OK, Some(&mut response))
                        .expect("Sending iface response");
                }
            );

            // Expect that we have requested a client SME proxy.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            let responder = assert_variant!(
                poll_device_service_req(&mut exec, &mut device_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceServiceRequest::GetClientSme {
                    iface_id: TEST_CLIENT_IFACE_ID, sme: _, responder
                }) => responder
            );

            // Send back a positive acknowledgement.
            assert!(responder.send(fuchsia_zircon::sys::ZX_OK).is_ok());

            // Run the future to completion.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        // Ensure that the client interface has been added.
        assert!(iface_manager.aps.is_empty());
        assert_eq!(iface_manager.clients[0].iface_id, TEST_CLIENT_IFACE_ID);
    }

    #[test]
    fn test_add_ap_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        {
            // Notify the IfaceManager of a new interface.
            let fut = iface_manager.handle_added_iface(TEST_AP_IFACE_ID);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

            // Expect that the interface properties are queried and notify that it is an AP iface.
            let mut device_service_fut = test_values.device_service_stream.into_future();
            assert_variant!(
                poll_device_service_req(&mut exec, &mut device_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceServiceRequest::QueryIface {
                    iface_id: TEST_AP_IFACE_ID, responder
                }) => {
                    let mut response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                        role: fidl_fuchsia_wlan_device::MacRole::Ap,
                        id: TEST_AP_IFACE_ID,
                        phy_id: 0,
                        phy_assigned_id: 0,
                        mac_addr: [0; 6]
                    };
                    responder
                        .send(fuchsia_zircon::sys::ZX_OK, Some(&mut response))
                        .expect("Sending iface response");
                }
            );

            // Run the future so that an AP SME proxy is requested.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

            let responder = assert_variant!(
                poll_device_service_req(&mut exec, &mut device_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceServiceRequest::GetApSme {
                    iface_id: TEST_AP_IFACE_ID, sme: _, responder
                }) => responder
            );

            // Send back a positive acknowledgement.
            assert!(responder.send(fuchsia_zircon::sys::ZX_OK).is_ok());

            // Run the future to completion.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        // Ensure that the AP interface has been added.
        assert!(iface_manager.clients.is_empty());
        assert_eq!(iface_manager.aps[0].iface_id, TEST_AP_IFACE_ID);
    }

    #[test]
    fn test_add_nonexistent_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        {
            // Notify the IfaceManager of a new interface.
            let fut = iface_manager.handle_added_iface(TEST_AP_IFACE_ID);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

            // Expect an iface query and send back an error
            let mut device_service_fut = test_values.device_service_stream.into_future();
            assert_variant!(
                poll_device_service_req(&mut exec, &mut device_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceServiceRequest::QueryIface {
                    iface_id: TEST_AP_IFACE_ID, responder
                }) => {
                    responder
                        .send(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND, None)
                        .expect("Sending iface response");
                }
            );

            // Run the future to completion.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        // Ensure that no interfaces have been added.
        assert!(iface_manager.clients.is_empty());
        assert!(iface_manager.aps.is_empty());
    }

    #[test]
    fn test_add_existing_client_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _next_sme_req) =
            create_iface_manager_with_client(&test_values, true);

        // Notify the IfaceManager of a new interface.
        {
            let fut = iface_manager.handle_added_iface(TEST_CLIENT_IFACE_ID);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

            // Expect an interface query and notify that it is a client.
            let mut device_service_fut = test_values.device_service_stream.into_future();
            assert_variant!(
                poll_device_service_req(&mut exec, &mut device_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceServiceRequest::QueryIface {
                    iface_id: TEST_CLIENT_IFACE_ID, responder
                }) => {
                    let mut response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                        role: fidl_fuchsia_wlan_device::MacRole::Client,
                        id: TEST_CLIENT_IFACE_ID,
                        phy_id: 0,
                        phy_assigned_id: 0,
                        mac_addr: [0; 6]
                    };
                    responder
                        .send(fuchsia_zircon::sys::ZX_OK, Some(&mut response))
                        .expect("Sending iface response");
                }
            );

            // The future should then run to completion as it finds the existing interface.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        // Verify that nothing new has been appended to the clients vector or the aps vector.
        assert_eq!(iface_manager.clients.len(), 1);
        assert_eq!(iface_manager.aps.len(), 0);
    }

    #[test]
    fn test_add_existing_ap_iface() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);

        // Notify the IfaceManager of a new interface.
        {
            let fut = iface_manager.handle_added_iface(TEST_AP_IFACE_ID);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

            // Expect an interface query and notify that it is a client.
            let mut device_service_fut = test_values.device_service_stream.into_future();
            assert_variant!(
                poll_device_service_req(&mut exec, &mut device_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceServiceRequest::QueryIface {
                    iface_id: TEST_AP_IFACE_ID, responder
                }) => {
                    let mut response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                        role: fidl_fuchsia_wlan_device::MacRole::Ap,
                        id: TEST_AP_IFACE_ID,
                        phy_id: 0,
                        phy_assigned_id: 0,
                        mac_addr: [0; 6]
                    };
                    responder
                        .send(fuchsia_zircon::sys::ZX_OK, Some(&mut response))
                        .expect("Sending iface response");
                }
            );

            // The future should then run to completion as it finds the existing interface.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        // Verify that nothing new has been appended to the clients vector or the aps vector.
        assert_eq!(iface_manager.clients.len(), 0);
        assert_eq!(iface_manager.aps.len(), 1);
    }

    enum TestType {
        Pass,
        Fail,
        ClientError,
    }

    fn run_service_test<T: std::fmt::Debug>(
        mut exec: fuchsia_async::Executor,
        iface_manager: IfaceManagerService,
        req: IfaceManagerRequest,
        mut req_receiver: oneshot::Receiver<Result<T, Error>>,
        test_type: TestType,
    ) {
        // Start the service loop
        let (mut sender, receiver) = mpsc::channel(1);
        let serve_fut = serve_iface_manager_requests(iface_manager, receiver);
        pin_mut!(serve_fut);

        // Send the client's request
        sender.try_send(req).expect("failed to send request");

        match test_type {
            TestType::Pass => {
                // Process the request.
                assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

                // Assert that the receiving end gets a successful result.
                assert_variant!(exec.run_until_stalled(&mut req_receiver), Poll::Ready(Ok(Ok(_))));
            }
            TestType::Fail => {
                // Process the request.
                assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

                // Assert that the receiving end gets a successful result.
                assert_variant!(exec.run_until_stalled(&mut req_receiver), Poll::Ready(Ok(Err(_))));
            }
            TestType::ClientError => {
                // Simulate a client failure.
                drop(req_receiver);
            }
        }

        // Make sure the service keeps running.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
    }

    fn run_service_test_with_unit_return(
        mut exec: fuchsia_async::Executor,
        iface_manager: IfaceManagerService,
        req: IfaceManagerRequest,
        mut req_receiver: oneshot::Receiver<()>,
        test_type: TestType,
    ) {
        // Start the service loop
        let (mut sender, receiver) = mpsc::channel(1);
        let serve_fut = serve_iface_manager_requests(iface_manager, receiver);
        pin_mut!(serve_fut);

        // Send the client's request
        sender.try_send(req).expect("failed to send request");

        match test_type {
            TestType::Pass => {
                // Process the request.
                assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

                // Assert that the receiving end gets a successful result.
                assert_variant!(exec.run_until_stalled(&mut req_receiver), Poll::Ready(Ok(())));
            }
            TestType::Fail => {
                // Process the request.
                assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

                // Assert that the receiving end gets a successful result.
                assert_variant!(exec.run_until_stalled(&mut req_receiver), Poll::Ready(Ok(())));
            }
            TestType::ClientError => {
                // Simulate a client failure.
                drop(req_receiver);
            }
        }

        // Make sure the service keeps running.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
    }

    #[test_case(FakeClient {connect_ok: true, disconnect_ok: true, exit_ok: true}, TestType::Pass; "successfully disconnects configured client")]
    #[test_case(FakeClient {connect_ok: true, disconnect_ok: false, exit_ok: true}, TestType::Fail; "fails to disconnect configured client")]
    #[test_case(FakeClient {connect_ok: true, disconnect_ok: true, exit_ok: true}, TestType::ClientError; "client drops receiver")]
    fn service_disconnect_test(fake_client: FakeClient, test_type: TestType) {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _stream) = create_iface_manager_with_client(&test_values, true);
        iface_manager.clients[0].client_state_machine = Box::new(fake_client);

        // Send a disconnect request.
        let (ack_sender, ack_receiver) = oneshot::channel();
        let req = DisconnectRequest {
            network_id: fidl_fuchsia_wlan_policy::NetworkIdentifier {
                ssid: TEST_SSID.as_bytes().to_vec(),
                type_: fidl_fuchsia_wlan_policy::SecurityType::Wpa,
            },
            responder: ack_sender,
        };
        let req = IfaceManagerRequest::Disconnect(req);

        run_service_test(exec, iface_manager, req, ack_receiver, test_type);
    }

    #[test_case(FakeClient {connect_ok: true, disconnect_ok: true, exit_ok: true}, TestType::Pass; "successfully connected a client")]
    #[test_case(FakeClient {connect_ok: false, disconnect_ok: true, exit_ok: true}, TestType::Fail; "fails to connect a client")]
    #[test_case(FakeClient {connect_ok: true, disconnect_ok: true, exit_ok: true}, TestType::ClientError; "client drops receiver")]
    fn service_connect_test(fake_client: FakeClient, test_type: TestType) {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _stream) = create_iface_manager_with_client(&test_values, false);
        iface_manager.clients[0].client_state_machine = Box::new(fake_client);

        // Send a connect request.
        let (ack_sender, ack_receiver) = oneshot::channel();
        let req = ConnectRequest {
            request: create_connect_request(TEST_SSID, TEST_PASSWORD),
            responder: ack_sender,
        };
        let req = IfaceManagerRequest::Connect(req);

        run_service_test(exec, iface_manager, req, ack_receiver, test_type);
    }

    // This test is a bit of a twofer as it covers both the mechanism for recording idle interfaces
    // as well as the mechanism for querying idle interfaces.
    #[test]
    fn test_service_record_idle_client() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (iface_manager, _stream) = create_iface_manager_with_client(&test_values, true);

        // Assert that there are no idle interfaces to begin with as we created the interface
        // manager with a configured client interface.
        assert!(!iface_manager.has_idle_client());

        // Create mpsc channel to handle requests.
        let (mut sender, receiver) = mpsc::channel(1);
        let serve_fut = serve_iface_manager_requests(iface_manager, receiver);
        pin_mut!(serve_fut);

        // Query the service to see if any interfaces are idle.
        let (idle_iface_sender, mut idle_iface_receiver) = oneshot::channel();
        let req = HasIdleIfaceRequest { responder: idle_iface_sender };
        let req = IfaceManagerRequest::HasIdleIface(req);
        sender.try_send(req).expect("failed to send request");

        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut idle_iface_receiver), Poll::Ready(Ok(false)));

        // Send an idle interface request
        let (ack_sender, mut ack_receiver) = oneshot::channel();
        let req = RecordIdleIfaceRequest { iface_id: TEST_CLIENT_IFACE_ID, responder: ack_sender };
        let req = IfaceManagerRequest::RecordIdleIface(req);

        sender.try_send(req).expect("failed to send request");

        // Run the service loop and expect the request to be serviced and for the loop to not exit.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Wait for the response.
        assert_variant!(exec.run_until_stalled(&mut ack_receiver), Poll::Ready(Ok(())));

        // Check if an idle interface is present.
        let (idle_iface_sender, mut idle_iface_receiver) = oneshot::channel();
        let req = HasIdleIfaceRequest { responder: idle_iface_sender };
        let req = IfaceManagerRequest::HasIdleIface(req);
        sender.try_send(req).expect("failed to send request");

        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Make sure that the interface has been marked idle.
        assert_variant!(exec.run_until_stalled(&mut idle_iface_receiver), Poll::Ready(Ok(true)));
    }

    #[test]
    fn test_service_record_idle_client_response_failure() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (iface_manager, _stream) = create_iface_manager_with_client(&test_values, true);

        // Create mpsc channel to handle requests.
        let (mut sender, receiver) = mpsc::channel(1);
        let serve_fut = serve_iface_manager_requests(iface_manager, receiver);
        pin_mut!(serve_fut);

        // Send an idle interface request
        let (ack_sender, ack_receiver) = oneshot::channel();
        let req = RecordIdleIfaceRequest { iface_id: TEST_CLIENT_IFACE_ID, responder: ack_sender };
        let req = IfaceManagerRequest::RecordIdleIface(req);

        sender.try_send(req).expect("failed to send request");

        // Drop the receiving end and make sure the service continues running.
        drop(ack_receiver);

        // Run the service loop and expect the request to be serviced and for the loop to not exit.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
    }

    #[test]
    fn test_service_query_idle_client_response_failure() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (iface_manager, _stream) = create_iface_manager_with_client(&test_values, true);

        // Create mpsc channel to handle requests.
        let (mut sender, receiver) = mpsc::channel(1);
        let serve_fut = serve_iface_manager_requests(iface_manager, receiver);
        pin_mut!(serve_fut);

        // Check if an idle interface is present.
        let (idle_iface_sender, idle_iface_receiver) = oneshot::channel();
        let req = HasIdleIfaceRequest { responder: idle_iface_sender };
        let req = IfaceManagerRequest::HasIdleIface(req);
        sender.try_send(req).expect("failed to send request");

        // Drop the receiving end and make sure the service continues running.
        drop(idle_iface_receiver);

        // Run the service loop and expect the request to be serviced and for the loop to not exit.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
    }

    #[test]
    fn test_service_add_iface_succeeds() {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
        let iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        // Create mpsc channel to handle requests.
        let (mut sender, receiver) = mpsc::channel(1);
        let serve_fut = serve_iface_manager_requests(iface_manager, receiver);
        pin_mut!(serve_fut);

        // Report a new interface.
        let (new_iface_sender, mut new_iface_receiver) = oneshot::channel();
        let req = AddIfaceRequest { iface_id: TEST_CLIENT_IFACE_ID, responder: new_iface_sender };
        let req = IfaceManagerRequest::AddIface(req);
        sender.try_send(req).expect("failed to send request");

        // Run the service loop to begin processing the request.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Expect and interface query and notify that this is a client interface.
        let mut device_service_fut = test_values.device_service_stream.into_future();
        assert_variant!(
            poll_device_service_req(&mut exec, &mut device_service_fut),
            Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceServiceRequest::QueryIface {
                iface_id: TEST_CLIENT_IFACE_ID, responder
            }) => {
                let mut response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                    role: fidl_fuchsia_wlan_device::MacRole::Client,
                    id: TEST_CLIENT_IFACE_ID,
                    phy_id: 0,
                    phy_assigned_id: 0,
                    mac_addr: [0; 6]
                };
                responder
                    .send(fuchsia_zircon::sys::ZX_OK, Some(&mut response))
                    .expect("Sending iface response");
            }
        );

        // Expect that we have requested a client SME proxy.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let responder = assert_variant!(
            poll_device_service_req(&mut exec, &mut device_service_fut),
            Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceServiceRequest::GetClientSme {
                iface_id: TEST_CLIENT_IFACE_ID, sme: _, responder
            }) => responder
        );

        // Send back a positive acknowledgement.
        assert!(responder.send(fuchsia_zircon::sys::ZX_OK).is_ok());

        // Run the service again to ensure the response is sent.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Check that the response was received.
        assert_variant!(exec.run_until_stalled(&mut new_iface_receiver), Poll::Ready(Ok(())));
    }

    #[test_case(TestType::Fail; "failed to add interface")]
    #[test_case(TestType::ClientError; "client dropped receiver")]
    fn service_add_iface_negative_tests(test_type: TestType) {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
        let iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        // Report a new interface.
        let (new_iface_sender, new_iface_receiver) = oneshot::channel();
        let req = AddIfaceRequest { iface_id: TEST_CLIENT_IFACE_ID, responder: new_iface_sender };
        let req = IfaceManagerRequest::AddIface(req);

        // Drop the device stream so that querying the interface properties will fail.
        drop(test_values.device_service_stream);

        run_service_test_with_unit_return(exec, iface_manager, req, new_iface_receiver, test_type);
    }

    #[test_case(TestType::Pass; "successfully removed iface")]
    #[test_case(TestType::ClientError; "client dropped receiving end")]
    fn service_remove_iface_test(test_type: TestType) {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (iface_manager, _stream) = create_iface_manager_with_client(&test_values, true);

        // Notify of interface removal.
        let (remove_iface_sender, remove_iface_receiver) = oneshot::channel();
        let req =
            RemoveIfaceRequest { iface_id: TEST_CLIENT_IFACE_ID, responder: remove_iface_sender };
        let req = IfaceManagerRequest::RemoveIface(req);

        run_service_test_with_unit_return(
            exec,
            iface_manager,
            req,
            remove_iface_receiver,
            test_type,
        );
    }

    #[test_case(TestType::Pass; "successfully scanned")]
    #[test_case(TestType::Fail; "failed to scanned")]
    #[test_case(TestType::ClientError; "client drops receiver")]
    fn service_scan_test(test_type: TestType) {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (iface_manager, _stream) = match test_type {
            TestType::Pass | TestType::ClientError => {
                let (iface_manager, stream) = create_iface_manager_with_client(&test_values, true);
                (iface_manager, Some(stream))
            }
            TestType::Fail => {
                let phy_manager =
                    phy_manager::PhyManager::new(test_values.device_service_proxy.clone());
                let iface_manager = IfaceManagerService::new(
                    Arc::new(Mutex::new(phy_manager)),
                    test_values.client_update_sender,
                    test_values.ap_update_sender,
                    test_values.device_service_proxy,
                    test_values.saved_networks,
                    test_values.client_event_sender,
                );
                (iface_manager, None)
            }
        };

        // Make a scan request.
        let (scan_sender, scan_receiver) = oneshot::channel();
        let req = ScanRequest {
            timeout: 10,
            scan_type: fidl_fuchsia_wlan_common::ScanType::Active,
            responder: scan_sender,
        };
        let req = IfaceManagerRequest::Scan(req);

        run_service_test(exec, iface_manager, req, scan_receiver, test_type);
    }

    #[test_case(FakePhyManager { create_iface_ok: true, destroy_iface_ok: true }, TestType::Pass; "successfully started client connections")]
    #[test_case(FakePhyManager { create_iface_ok: false, destroy_iface_ok: true }, TestType::Fail; "failed to start client connections")]
    #[test_case(FakePhyManager { create_iface_ok: true, destroy_iface_ok: true }, TestType::ClientError; "client dropped receiver")]
    fn service_start_client_connections_test(phy_manager: FakePhyManager, test_type: TestType) {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        // Make start client connections request
        let (start_sender, start_receiver) = oneshot::channel();
        let req = StartClientConnectionsRequest { responder: start_sender };
        let req = IfaceManagerRequest::StartClientConnections(req);

        run_service_test(exec, iface_manager, req, start_receiver, test_type);
    }

    #[test_case(FakePhyManager { create_iface_ok: true, destroy_iface_ok: true }, TestType::Pass; "successfully stopped client connections")]
    #[test_case(FakePhyManager { create_iface_ok: true, destroy_iface_ok: false }, TestType::Fail; "failed to stop client connections")]
    #[test_case(FakePhyManager { create_iface_ok: true, destroy_iface_ok: true }, TestType::ClientError; "client dropped receiver")]
    fn service_stop_client_connections_test(phy_manager: FakePhyManager, test_type: TestType) {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.device_service_proxy,
            test_values.saved_networks,
            test_values.client_event_sender,
        );

        // Make stop client connections request
        let (stop_sender, stop_receiver) = oneshot::channel();
        let req = StopClientConnectionsRequest { responder: stop_sender };
        let req = IfaceManagerRequest::StopClientConnections(req);

        run_service_test(exec, iface_manager, req, stop_receiver, test_type);
    }

    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::Pass; "successfully starts AP")]
    #[test_case(FakeAp { start_succeeds: false, stop_succeeds: true, exit_succeeds: true }, TestType::Fail; "fails to start AP")]
    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::ClientError; "client drops receiver")]
    fn service_start_ap_test(fake_ap: FakeAp, test_type: TestType) {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an IfaceManager with a fake AP interface.
        let iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);

        // Request that an AP be started.
        let (start_sender, start_receiver) = oneshot::channel();
        let req = StartApRequest {
            config: create_ap_config(TEST_SSID, TEST_PASSWORD),
            responder: start_sender,
        };
        let req = IfaceManagerRequest::StartAp(req);

        run_service_test(exec, iface_manager, req, start_receiver, test_type);
    }

    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::Pass; "successfully stops AP")]
    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: false, exit_succeeds: true }, TestType::Fail; "fails to stop AP")]
    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::ClientError; "client drops receiver")]
    fn service_stop_ap_test(fake_ap: FakeAp, test_type: TestType) {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an IfaceManager with a configured fake AP interface.
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(TEST_SSID, TEST_PASSWORD);
        iface_manager.aps[0].config = Some(config);

        // Request that an AP be stopped.
        let (stop_sender, stop_receiver) = oneshot::channel();
        let req = StopApRequest {
            ssid: TEST_SSID.as_bytes().to_vec(),
            password: TEST_PASSWORD.as_bytes().to_vec(),
            responder: stop_sender,
        };
        let req = IfaceManagerRequest::StopAp(req);

        run_service_test(exec, iface_manager, req, stop_receiver, test_type);
    }

    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::Pass; "successfully stops AP")]
    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: false, exit_succeeds: true }, TestType::Fail; "fails to stop AP")]
    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::ClientError; "client drops receiver")]
    fn service_stop_all_aps_test(fake_ap: FakeAp, test_type: TestType) {
        let mut exec = fuchsia_async::Executor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an IfaceManager with a fake AP interface.
        let iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);

        // Request that an AP be started.
        let (stop_sender, stop_receiver) = oneshot::channel();
        let req = StopAllApsRequest { responder: stop_sender };
        let req = IfaceManagerRequest::StopAllAps(req);

        run_service_test(exec, iface_manager, req, stop_receiver, test_type);
    }
}
