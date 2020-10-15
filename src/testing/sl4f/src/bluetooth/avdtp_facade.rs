// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_bluetooth_avdtp::{
    PeerControllerMarker, PeerControllerProxy, PeerManagerEvent, PeerManagerMarker,
    PeerManagerProxy,
};
use fuchsia_async as fasync;
use fuchsia_component::{client, fuchsia_single_component_package_url};
use fuchsia_syslog::macros::*;
use futures::stream::StreamExt;
use parking_lot::RwLock;
use std::sync::Arc;
use std::{collections::hash_map::Entry, collections::HashMap};

use crate::bluetooth::types::PeerFactoryMap;
use crate::common_utils::common::macros::{fx_err_and_bail, with_line};

#[derive(Debug)]
struct AvdtpFacadeInner {
    /// The current Avdtp service Proxy
    avdtp_service_proxy: Option<PeerManagerProxy>,

    ///The hashmap of Peer ids to PeerControllerProxys
    peer_map: Arc<RwLock<PeerFactoryMap>>,
}

#[derive(Debug)]
pub struct AvdtpFacade {
    inner: RwLock<AvdtpFacadeInner>,
}

/// Perform Bluetooth AVDTP fucntions for both Sink and Source.
///
/// Note this object is shared among all threads created by server.
///
impl AvdtpFacade {
    pub fn new() -> AvdtpFacade {
        AvdtpFacade {
            inner: RwLock::new(AvdtpFacadeInner {
                avdtp_service_proxy: None,
                peer_map: Arc::new(RwLock::new(HashMap::new())),
            }),
        }
    }

    /// Creates a Peer Manager Proxy
    async fn create_avdtp_service_proxy(&self, role: String) -> Result<PeerManagerProxy, Error> {
        let tag = "AvdtpFacade::create_avdtp_service_proxy";
        match self.inner.read().avdtp_service_proxy.clone() {
            Some(avdtp_service_proxy) => {
                fx_log_info!(
                    tag: &with_line!(tag),
                    "Current Avdtp service proxy: {:?}",
                    avdtp_service_proxy
                );
                Ok(avdtp_service_proxy)
            }
            None => {
                fx_log_info!(
                    tag: &with_line!(tag),
                    "Launching A2DP and setting new Avdtp service proxy"
                );
                let launcher = match client::launcher() {
                    Ok(r) => r,
                    Err(err) => fx_err_and_bail!(
                        &with_line!(tag),
                        format_err!("Failed to get launcher service: {}", err)
                    ),
                };
                let bt_a2dp_package_url = match role.as_ref() {
                    "source" => fuchsia_single_component_package_url!("bt-a2dp-source"),
                    "sink" => fuchsia_single_component_package_url!("bt-a2dp-sink"),
                    _ => fx_err_and_bail!(
                        &with_line!(tag),
                        format!("Invalid A2DP profile role: {}", role)
                    ),
                }
                .to_string();

                let bt_a2dp = client::launch(&launcher, bt_a2dp_package_url, None)?;

                let avdtp_service_proxy = bt_a2dp.connect_to_service::<PeerManagerMarker>();
                if let Err(err) = avdtp_service_proxy {
                    fx_err_and_bail!(
                        &with_line!(tag),
                        format_err!("Failed to create Avdtp service proxy: {}", err)
                    );
                }
                bt_a2dp.controller().detach()?;
                avdtp_service_proxy
            }
        }
    }

    /// Initialize the Avdtp service and starts A2DP Sink or Source based on input role.
    ///
    /// # Arguments
    /// * `role`: A String representing "sink" or "source".
    pub async fn init_avdtp_service_proxy(&self, role: String) -> Result<(), Error> {
        let tag = "AvdtpFacade::init_avdtp_service_proxy";
        self.inner.write().avdtp_service_proxy = Some(self.create_avdtp_service_proxy(role).await?);

        let avdtp_svc = match &self.inner.read().avdtp_service_proxy {
            Some(p) => p.clone(),
            None => fx_err_and_bail!(&with_line!(tag), "No AVDTP Service proxy created"),
        };

        let avdtp_service_future =
            AvdtpFacade::monitor_avdtp_event_stream(avdtp_svc, self.inner.write().peer_map.clone());

        let fut = async move {
            let result = avdtp_service_future.await;
            if let Err(_err) = result {
                fx_log_err!("Failed to monitor AVDTP event stream.");
            }
        };
        fasync::Task::spawn(fut).detach();

        Ok(())
    }

