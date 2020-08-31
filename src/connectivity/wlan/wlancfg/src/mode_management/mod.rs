// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        client::state_machine as client_fsm, config_management::SavedNetworksManager,
        util::listener,
    },
    anyhow::Error,
    futures::{channel::mpsc, lock::Mutex, Future},
    std::sync::Arc,
    void::Void,
};

mod iface_manager;
pub mod iface_manager_api;
mod iface_manager_types;
pub mod phy_manager;

pub(crate) fn create_iface_manager(
    phy_manager: Arc<Mutex<dyn phy_manager::PhyManagerApi + Send>>,
    client_update_sender: listener::ClientListenerMessageSender,
    ap_update_sender: listener::ApListenerMessageSender,
    dev_svc_proxy: fidl_fuchsia_wlan_device_service::DeviceServiceProxy,
    saved_networks: Arc<SavedNetworksManager>,
    client_event_sender: mpsc::Sender<client_fsm::ClientStateMachineNotification>,
) -> (iface_manager_api::IfaceManager, impl Future<Output = Result<Void, Error>>) {
    let (sender, receiver) = mpsc::channel(0);
    let iface_manager_sender = iface_manager_api::IfaceManager { sender };
    let iface_manager = iface_manager::IfaceManagerService::new(
        phy_manager,
        client_update_sender,
        ap_update_sender,
        dev_svc_proxy,
        saved_networks,
        client_event_sender,
    );
    let iface_manager_service =
        iface_manager::serve_iface_manager_requests(iface_manager, receiver);

    (iface_manager_sender, iface_manager_service)
}
