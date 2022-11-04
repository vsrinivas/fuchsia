// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        access_point::{state_machine as ap_fsm, state_machine::AccessPointApi, types as ap_types},
        client::{
            network_selection::{score_connection_quality, NetworkSelector},
            state_machine::{
                self as client_fsm, ConnectionStatsReceiver, ConnectionStatsSender,
                PeriodicConnectionStats,
            },
            types as client_types,
        },
        config_management::SavedNetworksManagerApi,
        mode_management::{
            iface_manager_api::{ConnectAttemptRequest, IfaceManagerApi, SmeForScan},
            iface_manager_types::*,
            phy_manager::{CreateClientIfacesReason, PhyManagerApi},
            Defect,
        },
        telemetry::{TelemetryEvent, TelemetrySender},
        util::{atomic_oneshot_stream, future_with_metadata, listener},
    },
    anyhow::{format_err, Error},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy, fidl_fuchsia_wlan_sme,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{
        channel::{mpsc, oneshot},
        future::{ready, BoxFuture},
        lock::Mutex,
        select,
        stream::FuturesUnordered,
        FutureExt, StreamExt,
    },
    log::{debug, error, info, warn},
    std::{convert::Infallible, fmt::Debug, sync::Arc, unimplemented},
};

// Maximum allowed interval between scans when attempting to reconnect client interfaces.  This
// value is taken from legacy state machine.
const MAX_AUTO_CONNECT_RETRY_SECONDS: i64 = 10;

/// The threshold for whether or not to look for another network to roam to. The value that this
/// is is compared against should be between 0 and 1.
const THRESHOLD_BAD_CONNECTION: f32 = 0.0;

/// The time to wait between roam scans to avoid constant scanning.
const DURATION_BETWEEN_ROAM_SCANS: zx::Duration = zx::Duration::from_seconds(5 * 60);

/// Wraps around vital information associated with a WLAN client interface.  In all cases, a client
/// interface will have an ID and a ClientSmeProxy to make requests of the interface.  If a client
/// is configured to connect to a WLAN network, it will store the network configuration information
/// associated with that network as well as a communcation channel to make requests of the state
/// machine that maintains client connectivity.
struct ClientIfaceContainer {
    iface_id: u16,
    sme_proxy: fidl_fuchsia_wlan_sme::ClientSmeProxy,
    config: Option<ap_types::NetworkIdentifier>,
    client_state_machine: Option<Box<dyn client_fsm::ClientApi + Send>>,
    security_support: fidl_common::SecuritySupport,
    /// The time of the last scan for roaming or new connection on this iface.
    last_roam_time: fasync::Time,
}

pub(crate) struct ApIfaceContainer {
    pub iface_id: u16,
    pub config: Option<ap_fsm::ApConfig>,
    pub ap_state_machine: Box<dyn AccessPointApi + Send + Sync>,
    enabled_time: Option<zx::Time>,
}

#[derive(Clone, Debug)]
pub struct StateMachineMetadata {
    pub iface_id: u16,
    pub role: fidl_fuchsia_wlan_common::WlanMacRole,
}

async fn create_client_state_machine(
    iface_id: u16,
    dev_monitor_proxy: &mut fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
    client_update_sender: listener::ClientListenerMessageSender,
    saved_networks: Arc<dyn SavedNetworksManagerApi>,
    connect_selection: Option<client_types::ConnectSelection>,
    telemetry_sender: TelemetrySender,
    stats_sender: ConnectionStatsSender,
    defect_sender: mpsc::UnboundedSender<Defect>,
) -> Result<
    (
        Box<dyn client_fsm::ClientApi + Send>,
        future_with_metadata::FutureWithMetadata<(), StateMachineMetadata>,
    ),
    Error,
> {
    if connect_selection.is_some() {
        telemetry_sender.send(TelemetryEvent::StartEstablishConnection { reset_start_time: false });
    }

    // Create a client state machine for the newly discovered interface.
    let (sender, receiver) = mpsc::channel(1);
    let new_client = client_fsm::Client::new(sender);

    // Create a new client SME proxy.  This is required because each new client state machine will
    // take the event stream from the SME proxy.  A subsequent attempt to take the event stream
    // would cause wlancfg to panic.
    let (sme_proxy, remote) = create_proxy()?;
    dev_monitor_proxy.get_client_sme(iface_id, remote).await?.map_err(zx::Status::from_raw)?;
    let event_stream = sme_proxy.take_event_stream();

    let fut = client_fsm::serve(
        iface_id,
        sme_proxy,
        event_stream,
        receiver,
        client_update_sender,
        saved_networks,
        connect_selection,
        telemetry_sender,
        stats_sender,
        defect_sender,
    );

    let metadata =
        StateMachineMetadata { iface_id, role: fidl_fuchsia_wlan_common::WlanMacRole::Client };
    let fut = future_with_metadata::FutureWithMetadata::new(metadata, Box::pin(fut));

    Ok((Box::new(new_client), fut))
}

/// Accounts for WLAN interfaces that are present and utilizes them to service requests that are
/// made of the policy layer.
pub(crate) struct IfaceManagerService {
    phy_manager: Arc<Mutex<dyn PhyManagerApi + Send>>,
    client_update_sender: listener::ClientListenerMessageSender,
    ap_update_sender: listener::ApListenerMessageSender,
    dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
    clients: Vec<ClientIfaceContainer>,
    aps: Vec<ApIfaceContainer>,
    saved_networks: Arc<dyn SavedNetworksManagerApi>,
    fsm_futures:
        FuturesUnordered<future_with_metadata::FutureWithMetadata<(), StateMachineMetadata>>,
    network_selection_futures:
        FuturesUnordered<BoxFuture<'static, Option<client_types::ScannedCandidate>>>,
    bss_selection_futures: FuturesUnordered<
        BoxFuture<'static, (ConnectAttemptRequest, Option<client_types::ScannedCandidate>)>,
    >,
    telemetry_sender: TelemetrySender,
    // A sender to be cloned for each connection to send periodic data about connection quality.
    stats_sender: ConnectionStatsSender,
    // A sender to be cloned for state machines to report defects to the IfaceManager.
    defect_sender: mpsc::UnboundedSender<Defect>,
}

