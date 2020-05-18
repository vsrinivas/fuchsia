// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client,
        config_management::SavedNetworksManager,
        legacy::{client as legacy_client, shim},
        mode_management::phy_manager::PhyManagerApi,
    },
    anyhow::format_err,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_wlan_device as wlan,
    fidl_fuchsia_wlan_device_service::{DeviceServiceProxy, DeviceWatcherEvent},
    fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::lock::Mutex,
    log::info,
    std::sync::Arc,
};

pub(crate) struct Listener {
    proxy: DeviceServiceProxy,
    legacy_client: shim::ClientRef,
    phy_manager: Arc<Mutex<dyn PhyManagerApi + Send>>,
    client: client::ClientPtr,
}

pub(crate) async fn handle_event(
    listener: &Listener,
    evt: DeviceWatcherEvent,
    saved_networks: Arc<SavedNetworksManager>,
) {
    info!("got event: {:?}", evt);
    match evt {
        DeviceWatcherEvent::OnPhyAdded { phy_id } => {
            on_phy_added(listener, phy_id).await;
        }
        DeviceWatcherEvent::OnPhyRemoved { phy_id } => {
            info!("phy removed: {}", phy_id);
            let mut phy_manager = listener.phy_manager.lock().await;
            phy_manager.remove_phy(phy_id);
        }
        DeviceWatcherEvent::OnIfaceAdded { iface_id } => {
            let mut phy_manager = listener.phy_manager.lock().await;
            match phy_manager.on_iface_added(iface_id).await {
                Ok(()) => match on_iface_added(listener, iface_id, saved_networks).await {
                    Ok(()) => {}
                    Err(e) => info!("error adding new iface {}: {}", iface_id, e),
                },
                Err(e) => info!("error adding new iface {}: {}", iface_id, e),
            }
        }
        DeviceWatcherEvent::OnIfaceRemoved { iface_id } => {
            let mut phy_manager = listener.phy_manager.lock().await;
            phy_manager.on_iface_removed(iface_id);

            listener.legacy_client.remove_if_matching(iface_id);
            info!("iface removed: {}", iface_id);
        }
    }
}

async fn on_phy_added(listener: &Listener, phy_id: u16) {
    info!("phy {} added", phy_id);
    let mut phy_manager = listener.phy_manager.lock().await;
    if let Err(e) = phy_manager.add_phy(phy_id).await {
        info!("error adding new phy {}: {}", phy_id, e);
    }

    if let Err(e) = phy_manager.create_all_client_ifaces().await {
        info!("error starting client interfaces: {:?}", e);
        return;
    }

    // When a new PHY is detected, attempt to get a client.  It is possible that the PHY does not
    // support client or AP mode, so failing to get an interface for either role should not result
    // in failure.
    match phy_manager.get_client() {
        Some(iface_id) => info!("created client iface {}", iface_id),
        None => {}
    }
}

async fn on_iface_added(
    listener: &Listener,
    iface_id: u16,
    saved_networks: Arc<SavedNetworksManager>,
) -> Result<(), anyhow::Error> {
    let (status, response) = listener.proxy.query_iface(iface_id).await?;
    match status {
        fuchsia_zircon::sys::ZX_OK => {}
        fuchsia_zircon::sys::ZX_ERR_NOT_FOUND => {
            return Err(format_err!("Could not find iface: {}", iface_id));
        }
        ref status => return Err(format_err!("Could not query iface information: {}", status)),
    }

    let response =
        response.ok_or_else(|| format_err!("Iface information is missing: {}", iface_id))?;

    let service = listener.proxy.clone();

    match response.role {
        wlan::MacRole::Client => {
            let legacy_client = listener.legacy_client.clone();
            let client = listener.client.clone();
            let (sme, remote) = create_proxy()
                .map_err(|e| format_err!("Failed to create a FIDL channel: {}", e))?;

            let status = service
                .get_client_sme(iface_id, remote)
                .await
                .map_err(|e| format_err!("Failed to get client SME: {}", e))?;

            zx::Status::ok(status)
                .map_err(|e| format_err!("GetClientSme returned an error: {}", e))?;

            let (c, fut) = legacy_client::new_client(iface_id, sme.clone(), saved_networks);
            fasync::spawn(fut);
            let lc = shim::Client { service, client: c, sme: sme.clone(), iface_id };
            legacy_client.set_if_empty(lc);
            client.lock().await.set_sme(sme);
        }
        // The AP service make direct use of the PhyManager to get interfaces.
        wlan::MacRole::Ap => {}
        wlan::MacRole::Mesh => {
            return Err(format_err!("Unexpectedly observed a mesh iface: {}", iface_id))
        }
    }
    info!("new iface {} added successfully", iface_id);
    Ok(())
}

impl Listener {
    pub fn new(
        proxy: DeviceServiceProxy,
        legacy_client: shim::ClientRef,
        phy_manager: Arc<Mutex<dyn PhyManagerApi + Send>>,
        client: client::ClientPtr,
    ) -> Self {
        Listener { proxy, legacy_client, phy_manager, client }
    }
}