    /// Gets the currently connected peers.
    pub async fn get_connected_peers(&self) -> Result<Vec<u64>, Error> {
        let tag = "AvdtpFacade::get_connected_peers";
        let peer_ids = match &self.inner.read().avdtp_service_proxy {
            Some(p) => {
                let connected_peers = p.connected_peers().await?;
                let mut peer_id_list = Vec::new();
                for peer in connected_peers {
                    peer_id_list.push(peer.value);
                }
                peer_id_list
            }
            None => fx_err_and_bail!(&with_line!(tag), "No AVDTP Service proxy created"),
        };
        Ok(peer_ids)
    }

    /// Gets the PeerController by input peer_id.
    ///
    /// # Arguments
    /// * `peer_id`: The unique peer_id for the PeerController.
    fn get_peer_controller_by_id(&self, peer_id: u64) -> Option<PeerControllerProxy> {
        match self.inner.read().peer_map.write().get(&peer_id.to_string()) {
            Some(p) => Some(p.clone()),
            None => None,
        }
    }

    /// Initiate a stream configuration procedure for the input peer_id.
    ///
    /// # Arguments
    /// * `peer_id`: The peer id associated with the PeerController.
    pub async fn set_configuration(&self, peer_id: u64) -> Result<(), Error> {
        let tag = "AvdtpFacade::set_configuration";
        if let Some(p) = self.get_peer_controller_by_id(peer_id) {
            match p.set_configuration().await? {
                Err(err) => {
                    let err_msg = format_err!("Error: {:?}", err);
                    fx_err_and_bail!(&with_line!(tag), err_msg)
                }
                Ok(()) => Ok(()),
            }
        } else {
            fx_err_and_bail!(&with_line!(tag), format!("Peer id {:?} not found.", peer_id))
        }
    }

    /// Initiate a procedure to get the configuration information of the peer stream
    /// for the input peer_id.
    ///
    /// # Arguments
    /// * `peer_id`: The peer id associated with the PeerController.
    pub async fn get_configuration(&self, peer_id: u64) -> Result<(), Error> {
        let tag = "AvdtpFacade::get_configuration";
        if let Some(p) = self.get_peer_controller_by_id(peer_id) {
            match p.get_configuration().await? {
                Err(err) => {
                    let err_msg = format_err!("Error: {:?}", err);
                    fx_err_and_bail!(&with_line!(tag), err_msg)
                }
                Ok(()) => Ok(()),
            }
        } else {
            fx_err_and_bail!(&with_line!(tag), format!("Peer id {:?} not found.", peer_id))
        }
    }