impl IfaceManagerService {
    pub fn new(
        phy_manager: Arc<Mutex<dyn PhyManagerApi + Send>>,
        client_update_sender: listener::ClientListenerMessageSender,
        ap_update_sender: listener::ApListenerMessageSender,
        dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
        saved_networks: Arc<dyn SavedNetworksManagerApi>,
        telemetry_sender: TelemetrySender,
        stats_sender: ConnectionStatsSender,
        defect_sender: mpsc::UnboundedSender<Defect>,
    ) -> Self {
        IfaceManagerService {
            phy_manager: phy_manager.clone(),
            client_update_sender,
            ap_update_sender,
            dev_monitor_proxy,
            clients: Vec::new(),
            aps: Vec::new(),
            saved_networks: saved_networks,
            fsm_futures: FuturesUnordered::new(),
            network_selection_futures: FuturesUnordered::new(),
            bss_selection_futures: FuturesUnordered::new(),
            telemetry_sender,
            stats_sender,
            defect_sender,
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
        self.setup_client_container(iface_id).await
    }

    async fn get_wpa3_capable_client(
        &mut self,
        iface_id: Option<u16>,
    ) -> Result<ClientIfaceContainer, Error> {
        let iface_id = match iface_id {
            Some(iface_id) => iface_id,
            None => {
                // If no iface_id is specified and if there there are any unconfigured client
                // ifaces, use the first available unconfigured iface.
                if let Some(removal_index) = self.clients.iter().position(|client_container| {
                    client_container.config.is_none()
                        && wpa3_supported(client_container.security_support)
                }) {
                    return Ok(self.clients.remove(removal_index));
                }

                // If all of the known client ifaces are configured, ask the PhyManager for a
                // client iface.
                match self.phy_manager.lock().await.get_wpa3_capable_client() {
                    None => return Err(format_err!("no client ifaces available")),
                    Some(id) => id,
                }
            }
        };

        self.setup_client_container(iface_id).await
    }

    /// Remove the client iface from the list of configured ifaces if it is there, and create a
    /// ClientIfaceContainer for it if it is needed.
    async fn setup_client_container(
        &mut self,
        iface_id: u16,
    ) -> Result<ClientIfaceContainer, Error> {
        // See if the selected iface ID is among the configured clients.
        if let Some(removal_index) =
            self.clients.iter().position(|client_container| client_container.iface_id == iface_id)
        {
            return Ok(self.clients.remove(removal_index));
        }

        // If the iface ID is not among configured clients, create a new ClientIfaceContainer for
        // the iface ID.
        let (sme_proxy, sme_server) = create_proxy()?;
        self.dev_monitor_proxy
            .get_client_sme(iface_id, sme_server)
            .await?
            .map_err(zx::Status::from_raw)?;
        let (features_proxy, features_server) = create_proxy()?;
        self.dev_monitor_proxy.get_feature_support(iface_id, features_server).await?.map_err(
            |e| format_err!("Error occurred getting iface's features support proxy: {}", e),
        )?;

        // Get the security support for this iface.
        let security_support =
            features_proxy.query_security_support().await?.map_err(zx::Status::from_raw)?;
        Ok(ClientIfaceContainer {
            iface_id: iface_id,
            sme_proxy,
            config: None,
            client_state_machine: None,
            security_support,
            last_roam_time: fasync::Time::now(),
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
        let (sme_proxy, sme_server) = create_proxy()?;
        self.dev_monitor_proxy
            .get_ap_sme(iface_id, sme_server)
            .await?
            .map_err(zx::Status::from_raw)?;

        // Spawn the AP state machine.
        let (sender, receiver) = mpsc::channel(1);
        let state_machine = ap_fsm::AccessPoint::new(sender);

        let event_stream = sme_proxy.take_event_stream();
        let state_machine_fut = ap_fsm::serve(
            iface_id,
            sme_proxy,
            event_stream,
            receiver.fuse(),
            self.ap_update_sender.clone(),
            self.telemetry_sender.clone(),
            self.defect_sender.clone(),
        )
        .boxed();

        // Begin running and monitoring the AP state machine future.
        let metadata = StateMachineMetadata {
            iface_id: iface_id,
            role: fidl_fuchsia_wlan_common::WlanMacRole::Ap,
        };
        let fut = future_with_metadata::FutureWithMetadata::new(metadata, state_machine_fut);
        self.fsm_futures.push(fut);

        Ok(ApIfaceContainer {
            iface_id: iface_id,
            config: None,
            ap_state_machine: Box::new(state_machine),
            enabled_time: None,
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
        reason: client_types::DisconnectReason,
    ) -> BoxFuture<'static, Result<(), Error>> {
        // Find the client interface associated with the given network config and disconnect from
        // the network.
        let mut fsm_ack_receiver = None;
        let mut iface_id = None;

        // If a client is configured for the specified network, tell the state machine to
        // disconnect.  This will cause the state machine's future to exit so that the monitoring
        // loop discovers the completed future and attempts to reconnect the interface.
        for client in self.clients.iter_mut() {
            if client.config.as_ref() == Some(&network_id) {
                client.config = None;

                let (responder, receiver) = oneshot::channel();
                match client.client_state_machine.as_mut() {
                    Some(state_machine) => match state_machine.disconnect(reason, responder) {
                        Ok(()) => {}
                        Err(e) => {
                            client.client_state_machine = None;

                            return ready(Err(format_err!("failed to send disconnect: {:?}", e)))
                                .boxed();
                        }
                    },
                    None => {
                        return ready(Ok(())).boxed();
                    }
                }

                client.config = None;
                client.client_state_machine = None;
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

    async fn handle_connect_request(
        &mut self,
        connect_request: ConnectAttemptRequest,
        network_selector: Arc<NetworkSelector>,
    ) -> Result<(), Error> {
        // Check if already connected to requested network
        if self.clients.iter().any(|client| match &client.config {
            Some(config) => config == &connect_request.network,
            None => false,
        }) {
            info!("Received connect request to already connected network.");
            return Ok(());
        };

        self.telemetry_sender
            .send(TelemetryEvent::StartEstablishConnection { reset_start_time: true });

        // Send listener update that connection attempt is starting.
        let networks = vec![listener::ClientNetworkState {
            id: connect_request.network.clone(),
            state: client_types::ConnectionState::Connecting,
            status: None,
        }];

        let update = listener::ClientStateUpdate {
            state: client_types::ClientState::ConnectionsEnabled,
            networks: networks,
        };
        match self
            .client_update_sender
            .clone()
            .unbounded_send(listener::Message::NotifyListeners(update))
        {
            Ok(_) => (),
            Err(e) => error!("failed to send state update: {:?}", e),
        };

        initiate_bss_selection(connect_request, self, network_selector).await
    }

    async fn connect(&mut self, selection: client_types::ConnectSelection) -> Result<(), Error> {
        // Get a ClientIfaceContainer.
        let mut client_iface =
            if selection.target.network.security_type == client_types::SecurityType::Wpa3 {
                self.get_wpa3_capable_client(None).await?
            } else {
                self.get_client(None).await?
            };
        // Set the new config on this client
        client_iface.config = Some(selection.target.network.clone());

        // Check if there's an existing state machine we can use
        match client_iface.client_state_machine.as_mut() {
            Some(existing_csm) => {
                existing_csm.connect(selection)?;
            }
            None => {
                // Create the state machine and controller.
                let (new_client, fut) = create_client_state_machine(
                    client_iface.iface_id,
                    &mut self.dev_monitor_proxy,
                    self.client_update_sender.clone(),
                    self.saved_networks.clone(),
                    Some(selection),
                    self.telemetry_sender.clone(),
                    self.stats_sender.clone(),
                    self.defect_sender.clone(),
                )
                .await?;
                client_iface.client_state_machine = Some(new_client);

                // Begin running and monitoring the client state machine future.
                self.fsm_futures.push(fut);
            }
        }

        // Cancel any ongoing attempt to auto connect the previously idle iface.
        self.network_selection_futures.clear();

        client_iface.last_roam_time = fasync::Time::now();
        self.clients.push(client_iface);
        Ok(())
    }

    fn record_idle_client(&mut self, iface_id: u16) {
        for client in self.clients.iter_mut() {
            if client.iface_id == iface_id {
                // Check if the state machine has exited.  If it has not, then another call to
                // connect has replaced the state machine already and this interface should be left
                // alone.
                match client.client_state_machine.as_ref() {
                    Some(state_machine) => {
                        if state_machine.is_alive() {
                            return;
                        }
                    }
                    None => {}
                }
                client.config = None;
                client.client_state_machine = None;
                return;
            }
        }
    }

    fn idle_clients(&self) -> Vec<u16> {
        let mut idle_clients = Vec::new();

        for client in self.clients.iter() {
            if client.config.is_none() {
                idle_clients.push(client.iface_id);
                continue;
            }

            match client.client_state_machine.as_ref() {
                Some(state_machine) => {
                    if !state_machine.is_alive() {
                        idle_clients.push(client.iface_id);
                    }
                }
                None => idle_clients.push(client.iface_id),
            }
        }

        idle_clients
    }

    /// Checks the specified interface to see if there is an active state machine for it.  If there
    /// is, this indicates that a connect request has already reconnected this interface and no
    /// further action is required.  If no state machine exists for the interface, attempts to
    /// connect the interface to the specified network.
    async fn attempt_client_reconnect(
        &mut self,
        iface_id: u16,
        connect_selection: client_types::ConnectSelection,
    ) -> Result<(), Error> {
        for client in self.clients.iter_mut() {
            if client.iface_id == iface_id {
                match client.client_state_machine.as_ref() {
                    None => {}
                    Some(state_machine) => {
                        if state_machine.is_alive() {
                            return Ok(());
                        }
                    }
                }

                // Create the state machine and controller.
                let (new_client, fut) = create_client_state_machine(
                    client.iface_id,
                    &mut self.dev_monitor_proxy,
                    self.client_update_sender.clone(),
                    self.saved_networks.clone(),
                    Some(connect_selection.clone()),
                    self.telemetry_sender.clone(),
                    self.stats_sender.clone(),
                    self.defect_sender.clone(),
                )
                .await?;

                self.fsm_futures.push(fut);
                client.config = Some(connect_selection.target.network);
                client.client_state_machine = Some(new_client);
                client.last_roam_time = fasync::Time::now();
                break;
            }
        }

        Ok(())
    }

    async fn handle_added_iface(&mut self, iface_id: u16) -> Result<(), Error> {
        let iface_info =
            self.dev_monitor_proxy.query_iface(iface_id).await?.map_err(zx::Status::from_raw)?;

        match iface_info.role {
            fidl_fuchsia_wlan_common::WlanMacRole::Client => {
                let mut client_iface = self.get_client(Some(iface_id)).await?;

                // If this client has already been recorded and it has a client state machine
                // running, return success early.
                if client_iface.client_state_machine.is_some() {
                    self.clients.push(client_iface);
                    return Ok(());
                }

                // Create the state machine and controller.  The state machine is setup with no
                // initial network config.  This will cause it to quickly exit, notifying the
                // monitor loop that the interface needs attention.
                let (new_client, fut) = create_client_state_machine(
                    client_iface.iface_id,
                    &mut self.dev_monitor_proxy,
                    self.client_update_sender.clone(),
                    self.saved_networks.clone(),
                    None,
                    self.telemetry_sender.clone(),
                    self.stats_sender.clone(),
                    self.defect_sender.clone(),
                )
                .await?;

                // Begin running and monitoring the client state machine future.
                self.fsm_futures.push(fut);

                client_iface.client_state_machine = Some(new_client);
                self.clients.push(client_iface);
            }
            fidl_fuchsia_wlan_common::WlanMacRole::Ap => {
                let ap_iface = self.get_ap(Some(iface_id)).await?;
                self.aps.push(ap_iface);
            }
            fidl_fuchsia_wlan_common::WlanMacRole::Mesh => {
                // Mesh roles are not currently supported.
            }
        }

        Ok(())
    }

    async fn handle_removed_iface(&mut self, iface_id: u16) {
        // Delete the reference from the PhyManager.
        self.phy_manager.lock().await.on_iface_removed(iface_id);

        // If the interface was deleted, but IfaceManager still has a reference to it, then the
        // driver has likely performed some low-level recovery or the interface driver has crashed.
        // In this case, remove the old reference to the interface ID and then create a new
        // interface of the appropriate type.
        if let Some(iface_index) =
            self.clients.iter().position(|client_container| client_container.iface_id == iface_id)
        {
            let _ = self.clients.remove(iface_index);
            let client_ifaces = match self
                .phy_manager
                .lock()
                .await
                .create_all_client_ifaces(CreateClientIfacesReason::RecoverClientIfaces)
                .await
            {
                Ok(iface_ids) => iface_ids,
                Err((iface_ids, e)) => {
                    warn!("failed to recover some client interfaces: {:?}", e);
                    iface_ids
                }
            };

            for iface_id in client_ifaces {
                match self.get_client(Some(iface_id)).await {
                    Ok(iface) => self.clients.push(iface),
                    Err(e) => {
                        error!("failed to recreate client {}: {:?}", iface_id, e);
                    }
                };
            }
        }

        // Check to see if there are any remaining client interfaces.  If there are not any, send
        // listeners a notification indicating that client connections are disabled.
        if self.clients.is_empty() {
            let update = listener::ClientStateUpdate {
                state: fidl_fuchsia_wlan_policy::WlanClientState::ConnectionsDisabled,
                networks: vec![],
            };
            if let Err(e) =
                self.client_update_sender.unbounded_send(listener::Message::NotifyListeners(update))
            {
                error!("Failed to notify listeners of lack of client interfaces: {:?}", e)
            };
        }

        // While client behavior is automated based on saved network configs and available SSIDs
        // observed in scan results, the AP behavior is largely controlled by API clients.  The AP
        // state machine will send back a failure notification in the event that the interface was
        // destroyed unexpectedly.  For the AP, simply remove the reference to the interface.  If
        // the API client so desires, they may ask the policy layer to start another AP interface.
        self.aps.retain(|ap_container| ap_container.iface_id != iface_id);
    }

    async fn get_sme_proxy_for_scan(&mut self) -> Result<SmeForScan, Error> {
        let client_iface = self.get_client(None).await?;
        let proxy = client_iface.sme_proxy.clone();
        let iface_id = client_iface.iface_id;
        self.clients.push(client_iface);
        Ok(SmeForScan::new(proxy, iface_id, self.defect_sender.clone()))
    }

    fn stop_client_connections(
        &mut self,
        reason: client_types::DisconnectReason,
    ) -> BoxFuture<'static, Result<(), Error>> {
        self.telemetry_sender.send(TelemetryEvent::ClearEstablishConnectionStartTime);

        let client_ifaces: Vec<ClientIfaceContainer> = self.clients.drain(..).collect();
        let phy_manager = self.phy_manager.clone();
        let update_sender = self.client_update_sender.clone();

        let fut = async move {
            // Disconnect and discard all of the configured client ifaces.
            for mut client_iface in client_ifaces {
                let client = match client_iface.client_state_machine.as_mut() {
                    Some(state_machine) => state_machine,
                    None => continue,
                };
                let (responder, receiver) = oneshot::channel();
                match client.disconnect(reason, responder) {
                    Ok(()) => {}
                    Err(e) => error!("failed to issue disconnect: {:?}", e),
                }
                match receiver.await {
                    Ok(()) => {}
                    Err(e) => error!("failed to disconnect: {:?}", e),
                }
            }

            // Signal to the update listener that client connections have been disabled.
            let update = listener::ClientStateUpdate {
                state: fidl_fuchsia_wlan_policy::WlanClientState::ConnectionsDisabled,
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
        let client_iface_ids = match phy_manager
            .create_all_client_ifaces(CreateClientIfacesReason::StartClientConnections)
            .await
        {
            Ok(client_iface_ids) => client_iface_ids,
            Err((_, phy_manager_error)) => {
                return Err(format_err!(
                    "could not start client connection {:?}",
                    phy_manager_error
                ));
            }
        };

        // Resume client interfaces.
        drop(phy_manager);
        for iface_id in client_iface_ids.clone() {
            if let Err(e) = self.handle_added_iface(iface_id).await {
                error!("failed to resume client {}: {:?}", iface_id, e);
            };
        }

        Ok(())
    }

    async fn start_ap(&mut self, config: ap_fsm::ApConfig) -> Result<oneshot::Receiver<()>, Error> {
        let mut ap_iface_container = self.get_ap(None).await?;

        let (sender, receiver) = oneshot::channel();
        ap_iface_container.config = Some(config.clone());
        match ap_iface_container.ap_state_machine.start(config, sender) {
            Ok(()) => {
                if ap_iface_container.enabled_time.is_none() {
                    ap_iface_container.enabled_time = Some(zx::Time::get_monotonic());
                }

                self.aps.push(ap_iface_container)
            }
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
        ssid: ap_types::Ssid,
        credential: Vec<u8>,
    ) -> BoxFuture<'static, Result<(), Error>> {
        if let Some(removal_index) =
            self.aps.iter().position(|ap_container| match ap_container.config.as_ref() {
                Some(config) => config.id.ssid == ssid && config.credential == credential,
                None => false,
            })
        {
            let phy_manager = self.phy_manager.clone();
            let mut ap_container = self.aps.remove(removal_index);

            if let Some(start_time) = ap_container.enabled_time.take() {
                let enabled_duration = zx::Time::get_monotonic() - start_time;
                self.telemetry_sender.send(TelemetryEvent::StopAp { enabled_duration });
            }

            let fut = async move {
                let _ = &ap_container;
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

        for ap_container in aps.iter_mut() {
            if let Some(start_time) = ap_container.enabled_time.take() {
                let enabled_duration = zx::Time::get_monotonic() - start_time;
                self.telemetry_sender.send(TelemetryEvent::StopAp { enabled_duration });
            }
        }

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

    /// Returns whether or not there is a client interface that is WPA3 capable, in other words
    /// wheter or not this device can connect to a network using WPA3. Check with the PhyManager
    /// in case it is aware of an iface that IfaceManager is not tracking.
    pub async fn has_wpa3_capable_client(&self) -> bool {
        let guard = self.phy_manager.lock().await;
        return guard.has_wpa3_client_iface();
    }

    /// Returns the last time this interface roamed or started a new connection, or none if the
    /// iface is not found.
    pub fn get_iface_roam_scan_time(&mut self, iface_id: u16) -> Option<fasync::Time> {
        for client in self.clients.iter() {
            if client.iface_id == iface_id {
                return Some(client.last_roam_time);
            }
        }
        None
    }

    /// Sets the last roam scan time on the iface, or does nothing if an iface with the provided
    /// ID is not found.
    pub fn set_iface_roam_scan_time(&mut self, iface_id: u16, time: fasync::Time) {
        for client in self.clients.iter_mut() {
            if client.iface_id == iface_id {
                client.last_roam_time = time;
                return;
            }
        }
        info!("Roam scan time was not set, matching iface not found.");
    }
}

/// Returns whether the security support indicates WPA3 support.
pub fn wpa3_supported(security_support: fidl_common::SecuritySupport) -> bool {
    security_support.mfp.supported
        && (security_support.sae.driver_handler_supported
            || security_support.sae.sme_handler_supported)
}

async fn initiate_network_selection(
    iface_manager: &mut IfaceManagerService,
    network_selector: Arc<NetworkSelector>,
) {
    if !iface_manager.idle_clients().is_empty()
        && iface_manager.saved_networks.known_network_count().await > 0
        && iface_manager.network_selection_futures.is_empty()
    {
        iface_manager
            .telemetry_sender
            .send(TelemetryEvent::StartEstablishConnection { reset_start_time: false });
        info!("Initiating network selection for idle client interface.");
        let fut = async move { network_selector.find_and_select_scanned_candidate(None).await };
        iface_manager.network_selection_futures.push(fut.boxed());
    }
}

async fn initiate_bss_selection(
    connect_request: ConnectAttemptRequest,
    iface_manager: &mut IfaceManagerService,
    network_selector: Arc<NetworkSelector>,
) -> Result<(), Error> {
    // Create connection selection future and enqueue.
    let fut = async move {
        (
            connect_request.clone(),
            network_selector.find_and_select_scanned_candidate(Some(connect_request.network)).await,
        )
    };

    iface_manager.bss_selection_futures.push(fut.boxed());
    Ok(())
}

async fn handle_network_selection_results(
    network_selection_result: Option<client_types::ScannedCandidate>,
    iface_manager: &mut IfaceManagerService,
    reconnect_monitor_interval: &mut i64,
    connectivity_monitor_timer: &mut fasync::Interval,
) {
    if let Some(scanned_candidate) = network_selection_result {
        *reconnect_monitor_interval = 1;

        let connect_selection = client_types::ConnectSelection {
            target: scanned_candidate,
            reason: client_types::ConnectReason::IdleInterfaceAutoconnect,
        };

        let mut idle_clients = iface_manager.idle_clients();
        if !idle_clients.is_empty() {
            // Any client interfaces that have recently presented as idle will be
            // reconnected.
            for iface_id in idle_clients.drain(..) {
                if let Err(e) = iface_manager
                    .attempt_client_reconnect(iface_id, connect_selection.clone())
                    .await
                {
                    warn!("Could not reconnect iface {}: {:?}", iface_id, e);
                }
            }
        }
    } else {
        *reconnect_monitor_interval =
            (2 * (*reconnect_monitor_interval)).min(MAX_AUTO_CONNECT_RETRY_SECONDS);
    }

    *connectivity_monitor_timer =
        fasync::Interval::new(zx::Duration::from_seconds(*reconnect_monitor_interval));
}

async fn handle_bss_selection_for_connect_request_results(
    bss_selection_result: (ConnectAttemptRequest, Option<client_types::ScannedCandidate>),
    iface_manager: &mut IfaceManagerService,
    network_selector: Arc<NetworkSelector>,
) {
    let (mut request, selection_result) = bss_selection_result;
    request.attempts += 1;
    match selection_result {
        Some(scanned_candidate) => {
            let selection = client_types::ConnectSelection {
                target: scanned_candidate,
                reason: client_types::ConnectReason::FidlConnectRequest,
            };
            info!("Starting connection to {:?}", selection.target.network);
            let _ = iface_manager.connect(selection).await;
        }
        None => {
            if request.attempts < 3 {
                debug!("No candidates found for connect request, queueing retrying.");
                match initiate_bss_selection(request, iface_manager, network_selector.clone()).await
                {
                    Ok(_) => {}
                    Err(e) => error!("Failed to initiate bss selection for scan retry: {:?}", e),
                }
            } else {
                // Send connection failed update.
                let networks = vec![listener::ClientNetworkState {
                    id: request.network.clone(),
                    state: client_types::ConnectionState::Failed,
                    status: Some(client_types::DisconnectStatus::ConnectionFailed),
                }];

                let update = listener::ClientStateUpdate {
                    state: client_types::ClientState::ConnectionsEnabled,
                    networks: networks,
                };
                match iface_manager
                    .client_update_sender
                    .clone()
                    .unbounded_send(listener::Message::NotifyListeners(update))
                {
                    Ok(_) => (),
                    Err(e) => error!("failed to send connection_failed state update: {:?}", e),
                };
            }
        }
    }
}

async fn handle_terminated_state_machine(
    terminated_fsm: StateMachineMetadata,
    iface_manager: &mut IfaceManagerService,
    selector: Arc<NetworkSelector>,
) {
    match terminated_fsm.role {
        fidl_fuchsia_wlan_common::WlanMacRole::Ap => {
            // If the state machine exited normally, the IfaceManagerService will have already
            // destroyed the interface.  If not, then the state machine exited because it could not
            // communicate with the SME and the interface is likely unusable.
            let mut phy_manager = iface_manager.phy_manager.lock().await;
            if phy_manager.destroy_ap_iface(terminated_fsm.iface_id).await.is_err() {
                return;
            }
        }
        fidl_fuchsia_wlan_common::WlanMacRole::Client => {
            iface_manager.record_idle_client(terminated_fsm.iface_id);
            initiate_network_selection(iface_manager, selector.clone()).await;
        }
        fidl_fuchsia_wlan_common::WlanMacRole::Mesh => {
            // Not yet supported.
            unimplemented!();
        }
    }
}

fn initiate_set_country(
    iface_manager: &mut IfaceManagerService,
    req: SetCountryRequest,
) -> BoxFuture<'static, IfaceManagerOperation> {
    // Store the initial AP configs so that they can be started later.
    let initial_ap_configs =
        iface_manager.aps.iter().filter_map(|container| container.config.clone()).collect();

    // Create futures to stop all of the APs and stop client connections
    let stop_client_connections_fut = iface_manager
        .stop_client_connections(client_types::DisconnectReason::RegulatoryRegionChange);
    let stop_aps_fut = iface_manager.stop_all_aps();

    // Once the clients and APs have been stopped, set the country code.
    let phy_manager = iface_manager.phy_manager.clone();
    let regulatory_fut = async move {
        let client_connections_initially_enabled =
            phy_manager.lock().await.client_connections_enabled();

        let set_country_result = if let Err(e) = stop_client_connections_fut.await {
            Err(format_err!(
                "failed to stop client connection in preparation for setting country code: {:?}",
                e
            ))
        } else if let Err(e) = stop_aps_fut.await {
            Err(format_err!("failed to stop APs in preparation for setting country code: {:?}", e))
        } else {
            let mut phy_manager = phy_manager.lock().await;
            phy_manager
                .set_country_code(req.country_code)
                .await
                .map_err(|e| format_err!("failed to set regulatory region: {:?}", e))
        };

        let result = SetCountryOperationState {
            client_connections_initially_enabled,
            initial_ap_configs,
            set_country_result,
            responder: req.responder,
        };

        // Return all information required to resume to the old state.
        IfaceManagerOperation::SetCountry(result)
    };
    regulatory_fut.boxed()
}

async fn restore_state_after_setting_country_code(
    iface_manager: &mut IfaceManagerService,
    previous_state: SetCountryOperationState,
) {
    // Prior to setting the country code, it is essential that client connections and APs are all
    // stopped.  If stopping clients or APs fails or if setting the country code fails, the whole
    // process of setting the country code must be considered a failure.
    //
    // Bringing clients and APs back online following the regulatory region setting may fail and is
    // possibly recoverable.  Log failures, but do not report errors in scenarios where recreating
    // the client and AP interfaces fails.  This allows API clients to retry and attempt to create
    // the interfaces themselves by making policy API requests.
    if previous_state.client_connections_initially_enabled {
        if let Err(e) = iface_manager.start_client_connections().await {
            error!("failed to resume client connections after setting country code: {:?}", e);
        }
    }

    for config in previous_state.initial_ap_configs {
        if let Err(e) = iface_manager.start_ap(config).await {
            error!("failed to resume AP after setting country code: {:?}", e);
        }
    }

    if previous_state.responder.send(previous_state.set_country_result).is_err() {
        error!("could not respond to SetCountryRequest");
    }
}

fn handle_periodic_connection_stats(
    connection_stats: PeriodicConnectionStats,
    iface_manager: &mut IfaceManagerService,
    _iface_manager_client: Arc<Mutex<dyn IfaceManagerApi + Send>>,
    _network_selector: Arc<NetworkSelector>,
    roaming_search_futures: &mut FuturesUnordered<
        BoxFuture<'static, Option<client_types::ScannedCandidate>>,
    >,
) {
    // If a proactive network switch already being considered, ignore.
    if !roaming_search_futures.is_empty() {
        return;
    }

    // If a roaming scan already happened recently, ignore.
    if let Some(last_roam_scan_time) =
        iface_manager.get_iface_roam_scan_time(connection_stats.iface_id)
    {
        if fasync::Time::now() < last_roam_scan_time + DURATION_BETWEEN_ROAM_SCANS {
            return;
        }
    } else {
        warn!("Failed to find iface to get the last time of roam attempt, will not roam");
        return;
    }

    // Decide whether the connection quality is bad enough to justify a network switch
    // TODO(fxbug.dev/84551): determine whether a connection is considered bad enough to
    // consider roaming. Currently this will never cause a scan.
    let connection_quality = score_connection_quality(&connection_stats);
    if connection_quality < THRESHOLD_BAD_CONNECTION {
        // Record for metrics that a scan was performed for roaming.
        iface_manager.telemetry_sender.send(TelemetryEvent::RoamingScan);

        // Record that a roam scan happened and another should not happen again for a while.
        iface_manager.set_iface_roam_scan_time(connection_stats.iface_id, fasync::Time::now());
    }
}

// This function allows the defect recording to run in parallel with the regulatory region setting
// routine.  For full context, see fxb/112640.
fn initiate_record_defect(
    phy_manager: Arc<Mutex<dyn PhyManagerApi + Send>>,
    defect: Defect,
) -> BoxFuture<'static, IfaceManagerOperation> {
    let fut = async move {
        let mut phy_manager = phy_manager.lock().await;
        phy_manager.record_defect(defect).await;
        IfaceManagerOperation::ReportDefect
    };
    fut.boxed()
}

async fn handle_iface_manager_request(
    iface_manager: &mut IfaceManagerService,
    network_selector: Arc<NetworkSelector>,
    operation_futures: &mut FuturesUnordered<BoxFuture<'static, IfaceManagerOperation>>,
    token: atomic_oneshot_stream::Token,
    request: IfaceManagerRequest,
) {
    match request {
        IfaceManagerRequest::Connect(ConnectRequest { request, responder }) => {
            if responder
                .send(iface_manager.handle_connect_request(request, network_selector.clone()).await)
                .is_err()
            {
                error!("could not respond to ScanForConnectionSelection");
            }
        }
        IfaceManagerRequest::RecordIdleIface(RecordIdleIfaceRequest { iface_id, responder }) => {
            iface_manager.record_idle_client(iface_id);
            if responder.send(()).is_err() {
                error!("could not respond to RecordIdleIfaceRequest");
            }
        }
        IfaceManagerRequest::HasIdleIface(HasIdleIfaceRequest { responder }) => {
            if responder.send(!iface_manager.idle_clients().is_empty()).is_err() {
                error!("could not respond to  HasIdleIfaceRequest");
            }
        }
        IfaceManagerRequest::AddIface(AddIfaceRequest { iface_id, responder }) => {
            if let Err(e) = iface_manager.handle_added_iface(iface_id).await {
                warn!("failed to add new interface {}: {:?}", iface_id, e);
            }
            if responder.send(()).is_err() {
                error!("could not respond to AddIfaceRequest");
            }
        }
        IfaceManagerRequest::RemoveIface(RemoveIfaceRequest { iface_id, responder }) => {
            iface_manager.handle_removed_iface(iface_id).await;
            if responder.send(()).is_err() {
                error!("could not respond to RemoveIfaceRequest");
            }
        }
        IfaceManagerRequest::GetScanProxy(ScanProxyRequest { responder }) => {
            if responder.send(iface_manager.get_sme_proxy_for_scan().await).is_err() {
                error!("could not respond to ScanRequest");
            }
        }
        IfaceManagerRequest::StartClientConnections(StartClientConnectionsRequest {
            responder,
        }) => {
            if responder.send(iface_manager.start_client_connections().await).is_err() {
                error!("could not respond to StartClientConnectionRequest");
            }
        }
        IfaceManagerRequest::StartAp(StartApRequest { config, responder }) => {
            if responder.send(iface_manager.start_ap(config).await).is_err() {
                error!("could not respond to StartApRequest");
            }
        }
        IfaceManagerRequest::HasWpa3Iface(HasWpa3IfaceRequest { responder }) => {
            if responder.send(iface_manager.has_wpa3_capable_client().await).is_err() {
                error!("could not respond to HasWpa3IfaceRequest");
            }
        }
        IfaceManagerRequest::AtomicOperation(operation) => {
            let fut = match operation {
                AtomicOperation::Disconnect(DisconnectRequest {
                    network_id,
                    responder,
                    reason,
                }) => {
                    let fut = iface_manager.disconnect(network_id, reason);
                    let disconnect_fut = async move {
                        if responder.send(fut.await).is_err() {
                            error!("could not respond to DisconnectRequest");
                        }
                        IfaceManagerOperation::ConfigureStateMachine
                    };
                    disconnect_fut.boxed()
                }
                AtomicOperation::StopClientConnections(StopClientConnectionsRequest {
                    reason,
                    responder,
                }) => {
                    let fut = iface_manager.stop_client_connections(reason);
                    let stop_client_connections_fut = async move {
                        if responder.send(fut.await).is_err() {
                            error!("could not respond to StopClientConnectionsRequest");
                        }
                        IfaceManagerOperation::ConfigureStateMachine
                    };
                    stop_client_connections_fut.boxed()
                }
                AtomicOperation::StopAp(StopApRequest { ssid, password, responder }) => {
                    let stop_ap_fut = iface_manager.stop_ap(ssid, password);
                    let stop_ap_fut = async move {
                        if responder.send(stop_ap_fut.await).is_err() {
                            error!("could not respond to StopApRequest");
                        }
                        IfaceManagerOperation::ConfigureStateMachine
                    };
                    stop_ap_fut.boxed()
                }
                AtomicOperation::StopAllAps(StopAllApsRequest { responder }) => {
                    let stop_all_aps_fut = iface_manager.stop_all_aps();
                    let stop_all_aps_fut = async move {
                        if responder.send(stop_all_aps_fut.await).is_err() {
                            error!("could not respond to StopAllApsRequest");
                        }
                        IfaceManagerOperation::ConfigureStateMachine
                    };
                    stop_all_aps_fut.boxed()
                }
                AtomicOperation::SetCountry(req) => {
                    let regulatory_fut = initiate_set_country(iface_manager, req);
                    let regulatory_fut = async move { regulatory_fut.await };
                    regulatory_fut.boxed()
                }
            };

            let fut = attempt_atomic_operation(fut, token);
            operation_futures.push(fut);
        }
    };
}

// Bundle the operations of running the caller's future with dropping of the `AtomicOneshotStream`
// `Token`
fn attempt_atomic_operation<T: Debug + 'static>(
    fut: BoxFuture<'static, T>,
    token: atomic_oneshot_stream::Token,
) -> BoxFuture<'static, T> {
    Box::pin(async move {
        let result = fut.await;
        drop(token);
        result
    })
}

pub(crate) async fn serve_iface_manager_requests(
    mut iface_manager: IfaceManagerService,
    iface_manager_client: Arc<Mutex<dyn IfaceManagerApi + Send>>,
    network_selector: Arc<NetworkSelector>,
    requests: mpsc::Receiver<IfaceManagerRequest>,
    mut stats_receiver: ConnectionStatsReceiver,
    mut defect_receiver: mpsc::UnboundedReceiver<Defect>,
) -> Result<Infallible, Error> {
    // Client and AP state machines need to be allowed to run in order for several operations to
    // complete.  In such cases, futures can be added to this list to progress them once the state
    // machines have the opportunity to run.
    let mut operation_futures = FuturesUnordered::new();

    // This allows routines servicing `IfaceManagerRequest`s to prevent incoming requests from
    // being serviced to prevent potential deadlocks on the `PhyManager`.
    let mut requests = atomic_oneshot_stream::AtomicOneshotStream::new(requests);

    // Create a timer to periodically check to ensure that all client interfaces are connected.
    let mut reconnect_monitor_interval: i64 = 1;
    let mut connectivity_monitor_timer =
        fasync::Interval::new(zx::Duration::from_seconds(reconnect_monitor_interval));

    // Scans will be initiated to look for candidate networks to roam to if the current connection
    // is bad.
    let mut roaming_search_futures = FuturesUnordered::new();

    loop {
        let mut atomic_iface_manager_requests = requests.get_atomic_oneshot_stream();

        select! {
            terminated_fsm = iface_manager.fsm_futures.select_next_some() => {
                info!("state machine exited: {:?}", terminated_fsm.1);
                handle_terminated_state_machine(
                    terminated_fsm.1,
                    &mut iface_manager,
                    network_selector.clone(),
                ).await;
            },
            () = connectivity_monitor_timer.select_next_some() => {
                initiate_network_selection(
                    &mut iface_manager,
                    network_selector.clone(),
                ).await;
            },
            op = operation_futures.select_next_some() => match op {
                IfaceManagerOperation::SetCountry(previous_state) => {
                    restore_state_after_setting_country_code(
                        &mut iface_manager,
                        previous_state
                    ).await;
                },
                IfaceManagerOperation::ConfigureStateMachine
                | IfaceManagerOperation::ReportDefect => {},
            },
            network_selection_result = iface_manager.network_selection_futures.select_next_some() => {
                handle_network_selection_results(
                    network_selection_result,
                    &mut iface_manager,
                    &mut reconnect_monitor_interval,
                    &mut connectivity_monitor_timer
                ).await;
            },
            bss_selection_result = iface_manager.bss_selection_futures.select_next_some() => {
                // TODO(fxbug.dev/110825): There may be multiple reasons to do BSS selection,
                // not just for connect requests. Create new enum.
                handle_bss_selection_for_connect_request_results(
                    bss_selection_result,
                    &mut iface_manager,
                    network_selector.clone(),
                ).await;
            },
            connection_stats = stats_receiver.select_next_some() => {
                handle_periodic_connection_stats(
                    connection_stats,
                    &mut iface_manager,
                    iface_manager_client.clone(),
                    network_selector.clone(),
                    &mut roaming_search_futures
                );
            },
            defect = defect_receiver.select_next_some() => {
                operation_futures.push(initiate_record_defect(iface_manager.phy_manager.clone(), defect))
            },
            _connection_candidate = roaming_search_futures.select_next_some() => {
                // TODO(fxbug.dev/84548): decide whether the best network found is better than the
                // current network, and if so trigger a connection
            },
            (token, req) = atomic_iface_manager_requests.select_next_some() => {
                handle_iface_manager_request(
                    &mut iface_manager,
                    network_selector.clone(),
                    &mut operation_futures,
                    token,
                    req
                ).await;
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
            client::{scan, types as client_types},
            config_management::{
                Credential, NetworkIdentifier, SavedNetworksManager, SecurityType,
            },
            mode_management::{
                phy_manager::{self, PhyManagerError},
                IfaceFailure, PhyFailure,
            },
            regulatory_manager::REGION_CODE_LEN,
            telemetry::{TelemetryEvent, TelemetrySender},
            util::testing::{
                create_inspect_persistence_channel, create_wlan_hasher, fakes::FakeScanRequester,
                poll_sme_req,
            },
        },
        async_trait::async_trait,
        eui48::MacAddress,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_stash as fidl_stash, fidl_fuchsia_wlan_common as fidl_common,
        fuchsia_async::{DurationExt, TestExecutor},
        fuchsia_inspect::{self as inspect},
        futures::{
            channel::mpsc,
            stream::{StreamExt, StreamFuture},
            task::Poll,
            TryStreamExt,
        },
        lazy_static::lazy_static,
        pin_utils::pin_mut,
        std::convert::TryFrom,
        test_case::test_case,
        wlan_common::{
            assert_variant, channel::Cbw, random_fidl_bss_description,
            security::SecurityDescriptor, RadioConfig,
        },
    };

    // Responses that FakePhyManager will provide
    pub const TEST_CLIENT_IFACE_ID: u16 = 0;
    pub const TEST_AP_IFACE_ID: u16 = 1;

    // Fake WLAN network that tests will scan for and connect to.
    lazy_static! {
        pub static ref TEST_SSID: ap_types::Ssid = ap_types::Ssid::try_from("test_ssid").unwrap();
    }
    pub static TEST_PASSWORD: &str = "test_password";

    /// Produces wlan network configuration objects to be used in tests.
    pub fn create_connect_selection(
        ssid: &ap_types::Ssid,
        password: &str,
    ) -> client_types::ConnectSelection {
        let network = ap_types::NetworkIdentifier {
            ssid: ssid.clone(),
            security_type: ap_types::SecurityType::Wpa,
        };
        let credential = Credential::Password(password.as_bytes().to_vec());

        client_types::ConnectSelection {
            target: client_types::ScannedCandidate {
                network: network,
                credential: credential,
                bss_description: random_fidl_bss_description!(Wpa1, ssid: ssid.clone()),
                observation: client_types::ScanObservation::Passive,
                has_multiple_bss_candidates: true,
                mutual_security_protocols: [SecurityDescriptor::WPA1].into_iter().collect(),
            },
            reason: client_types::ConnectReason::FidlConnectRequest,
        }
    }

    /// Holds all of the boilerplate required for testing IfaceManager.
    /// * DeviceMonitorProxy and DeviceMonitorRequestStream
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
        pub monitor_service_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
        pub monitor_service_stream: fidl_fuchsia_wlan_device_service::DeviceMonitorRequestStream,
        pub client_update_sender: listener::ClientListenerMessageSender,
        pub client_update_receiver: mpsc::UnboundedReceiver<listener::ClientListenerMessage>,
        pub ap_update_sender: listener::ApListenerMessageSender,
        pub ap_update_receiver: mpsc::UnboundedReceiver<listener::ApMessage>,
        pub saved_networks: Arc<dyn SavedNetworksManagerApi>,
        pub network_selector: Arc<NetworkSelector>,
        pub scan_requester: Arc<FakeScanRequester>,
        pub node: inspect::Node,
        pub telemetry_sender: TelemetrySender,
        pub telemetry_receiver: mpsc::Receiver<TelemetryEvent>,
        pub stats_sender: ConnectionStatsSender,
        pub stats_receiver: ConnectionStatsReceiver,
        pub defect_sender: mpsc::UnboundedSender<Defect>,
        pub defect_receiver: mpsc::UnboundedReceiver<Defect>,
    }

    /// Create a TestValues for a unit test.
    pub fn test_setup(exec: &mut TestExecutor) -> TestValues {
        let (monitor_service_proxy, monitor_service_requests) =
            create_proxy::<fidl_fuchsia_wlan_device_service::DeviceMonitorMarker>()
                .expect("failed to create SeviceService proxy");
        let monitor_service_stream =
            monitor_service_requests.into_stream().expect("failed to create stream");

        let (client_sender, client_receiver) = mpsc::unbounded();
        let (ap_sender, ap_receiver) = mpsc::unbounded();

        let saved_networks = exec
            .run_singlethreaded(SavedNetworksManager::new_for_test())
            .expect("failed to create saved networks manager.");
        let saved_networks = Arc::new(saved_networks);
        let inspector = inspect::Inspector::new();
        let node = inspector.root().create_child("phy_manager");
        let (persistence_req_sender, _persistence_stream) = create_inspect_persistence_channel();
        let (telemetry_sender, telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let telemetry_sender = TelemetrySender::new(telemetry_sender);
        let scan_requester = Arc::new(FakeScanRequester::new());
        let network_selector = Arc::new(NetworkSelector::new(
            saved_networks.clone(),
            scan_requester.clone(),
            create_wlan_hasher(),
            inspector.root().create_child("network_selection"),
            persistence_req_sender,
            telemetry_sender.clone(),
        ));
        let (stats_sender, stats_receiver) = mpsc::unbounded();
        let (defect_sender, defect_receiver) = mpsc::unbounded();

        TestValues {
            monitor_service_proxy,
            monitor_service_stream,
            client_update_sender: client_sender,
            client_update_receiver: client_receiver,
            ap_update_sender: ap_sender,
            ap_update_receiver: ap_receiver,
            saved_networks: saved_networks,
            scan_requester,
            node: node,
            network_selector,
            telemetry_sender,
            telemetry_receiver,
            stats_sender,
            stats_receiver,
            defect_sender,
            defect_receiver,
        }
    }

    /// Creates a new PhyManagerPtr for tests.
    fn create_empty_phy_manager(
        monitor_service: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
        node: inspect::Node,
        telemetry_sender: TelemetrySender,
    ) -> Arc<Mutex<dyn PhyManagerApi + Send>> {
        Arc::new(Mutex::new(phy_manager::PhyManager::new(monitor_service, node, telemetry_sender)))
    }

    #[derive(Debug)]
    struct FakePhyManager {
        create_iface_ok: bool,
        destroy_iface_ok: bool,
        set_country_ok: bool,
        wpa3_iface: Option<u16>,
        country_code: Option<[u8; REGION_CODE_LEN]>,
        client_connections_enabled: bool,
        client_ifaces: Vec<u16>,
        defects: Vec<Defect>,
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

        async fn create_all_client_ifaces(
            &mut self,
            _reason: CreateClientIfacesReason,
        ) -> Result<Vec<u16>, (Vec<u16>, PhyManagerError)> {
            if self.create_iface_ok {
                Ok(self.client_ifaces.clone())
            } else {
                Err((vec![], PhyManagerError::IfaceCreateFailure))
            }
        }

        fn client_connections_enabled(&self) -> bool {
            self.client_connections_enabled
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

        fn get_wpa3_capable_client(&mut self) -> Option<u16> {
            self.wpa3_iface
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

        fn log_phy_add_failure(&mut self) {
            unimplemented!();
        }

        async fn set_country_code(
            &mut self,
            country_code: Option<[u8; REGION_CODE_LEN]>,
        ) -> Result<(), PhyManagerError> {
            if self.set_country_ok {
                self.country_code = country_code;
                Ok(())
            } else {
                Err(PhyManagerError::PhySetCountryFailure)
            }
        }

        fn has_wpa3_client_iface(&self) -> bool {
            self.wpa3_iface.is_some()
        }

        async fn set_power_state(
            &mut self,
            _low_power_enabled: fidl_fuchsia_wlan_common::PowerSaveType,
        ) -> Result<fuchsia_zircon::Status, anyhow::Error> {
            unimplemented!();
        }

        async fn record_defect(&mut self, defect: Defect) {
            self.defects.push(defect);
        }
    }

    struct FakeClient {
        disconnect_ok: bool,
        is_alive: bool,
        expected_connect_selection: Option<client_types::ConnectSelection>,
    }

    impl FakeClient {
        fn new() -> Self {
            FakeClient { disconnect_ok: true, is_alive: true, expected_connect_selection: None }
        }
    }

    #[async_trait]
    impl client_fsm::ClientApi for FakeClient {
        fn connect(&mut self, selection: client_types::ConnectSelection) -> Result<(), Error> {
            assert_eq!(Some(selection), self.expected_connect_selection);
            Ok(())
        }
        fn disconnect(
            &mut self,
            _reason: client_types::DisconnectReason,
            responder: oneshot::Sender<()>,
        ) -> Result<(), Error> {
            if self.disconnect_ok {
                let _ = responder.send(());
                Ok(())
            } else {
                Err(format_err!("fake failed to disconnect"))
            }
        }
        fn is_alive(&self) -> bool {
            self.is_alive
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
            responder: oneshot::Sender<()>,
        ) -> Result<(), anyhow::Error> {
            if self.start_succeeds {
                let _ = responder.send(());
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
            client_state_machine: None,
            security_support: fake_security_support(),
            last_roam_time: fasync::Time::now(),
        };
        let phy_manager = FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![TEST_CLIENT_IFACE_ID],
            defects: vec![],
        };
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender.clone(),
            test_values.ap_update_sender.clone(),
            test_values.monitor_service_proxy.clone(),
            test_values.saved_networks.clone(),
            test_values.telemetry_sender.clone(),
            test_values.stats_sender.clone(),
            test_values.defect_sender.clone(),
        );

        if configured {
            client_container.config = Some(ap_types::NetworkIdentifier {
                ssid: TEST_SSID.clone(),
                security_type: ap_types::SecurityType::Wpa,
            });
            client_container.client_state_machine = Some(Box::new(FakeClient::new()));
        }
        iface_manager.clients.push(client_container);

        (iface_manager, server.into_stream().unwrap().into_future())
    }

    fn create_ap_config(ssid: &ap_types::Ssid, password: &str) -> ap_fsm::ApConfig {
        let radio_config = RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6);
        ap_fsm::ApConfig {
            id: ap_types::NetworkIdentifier {
                ssid: ssid.clone(),
                security_type: ap_types::SecurityType::None,
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
            enabled_time: None,
        };
        let phy_manager = FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![],
            defects: vec![],
        };
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender.clone(),
            test_values.ap_update_sender.clone(),
            test_values.monitor_service_proxy.clone(),
            test_values.saved_networks.clone(),
            test_values.telemetry_sender.clone(),
            test_values.stats_sender.clone(),
            test_values.defect_sender.clone(),
        );

        iface_manager.aps.push(ap_container);
        iface_manager
    }

    /// Move stash requests forward so that a save request can progress.
    fn process_stash_write(
        exec: &mut fuchsia_async::TestExecutor,
        stash_server: &mut fidl_stash::StoreAccessorRequestStream,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_stash::StoreAccessorRequest::SetValue { .. })))
        );
        process_stash_flush(exec, stash_server);
    }

    fn process_stash_delete(
        exec: &mut fuchsia_async::TestExecutor,
        stash_server: &mut fidl_stash::StoreAccessorRequestStream,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_stash::StoreAccessorRequest::DeletePrefix { .. })))
        );
        process_stash_flush(exec, stash_server);
    }

    fn process_stash_flush(
        exec: &mut fuchsia_async::TestExecutor,
        stash_server: &mut fidl_stash::StoreAccessorRequestStream,
    ) {
        assert_variant!(
            exec.run_until_stalled(&mut stash_server.try_next()),
            Poll::Ready(Ok(Some(fidl_stash::StoreAccessorRequest::Flush{responder}))) => {
                responder.send(&mut Ok(())).expect("failed to send stash response");
            }
        );
    }

    #[track_caller]
    fn run_state_machine_futures(
        exec: &mut fuchsia_async::TestExecutor,
        iface_manager: &mut IfaceManagerService,
    ) {
        for mut state_machine in iface_manager.fsm_futures.iter_mut() {
            assert_variant!(exec.run_until_stalled(&mut state_machine), Poll::Pending);
        }
    }

    /// Tests the case where connect is called and the only available client interface is already
    /// configured.
    #[fuchsia::test]
    fn test_connect_with_configured_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let other_test_ssid = ap_types::Ssid::try_from("other_ssid_connecting").unwrap();

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Configure the mock CSM with the expected connect request
        let connect_selection = create_connect_selection(&other_test_ssid, TEST_PASSWORD);
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: false,
            is_alive: true,
            expected_connect_selection: Some(connect_selection.clone()),
        }));

        // Ask the IfaceManager to connect.
        {
            let connect_fut = iface_manager.connect(connect_selection);
            pin_mut!(connect_fut);

            // Run the connect request to completion.
            match exec.run_until_stalled(&mut connect_fut) {
                Poll::Ready(connect_result) => match connect_result {
                    Ok(_) => {}
                    Err(e) => panic!("failed to connect with {}", e),
                },
                Poll::Pending => panic!("expected the connect request to finish"),
            };
        }
        // Start running the client state machine.
        run_state_machine_futures(&mut exec, &mut iface_manager);

        // Verify that the ClientIfaceContainer has the correct config.
        assert_eq!(iface_manager.clients.len(), 1);
        assert_eq!(
            iface_manager.clients[0].config,
            Some(NetworkIdentifier::new(other_test_ssid, SecurityType::Wpa))
        );
    }

    /// Tests the case where connect is called while the only available interface is currently
    /// unconfigured.
    #[fuchsia::test]
    fn test_connect_with_unconfigured_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        let mut test_values = test_setup(&mut exec);
        let (mut iface_manager, mut _sme_stream) =
            create_iface_manager_with_client(&test_values, false);
        let (saved_networks, mut stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server());
        test_values.saved_networks = Arc::new(saved_networks);

        // Add credentials for the test network to the saved networks.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        let save_network_fut = test_values.saved_networks.store(network_id.clone(), credential);
        pin_mut!(save_network_fut);
        assert_variant!(exec.run_until_stalled(&mut save_network_fut), Poll::Pending);

        process_stash_write(&mut exec, &mut stash_server);

        {
            let selection = create_connect_selection(&TEST_SSID, TEST_PASSWORD);
            {
                let connect_fut = iface_manager.connect(selection);
                pin_mut!(connect_fut);

                // Expect that we have requested a client SME proxy.
                assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Pending);

                let mut monitor_service_fut = test_values.monitor_service_stream.into_future();
                let sme_server = assert_variant!(
                    poll_service_req(&mut exec, &mut monitor_service_fut),
                    Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                        iface_id: TEST_CLIENT_IFACE_ID, sme_server, responder
                    }) => {
                        // Send back a positive acknowledgement.
                        assert!(responder.send(&mut Ok(())).is_ok());

                        sme_server
                    }
                );
                _sme_stream = sme_server.into_stream().unwrap().into_future();

                pin_mut!(connect_fut);
                match exec.run_until_stalled(&mut connect_fut) {
                    Poll::Ready(connect_result) => match connect_result {
                        Ok(_) => {}
                        Err(e) => panic!("failed to connect with {}", e),
                    },
                    Poll::Pending => panic!("expected the connect request to finish"),
                };
            }
            // Start running the client state machine.
            run_state_machine_futures(&mut exec, &mut iface_manager);

            // Acknowledge the disconnection attempt.
            assert_variant!(
                poll_sme_req(&mut exec, &mut _sme_stream),
                Poll::Ready(fidl_fuchsia_wlan_sme::ClientSmeRequest::Disconnect{ responder, reason: fidl_fuchsia_wlan_sme::UserDisconnectReason::Startup }) => {
                    responder.send().expect("could not send response")
                }
            );

            // Make sure that the connect request has been sent out.
            run_state_machine_futures(&mut exec, &mut iface_manager);
            let connect_txn_handle = assert_variant!(
                poll_sme_req(&mut exec, &mut _sme_stream),
                Poll::Ready(fidl_fuchsia_wlan_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                    assert_eq!(req.ssid, TEST_SSID.clone());
                    assert_eq!(Credential::Password(TEST_PASSWORD.as_bytes().to_vec()), req.authentication.credentials);
                    let (_stream, ctrl) = txn.expect("connect txn unused")
                        .into_stream_and_control_handle().expect("error accessing control handle");
                    ctrl
                }
            );
            connect_txn_handle
                .send_on_connect_result(&mut fake_successful_connect_result())
                .expect("failed to send connection completion");

            // Run the state machine future again so that it acks the oneshot.
            run_state_machine_futures(&mut exec, &mut iface_manager);
        }

        // Verify that the ClientIfaceContainer has been moved from unconfigured to configured.
        assert_eq!(iface_manager.clients.len(), 1);
        assert_eq!(iface_manager.clients[0].config, Some(network_id));
    }

    #[fuchsia::test]
    fn test_connect_cancels_auto_reconnect_future() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        let mut test_values = test_setup(&mut exec);
        let (mut iface_manager, mut _sme_stream) =
            create_iface_manager_with_client(&test_values, false);
        let (saved_networks, mut stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server());
        test_values.saved_networks = Arc::new(saved_networks);

        // Add credentials for the test network to the saved networks.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        let save_network_fut = test_values.saved_networks.store(network_id.clone(), credential);
        pin_mut!(save_network_fut);
        assert_variant!(exec.run_until_stalled(&mut save_network_fut), Poll::Pending);

        process_stash_write(&mut exec, &mut stash_server);

        // Add a network selection future which won't complete that should be canceled by a
        // connect request.
        async fn blocking_fn() -> Option<client_types::ScannedCandidate> {
            loop {
                fasync::Timer::new(zx::Duration::from_millis(1).after_now()).await
            }
        }
        iface_manager.network_selection_futures.push(blocking_fn().boxed());

        // Request a connect through IfaceManager and respond to requests needed to complete it.
        {
            let config = create_connect_selection(&TEST_SSID, TEST_PASSWORD);
            let connect_fut = iface_manager.connect(config);
            pin_mut!(connect_fut);

            // Expect that we have requested a client SME proxy.
            assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Pending);
            let mut monitor_service_fut = test_values.monitor_service_stream.into_future();
            assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                    iface_id: TEST_CLIENT_IFACE_ID, sme_server: _, responder
                }) => {
                    // Send back a positive acknowledgement.
                    assert!(responder.send(&mut Ok(())).is_ok());
                }
            );

            pin_mut!(connect_fut);
            assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Ready(connect_result) => {
                assert!(connect_result.is_ok());
            });
        }

        // Verify that the network selection future was dropped from the list.
        assert!(iface_manager.network_selection_futures.is_empty());
    }

    #[fuchsia::test]
    fn test_wpa3_supported() {
        // Valid WPA3 feature sets
        let mut driver_handler_mfp = fake_security_support();
        driver_handler_mfp.sae.driver_handler_supported = true;
        driver_handler_mfp.mfp.supported = true;
        assert!(wpa3_supported(driver_handler_mfp));

        let mut sme_handler_mfp = fake_security_support();
        sme_handler_mfp.sae.sme_handler_supported = true;
        sme_handler_mfp.mfp.supported = true;
        assert!(wpa3_supported(sme_handler_mfp));

        let mut driver_and_sme_handler_mfp = fake_security_support();
        driver_and_sme_handler_mfp.sae.driver_handler_supported = true;
        driver_and_sme_handler_mfp.sae.sme_handler_supported = true;
        driver_and_sme_handler_mfp.mfp.supported = true;
        assert!(wpa3_supported(driver_and_sme_handler_mfp));

        // Invalid WPA3 feature sets
        let mut driver_handler_only = fake_security_support();
        driver_handler_only.sae.driver_handler_supported = true;
        assert!(!wpa3_supported(driver_handler_only));

        let mut sme_handler_only = fake_security_support();
        sme_handler_only.sae.sme_handler_supported = true;
        assert!(!wpa3_supported(sme_handler_only));

        let mut driver_and_sme_handler_only = fake_security_support();
        driver_and_sme_handler_only.sae.driver_handler_supported = true;
        driver_and_sme_handler_only.sae.sme_handler_supported = true;
        assert!(!wpa3_supported(driver_and_sme_handler_only));

        let mut mfp_only = fake_security_support();
        mfp_only.mfp.supported = true;
        assert!(!wpa3_supported(mfp_only));
    }

    /// Tests the case where connect is called for a WPA3 connection an there is an unconfigured
    /// WPA3 iface available.
    #[test_case(true, true, false)]
    #[test_case(true, false, true)]
    #[test_case(true, true, true)]
    #[fuchsia::test(add_test_attr = false)]
    fn test_connect_with_unconfigured_wpa3_iface(
        mfp_supported: bool,
        sae_driver_handler_supported: bool,
        sae_sme_handler_supported: bool,
    ) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        let mut test_values = test_setup(&mut exec);
        // The default FakePhyManager will not have a WPA3 iface which is intentional because if
        // an unconfigured iface is available with WPA3, it should be used without asking
        // PhyManager for an iface.
        let (mut iface_manager, mut _sme_stream) =
            create_iface_manager_with_client(&test_values, false);
        let mut security_support = fake_security_support();
        security_support.mfp.supported = mfp_supported;
        security_support.sae.driver_handler_supported = sae_driver_handler_supported;
        security_support.sae.sme_handler_supported = sae_sme_handler_supported;
        iface_manager.clients[0].security_support = security_support;

        let (saved_networks, mut stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server());
        test_values.saved_networks = Arc::new(saved_networks);

        // Add credentials for the test network to the saved networks.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa3);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        let save_network_fut =
            test_values.saved_networks.store(network_id.clone(), credential.clone());
        pin_mut!(save_network_fut);
        assert_variant!(exec.run_until_stalled(&mut save_network_fut), Poll::Pending);

        process_stash_write(&mut exec, &mut stash_server);

        {
            let selection = client_types::ConnectSelection {
                target: client_types::ScannedCandidate {
                    network: network_id.clone(),
                    credential: credential,
                    bss_description: random_fidl_bss_description!(Wpa3, ssid: TEST_SSID.clone()),
                    observation: client_types::ScanObservation::Passive,
                    has_multiple_bss_candidates: true,
                    mutual_security_protocols: [SecurityDescriptor::WPA3_PERSONAL]
                        .into_iter()
                        .collect(),
                },
                reason: client_types::ConnectReason::FidlConnectRequest,
            };
            {
                let connect_fut = iface_manager.connect(selection);
                pin_mut!(connect_fut);

                // Expect that we have requested a client SME proxy.
                assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Pending);

                let mut monitor_service_fut = test_values.monitor_service_stream.into_future();
                let sme_server = assert_variant!(
                    poll_service_req(&mut exec, &mut monitor_service_fut),
                    Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                        iface_id: TEST_CLIENT_IFACE_ID, sme_server, responder
                    }) => {
                        // Send back a positive acknowledgement.
                        assert!(responder.send(&mut Ok(())).is_ok());

                        sme_server
                    }
                );
                _sme_stream = sme_server.into_stream().unwrap().into_future();

                match exec.run_until_stalled(&mut connect_fut) {
                    Poll::Ready(connect_result) => match connect_result {
                        Ok(_) => {}
                        Err(e) => panic!("failed to connect with {}", e),
                    },
                    Poll::Pending => panic!("expected the connect request to finish"),
                };
            }
            // Start running the client state machine.
            run_state_machine_futures(&mut exec, &mut iface_manager);

            // Acknowledge the disconnection attempt.
            assert_variant!(
                poll_sme_req(&mut exec, &mut _sme_stream),
                Poll::Ready(fidl_fuchsia_wlan_sme::ClientSmeRequest::Disconnect{ responder, reason: fidl_fuchsia_wlan_sme::UserDisconnectReason::Startup }) => {
                    responder.send().expect("could not send response")
                }
            );

            // Make sure that the connect request has been sent out.
            run_state_machine_futures(&mut exec, &mut iface_manager);
            let connect_txn_handle = assert_variant!(
                poll_sme_req(&mut exec, &mut _sme_stream),
                Poll::Ready(fidl_fuchsia_wlan_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                    assert_eq!(req.ssid, TEST_SSID.clone());
                    assert_eq!(Credential::Password(TEST_PASSWORD.as_bytes().to_vec()), req.authentication.credentials);
                    let (_stream, ctrl) = txn.expect("connect txn unused")
                        .into_stream_and_control_handle().expect("error accessing control handle");
                    ctrl
                }
            );
            connect_txn_handle
                .send_on_connect_result(&mut fake_successful_connect_result())
                .expect("failed to send connection completion");

            // Run the state machine future again so that it acks the oneshot.
            run_state_machine_futures(&mut exec, &mut iface_manager);
        }

        // Verify that the ClientIfaceContainer has been moved from unconfigured to configured.
        assert_eq!(iface_manager.clients.len(), 1);
        assert_eq!(iface_manager.clients[0].config, Some(network_id));
    }

    /// Tests the case where connect is called for a WPA3 connection and a client iface exists but
    /// does not support WPA3. The connect should fail without trying to connect.
    #[fuchsia::test]
    fn test_connect_wpa3_no_wpa3_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        let test_values = test_setup(&mut exec);
        // By default IfaceManager has an iface but not one with WPA3.
        let (mut iface_manager, mut _sme_stream) =
            create_iface_manager_with_client(&test_values, false);

        // Call connect on the IfaceManager
        let ssid = ap_types::Ssid::try_from("some_ssid").unwrap();
        let network = ap_types::NetworkIdentifier {
            ssid: ssid.clone(),
            security_type: ap_types::SecurityType::Wpa3,
        };
        let credential = Credential::Password(b"some_password".to_vec());

        let selection = client_types::ConnectSelection {
            target: client_types::ScannedCandidate {
                network: network,
                credential: credential,
                bss_description: random_fidl_bss_description!(Wpa3, ssid: ssid.clone()),
                observation: client_types::ScanObservation::Passive,
                has_multiple_bss_candidates: false,
                mutual_security_protocols: [SecurityDescriptor::WPA3_PERSONAL]
                    .into_iter()
                    .collect(),
            },
            reason: client_types::ConnectReason::FidlConnectRequest,
        };
        let connect_fut = iface_manager.connect(selection);

        // Verify that the request to connect results in an error.
        pin_mut!(connect_fut);
        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where a connect is called and the WPA3 capable iface is configured.
    /// PhyManager should be asked for an iface that supports WPA3.
    #[fuchsia::test]
    fn test_connect_wpa3_with_configured_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        // Build the connect request for connecting to the WPA3 network.
        let ssid = ap_types::Ssid::try_from("some_wpa3_network").unwrap();
        let network = ap_types::NetworkIdentifier {
            ssid: ssid.clone(),
            security_type: ap_types::SecurityType::Wpa3,
        };
        let credential = Credential::Password(b"password".to_vec());
        let connect_selection = client_types::ConnectSelection {
            target: client_types::ScannedCandidate {
                network: network.clone(),
                credential: credential,
                bss_description: random_fidl_bss_description!(Wpa3, ssid: ssid.clone()),
                observation: client_types::ScanObservation::Passive,
                has_multiple_bss_candidates: true,
                mutual_security_protocols: [SecurityDescriptor::WPA3_PERSONAL]
                    .into_iter()
                    .collect(),
            },
            reason: client_types::ConnectReason::FidlConnectRequest,
        };

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        // Use a PhyManager that has a WPA3 client iface.
        iface_manager.phy_manager = Arc::new(Mutex::new(FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: false,
            wpa3_iface: Some(0),
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![TEST_CLIENT_IFACE_ID],
            defects: vec![],
        }));

        // Configure the mock CSM with the expected connect request
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: false,
            is_alive: true,
            expected_connect_selection: Some(connect_selection.clone()),
        }));
        {
            let connect_fut = iface_manager.connect(connect_selection);
            pin_mut!(connect_fut);

            // Run the connect request to completion.
            match exec.run_until_stalled(&mut connect_fut) {
                Poll::Ready(connect_result) => match connect_result {
                    Ok(_) => {}
                    Err(e) => panic!("failed to connect with {}", e),
                },
                Poll::Pending => panic!("expected the connect request to finish"),
            };
        }

        // Start running the client state machine.
        run_state_machine_futures(&mut exec, &mut iface_manager);

        // Verify that the ClientIfaceContainer has the correct config.
        assert_eq!(iface_manager.clients.len(), 1);
        assert_eq!(iface_manager.clients[0].config, Some(network));
    }

    /// Tests the case where connect is called, but no client ifaces exist.
    #[fuchsia::test]
    fn test_connect_with_no_ifaces() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a PhyManager with no knowledge of any client ifaces.
        let test_values = test_setup(&mut exec);
        let phy_manager = create_empty_phy_manager(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );

        let mut iface_manager = IfaceManagerService::new(
            phy_manager,
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks,
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        // Call connect on the IfaceManager
        let selection = create_connect_selection(&TEST_SSID, TEST_PASSWORD);
        let connect_fut = iface_manager.connect(selection);

        // Verify that the request to connect results in an error.
        pin_mut!(connect_fut);
        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where the PhyManager knows of a client iface, but the IfaceManager is not
    /// able to create an SME proxy for it.
    #[fuchsia::test]
    fn test_connect_sme_creation_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create an IfaceManager and drop its client
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        let _ = iface_manager.clients.pop();

        // Drop the serving end of our device service proxy so that the request to create an SME
        // proxy fails.
        drop(test_values.monitor_service_stream);

        // Update the saved networks with knowledge of the test SSID and credentials.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        assert!(exec
            .run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password")
            .is_none());

        // Ask the IfaceManager to connect and make sure that it fails.
        let selection = create_connect_selection(&TEST_SSID, TEST_PASSWORD);
        let connect_fut = iface_manager.connect(selection);

        pin_mut!(connect_fut);
        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where disconnect is called on a configured client.
    #[fuchsia::test]
    fn test_disconnect_configured_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        assert!(exec
            .run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password")
            .is_none());

        {
            // Issue a call to disconnect from the network.
            let network_id = ap_types::NetworkIdentifier {
                ssid: TEST_SSID.clone(),
                security_type: ap_types::SecurityType::Wpa,
            };
            let disconnect_fut = iface_manager
                .disconnect(network_id, client_types::DisconnectReason::NetworkUnsaved);

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
    #[fuchsia::test]
    fn test_disconnect_nonexistent_config() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a ClientIfaceContainer with a valid client.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Create a PhyManager with knowledge of a single client iface.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        assert!(exec
            .run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password")
            .is_none());

        {
            // Issue a disconnect request for a bogus network configuration.
            let network_id = ap_types::NetworkIdentifier {
                ssid: ap_types::Ssid::try_from("nonexistent_ssid").unwrap(),
                security_type: ap_types::SecurityType::Wpa,
            };
            let disconnect_fut = iface_manager
                .disconnect(network_id, client_types::DisconnectReason::NetworkUnsaved);

            // Ensure that the request returns immediately.
            pin_mut!(disconnect_fut);
            assert!(exec.run_until_stalled(&mut disconnect_fut).is_ready());
        }

        // Verify that the configured client has not been affected.
        assert_eq!(iface_manager.clients.len(), 1);
        assert!(iface_manager.clients[0].config.is_some());
    }

    /// Tests the case where disconnect is called and no client ifaces are present.
    #[fuchsia::test]
    fn test_disconnect_no_clients() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks,
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        // Call disconnect on the IfaceManager
        let network_id = ap_types::NetworkIdentifier {
            ssid: ap_types::Ssid::try_from("nonexistent_ssid").unwrap(),
            security_type: ap_types::SecurityType::Wpa,
        };
        let disconnect_fut =
            iface_manager.disconnect(network_id, client_types::DisconnectReason::NetworkUnsaved);

        // Verify that disconnect returns immediately.
        pin_mut!(disconnect_fut);
        assert_variant!(exec.run_until_stalled(&mut disconnect_fut), Poll::Ready(Ok(_)));
    }

    /// Tests the case where the call to disconnect the client fails.
    #[fuchsia::test]
    fn test_disconnect_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Make the client state machine's disconnect call fail.
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: false,
            is_alive: true,
            expected_connect_selection: None,
        }));

        // Call disconnect on the IfaceManager
        let network_id = ap_types::NetworkIdentifier {
            ssid: TEST_SSID.clone(),
            security_type: ap_types::SecurityType::Wpa,
        };
        let disconnect_fut =
            iface_manager.disconnect(network_id, client_types::DisconnectReason::NetworkUnsaved);

        pin_mut!(disconnect_fut);
        assert_variant!(exec.run_until_stalled(&mut disconnect_fut), Poll::Ready(Err(_)));
    }

    /// Tests stop_client_connections when there is a client that is connected.
    #[fuchsia::test]
    fn test_stop_connected_client() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let mut test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Create a PhyManager with a single, known client iface.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        assert!(exec
            .run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password")
            .is_none());

        {
            // Stop all client connections.
            let stop_fut = iface_manager.stop_client_connections(
                client_types::DisconnectReason::FidlStopClientConnectionsRequest,
            );
            pin_mut!(stop_fut);
            assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
        }

        // Ensure that no client interfaces are accounted for.
        assert!(iface_manager.clients.is_empty());

        // Ensure an update was sent
        let client_state_update = listener::ClientStateUpdate {
            state: fidl_fuchsia_wlan_policy::WlanClientState::ConnectionsDisabled,
            networks: vec![],
        };
        assert_variant!(
            test_values.client_update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
            assert_eq!(updates, client_state_update);
        });
    }

    /// Call stop_client_connections when the only available client is unconfigured.
    #[fuchsia::test]
    fn test_stop_unconfigured_client() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Create a PhyManager with one known client.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        assert!(exec
            .run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password")
            .is_none());

        {
            // Call stop_client_connections.
            let stop_fut = iface_manager.stop_client_connections(
                client_types::DisconnectReason::FidlStopClientConnectionsRequest,
            );
            pin_mut!(stop_fut);
            assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
        }

        // Ensure there are no remaining client ifaces.
        assert!(iface_manager.clients.is_empty());
    }

    /// Tests the case where stop_client_connections is called, but there are no client ifaces.
    #[fuchsia::test]
    fn test_stop_no_clients() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create and empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks,
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        // Call stop_client_connections.
        {
            let stop_fut = iface_manager.stop_client_connections(
                client_types::DisconnectReason::FidlStopClientConnectionsRequest,
            );

            // Ensure stop_client_connections returns immediately and is successful.
            pin_mut!(stop_fut);
            assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
        }
    }

    /// Tests the case where client connections are stopped, but stopping one of the client state
    /// machines fails.
    #[fuchsia::test]
    fn test_stop_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: false,
            is_alive: true,
            expected_connect_selection: None,
        }));

        // Create a PhyManager with a single, known client iface.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        assert!(exec
            .run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password")
            .is_none());

        {
            // Stop all client connections.
            let stop_fut = iface_manager.stop_client_connections(
                client_types::DisconnectReason::FidlStopClientConnectionsRequest,
            );
            pin_mut!(stop_fut);
            assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
        }

        // Ensure that no client interfaces are accounted for.
        assert!(iface_manager.clients.is_empty());
    }

    /// Tests the case where the IfaceManager fails to tear down all of the client ifaces.
    #[fuchsia::test]
    fn test_stop_iface_destruction_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        iface_manager.phy_manager = Arc::new(Mutex::new(FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: false,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![TEST_CLIENT_IFACE_ID],
            defects: vec![],
        }));

        // Create a PhyManager with a single, known client iface.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        assert!(exec
            .run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password")
            .is_none());

        {
            // Stop all client connections.
            let stop_fut = iface_manager.stop_client_connections(
                client_types::DisconnectReason::FidlStopClientConnectionsRequest,
            );
            pin_mut!(stop_fut);
            assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Err(_)));
        }

        // Ensure that no client interfaces are accounted for.
        assert!(iface_manager.clients.is_empty());
    }

    /// Tests the case where StopClientConnections is called when the client interfaces are already
    /// stopped.
    #[fuchsia::test]
    fn test_stop_client_when_already_stopped() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks,
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        // Call stop_client_connections.
        {
            let stop_fut = iface_manager.stop_client_connections(
                client_types::DisconnectReason::FidlStopClientConnectionsRequest,
            );

            // Ensure stop_client_connections returns immediately and is successful.
            pin_mut!(stop_fut);
            assert_variant!(exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
        }

        // Verify that telemetry event has been sent
        let event = assert_variant!(test_values.telemetry_receiver.try_next(), Ok(Some(ev)) => ev);
        assert_variant!(event, TelemetryEvent::ClearEstablishConnectionStartTime);
    }

    /// Tests the case where an existing iface is marked as idle.
    #[fuchsia::test]
    fn test_mark_iface_idle() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Setup the client state machine so that it looks like it is no longer alive.
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: true,
            is_alive: false,
            expected_connect_selection: None,
        }));

        assert!(iface_manager.clients[0].config.is_some());
        iface_manager.record_idle_client(TEST_CLIENT_IFACE_ID);
        assert!(iface_manager.clients[0].config.is_none());
    }

    /// Tests the case where a running and configured iface is marked as idle.
    #[fuchsia::test]
    fn test_mark_active_iface_idle() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        assert!(iface_manager.clients[0].config.is_some());

        // The request to mark the interface as idle should be ignored since the interface's state
        // machine is still running.
        iface_manager.record_idle_client(TEST_CLIENT_IFACE_ID);
        assert!(iface_manager.clients[0].config.is_some());
    }

    /// Tests the case where a non-existent iface is marked as idle.
    #[fuchsia::test]
    fn test_mark_nonexistent_iface_idle() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        assert!(iface_manager.clients[0].config.is_some());
        iface_manager.record_idle_client(123);
        assert!(iface_manager.clients[0].config.is_some());
    }

    /// Tests the case where an iface is not configured and has_idle_client is called.
    #[fuchsia::test]
    fn test_unconfigured_iface_idle_check() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (iface_manager, _) = create_iface_manager_with_client(&test_values, false);
        assert!(!iface_manager.idle_clients().is_empty());
    }

    /// Tests the case where an iface is configured and alive and has_idle_client is called.
    #[fuchsia::test]
    fn test_configured_alive_iface_idle_check() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        assert!(iface_manager.idle_clients().is_empty());
    }

    /// Tests the case where an iface is configured and dead and has_idle_client is called.
    #[fuchsia::test]
    fn test_configured_dead_iface_idle_check() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Make the client state machine's liveness check fail.
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: true,
            is_alive: false,
            expected_connect_selection: None,
        }));

        assert!(!iface_manager.idle_clients().is_empty());
    }

    /// Tests the case where not ifaces are present and has_idle_client is called.
    #[fuchsia::test]
    fn test_no_ifaces_idle_check() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        let _ = iface_manager.clients.pop();
        assert!(iface_manager.idle_clients().is_empty());
    }

    /// Tests the case where starting client connections succeeds.
    #[fuchsia::test]
    fn test_start_clients_succeeds() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks,
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        {
            let start_fut = iface_manager.start_client_connections();

            // Ensure start_client_connections returns immediately and is successful.
            pin_mut!(start_fut);
            assert_variant!(exec.run_until_stalled(&mut start_fut), Poll::Ready(Ok(_)));
        }

        // Ensure no update is sent
        assert_variant!(test_values.client_update_receiver.try_next(), Err(_));
    }

    /// Tests the case where starting client connections fails.
    #[fuchsia::test]
    fn test_start_clients_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        iface_manager.phy_manager = Arc::new(Mutex::new(FakePhyManager {
            create_iface_ok: false,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![],
            defects: vec![],
        }));

        {
            let start_fut = iface_manager.start_client_connections();
            pin_mut!(start_fut);
            assert_variant!(exec.run_until_stalled(&mut start_fut), Poll::Ready(Err(_)));
        }
    }

    /// Tests the case where there is a lingering client interface to ensure that it is resumed to
    /// a working state.
    #[fuchsia::test]
    fn test_start_clients_with_lingering_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create an IfaceManager with no clients and client connections initially disabled.  Seed
        // it with a PhyManager that knows of a lingering client interface.
        let mut test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, false);
        iface_manager.phy_manager = Arc::new(Mutex::new(FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: false,
            client_ifaces: vec![TEST_CLIENT_IFACE_ID],
            defects: vec![],
        }));

        // Delete all client records initially.
        iface_manager.clients.clear();
        assert!(iface_manager.fsm_futures.is_empty());

        {
            let start_fut = iface_manager.start_client_connections();
            pin_mut!(start_fut);

            // The IfaceManager will first query to determine the type of interface.
            assert_variant!(exec.run_until_stalled(&mut start_fut), Poll::Pending);
            assert_variant!(
                exec.run_until_stalled(&mut test_values.monitor_service_stream.next()),
                Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::QueryIface {
                    iface_id: TEST_CLIENT_IFACE_ID, responder
                }))) => {
                    let response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                        role: fidl_fuchsia_wlan_common::WlanMacRole::Client,
                        id: TEST_CLIENT_IFACE_ID,
                        phy_id: 0,
                        phy_assigned_id: 0,
                        sta_addr: [0; 6],
                    };
                    responder
                        .send(&mut Ok(response))
                        .expect("Failed to send iface response");
                }
            );

            // The request should stall out while attempting to get a client interface.
            assert_variant!(exec.run_until_stalled(&mut start_fut), Poll::Pending);
            assert_variant!(
                exec.run_until_stalled(&mut test_values.monitor_service_stream.next()),
                Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                    iface_id: TEST_CLIENT_IFACE_ID, sme_server: _, responder
                }))) => {
                    // Send back a positive acknowledgement.
                    assert!(responder.send(&mut Ok(())).is_ok());
                }
            );

            // There will be a security support query.
            assert_variant!(exec.run_until_stalled(&mut start_fut), Poll::Pending);
            let features_server = assert_variant!(
                exec.run_until_stalled(&mut test_values.monitor_service_stream.next()),
                Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetFeatureSupport {
                    iface_id: TEST_CLIENT_IFACE_ID, feature_support_server, responder
                }))) => {
                    assert!(responder.send(&mut Ok(())).is_ok());
                    feature_support_server
                }
            );
            assert_variant!(exec.run_until_stalled(&mut start_fut), Poll::Pending);
            let mut features_req_fut = features_server
                .into_stream()
                .expect("Failed to create features req stream")
                .into_future();
            assert_variant!(
                poll_service_req(&mut exec, &mut features_req_fut),
                Poll::Ready(fidl_fuchsia_wlan_sme::FeatureSupportRequest::QuerySecuritySupport {
                    responder
                }) => {
                    responder.send(&mut Ok(fake_security_support())).expect("Failed to send security support response");
                }
            );

            // Expect that we have requested a client SME proxy from creating the client state
            // machine.
            assert_variant!(exec.run_until_stalled(&mut start_fut), Poll::Pending);
            assert_variant!(
                exec.run_until_stalled(&mut test_values.monitor_service_stream.next()),
                Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                    iface_id: TEST_CLIENT_IFACE_ID, sme_server: _, responder
                }))) => {
                    // Send back a positive acknowledgement.
                    assert!(responder.send(&mut Ok(())).is_ok());
                }
            );

            // The request should complete successfully.
            assert_variant!(exec.run_until_stalled(&mut start_fut), Poll::Ready(Ok(())));
        }

        assert!(!iface_manager.clients.is_empty());
        assert!(!iface_manager.fsm_futures.is_empty());
    }

    /// Tests the case where the IfaceManager is able to request that the AP state machine start
    /// the access point.
    #[fuchsia::test]
    fn test_start_ap_succeeds() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(&TEST_SSID, TEST_PASSWORD);

        {
            let fut = iface_manager.start_ap(config);

            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(_)));
        }

        assert!(iface_manager.aps[0].enabled_time.is_some());
    }

    /// Tests the case where the IfaceManager is not able to request that the AP state machine start
    /// the access point.
    #[fuchsia::test]
    fn test_start_ap_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: false, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(&TEST_SSID, TEST_PASSWORD);

        {
            let fut = iface_manager.start_ap(config);

            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
        }
    }

    /// Tests the case where start is called on the IfaceManager, but there are no AP ifaces.
    #[fuchsia::test]
    fn test_start_ap_no_ifaces() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks,
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        // Call start_ap.
        let config = create_ap_config(&TEST_SSID, TEST_PASSWORD);
        let fut = iface_manager.start_ap(config);

        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
    }

    /// Tests the case where stop_ap is called for a config that is accounted for by the
    /// IfaceManager.
    #[fuchsia::test]
    fn test_stop_ap_succeeds() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(&TEST_SSID, TEST_PASSWORD);
        iface_manager.aps[0].config = Some(config);
        iface_manager.aps[0].enabled_time = Some(zx::Time::get_monotonic());

        {
            let fut = iface_manager.stop_ap(TEST_SSID.clone(), TEST_PASSWORD.as_bytes().to_vec());
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
        }
        assert!(iface_manager.aps.is_empty());

        // Ensure a metric was logged.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::StopAp { .. }))
        );
    }

    /// Tests the case where IfaceManager is requested to stop a config that is not accounted for.
    #[fuchsia::test]
    fn test_stop_ap_invalid_config() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        iface_manager.aps[0].enabled_time = Some(zx::Time::get_monotonic());

        {
            let fut = iface_manager.stop_ap(TEST_SSID.clone(), TEST_PASSWORD.as_bytes().to_vec());
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
        }
        assert!(!iface_manager.aps.is_empty());

        // Ensure no metric was logged.
        assert_variant!(test_values.telemetry_receiver.try_next(), Err(_));

        // Ensure the AP start time has not been cleared.
        assert!(iface_manager.aps[0].enabled_time.is_some());
    }

    /// Tests the case where IfaceManager attempts to stop the AP state machine, but the request
    /// fails.
    #[fuchsia::test]
    fn test_stop_ap_stop_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: false, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(&TEST_SSID, TEST_PASSWORD);
        iface_manager.aps[0].config = Some(config);
        iface_manager.aps[0].enabled_time = Some(zx::Time::get_monotonic());

        {
            let fut = iface_manager.stop_ap(TEST_SSID.clone(), TEST_PASSWORD.as_bytes().to_vec());
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
        }

        assert!(iface_manager.aps.is_empty());

        // Ensure metric was logged.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::StopAp { .. }))
        );
    }

    /// Tests the case where IfaceManager stops the AP state machine, but the request to exit
    /// fails.
    #[fuchsia::test]
    fn test_stop_ap_exit_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: false };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(&TEST_SSID, TEST_PASSWORD);
        iface_manager.aps[0].config = Some(config);
        iface_manager.aps[0].enabled_time = Some(zx::Time::get_monotonic());

        {
            let fut = iface_manager.stop_ap(TEST_SSID.clone(), TEST_PASSWORD.as_bytes().to_vec());
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
        }

        assert!(iface_manager.aps.is_empty());

        // Ensure metric was logged.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::StopAp { .. }))
        );
    }

    /// Tests the case where stop is called on the IfaceManager, but there are no AP ifaces.
    #[fuchsia::test]
    fn test_stop_ap_no_ifaces() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks,
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );
        let fut = iface_manager.stop_ap(TEST_SSID.clone(), TEST_PASSWORD.as_bytes().to_vec());
        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    /// Tests the case where stop_all_aps is called and it succeeds.
    #[fuchsia::test]
    fn test_stop_all_aps_succeeds() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        iface_manager.aps[0].enabled_time = Some(zx::Time::get_monotonic());

        // Insert a second iface and add it to the list of APs.
        let second_iface = ApIfaceContainer {
            iface_id: 2,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
            enabled_time: Some(zx::Time::get_monotonic()),
        };
        iface_manager.aps.push(second_iface);

        {
            let fut = iface_manager.stop_all_aps();
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
        }
        assert!(iface_manager.aps.is_empty());

        // Ensure metrics are logged for both AP interfaces.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::StopAp { .. }))
        );
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::StopAp { .. }))
        );
    }

    /// Tests the case where stop_all_aps is called and the request to stop fails for an iface.
    #[fuchsia::test]
    fn test_stop_all_aps_stop_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: false, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        iface_manager.aps[0].enabled_time = Some(zx::Time::get_monotonic());

        // Insert a second iface and add it to the list of APs.
        let second_iface = ApIfaceContainer {
            iface_id: 2,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
            enabled_time: Some(zx::Time::get_monotonic()),
        };
        iface_manager.aps.push(second_iface);

        {
            let fut = iface_manager.stop_all_aps();
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
        }
        assert!(iface_manager.aps.is_empty());

        // Ensure metrics are logged for both AP interfaces.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::StopAp { .. }))
        );
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::StopAp { .. }))
        );
    }

    /// Tests the case where stop_all_aps is called and the request to stop fails for an iface.
    #[fuchsia::test]
    fn test_stop_all_aps_exit_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: false };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        iface_manager.aps[0].enabled_time = Some(zx::Time::get_monotonic());

        // Insert a second iface and add it to the list of APs.
        let second_iface = ApIfaceContainer {
            iface_id: 2,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
            enabled_time: Some(zx::Time::get_monotonic()),
        };
        iface_manager.aps.push(second_iface);

        {
            let fut = iface_manager.stop_all_aps();
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
        }
        assert!(iface_manager.aps.is_empty());

        // Ensure metrics are logged for both AP interfaces.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::StopAp { .. }))
        );
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::StopAp { .. }))
        );
    }

    /// Tests the case where stop_all_aps is called on the IfaceManager, but there are no AP
    /// ifaces.
    #[fuchsia::test]
    fn test_stop_all_aps_no_ifaces() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks,
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        let fut = iface_manager.stop_all_aps();
        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));

        // Ensure no metrics are logged.
        assert_variant!(test_values.telemetry_receiver.try_next(), Err(_));
    }

    /// Tests the case where there is a single AP interface and it is asked to start twice and then
    /// asked to stop.
    #[fuchsia::test]
    fn test_start_ap_twice_then_stop() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);
        let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true };
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(&TEST_SSID, TEST_PASSWORD);

        // Issue an initial start command.
        {
            let fut = iface_manager.start_ap(config);

            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(_)));
        }

        // Record the initial start time.
        let initial_start_time = iface_manager.aps[0].enabled_time;

        // Now issue a second start command.
        let alternate_ssid = ap_types::Ssid::try_from("some_other_ssid").unwrap();
        let alternate_password = "some_other_password";
        let config = create_ap_config(&alternate_ssid, alternate_password);
        {
            let fut = iface_manager.start_ap(config);

            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(_)));
        }

        // Verify that the start time has not been updated.
        assert_eq!(initial_start_time, iface_manager.aps[0].enabled_time);

        // Verify that no metric has been recorded.
        assert_variant!(test_values.telemetry_receiver.try_next(), Err(_));

        // Now issue a stop command.
        {
            let fut = iface_manager.stop_all_aps();
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
        }
        assert!(iface_manager.aps.is_empty());

        // Make sure the metric has been sent.
        assert_variant!(
            test_values.telemetry_receiver.try_next(),
            Ok(Some(TelemetryEvent::StopAp { .. }))
        );
    }

    #[fuchsia::test]
    fn test_recover_removed_client_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create an IfaceManager with a client and an AP.
        let mut test_values = test_setup(&mut exec);
        let (mut iface_manager, _next_sme_req) =
            create_iface_manager_with_client(&test_values, true);
        let removed_iface_id = 123;
        iface_manager.clients[0].iface_id = removed_iface_id;

        let ap_iface = ApIfaceContainer {
            iface_id: TEST_AP_IFACE_ID,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
            enabled_time: None,
        };
        iface_manager.aps.push(ap_iface);

        // Notify the IfaceManager that the client interface has been removed.
        {
            let fut = iface_manager.handle_removed_iface(removed_iface_id);
            pin_mut!(fut);

            // Expect a DeviceService request an SME proxy.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            assert_variant!(
                exec.run_until_stalled(&mut test_values.monitor_service_stream.next()),
                Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                    iface_id: TEST_CLIENT_IFACE_ID, sme_server: _, responder
                }))) => {
                    responder
                        .send(&mut Ok(()))
                        .expect("failed to send AP SME response.");
                }
            );

            // Expected a DeviceService request to get iface info.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            let features_server = assert_variant!(
                exec.run_until_stalled(&mut test_values.monitor_service_stream.next()),
                Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetFeatureSupport {
                    iface_id: TEST_CLIENT_IFACE_ID, feature_support_server, responder
                }))) => {
                    assert!(responder.send(&mut Ok(())).is_ok());
                    feature_support_server
                }
            );
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            let mut features_req_fut = features_server
                .into_stream()
                .expect("Failed to create features req stream")
                .into_future();
            assert_variant!(
                poll_service_req(&mut exec, &mut features_req_fut),
                Poll::Ready(fidl_fuchsia_wlan_sme::FeatureSupportRequest::QuerySecuritySupport {
                    responder
                }) => {
                    responder.send(&mut Ok(fake_security_support())).expect("Failed to send security support response");
                }
            );

            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        assert!(!iface_manager.aps.is_empty());

        // Verify that a new client interface was created.
        assert_eq!(iface_manager.clients.len(), 1);
        assert_eq!(iface_manager.clients[0].iface_id, TEST_CLIENT_IFACE_ID);
    }

    #[fuchsia::test]
    fn test_client_iface_recovery_fails() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create an IfaceManager with a client and an AP.
        let mut test_values = test_setup(&mut exec);
        let (mut iface_manager, _next_sme_req) =
            create_iface_manager_with_client(&test_values, true);
        let removed_iface_id = 123;
        iface_manager.clients[0].iface_id = removed_iface_id;

        let ap_iface = ApIfaceContainer {
            iface_id: TEST_AP_IFACE_ID,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
            enabled_time: None,
        };
        iface_manager.aps.push(ap_iface);

        // Notify the IfaceManager that the client interface has been removed.
        {
            let fut = iface_manager.handle_removed_iface(removed_iface_id);
            pin_mut!(fut);

            // Expect a DeviceService request an SME proxy.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            assert_variant!(
                exec.run_until_stalled(&mut test_values.monitor_service_stream.next()),
                Poll::Ready(Some(Ok(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                    iface_id: TEST_CLIENT_IFACE_ID, sme_server: _, responder
                }))) => {
                    responder
                        .send(&mut Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND))
                        .expect("failed to send client SME response.");
                }
            );

            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        assert!(!iface_manager.aps.is_empty());

        // Verify that not new client interface was created.
        assert!(iface_manager.clients.is_empty());

        // Verify that a ConnectionsDisabled notification was sent.
        // Ensure an update was sent
        let expected_update = listener::ClientStateUpdate {
            state: fidl_fuchsia_wlan_policy::WlanClientState::ConnectionsDisabled,
            networks: vec![],
        };
        assert_variant!(
            test_values.client_update_receiver.try_next(),
            Ok(Some(listener::Message::NotifyListeners(updates))) => {
                assert_eq!(updates, expected_update);
            }
        );
    }

    #[fuchsia::test]
    fn test_do_not_recover_removed_ap_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create an IfaceManager with a client and an AP.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _next_sme_req) =
            create_iface_manager_with_client(&test_values, true);

        let removed_iface_id = 123;
        let ap_iface = ApIfaceContainer {
            iface_id: removed_iface_id,
            config: None,
            ap_state_machine: Box::new(FakeAp {
                start_succeeds: true,
                stop_succeeds: true,
                exit_succeeds: true,
            }),
            enabled_time: None,
        };
        iface_manager.aps.push(ap_iface);

        // Notify the IfaceManager that the AP interface has been removed.
        {
            let fut = iface_manager.handle_removed_iface(removed_iface_id);
            pin_mut!(fut);

            // The future should now run to completion.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        assert!(!iface_manager.clients.is_empty());

        // Verify that a new AP interface was not created.
        assert!(iface_manager.aps.is_empty());
    }

    #[fuchsia::test]
    fn test_remove_nonexistent_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

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
            enabled_time: None,
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

    fn poll_service_req<
        T,
        E: std::fmt::Debug,
        R: fidl::endpoints::RequestStream
            + futures::Stream<Item = Result<T, E>>
            + futures::TryStream<Ok = T>,
    >(
        exec: &mut fuchsia_async::TestExecutor,
        next_req: &mut StreamFuture<R>,
    ) -> Poll<fidl::endpoints::Request<R::Protocol>> {
        exec.run_until_stalled(next_req).map(|(req, stream)| {
            *next_req = stream.into_future();
            req.expect("did not expect the SME request stream to end")
                .expect("error polling SME request stream")
        })
    }

    #[fuchsia::test]
    fn test_add_client_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks,
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        {
            // Notify the IfaceManager of a new interface.
            let fut = iface_manager.handle_added_iface(TEST_CLIENT_IFACE_ID);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

            // Expect and interface query and notify that this is a client interface.
            let mut monitor_service_fut = test_values.monitor_service_stream.into_future();
            assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::QueryIface {
                    iface_id: TEST_CLIENT_IFACE_ID, responder
                }) => {
                    let response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                        role: fidl_fuchsia_wlan_common::WlanMacRole::Client,
                        id: TEST_CLIENT_IFACE_ID,
                        phy_id: 0,
                        phy_assigned_id: 0,
                        sta_addr: [0; 6],
                    };
                    responder
                        .send(&mut Ok(response))
                        .expect("Sending iface response");
                }
            );

            // Expect that we have requested a client SME proxy from get_client.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                    iface_id: TEST_CLIENT_IFACE_ID, sme_server: _, responder
                }) => {
                    // Send back a positive acknowledgement.
                    assert!(responder.send(&mut Ok(())).is_ok());
                }
            );

            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            let features_server = assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetFeatureSupport {
                    iface_id: TEST_CLIENT_IFACE_ID, feature_support_server, responder
                }) => {
                    assert!(responder.send(&mut Ok(())).is_ok());
                    feature_support_server
                }
            );
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            let mut features_req_fut = features_server
                .into_stream()
                .expect("Failed to create features req stream")
                .into_future();
            assert_variant!(
                poll_service_req(&mut exec, &mut features_req_fut),
                Poll::Ready(fidl_fuchsia_wlan_sme::FeatureSupportRequest::QuerySecuritySupport {
                    responder
                }) => {
                    responder.send(&mut Ok(fake_security_support())).expect("Failed to send security support response");
                }
            );

            // Expect that we have requested a client SME proxy from creating the client state
            // machine.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                    iface_id: TEST_CLIENT_IFACE_ID, sme_server: _, responder
                }) => {
                    // Send back a positive acknowledgement.
                    assert!(responder.send(&mut Ok(())).is_ok());
                }
            );

            // Run the future to completion.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
        }

        // Ensure that the client interface has been added.
        assert!(iface_manager.aps.is_empty());
        assert_eq!(iface_manager.clients[0].iface_id, TEST_CLIENT_IFACE_ID);
    }

    #[fuchsia::test]
    fn test_add_ap_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks,
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        {
            // Notify the IfaceManager of a new interface.
            let fut = iface_manager.handle_added_iface(TEST_AP_IFACE_ID);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

            // Expect that the interface properties are queried and notify that it is an AP iface.
            let mut monitor_service_fut = test_values.monitor_service_stream.into_future();
            assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::QueryIface {
                    iface_id: TEST_AP_IFACE_ID, responder
                }) => {
                    let response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                        role: fidl_fuchsia_wlan_common::WlanMacRole::Ap,
                        id: TEST_AP_IFACE_ID,
                        phy_id: 0,
                        phy_assigned_id: 0,
                        sta_addr: [0; 6],
                    };
                    responder
                        .send(&mut Ok(response))
                        .expect("Sending iface response");
                }
            );

            // Run the future so that an AP SME proxy is requested.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

            let responder = assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetApSme {
                    iface_id: TEST_AP_IFACE_ID, sme_server: _, responder
                }) => responder
            );

            // Send back a positive acknowledgement.
            assert!(responder.send(&mut Ok(())).is_ok());

            // Run the future to completion.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
        }

        // Ensure that the AP interface has been added.
        assert!(iface_manager.clients.is_empty());
        assert_eq!(iface_manager.aps[0].iface_id, TEST_AP_IFACE_ID);
    }

    #[fuchsia::test]
    fn test_add_nonexistent_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let mut iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks,
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        {
            // Notify the IfaceManager of a new interface.
            let fut = iface_manager.handle_added_iface(TEST_AP_IFACE_ID);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

            // Expect an iface query and send back an error
            let mut monitor_service_fut = test_values.monitor_service_stream.into_future();
            assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::QueryIface {
                    iface_id: TEST_AP_IFACE_ID, responder
                }) => {
                    responder
                        .send(&mut Err(fuchsia_zircon::sys::ZX_ERR_NOT_FOUND))
                        .expect("Sending iface response");
                }
            );

            // Run the future to completion.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Err(_)));
        }

        // Ensure that no interfaces have been added.
        assert!(iface_manager.clients.is_empty());
        assert!(iface_manager.aps.is_empty());
    }

    #[fuchsia::test]
    fn test_add_existing_client_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

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
            let mut monitor_service_fut = test_values.monitor_service_stream.into_future();
            assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::QueryIface {
                    iface_id: TEST_CLIENT_IFACE_ID, responder
                }) => {
                    let response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                        role: fidl_fuchsia_wlan_common::WlanMacRole::Client,
                        id: TEST_CLIENT_IFACE_ID,
                        phy_id: 0,
                        phy_assigned_id: 0,
                        sta_addr: [0; 6],
                    };
                    responder
                        .send(&mut Ok(response))
                        .expect("Sending iface response");
                }
            );

            // The future should then run to completion as it finds the existing interface.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
        }

        // Verify that nothing new has been appended to the clients vector or the aps vector.
        assert_eq!(iface_manager.clients.len(), 1);
        assert_eq!(iface_manager.aps.len(), 0);
    }

    #[fuchsia::test]
    fn test_add_existing_ap_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

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
            let mut monitor_service_fut = test_values.monitor_service_stream.into_future();
            assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::QueryIface {
                    iface_id: TEST_AP_IFACE_ID, responder
                }) => {
                    let response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                        role: fidl_fuchsia_wlan_common::WlanMacRole::Ap,
                        id: TEST_AP_IFACE_ID,
                        phy_id: 0,
                        phy_assigned_id: 0,
                        sta_addr: [0; 6],
                    };
                    responder
                        .send(&mut Ok(response))
                        .expect("Sending iface response");
                }
            );

            // The future should then run to completion as it finds the existing interface.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
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
        exec: &mut fuchsia_async::TestExecutor,
        network_selector: Arc<NetworkSelector>,
        iface_manager: IfaceManagerService,
        req: IfaceManagerRequest,
        mut req_receiver: oneshot::Receiver<Result<T, Error>>,
        monitor_service_stream: fidl_fuchsia_wlan_device_service::DeviceMonitorRequestStream,
        test_type: TestType,
    ) {
        // Create other components to run the service.
        let iface_manager_client = Arc::new(Mutex::new(FakeIfaceManagerRequester::new()));

        // Start the service loop
        let (mut sender, receiver) = mpsc::channel(1);
        let (_stats_sender, stats_receiver) = mpsc::unbounded();
        let (_defect_sender, defect_receiver) = mpsc::unbounded();
        let serve_fut = serve_iface_manager_requests(
            iface_manager,
            iface_manager_client,
            network_selector,
            receiver,
            stats_receiver,
            defect_receiver,
        );
        pin_mut!(serve_fut);

        // Send the client's request
        sender.try_send(req).expect("failed to send request");

        // Service any device service requests in the event that a new client SME proxy is required
        // for the operation under test.
        let mut monitor_service_fut = monitor_service_stream.into_future();
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        match poll_service_req(exec, &mut monitor_service_fut) {
            Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                iface_id: TEST_CLIENT_IFACE_ID,
                sme_server: _,
                responder,
            }) => {
                // Send back a positive acknowledgement.
                assert!(responder.send(&mut Ok(())).is_ok());
            }
            Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetApSme {
                iface_id: TEST_AP_IFACE_ID,
                sme_server: _,
                responder,
            }) => {
                // Send back a positive acknowledgement.
                assert!(responder.send(&mut Ok(())).is_ok());
            }
            _ => {}
        }

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
        exec: &mut fuchsia_async::TestExecutor,
        network_selector: Arc<NetworkSelector>,
        iface_manager: IfaceManagerService,
        req: IfaceManagerRequest,
        mut req_receiver: oneshot::Receiver<()>,
        test_type: TestType,
    ) {
        // Create other components to run the service.
        let iface_manager_client = Arc::new(Mutex::new(FakeIfaceManagerRequester::new()));

        // Start the service loop
        let (mut sender, receiver) = mpsc::channel(1);
        let (_stats_sender, stats_receiver) = mpsc::unbounded();
        let (_defect_sender, defect_receiver) = mpsc::unbounded();
        let serve_fut = serve_iface_manager_requests(
            iface_manager,
            iface_manager_client,
            network_selector,
            receiver,
            stats_receiver,
            defect_receiver,
        );
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

    #[test_case(FakeClient {disconnect_ok: true, is_alive:true, expected_connect_selection: None},
        TestType::Pass; "successfully disconnects configured client")]
    #[test_case(FakeClient {disconnect_ok: false, is_alive:true, expected_connect_selection: None},
        TestType::Fail; "fails to disconnect configured client")]
    #[test_case(FakeClient {disconnect_ok: true, is_alive:true, expected_connect_selection: None},
        TestType::ClientError; "client drops receiver")]
    #[fuchsia::test(add_test_attr = false)]
    fn service_disconnect_test(fake_client: FakeClient, test_type: TestType) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _stream) = create_iface_manager_with_client(&test_values, true);
        iface_manager.clients[0].client_state_machine = Some(Box::new(fake_client));

        // Send a disconnect request.
        let (ack_sender, ack_receiver) = oneshot::channel();
        let req = DisconnectRequest {
            network_id: client_types::NetworkIdentifier {
                ssid: TEST_SSID.clone(),
                security_type: client_types::SecurityType::Wpa,
            },
            reason: client_types::DisconnectReason::NetworkUnsaved,
            responder: ack_sender,
        };
        let req = IfaceManagerRequest::AtomicOperation(AtomicOperation::Disconnect(req));

        run_service_test(
            &mut exec,
            test_values.network_selector,
            iface_manager,
            req,
            ack_receiver,
            test_values.monitor_service_stream,
            test_type,
        );
    }

    #[test_case(FakeClient {disconnect_ok: true, is_alive:true, expected_connect_selection: Some(create_connect_selection(&TEST_SSID, TEST_PASSWORD))},
        TestType::Pass; "successfully connected a client")]
    #[test_case(FakeClient {disconnect_ok: true, is_alive:true, expected_connect_selection: Some(create_connect_selection(&TEST_SSID, TEST_PASSWORD))},
        TestType::ClientError; "client drops receiver")]
    #[fuchsia::test(add_test_attr = false)]
    fn service_connect_test(fake_client: FakeClient, test_type: TestType) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _stream) = create_iface_manager_with_client(&test_values, false);
        let connect_selection = fake_client.expected_connect_selection.clone().unwrap();
        iface_manager.clients[0].client_state_machine = Some(Box::new(fake_client));

        // Send a connect request.
        let (ack_sender, ack_receiver) = oneshot::channel();
        let req = ConnectRequest {
            request: ConnectAttemptRequest::new(
                connect_selection.target.network,
                connect_selection.target.credential,
                client_types::ConnectReason::FidlConnectRequest,
            ),
            responder: ack_sender,
        };
        let req = IfaceManagerRequest::Connect(req);

        // Currently the FakeScanRequester will panic if a scan is requested and no results are
        // queued, so add something. This test needs a few:
        // initial idle interface selection
        // for the connect request
        // idle interface selection after connect request fails
        exec.run_singlethreaded(test_values.scan_requester.add_scan_result(Ok(vec![])));
        exec.run_singlethreaded(test_values.scan_requester.add_scan_result(Ok(vec![])));
        exec.run_singlethreaded(test_values.scan_requester.add_scan_result(Ok(vec![])));

        run_service_test(
            &mut exec,
            test_values.network_selector,
            iface_manager,
            req,
            ack_receiver,
            test_values.monitor_service_stream,
            test_type,
        );
    }

    // This test is a bit of a twofer as it covers both the mechanism for recording idle interfaces
    // as well as the mechanism for querying idle interfaces.
    #[fuchsia::test]
    fn test_service_record_idle_client() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _stream) = create_iface_manager_with_client(&test_values, true);

        // Make the client state machine's liveness check fail.
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: true,
            is_alive: false,
            expected_connect_selection: None,
        }));

        // Create other components to run the service.
        let iface_manager_client = Arc::new(Mutex::new(FakeIfaceManagerRequester::new()));
        let (persistence_req_sender, _persistence_stream) = create_inspect_persistence_channel();
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let network_selector = Arc::new(NetworkSelector::new(
            test_values.saved_networks,
            test_values.scan_requester,
            create_wlan_hasher(),
            inspect::Inspector::new().root().create_child("network_selector"),
            persistence_req_sender,
            TelemetrySender::new(telemetry_sender),
        ));

        // Create mpsc channel to handle requests.
        let (mut sender, receiver) = mpsc::channel(1);
        let (_stats_sender, stats_receiver) = mpsc::unbounded();
        let serve_fut = serve_iface_manager_requests(
            iface_manager,
            iface_manager_client,
            network_selector,
            receiver,
            stats_receiver,
            test_values.defect_receiver,
        );
        pin_mut!(serve_fut);

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

    #[fuchsia::test]
    fn test_service_record_idle_client_response_failure() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (iface_manager, _stream) = create_iface_manager_with_client(&test_values, true);

        // Create other components to run the service.
        let iface_manager_client = Arc::new(Mutex::new(FakeIfaceManagerRequester::new()));
        let (persistence_req_sender, _persistence_stream) = create_inspect_persistence_channel();
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let network_selector = Arc::new(NetworkSelector::new(
            test_values.saved_networks,
            test_values.scan_requester,
            create_wlan_hasher(),
            inspect::Inspector::new().root().create_child("network_selector"),
            persistence_req_sender,
            TelemetrySender::new(telemetry_sender),
        ));

        // Create mpsc channel to handle requests.
        let (mut sender, receiver) = mpsc::channel(1);
        let (_stats_sender, stats_receiver) = mpsc::unbounded();
        let serve_fut = serve_iface_manager_requests(
            iface_manager,
            iface_manager_client,
            network_selector,
            receiver,
            stats_receiver,
            test_values.defect_receiver,
        );
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

    #[fuchsia::test]
    fn test_service_query_idle_client_response_failure() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (iface_manager, _stream) = create_iface_manager_with_client(&test_values, true);

        // Create other components to run the service.
        let iface_manager_client = Arc::new(Mutex::new(FakeIfaceManagerRequester::new()));
        let (persistence_req_sender, _persistence_stream) = create_inspect_persistence_channel();
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let network_selector = Arc::new(NetworkSelector::new(
            test_values.saved_networks,
            test_values.scan_requester,
            create_wlan_hasher(),
            inspect::Inspector::new().root().create_child("network_selector"),
            persistence_req_sender,
            TelemetrySender::new(telemetry_sender),
        ));

        // Create mpsc channel to handle requests.
        let (mut sender, receiver) = mpsc::channel(1);
        let (_stats_sender, stats_receiver) = mpsc::unbounded();
        let serve_fut = serve_iface_manager_requests(
            iface_manager,
            iface_manager_client,
            network_selector,
            receiver,
            stats_receiver,
            test_values.defect_receiver,
        );
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

    #[derive(Debug)]
    struct FakeIfaceManagerRequester {}

    impl FakeIfaceManagerRequester {
        fn new() -> Self {
            FakeIfaceManagerRequester {}
        }
    }

    #[async_trait]
    impl IfaceManagerApi for FakeIfaceManagerRequester {
        async fn disconnect(
            &mut self,
            _network_id: types::NetworkIdentifier,
            _reason: client_types::DisconnectReason,
        ) -> Result<(), Error> {
            unimplemented!()
        }

        async fn connect(&mut self, _connect_req: ConnectAttemptRequest) -> Result<(), Error> {
            unimplemented!()
        }

        async fn record_idle_client(&mut self, _iface_id: u16) -> Result<(), Error> {
            unimplemented!()
        }

        async fn has_idle_client(&mut self) -> Result<bool, Error> {
            unimplemented!()
        }

        async fn handle_added_iface(&mut self, _iface_id: u16) -> Result<(), Error> {
            unimplemented!()
        }

        async fn handle_removed_iface(&mut self, _iface_id: u16) -> Result<(), Error> {
            unimplemented!()
        }

        async fn get_sme_proxy_for_scan(&mut self) -> Result<SmeForScan, Error> {
            unimplemented!()
        }

        async fn stop_client_connections(
            &mut self,
            _reason: client_types::DisconnectReason,
        ) -> Result<(), Error> {
            unimplemented!()
        }

        async fn start_client_connections(&mut self) -> Result<(), Error> {
            unimplemented!()
        }

        async fn start_ap(
            &mut self,
            _config: ap_fsm::ApConfig,
        ) -> Result<oneshot::Receiver<()>, Error> {
            unimplemented!()
        }

        async fn stop_ap(
            &mut self,
            _ssid: ap_types::Ssid,
            _password: Vec<u8>,
        ) -> Result<(), Error> {
            unimplemented!()
        }

        async fn stop_all_aps(&mut self) -> Result<(), Error> {
            unimplemented!()
        }

        async fn has_wpa3_capable_client(&mut self) -> Result<bool, Error> {
            unimplemented!()
        }

        async fn set_country(
            &mut self,
            _country_code: Option<[u8; REGION_CODE_LEN]>,
        ) -> Result<(), Error> {
            unimplemented!()
        }
    }

    #[fuchsia::test]
    fn test_service_add_iface_succeeds() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks.clone(),
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        // Create other components to run the service.
        let iface_manager_client = Arc::new(Mutex::new(FakeIfaceManagerRequester::new()));
        let (persistence_req_sender, _persistence_stream) = create_inspect_persistence_channel();
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let network_selector = Arc::new(NetworkSelector::new(
            test_values.saved_networks,
            test_values.scan_requester,
            create_wlan_hasher(),
            inspect::Inspector::new().root().create_child("network_selector"),
            persistence_req_sender,
            TelemetrySender::new(telemetry_sender),
        ));

        // Create mpsc channel to handle requests.
        let (mut sender, receiver) = mpsc::channel(1);
        let serve_fut = serve_iface_manager_requests(
            iface_manager,
            iface_manager_client,
            network_selector,
            receiver,
            test_values.stats_receiver,
            test_values.defect_receiver,
        );
        pin_mut!(serve_fut);

        // Report a new interface.
        let (new_iface_sender, mut new_iface_receiver) = oneshot::channel();
        let req = AddIfaceRequest { iface_id: TEST_CLIENT_IFACE_ID, responder: new_iface_sender };
        let req = IfaceManagerRequest::AddIface(req);
        sender.try_send(req).expect("failed to send request");

        // Run the service loop to begin processing the request.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Expect and interface query and notify that this is a client interface.
        let mut monitor_service_fut = test_values.monitor_service_stream.into_future();
        assert_variant!(
            poll_service_req(&mut exec, &mut monitor_service_fut),
            Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::QueryIface {
                iface_id: TEST_CLIENT_IFACE_ID, responder
            }) => {
                let response = fidl_fuchsia_wlan_device_service::QueryIfaceResponse {
                    role: fidl_fuchsia_wlan_common::WlanMacRole::Client,
                    id: TEST_CLIENT_IFACE_ID,
                    phy_id: 0,
                    phy_assigned_id: 0,
                    sta_addr: [0; 6],
                };
                responder
                    .send(&mut Ok(response))
                    .expect("Sending iface response");
            }
        );

        // Expect that we have requested a client SME proxy from get_client.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            poll_service_req(&mut exec, &mut monitor_service_fut),
            Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                iface_id: TEST_CLIENT_IFACE_ID, sme_server: _, responder
            }) => {
                // Send back a positive acknowledgement.
                assert!(responder.send(&mut Ok(())).is_ok());
            }
        );

        // Expect that we have queried an iface from get_client creating a new IfaceContainer.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let features_server = assert_variant!(
            poll_service_req(&mut exec, &mut monitor_service_fut),
            Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetFeatureSupport {
                iface_id: TEST_CLIENT_IFACE_ID, feature_support_server, responder
            }) => {
                assert!(responder.send(&mut Ok(())).is_ok());
                feature_support_server
            }
        );
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        let mut features_req_fut = features_server
            .into_stream()
            .expect("Failed to create features req stream")
            .into_future();
        assert_variant!(
            poll_service_req(&mut exec, &mut features_req_fut),
            Poll::Ready(fidl_fuchsia_wlan_sme::FeatureSupportRequest::QuerySecuritySupport {
                responder
            }) => {
                responder.send(&mut Ok(fake_security_support())).expect("Failed to send security support response");
            }
        );

        // Expect that we have requested a client SME proxy from creating the client state machine.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(
            poll_service_req(&mut exec, &mut monitor_service_fut),
            Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                iface_id: TEST_CLIENT_IFACE_ID, sme_server: _, responder
            }) => {
                // Send back a positive acknowledgement.
                assert!(responder.send(&mut Ok(())).is_ok());
            }
        );

        // Run the service again to ensure the response is sent.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Check that the response was received.
        assert_variant!(exec.run_until_stalled(&mut new_iface_receiver), Poll::Ready(Ok(())));
    }

    #[test_case(TestType::Fail; "failed to add interface")]
    #[test_case(TestType::ClientError; "client dropped receiver")]
    #[fuchsia::test(add_test_attr = false)]
    fn service_add_iface_negative_tests(test_type: TestType) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let phy_manager = phy_manager::PhyManager::new(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks.clone(),
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        // Report a new interface.
        let (new_iface_sender, new_iface_receiver) = oneshot::channel();
        let req = AddIfaceRequest { iface_id: TEST_CLIENT_IFACE_ID, responder: new_iface_sender };
        let req = IfaceManagerRequest::AddIface(req);

        // Drop the device monitor stream so that querying the interface properties will fail.
        drop(test_values.monitor_service_stream);

        run_service_test_with_unit_return(
            &mut exec,
            test_values.network_selector,
            iface_manager,
            req,
            new_iface_receiver,
            test_type,
        );
    }

    #[test_case(TestType::Pass; "successfully removed iface")]
    #[test_case(TestType::ClientError; "client dropped receiving end")]
    #[fuchsia::test(add_test_attr = false)]
    fn service_remove_iface_test(test_type: TestType) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (iface_manager, _stream) = create_iface_manager_with_client(&test_values, true);

        // Notify of interface removal.
        let (remove_iface_sender, remove_iface_receiver) = oneshot::channel();
        let req = RemoveIfaceRequest { iface_id: 123, responder: remove_iface_sender };
        let req = IfaceManagerRequest::RemoveIface(req);

        run_service_test_with_unit_return(
            &mut exec,
            test_values.network_selector,
            iface_manager,
            req,
            remove_iface_receiver,
            test_type,
        );
    }

    #[test_case(
        FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![],
            defects: vec![],
        },
        TestType::Pass;
        "successfully started client connections"
    )]
    #[test_case(
        FakePhyManager {
            create_iface_ok: false,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![TEST_CLIENT_IFACE_ID],
            defects: vec![],
        },
        TestType::Fail;
        "failed to start client connections"
    )]
    #[test_case(
        FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![TEST_CLIENT_IFACE_ID],
            defects: vec![],
        },
        TestType::ClientError;
        "client dropped receiver"
    )]
    #[fuchsia::test(add_test_attr = false)]
    fn service_start_client_connections_test(phy_manager: FakePhyManager, test_type: TestType) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks.clone(),
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        // Make start client connections request
        let (start_sender, start_receiver) = oneshot::channel();
        let req = StartClientConnectionsRequest { responder: start_sender };
        let req = IfaceManagerRequest::StartClientConnections(req);

        run_service_test(
            &mut exec,
            test_values.network_selector,
            iface_manager,
            req,
            start_receiver,
            test_values.monitor_service_stream,
            test_type,
        );
    }

    #[test_case(
        FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![TEST_CLIENT_IFACE_ID],
            defects: vec![],
        },
        TestType::Pass;
        "successfully stopped client connections"
    )]
    #[test_case(
        FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: false,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![TEST_CLIENT_IFACE_ID],
            defects: vec![],
        },
        TestType::Fail;
        "failed to stop client connections"
    )]
    #[test_case(
        FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![TEST_CLIENT_IFACE_ID],
            defects: vec![],
        },
        TestType::ClientError;
        "client dropped receiver"
    )]
    #[fuchsia::test(add_test_attr = false)]
    fn service_stop_client_connections_test(phy_manager: FakePhyManager, test_type: TestType) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);

        // Create an empty PhyManager and IfaceManager.
        let iface_manager = IfaceManagerService::new(
            Arc::new(Mutex::new(phy_manager)),
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks.clone(),
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        // Make stop client connections request
        let (stop_sender, stop_receiver) = oneshot::channel();
        let req = StopClientConnectionsRequest {
            responder: stop_sender,
            reason: client_types::DisconnectReason::FidlStopClientConnectionsRequest,
        };
        let req = IfaceManagerRequest::AtomicOperation(AtomicOperation::StopClientConnections(req));

        run_service_test(
            &mut exec,
            test_values.network_selector,
            iface_manager,
            req,
            stop_receiver,
            test_values.monitor_service_stream,
            test_type,
        );
    }

    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::Pass; "successfully starts AP")]
    #[test_case(FakeAp { start_succeeds: false, stop_succeeds: true, exit_succeeds: true }, TestType::Fail; "fails to start AP")]
    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::ClientError; "client drops receiver")]
    #[fuchsia::test(add_test_attr = false)]
    fn service_start_ap_test(fake_ap: FakeAp, test_type: TestType) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an IfaceManager with a fake AP interface.
        let iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);

        // Request that an AP be started.
        let (start_sender, start_receiver) = oneshot::channel();
        let req = StartApRequest {
            config: create_ap_config(&TEST_SSID, TEST_PASSWORD),
            responder: start_sender,
        };
        let req = IfaceManagerRequest::StartAp(req);

        run_service_test(
            &mut exec,
            test_values.network_selector,
            iface_manager,
            req,
            start_receiver,
            test_values.monitor_service_stream,
            test_type,
        );
    }

    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::Pass; "successfully stops AP")]
    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: false, exit_succeeds: true }, TestType::Fail; "fails to stop AP")]
    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::ClientError; "client drops receiver")]
    #[fuchsia::test(add_test_attr = false)]
    fn service_stop_ap_test(fake_ap: FakeAp, test_type: TestType) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an IfaceManager with a configured fake AP interface.
        let mut iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);
        let config = create_ap_config(&TEST_SSID, TEST_PASSWORD);
        iface_manager.aps[0].config = Some(config);

        // Request that an AP be stopped.
        let (stop_sender, stop_receiver) = oneshot::channel();
        let req = StopApRequest {
            ssid: TEST_SSID.clone(),
            password: TEST_PASSWORD.as_bytes().to_vec(),
            responder: stop_sender,
        };
        let req = IfaceManagerRequest::AtomicOperation(AtomicOperation::StopAp(req));

        run_service_test(
            &mut exec,
            test_values.network_selector,
            iface_manager,
            req,
            stop_receiver,
            test_values.monitor_service_stream,
            test_type,
        );
    }

    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::Pass; "successfully stops AP")]
    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: false, exit_succeeds: true }, TestType::Fail; "fails to stop AP")]
    #[test_case(FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true }, TestType::ClientError; "client drops receiver")]
    #[fuchsia::test(add_test_attr = false)]
    fn service_stop_all_aps_test(fake_ap: FakeAp, test_type: TestType) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an IfaceManager with a fake AP interface.
        let iface_manager = create_iface_manager_with_ap(&test_values, fake_ap);

        // Request that an AP be started.
        let (stop_sender, stop_receiver) = oneshot::channel();
        let req = StopAllApsRequest { responder: stop_sender };
        let req = IfaceManagerRequest::AtomicOperation(AtomicOperation::StopAllAps(req));

        run_service_test(
            &mut exec,
            test_values.network_selector,
            iface_manager,
            req,
            stop_receiver,
            test_values.monitor_service_stream,
            test_type,
        );
    }

    /// Tests the case where the IfaceManager attempts to reconnect a client interface that has
    /// disconnected.
    #[fuchsia::test]
    fn test_reconnect_disconnected_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let mut test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Make the client state machine report that it is dead.
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: false,
            is_alive: false,
            expected_connect_selection: None,
        }));

        let (saved_networks, mut stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server());
        test_values.saved_networks = Arc::new(saved_networks);

        // Update the saved networks with knowledge of the test SSID and credentials.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        let save_network_fut = test_values.saved_networks.store(network_id, credential);
        pin_mut!(save_network_fut);
        assert_variant!(exec.run_until_stalled(&mut save_network_fut), Poll::Pending);

        process_stash_write(&mut exec, &mut stash_server);

        // Ask the IfaceManager to reconnect.
        let mut sme_stream = {
            let config = create_connect_selection(&TEST_SSID, TEST_PASSWORD);
            let reconnect_fut =
                iface_manager.attempt_client_reconnect(TEST_CLIENT_IFACE_ID, config);
            pin_mut!(reconnect_fut);
            assert_variant!(exec.run_until_stalled(&mut reconnect_fut), Poll::Pending);

            // There should be a request for a client SME proxy.
            let mut monitor_service_fut = test_values.monitor_service_stream.into_future();
            let sme_server = assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                    iface_id: TEST_CLIENT_IFACE_ID, sme_server, responder
                }) => {
                    assert!(responder.send(&mut Ok(())).is_ok());
                    sme_server
                }
            );

            // The reconnect future should finish up.
            assert_variant!(exec.run_until_stalled(&mut reconnect_fut), Poll::Ready(Ok(())));

            sme_server.into_stream().unwrap().into_future()
        };

        // Start running the new state machine.
        run_state_machine_futures(&mut exec, &mut iface_manager);

        // Verify telemetry event has been sent.
        let event = assert_variant!(test_values.telemetry_receiver.try_next(), Ok(Some(ev)) => ev);
        assert_variant!(
            event,
            TelemetryEvent::StartEstablishConnection { reset_start_time: false }
        );

        // Acknowledge the disconnection attempt.
        assert_variant!(
            poll_sme_req(&mut exec, &mut sme_stream),
            Poll::Ready(fidl_fuchsia_wlan_sme::ClientSmeRequest::Disconnect{ responder, reason: fidl_fuchsia_wlan_sme::UserDisconnectReason::Startup }) => {
                responder.send().expect("could not send response")
            }
        );

        // Make sure that the connect request has been sent out.
        run_state_machine_futures(&mut exec, &mut iface_manager);
        let connect_txn_handle = assert_variant!(
            poll_sme_req(&mut exec, &mut sme_stream),
            Poll::Ready(fidl_fuchsia_wlan_sme::ClientSmeRequest::Connect{ req, txn, control_handle: _ }) => {
                assert_eq!(req.ssid, TEST_SSID.clone());
                assert_eq!(Credential::Password(TEST_PASSWORD.as_bytes().to_vec()), req.authentication.credentials);
                let (_stream, ctrl) = txn.expect("connect txn unused")
                    .into_stream_and_control_handle().expect("error accessing control handle");
                ctrl
            }
        );
        connect_txn_handle
            .send_on_connect_result(&mut fake_successful_connect_result())
            .expect("failed to send connection completion");

        // Verify that the state machine future is still alive.
        run_state_machine_futures(&mut exec, &mut iface_manager);
        assert!(!iface_manager.fsm_futures.is_empty());
    }

    /// Tests the case where the IfaceManager attempts to reconnect a client interface that does
    /// not exist.  This simulates the case where the client state machine exits because client
    /// connections have been stopped.
    #[fuchsia::test]
    fn test_reconnect_nonexistent_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create an empty IfaceManager
        let test_values = test_setup(&mut exec);
        let phy_manager = create_empty_phy_manager(
            test_values.monitor_service_proxy.clone(),
            test_values.node,
            test_values.telemetry_sender.clone(),
        );
        let mut iface_manager = IfaceManagerService::new(
            phy_manager,
            test_values.client_update_sender,
            test_values.ap_update_sender,
            test_values.monitor_service_proxy,
            test_values.saved_networks.clone(),
            test_values.telemetry_sender,
            test_values.stats_sender,
            test_values.defect_sender,
        );

        // Update the saved networks with knowledge of the test SSID and credentials.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        assert!(exec
            .run_singlethreaded(test_values.saved_networks.store(network_id, credential))
            .expect("failed to store a network password")
            .is_none());

        // Ask the IfaceManager to reconnect.
        {
            let config = create_connect_selection(&TEST_SSID, TEST_PASSWORD);
            let reconnect_fut =
                iface_manager.attempt_client_reconnect(TEST_CLIENT_IFACE_ID, config);
            pin_mut!(reconnect_fut);
            assert_variant!(exec.run_until_stalled(&mut reconnect_fut), Poll::Ready(Ok(())));
        }

        // Ensure that there are no new state machines.
        assert!(iface_manager.fsm_futures.is_empty());
    }

    /// Tests the case where the IfaceManager attempts to reconnect a client interface that is
    /// already connected to another networks.  This simulates the case where a client state
    /// machine exits because a new connect request has come in.
    #[fuchsia::test]
    fn test_reconnect_connected_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let mut test_values = test_setup(&mut exec);
        let (mut iface_manager, _sme_stream) = create_iface_manager_with_client(&test_values, true);

        // Make the client state machine report that it is alive.
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: false,
            is_alive: true,
            expected_connect_selection: None,
        }));

        let (saved_networks, mut stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server());
        test_values.saved_networks = Arc::new(saved_networks);

        // Update the saved networks with knowledge of the test SSID and credentials.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        let save_network_fut = test_values.saved_networks.store(network_id, credential);
        pin_mut!(save_network_fut);
        assert_variant!(exec.run_until_stalled(&mut save_network_fut), Poll::Pending);

        process_stash_write(&mut exec, &mut stash_server);

        // Ask the IfaceManager to reconnect.
        {
            let config = create_connect_selection(&TEST_SSID, TEST_PASSWORD);
            let reconnect_fut =
                iface_manager.attempt_client_reconnect(TEST_CLIENT_IFACE_ID, config);
            pin_mut!(reconnect_fut);
            assert_variant!(exec.run_until_stalled(&mut reconnect_fut), Poll::Ready(Ok(())));
        }

        // Ensure that there are no new state machines.
        assert!(iface_manager.fsm_futures.is_empty());
    }

    enum NetworkSelectionMissingAttribute {
        AllAttributesPresent,
        IdleClient,
        SavedNetwork,
        NetworkSelectionInProgress,
    }

    #[test_case(NetworkSelectionMissingAttribute::AllAttributesPresent; "scan is requested")]
    #[test_case(NetworkSelectionMissingAttribute::IdleClient; "no idle clients")]
    #[test_case(NetworkSelectionMissingAttribute::SavedNetwork; "no saved networks")]
    #[test_case(NetworkSelectionMissingAttribute::NetworkSelectionInProgress; "selection already in progress")]
    #[fuchsia::test(add_test_attr = false)]
    fn test_initiate_network_selection(test_type: NetworkSelectionMissingAttribute) {
        // Start out by setting the test up such that we would expect a scan to be requested.
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let mut test_values = test_setup(&mut exec);

        // Insert a saved network.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        let mut stash_server = {
            let (saved_networks, mut stash_server) =
                exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server());
            test_values.saved_networks = Arc::new(saved_networks);

            // Update the saved networks with knowledge of the test SSID and credentials.
            let save_network_fut =
                test_values.saved_networks.store(network_id.clone(), credential.clone());
            pin_mut!(save_network_fut);
            assert_variant!(exec.run_until_stalled(&mut save_network_fut), Poll::Pending);

            process_stash_write(&mut exec, &mut stash_server);

            stash_server
        };

        // Make the client state machine report that it is not alive.
        let (mut iface_manager, _sme_stream) = create_iface_manager_with_client(&test_values, true);
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: true,
            is_alive: false,
            expected_connect_selection: None,
        }));

        // Create a network selector to be used by the network selection request.
        let (persistence_req_sender, _persistence_stream) = create_inspect_persistence_channel();
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let selector = Arc::new(NetworkSelector::new(
            test_values.saved_networks.clone(),
            test_values.scan_requester.clone(),
            create_wlan_hasher(),
            inspect::Inspector::new().root().create_child("network_selector"),
            persistence_req_sender,
            TelemetrySender::new(telemetry_sender),
        ));

        // Setup the test to prevent a network selection from happening for whatever reason was specified.
        match test_type {
            NetworkSelectionMissingAttribute::AllAttributesPresent => {
                // Currently the FakeScanRequester will panic if a scan is requested and no results
                // are queued, so add something.
                exec.run_singlethreaded(test_values.scan_requester.add_scan_result(Ok(vec![])));
            }
            NetworkSelectionMissingAttribute::IdleClient => {
                // Make the client state machine report that it is alive.
                iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
                    disconnect_ok: true,
                    is_alive: true,
                    expected_connect_selection: None,
                }));
            }
            NetworkSelectionMissingAttribute::SavedNetwork => {
                // Remove the saved network so that there are no known networks to connect to.
                let remove_network_fut =
                    test_values.saved_networks.remove(network_id.clone(), credential);
                pin_mut!(remove_network_fut);
                assert_variant!(exec.run_until_stalled(&mut remove_network_fut), Poll::Pending);
                process_stash_delete(&mut exec, &mut stash_server);
            }
            NetworkSelectionMissingAttribute::NetworkSelectionInProgress => {
                // Insert a future so that it looks like a scan is in progress.
                iface_manager.network_selection_futures.push(ready(None).boxed());
            }
        }

        {
            // Run the future to completion.
            let fut = initiate_network_selection(&mut iface_manager, selector);
            pin_mut!(fut);
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        // Verify telemetry event if the condition is right
        match test_type {
            NetworkSelectionMissingAttribute::AllAttributesPresent => {
                let event =
                    assert_variant!(test_values.telemetry_receiver.try_next(), Ok(Some(ev)) => ev);
                assert_variant!(
                    event,
                    TelemetryEvent::StartEstablishConnection { reset_start_time: false }
                );
            }
            _ => {
                assert_variant!(test_values.telemetry_receiver.try_next(), Err(_));
            }
        }

        // Run all network_selection futures to completion.
        for mut network_selection_future in iface_manager.network_selection_futures.iter_mut() {
            assert_variant!(exec.run_until_stalled(&mut network_selection_future), Poll::Ready(_));
        }

        // We are using a scan request issuance as a proxy to determine if the network selection
        // module took action.
        let scan_request_guard =
            exec.run_singlethreaded(test_values.scan_requester.scan_requests.lock());
        match test_type {
            NetworkSelectionMissingAttribute::AllAttributesPresent => {
                assert_eq!(
                    *scan_request_guard,
                    vec![(scan::ScanReason::NetworkSelection, vec![], vec![])]
                );
            }
            NetworkSelectionMissingAttribute::IdleClient
            | NetworkSelectionMissingAttribute::SavedNetwork
            | NetworkSelectionMissingAttribute::NetworkSelectionInProgress => {
                assert_eq!(*scan_request_guard, vec![]);
            }
        }
    }

    #[fuchsia::test]
    fn test_scan_result_backoff() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create an IfaceManagerService
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _stream) = create_iface_manager_with_client(&test_values, true);

        // Create a timer to periodically check to ensure that all client interfaces are connected.
        let mut reconnect_monitor_interval: i64 = 1;
        let mut connectivity_monitor_timer =
            fasync::Interval::new(zx::Duration::from_seconds(reconnect_monitor_interval));

        // Simulate multiple failed scan attempts and ensure that the timer interval backs off as
        // expected.
        let expected_wait_times =
            vec![2, 4, 8, MAX_AUTO_CONNECT_RETRY_SECONDS, MAX_AUTO_CONNECT_RETRY_SECONDS];

        for i in 0..5 {
            {
                let fut = handle_network_selection_results(
                    None,
                    &mut iface_manager,
                    &mut reconnect_monitor_interval,
                    &mut connectivity_monitor_timer,
                );
                pin_mut!(fut);
                assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
            }
            assert_eq!(reconnect_monitor_interval, expected_wait_times[i]);
        }
    }

    #[fuchsia::test]
    fn test_reconnect_on_network_selection_results() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an interface manager with an unconfigured client interface.
        let (mut iface_manager, _sme_stream) =
            create_iface_manager_with_client(&test_values, false);
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: true,
            is_alive: false,
            expected_connect_selection: None,
        }));

        // Setup for a reconnection attempt.
        let mut reconnect_monitor_interval = 1;
        let mut connectivity_monitor_timer =
            fasync::Interval::new(zx::Duration::from_seconds(reconnect_monitor_interval));

        // Create a candidate network.
        let ssid = TEST_SSID.clone();
        let network_id = NetworkIdentifier::new(ssid.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        let network = Some(client_types::ScannedCandidate {
            network: network_id,
            credential: credential,
            bss_description: random_fidl_bss_description!(Open, bssid: [20, 30, 40, 50, 60, 70]),
            observation: client_types::ScanObservation::Passive,
            has_multiple_bss_candidates: true,
            mutual_security_protocols: [SecurityDescriptor::OPEN].into_iter().collect(),
        });

        {
            // Run reconnection attempt
            let fut = handle_network_selection_results(
                network,
                &mut iface_manager,
                &mut reconnect_monitor_interval,
                &mut connectivity_monitor_timer,
            );

            pin_mut!(fut);

            // Expect a client SME proxy request
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            let mut monitor_service_fut = test_values.monitor_service_stream.into_future();
            assert_variant!(
                poll_service_req(&mut exec, &mut monitor_service_fut),
                Poll::Ready(fidl_fuchsia_wlan_device_service::DeviceMonitorRequest::GetClientSme {
                    iface_id: TEST_CLIENT_IFACE_ID, sme_server, responder
                }) => {
                    // Send back a positive acknowledgement.
                    assert!(responder.send(&mut Ok(())).is_ok());

                    sme_server
                }
            );

            // The future should then complete.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        // The reconnect attempt should have seen an idle client interface and created a new client
        // state machine future for it.
        assert!(!iface_manager.fsm_futures.is_empty());

        // There should not be any idle clients.
        assert!(iface_manager.idle_clients().is_empty());
    }

    #[fuchsia::test]
    fn test_idle_client_remains_after_failed_reconnection() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an interface manager with an unconfigured client interface.
        let (mut iface_manager, _sme_stream) =
            create_iface_manager_with_client(&test_values, false);
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: true,
            is_alive: false,
            expected_connect_selection: None,
        }));

        // Setup for a reconnection attempt.
        let mut reconnect_monitor_interval = 1;
        let mut connectivity_monitor_timer =
            fasync::Interval::new(zx::Duration::from_seconds(reconnect_monitor_interval));

        // Create a candidate network.
        let ssid = TEST_SSID.clone();
        let network_id = NetworkIdentifier::new(ssid.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        let network = Some(client_types::ScannedCandidate {
            network: network_id,
            credential: credential,
            bss_description: random_fidl_bss_description!(Open, bssid: [20, 30, 40, 50, 60, 70]),
            observation: client_types::ScanObservation::Passive,
            has_multiple_bss_candidates: true,
            mutual_security_protocols: [SecurityDescriptor::OPEN].into_iter().collect(),
        });

        {
            // Run reconnection attempt
            let fut = handle_network_selection_results(
                network,
                &mut iface_manager,
                &mut reconnect_monitor_interval,
                &mut connectivity_monitor_timer,
            );

            pin_mut!(fut);

            // Drop the device service stream so that the SME request fails.
            drop(test_values.monitor_service_stream);

            // The future should then complete.
            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        // There should still be an idle client
        assert!(!iface_manager.idle_clients().is_empty());
    }

    #[fuchsia::test]
    fn test_handle_bss_selection_for_connect_request_results() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Create an interface manager with an unconfigured client interface.
        let (mut iface_manager, _sme_stream) =
            create_iface_manager_with_client(&test_values, false);

        // Create a request
        let ssid = TEST_SSID.clone();
        let network_id = NetworkIdentifier::new(ssid.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        let request = ConnectAttemptRequest::new(
            network_id.clone(),
            credential.clone(),
            client_types::ConnectReason::FidlConnectRequest,
        );
        let scanned_candidate = client_types::ScannedCandidate {
            network: network_id.clone(),
            credential: credential.clone(),
            bss_description: random_fidl_bss_description!(Wpa1, bssid: [20, 30, 40, 50, 60, 70]),
            observation: client_types::ScanObservation::Passive,
            has_multiple_bss_candidates: true,
            mutual_security_protocols: [SecurityDescriptor::WPA1].into_iter().collect(),
        };

        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: true,
            is_alive: true,
            expected_connect_selection: Some(client_types::ConnectSelection {
                target: scanned_candidate.clone(),
                reason: client_types::ConnectReason::FidlConnectRequest,
            }),
        }));

        assert!(!iface_manager.idle_clients().is_empty());

        {
            let fut = handle_bss_selection_for_connect_request_results(
                (request, Some(scanned_candidate)),
                &mut iface_manager,
                test_values.network_selector.clone(),
            );
            pin_mut!(fut);

            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        // There should not be any idle clients.
        assert!(iface_manager.idle_clients().is_empty());
        assert_eq!(iface_manager.clients[0].config, Some(network_id));
    }

    #[fuchsia::test]
    fn test_terminated_client() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let mut test_values = test_setup(&mut exec);

        // Create a fake network entry so that a reconnect will be attempted.
        let network_id = NetworkIdentifier::new(TEST_SSID.clone(), SecurityType::Wpa);
        let credential = Credential::Password(TEST_PASSWORD.as_bytes().to_vec());
        let (saved_networks, mut stash_server) =
            exec.run_singlethreaded(SavedNetworksManager::new_and_stash_server());
        test_values.saved_networks = Arc::new(saved_networks);

        // Update the saved networks with knowledge of the test SSID and credentials.
        {
            let save_network_fut =
                test_values.saved_networks.store(network_id.clone(), credential.clone());
            pin_mut!(save_network_fut);
            assert_variant!(exec.run_until_stalled(&mut save_network_fut), Poll::Pending);

            process_stash_write(&mut exec, &mut stash_server);
        }

        // Create a network selector.
        let (persistence_req_sender, _persistence_stream) = create_inspect_persistence_channel();
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let selector = Arc::new(NetworkSelector::new(
            test_values.saved_networks.clone(),
            test_values.scan_requester.clone(),
            create_wlan_hasher(),
            inspect::Inspector::new().root().create_child("network_selector"),
            persistence_req_sender,
            TelemetrySender::new(telemetry_sender),
        ));

        // Create an interface manager with an unconfigured client interface.
        let (mut iface_manager, _sme_stream) =
            create_iface_manager_with_client(&test_values, false);
        iface_manager.clients[0].client_state_machine = Some(Box::new(FakeClient {
            disconnect_ok: true,
            is_alive: false,
            expected_connect_selection: None,
        }));

        // Create remaining boilerplate to call handle_terminated_state_machine.
        let metadata = StateMachineMetadata {
            role: fidl_fuchsia_wlan_common::WlanMacRole::Client,
            iface_id: TEST_CLIENT_IFACE_ID,
        };

        {
            let fut = handle_terminated_state_machine(metadata, &mut iface_manager, selector);
            pin_mut!(fut);

            assert_eq!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        // Verify that there is a disconnected client.
        assert!(iface_manager.idle_clients().contains(&TEST_CLIENT_IFACE_ID));

        // Verify that a scan has been kicked off.
        assert!(!iface_manager.network_selection_futures.is_empty());
    }

    #[fuchsia::test]
    fn test_terminated_ap() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let (persistence_req_sender, _persistence_stream) = create_inspect_persistence_channel();
        let (telemetry_sender, _telemetry_receiver) = mpsc::channel::<TelemetryEvent>(100);
        let selector = Arc::new(NetworkSelector::new(
            test_values.saved_networks.clone(),
            test_values.scan_requester.clone(),
            create_wlan_hasher(),
            inspect::Inspector::new().root().create_child("network_selector"),
            persistence_req_sender,
            TelemetrySender::new(telemetry_sender),
        ));

        // Create an interface manager with an unconfigured client interface.
        let (mut iface_manager, _next_sme_req) =
            create_iface_manager_with_client(&test_values, true);

        // Create remaining boilerplate to call handle_terminated_state_machine.
        let metadata = StateMachineMetadata {
            role: fidl_fuchsia_wlan_common::WlanMacRole::Ap,
            iface_id: TEST_AP_IFACE_ID,
        };

        {
            let fut = handle_terminated_state_machine(metadata, &mut iface_manager, selector);
            pin_mut!(fut);

            assert_eq!(exec.run_until_stalled(&mut fut), Poll::Ready(()));
        }

        // Verify that the IfaceManagerService does not have an AP interface.
        assert!(iface_manager.aps.is_empty());
    }

    /// Test that if PhyManager knows of a client iface that supports WPA3, has_wpa3_capable_client
    /// will return true.
    #[fuchsia::test]
    fn test_has_wpa3_capable_client() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);
        iface_manager.phy_manager = Arc::new(Mutex::new(FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: true,
            wpa3_iface: Some(0),
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![TEST_CLIENT_IFACE_ID],
            defects: vec![],
        }));
        let fut = iface_manager.has_wpa3_capable_client();
        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(true));
    }

    /// Test that if PhyManager does not have a WPA3 client iface, has_wpa3_capable_client will
    /// return false.
    #[fuchsia::test]
    fn test_has_no_wpa3_capable_iface() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create a configured ClientIfaceContainer.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        iface_manager.phy_manager = Arc::new(Mutex::new(FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![TEST_CLIENT_IFACE_ID],
            defects: vec![],
        }));
        let fut = iface_manager.has_wpa3_capable_client();
        pin_mut!(fut);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(false));
    }

    /// Tests the operation of setting the country code.  The test cases of interest are:
    /// 1. Client connections and APs are initially enabled and all operations succeed.
    /// 2. Client connections are initially enabled and all operations succeed except restarting
    ///    client connections.
    ///    * In this scenario, the country code has been properly applied and the device should be
    ///      allowed to continue running.  Higher layers can optionally re-enable client
    ///      connections to recover.
    /// 3. APs are initially enabled and all operations succeed except restarting the AP.
    ///    * As in the above scenario, the device can be allowed to continue running and the API
    ///      client can attempt to restart the AP manually.
    /// 4. Stop client connections fails.
    /// 5. Stop all APs fails.
    /// 6. Set country fails.
    #[test_case(
        true, true, true, true, true, TestType::Pass;
        "client and AP enabled and all operations succeed"
    )]
    #[test_case(
        true, false, true, true, false, TestType::Pass;
        "client enabled and restarting client fails"
    )]
    #[test_case(
        false, true, true, true, false, TestType::Pass;
        "AP enabled and start AP fails after setting country"
    )]
    #[test_case(
        true, false, false, true, true, TestType::Fail;
        "stop client connections fails"
    )]
    #[test_case(
        false, true, false, true, true, TestType::Fail;
        "stop APs fails"
    )]
    #[test_case(
        false, true, true, false, true, TestType::Fail;
        "set country fails"
    )]
    #[fuchsia::test(add_test_attr = false)]
    fn set_country_service_test(
        client_connections_enabled: bool,
        ap_enabled: bool,
        destroy_iface_ok: bool,
        set_country_ok: bool,
        create_iface_ok: bool,
        test_type: TestType,
    ) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);

        // Seed a FakePhyManager with the test configuration information and provide the PhyManager
        // to a new IfaceManagerService to test the behavior given the configuration.
        let phy_manager = Arc::new(Mutex::new(FakePhyManager {
            create_iface_ok,
            destroy_iface_ok,
            set_country_ok,
            wpa3_iface: None,
            country_code: None,
            client_connections_enabled,
            client_ifaces: vec![],
            defects: vec![],
        }));

        let mut iface_manager = IfaceManagerService::new(
            phy_manager.clone(),
            test_values.client_update_sender.clone(),
            test_values.ap_update_sender.clone(),
            test_values.monitor_service_proxy.clone(),
            test_values.saved_networks.clone(),
            test_values.telemetry_sender.clone(),
            test_values.stats_sender,
            test_values.defect_sender,
        );

        // If the test calls for it, create an AP interface to test that the IfaceManager preserves
        // the configuration and restores it after setting the country code.
        if ap_enabled {
            let fake_ap = FakeAp { start_succeeds: true, stop_succeeds: true, exit_succeeds: true };
            let ap_container = ApIfaceContainer {
                iface_id: TEST_AP_IFACE_ID,
                config: Some(create_ap_config(&TEST_SSID, TEST_PASSWORD)),
                ap_state_machine: Box::new(fake_ap),
                enabled_time: None,
            };
            iface_manager.aps.push(ap_container);
        }

        // Call set_country and drive the operation to completion.
        let (set_country_sender, set_country_receiver) = oneshot::channel();
        let req = SetCountryRequest { country_code: Some([0, 0]), responder: set_country_sender };
        let req = IfaceManagerRequest::AtomicOperation(AtomicOperation::SetCountry(req));

        run_service_test(
            &mut exec,
            test_values.network_selector,
            iface_manager,
            req,
            set_country_receiver,
            test_values.monitor_service_stream,
            test_type,
        );

        if destroy_iface_ok && set_country_ok {
            let phy_manager_fut = phy_manager.lock();
            pin_mut!(phy_manager_fut);
            assert_variant!(
                exec.run_until_stalled(&mut phy_manager_fut),
                Poll::Ready(phy_manager) => {
                    assert_eq!(phy_manager.country_code, Some([0, 0]))
                }
            );
        }
    }

    fn fake_successful_connect_result() -> fidl_fuchsia_wlan_sme::ConnectResult {
        fidl_fuchsia_wlan_sme::ConnectResult {
            code: fidl_fuchsia_wlan_ieee80211::StatusCode::Success,
            is_credential_rejected: false,
            is_reconnect: false,
        }
    }

    /// Create an empty security support structure.
    fn fake_security_support() -> fidl_common::SecuritySupport {
        fidl_common::SecuritySupport {
            mfp: fidl_common::MfpFeature { supported: false },
            sae: fidl_common::SaeFeature {
                driver_handler_supported: false,
                sme_handler_supported: false,
            },
        }
    }

    /// Ensure that defect reports are passed through to the PhyManager.
    #[fuchsia::test]
    fn test_record_defect() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let phy_manager = Arc::new(Mutex::new(FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![],
            defects: vec![],
        }));

        {
            let mut defect_fut = initiate_record_defect(
                phy_manager.clone(),
                Defect::Phy(PhyFailure::IfaceCreationFailure { phy_id: 2 }),
            );

            // The future should complete immediately.
            assert_variant!(
                exec.run_until_stalled(&mut defect_fut),
                Poll::Ready(IfaceManagerOperation::ReportDefect)
            );
        }

        // Verify that the defect has been recorded.
        let phy_manager_fut = phy_manager.lock();
        pin_mut!(phy_manager_fut);
        assert_variant!(
            exec.run_until_stalled(&mut phy_manager_fut),
            Poll::Ready(phy_manager) => {
                assert_eq!(phy_manager.defects, vec![Defect::Phy(PhyFailure::IfaceCreationFailure {phy_id: 2})])
            }
        );
    }

    /// Ensure that state machine defects are passed through to the PhyManager.
    #[fuchsia::test]
    fn test_record_state_machine_defect() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");
        let test_values = test_setup(&mut exec);
        let phy_manager = Arc::new(Mutex::new(FakePhyManager {
            create_iface_ok: true,
            destroy_iface_ok: true,
            wpa3_iface: None,
            set_country_ok: true,
            country_code: None,
            client_connections_enabled: true,
            client_ifaces: vec![],
            defects: vec![],
        }));
        let iface_manager = IfaceManagerService::new(
            phy_manager.clone(),
            test_values.client_update_sender.clone(),
            test_values.ap_update_sender.clone(),
            test_values.monitor_service_proxy.clone(),
            test_values.saved_networks.clone(),
            test_values.telemetry_sender.clone(),
            test_values.stats_sender.clone(),
            test_values.defect_sender.clone(),
        );

        // Send a defect to the IfaceManager service loop.
        test_values
            .defect_sender
            .unbounded_send(Defect::Iface(IfaceFailure::ApStartFailure { iface_id: 0 }))
            .expect("failed to send defect notification");

        // Run the IfaceManager service so that it can process the defect.
        let iface_manager_client = Arc::new(Mutex::new(FakeIfaceManagerRequester::new()));
        let (_, receiver) = mpsc::channel(0);
        let serve_fut = serve_iface_manager_requests(
            iface_manager,
            iface_manager_client,
            test_values.network_selector,
            receiver,
            test_values.stats_receiver,
            test_values.defect_receiver,
        );
        pin_mut!(serve_fut);
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);

        // Verify that the defect has been recorded.
        let phy_manager_fut = phy_manager.lock();
        pin_mut!(phy_manager_fut);
        assert_variant!(
            exec.run_until_stalled(&mut phy_manager_fut),
            Poll::Ready(phy_manager) => {
                assert_eq!(phy_manager.defects, vec![Defect::Iface(IfaceFailure::ApStartFailure {iface_id: 0})])
            }
        );
    }

    // Demonstrates that the token passed to attempt_atomic_operation is held until the wrapped
    // future completes.
    #[fuchsia::test]
    fn test_attempt_atomic_operation() {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // The mpsc channel pair represents a requestor/worker pair to be managed by an
        // AtomicOneshotReceiver.
        let (mpsc_sender, mpsc_receiver) = mpsc::unbounded();
        let mut mpsc_receiver = atomic_oneshot_stream::AtomicOneshotStream::new(mpsc_receiver);

        // A oneshot pair is created to allow the test to synchronize around some work that the
        // worker will do while holding the token from the AtomicOneshotReceiver.
        let (oneshot_sender, oneshot_receiver) = oneshot::channel::<()>();

        // Create a dummy future that waits on the receiver and just returns nil.  This will be the
        // "work" that the worker does when it receives a request.
        let fut = Box::pin(async move { oneshot_receiver.await });

        // The mpsc_sender will send two requests to show that the second one is not processed
        // until the atomic operation completes.
        mpsc_sender.unbounded_send(()).expect("failed to send first message");
        mpsc_sender.unbounded_send(()).expect("failed to send second message");

        // Grab the request and the token from the receiving end.
        let token = {
            let mut oneshot_stream = mpsc_receiver.get_atomic_oneshot_stream();
            assert_variant!(
                exec.run_until_stalled(&mut oneshot_stream.next()),
                Poll::Ready(Some((token, ()))) => token
            )
        };

        // Throw the future and token into the atomic operation wrapper.  Verify that the future is
        // waiting for the oneshot sender to send something.
        let mut fut = attempt_atomic_operation(fut, token);
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);

        // Demonstrate that the AtomicOneshotStream is not able to produce the second request yet.
        {
            let mut oneshot_stream = mpsc_receiver.get_atomic_oneshot_stream();
            assert_variant!(exec.run_until_stalled(&mut oneshot_stream.next()), Poll::Ready(None));
        }

        // Send on the oneshot sender so that the wrapped future can run to completion and the
        // token will be dropped, allowing new requests to be received.
        oneshot_sender.send(()).expect("failed to send oneshot message");
        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(_));

        let mut oneshot_stream = mpsc_receiver.get_atomic_oneshot_stream();
        assert_variant!(exec.run_until_stalled(&mut oneshot_stream.next()), Poll::Ready(Some(_)));
    }

    fn test_atomic_operation<T: Debug>(
        req: IfaceManagerRequest,
        mut receiver: oneshot::Receiver<T>,
    ) {
        let mut exec = fuchsia_async::TestExecutor::new().expect("failed to create an executor");

        // Create an IfaceManager that has both a fake client and a fake AP state machine.  Hold on
        // to the receiving ends of the state machine command channels.  The receiving ends will
        // never be serviced, resulting in the atomic operations stalling while holding the
        // AtomicOneshotStream Token.
        let test_values = test_setup(&mut exec);
        let (mut iface_manager, _) = create_iface_manager_with_client(&test_values, true);

        // Setup the fake client.
        let (client_sender, client_receiver) = mpsc::channel(1);
        iface_manager.clients[0].client_state_machine =
            Some(Box::new(client_fsm::Client::new(client_sender)));

        // Setup the fake AP.
        let (ap_sender, ap_receiver) = mpsc::channel(1);
        iface_manager.aps.push(ApIfaceContainer {
            iface_id: TEST_AP_IFACE_ID,
            config: Some(create_ap_config(&TEST_SSID, TEST_PASSWORD)),
            ap_state_machine: Box::new(ap_fsm::AccessPoint::new(ap_sender)),
            enabled_time: None,
        });

        // For all of the operations except SetCountry, the atomic operation can be stalled by
        // mocking out the state machine interactions.  For SetCountry, instead make it look as if
        // client connections are disabled and there are no APs.  This ensures that the second half
        // of the operations which restores interfaces does not trigger.
        match &req {
            &IfaceManagerRequest::AtomicOperation(AtomicOperation::SetCountry(_)) => {
                let phy_manager = Arc::new(Mutex::new(FakePhyManager {
                    create_iface_ok: false,
                    destroy_iface_ok: false,
                    wpa3_iface: None,
                    set_country_ok: false,
                    country_code: None,
                    client_connections_enabled: false,
                    client_ifaces: vec![],
                    defects: vec![],
                }));
                iface_manager.phy_manager = phy_manager;
                let _ = iface_manager.aps.drain(..);
            }
            _ => {}
        }

        // Create other components to run the service.
        let iface_manager_client = Arc::new(Mutex::new(FakeIfaceManagerRequester::new()));

        // Start the service loop
        let (mut req_sender, req_receiver) = mpsc::channel(1);
        let (_stats_sender, stats_receiver) = mpsc::unbounded();
        let (_defect_sender, defect_receiver) = mpsc::unbounded();
        let serve_fut = serve_iface_manager_requests(
            iface_manager,
            iface_manager_client,
            test_values.network_selector,
            req_receiver,
            stats_receiver,
            defect_receiver,
        );
        pin_mut!(serve_fut);

        // Make the atomic call and run the service future until it stalls.
        req_sender.try_send(req).expect("failed to make atomic request");
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Pending,);

        // Make a second IfaceManager call and observe that there is no response.
        let (idle_iface_sender, mut idle_iface_receiver) = oneshot::channel();
        let req = HasIdleIfaceRequest { responder: idle_iface_sender };
        req_sender
            .try_send(IfaceManagerRequest::HasIdleIface(req))
            .expect("failed to send idle client check");
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut idle_iface_receiver), Poll::Pending,);

        // It doesn't matter whether the state machines respond successfully, just that the
        // operation finishes so that the atomic operation can progress.  Simply drop the receivers
        // to demonstrate that behavior.
        drop(client_receiver);
        drop(ap_receiver);

        // The atomic portion of the operation should now complete.
        assert_variant!(exec.run_until_stalled(&mut serve_fut), Poll::Pending);
        assert_variant!(exec.run_until_stalled(&mut receiver), Poll::Ready(_),);

        // The second operation should also get a response.
        assert_variant!(exec.run_until_stalled(&mut idle_iface_receiver), Poll::Ready(_),);
    }

    #[fuchsia::test]
    fn test_atomic_disconnect() {
        let (ack_sender, ack_receiver) = oneshot::channel();
        let req = DisconnectRequest {
            network_id: client_types::NetworkIdentifier {
                ssid: TEST_SSID.clone(),
                security_type: client_types::SecurityType::Wpa,
            },
            reason: client_types::DisconnectReason::NetworkUnsaved,
            responder: ack_sender,
        };
        let req = IfaceManagerRequest::AtomicOperation(AtomicOperation::Disconnect(req));
        test_atomic_operation(req, ack_receiver);
    }

    #[fuchsia::test]
    fn test_atomic_stop_client_connections() {
        let (ack_sender, ack_receiver) = oneshot::channel();
        let req = StopClientConnectionsRequest {
            responder: ack_sender,
            reason: client_types::DisconnectReason::FidlStopClientConnectionsRequest,
        };
        let req = IfaceManagerRequest::AtomicOperation(AtomicOperation::StopClientConnections(req));
        test_atomic_operation(req, ack_receiver);
    }

    #[fuchsia::test]
    fn test_atomic_stop_all_aps() {
        let (ack_sender, ack_receiver) = oneshot::channel();
        let req = StopAllApsRequest { responder: ack_sender };
        let req = IfaceManagerRequest::AtomicOperation(AtomicOperation::StopAllAps(req));
        test_atomic_operation(req, ack_receiver);
    }

    #[fuchsia::test]
    fn test_atomic_stop_ap() {
        let (ack_sender, ack_receiver) = oneshot::channel();
        let req = StopApRequest {
            ssid: TEST_SSID.clone(),
            password: TEST_PASSWORD.as_bytes().to_vec(),
            responder: ack_sender,
        };
        let req = IfaceManagerRequest::AtomicOperation(AtomicOperation::StopAp(req));
        test_atomic_operation(req, ack_receiver);
    }

    #[fuchsia::test]
    fn test_atomic_set_country() {
        let (ack_sender, ack_receiver) = oneshot::channel();
        let req = SetCountryRequest { country_code: Some([0, 0]), responder: ack_sender };
        let req = IfaceManagerRequest::AtomicOperation(AtomicOperation::SetCountry(req));
        test_atomic_operation(req, ack_receiver);
    }
}