    /// Initiate a procedure to get the capabilities for the input peer_id.
    ///
    /// # Arguments
    /// * `peer_id`: The peer id associated with the PeerController.
    pub async fn get_capabilities(&self, peer_id: u64) -> Result<(), Error> {
        let tag = "AvdtpFacade::get_capabilities";
        if let Some(p) = self.get_peer_controller_by_id(peer_id) {
            let result = p.get_capabilities().await;
            match result {
                Ok(capabilities) => fx_log_info!("{:?}", capabilities),
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Error getting capabilities: {:?}", e)
                ),
            };
            Ok(())
        } else {
            fx_err_and_bail!(&with_line!(tag), format!("Peer id {:?} not found.", peer_id))
        }
    }

    /// Initiate a procedure to get all the capabilities for the input peer_id.
    ///
    /// # Arguments
    /// * `peer_id`: The peer id associated with the PeerController.
    pub async fn get_all_capabilities(&self, peer_id: u64) -> Result<(), Error> {
        let tag = "AvdtpFacade::get_all_capabilities";
        if let Some(p) = self.get_peer_controller_by_id(peer_id) {
            let result = p.get_all_capabilities().await;
            match result {
                Ok(capabilities) => fx_log_info!("{:?}", capabilities),
                Err(e) => fx_err_and_bail!(
                    &with_line!(tag),
                    format_err!("Error getting capabilities: {:?}", e)
                ),
            };
            Ok(())
        } else {
            fx_err_and_bail!(&with_line!(tag), format!("Peer id {:?} not found.", peer_id))
        }
    }

    /// Initiate a suspend request to the stream for the input peer_id.
    /// This command will not resume nor reconfigure the stream.
    ///
    /// # Arguments
    /// * `peer_id`: The peer id associated with the PeerController.
    pub async fn reconfigure_stream(&self, peer_id: u64) -> Result<(), Error> {
        let tag = "AvdtpFacade::reconfigure_stream";
        if let Some(p) = self.get_peer_controller_by_id(peer_id) {
            match p.reconfigure_stream().await? {
                Err(err) => {
                    let err_msg = format_err!("Error: {:?}", err);
                    fx_err_and_bail!(&with_line!(tag), err_msg)
                }
                Ok(()) => Ok(()),
            }
        } else {
            fx_err_and_bail!(&with_line!(tag), format!("Peer id {:?} not found.", peer_id))
        }
    }

    /// A "chained" set of procedures on the current stream for the input peer_id.
    /// SuspendStream() followed by ReconfigureStream().
    /// Reconfigure() configures the stream that is currently open.
    ///
    /// # Arguments
    /// * `peer_id`: The peer id associated with the PeerController.
    pub async fn suspend_stream(&self, peer_id: u64) -> Result<(), Error> {
        let tag = "AvdtpFacade::suspend_stream";
        if let Some(p) = self.get_peer_controller_by_id(peer_id) {
            match p.suspend_stream().await? {
                Err(err) => {
                    let err_msg = format_err!("Error: {:?}", err);
                    fx_err_and_bail!(&with_line!(tag), err_msg)
                }
                Ok(()) => Ok(()),
            }
        } else {
            fx_err_and_bail!(&with_line!(tag), format!("Peer id {:?} not found.", peer_id))
        }
    }

    /// Initiate a procedure to get the capabilities for the input peer_id.
    ///
    /// # Arguments
    /// * `peer_id`: The peer id associated with the PeerController.
    pub async fn suspend_and_reconfigure(&self, peer_id: u64) -> Result<(), Error> {
        let tag = "AvdtpFacade::suspend_and_reconfigure";
        if let Some(p) = self.get_peer_controller_by_id(peer_id) {
            match p.suspend_and_reconfigure().await? {
                Err(err) => {
                    let err_msg = format_err!("Error: {:?}", err);
                    fx_err_and_bail!(&with_line!(tag), err_msg)
                }
                Ok(()) => Ok(()),
            }
        } else {
            fx_err_and_bail!(&with_line!(tag), format!("Peer id {:?} not found.", peer_id))
        }
    }

    /// Release the current stream that is owned by the input peer_id.
    /// If the streaming channel doesn't exist, no action will be taken.
    ///
    /// # Arguments
    /// * `peer_id`: The peer id associated with the PeerController.
    pub async fn release_stream(&self, peer_id: u64) -> Result<(), Error> {
        let tag = "AvdtpFacade::release_stream";
        if let Some(p) = self.get_peer_controller_by_id(peer_id) {
            match p.release_stream().await? {
                Err(err) => {
                    let err_msg = format_err!("Error: {:?}", err);
                    fx_err_and_bail!(&with_line!(tag), err_msg)
                }
                Ok(()) => Ok(()),
            }
        } else {
            fx_err_and_bail!(&with_line!(tag), format!("Peer id {:?} not found.", peer_id))
        }
    }

    /// Initiate stream establishment for the input peer_id.
    ///
    /// # Arguments
    /// * `peer_id`: The peer id associated with the PeerController.
    pub async fn establish_stream(&self, peer_id: u64) -> Result<(), Error> {
        let tag = "AvdtpFacade::establish_stream";
        if let Some(p) = self.get_peer_controller_by_id(peer_id) {
            match p.establish_stream().await? {
                Err(err) => {
                    let err_msg = format_err!("Error: {:?}", err);
                    fx_err_and_bail!(&with_line!(tag), err_msg)
                }
                Ok(()) => Ok(()),
            }
        } else {
            fx_err_and_bail!(&with_line!(tag), format!("Peer id {:?} not found.", peer_id))
        }
    }

    /// Start stream for the input peer_id.
    ///
    /// # Arguments
    /// * `peer_id`: The peer id associated with the PeerController.
    pub async fn start_stream(&self, peer_id: u64) -> Result<(), Error> {
        let tag = "AvdtpFacade::start_stream";
        if let Some(p) = self.get_peer_controller_by_id(peer_id) {
            match p.start_stream().await? {
                Err(err) => {
                    let err_msg = format_err!("Error: {:?}", err);
                    fx_err_and_bail!(&with_line!(tag), err_msg)
                }
                Ok(()) => Ok(()),
            }
        } else {
            fx_err_and_bail!(&with_line!(tag), format!("Peer id {:?} not found.", peer_id))
        }
    }

    /// Abort stream for the input peer_id.
    ///
    /// # Arguments
    /// * `peer_id`: The peer id associated with the PeerController.
    pub async fn abort_stream(&self, peer_id: u64) -> Result<(), Error> {
        let tag = "AvdtpFacade::abort_stream";
        if let Some(p) = self.get_peer_controller_by_id(peer_id) {
            match p.abort_stream().await? {
                Err(err) => {
                    let err_msg = format_err!("Error: {:?}", err);
                    fx_err_and_bail!(&with_line!(tag), err_msg)
                }
                Ok(()) => Ok(()),
            }
        } else {
            fx_err_and_bail!(&with_line!(tag), format!("Peer id {:?} not found.", peer_id))
        }
    }

    /// A function to monitor incoming events from the Avdtp Event Stream.
    async fn monitor_avdtp_event_stream(
        avdtp_svc: PeerManagerProxy,
        peer_map: Arc<RwLock<PeerFactoryMap>>,
    ) -> Result<(), Error> {
        let tag = "AvdtpFacade::monitor_avdtp_event_stream";
        let mut stream = avdtp_svc.take_event_stream();

        while let Some(evt) = stream.next().await {
            match evt {
                Ok(e) => match e {
                    PeerManagerEvent::OnPeerConnected { mut peer_id } => {
                        let (client, server) = create_endpoints::<PeerControllerMarker>()
                            .expect("Failed to create peer endpoint");
                        let peer =
                            client.into_proxy().expect("Error: Couldn't obtain peer client proxy");
                        match peer_map.write().entry(peer_id.value.to_string()) {
                            Entry::Occupied(mut entry) => {
                                entry.insert(peer);
                                fx_log_info!("Overriding device in PeerFactoryMap");
                            }
                            Entry::Vacant(entry) => {
                                entry.insert(peer);
                                fx_log_info!("Inserted device into PeerFactoryMap");
                            }
                        };
                        // Establish channel with the given peer_id and server endpoint.
                        let _ = avdtp_svc.get_peer(&mut peer_id, server);
                        fx_log_info!("Getting peer with peer_id: {}", peer_id.value);
                    }
                },
                Err(e) => {
                    let log_err = format_err!("Error during handling request stream: {}", e);
                    fx_err_and_bail!(&with_line!(tag), log_err)
                }
            }
        }
        Ok(())
    }

    /// A function to remove the profile service proxy and clear connected devices.
    fn clear(&self) {
        self.inner.write().peer_map.write().clear();
        self.inner.write().avdtp_service_proxy = None;
    }

    /// A function to remove the profile service proxy and clear connected devices.
    pub async fn remove_service(&self) {
        self.clear()
    }

    /// Cleanup any Profile Server related objects.
    pub async fn cleanup(&self) -> Result<(), Error> {
        self.clear();
        Ok(())
    }
}
