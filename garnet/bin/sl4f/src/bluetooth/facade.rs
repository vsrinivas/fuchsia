// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context as _, Error};
use fidl;
use fidl_fuchsia_bluetooth_gatt::ServiceInfo;
use fidl_fuchsia_bluetooth_gatt::{
    LocalServiceDelegateMarker, LocalServiceMarker, LocalServiceProxy, Server_Marker, Server_Proxy,
};
use fuchsia_async::{
    self as fasync,
    temp::Either::{Left, Right},
};
use fuchsia_component as app;
use fuchsia_syslog::macros::*;
use fuchsia_zircon as zx;
use futures::future::{ready as fready, Future, TryFutureExt};
use parking_lot::RwLock;
use std::collections::HashMap;

use crate::common_utils::error::Sl4fError;

// BluetoothFacade: Stores Central and Peripheral proxies used for
// bluetooth scan and advertising requests.
//
// This object is shared among all threads created by server.
//
// Future plans: Object will store other common information like RemoteDevices
// found via scan, allowing for ease of state transfer between similar/related
// requests.
//
// Use: Create once per server instantiation. Calls to set_peripheral_proxy()
// and set_central_proxy() will update Facade object with proxy if no such proxy
// currently exists. If such a proxy exists, then update() will use pre-existing
// proxy.
#[derive(Debug)]
pub struct BluetoothFacade {
    // GATT related state
    // server_proxy: The proxy for Gatt server
    server_proxy: Option<Server_Proxy>,

    // service_proxies: HashMap of key = String (randomly generated local_service_id) and val:
    // LocalServiceProxy
    service_proxies: HashMap<String, (LocalServiceProxy, fasync::Channel)>,
}

impl BluetoothFacade {
    pub fn new() -> RwLock<BluetoothFacade> {
        RwLock::new(BluetoothFacade { server_proxy: None, service_proxies: HashMap::new() })
    }

    pub fn set_server_proxy(bt_facade: &RwLock<BluetoothFacade>) {
        let new_server = match bt_facade.read().server_proxy.clone() {
            Some(s) => {
                fx_log_info!(tag: "set_server_proxy", "Current service proxy: {:?}", s);
                Some(s)
            }
            None => {
                fx_log_info!(tag: "set_server_proxy", "Setting new server proxy");
                Some(
                    app::client::connect_to_service::<Server_Marker>()
                        .context("Failed to connect to service.")
                        .unwrap(),
                )
            }
        };

        bt_facade.write().server_proxy = new_server;
    }

    pub fn get_server_proxy(&self) -> &Option<Server_Proxy> {
        &self.server_proxy
    }

    pub fn get_service_proxies(&self) -> &HashMap<String, (LocalServiceProxy, fasync::Channel)> {
        &self.service_proxies
    }

    pub fn cleanup_server_proxy(bt_facade: &RwLock<BluetoothFacade>) {
        bt_facade.write().server_proxy = None
    }

    pub fn cleanup_service_proxies(bt_facade: &RwLock<BluetoothFacade>) {
        bt_facade.write().service_proxies.clear()
    }

    pub fn cleanup_gatt(bt_facade: &RwLock<BluetoothFacade>) {
        BluetoothFacade::cleanup_server_proxy(bt_facade);
        BluetoothFacade::cleanup_service_proxies(bt_facade);
    }

    // Close both central and peripheral proxies
    pub fn cleanup(bt_facade: &RwLock<BluetoothFacade>) {
        BluetoothFacade::cleanup_gatt(bt_facade);
    }

    pub fn print(&self) {
        fx_log_info!(tag: "print",
            "BluetoothFacade: Server Proxy: {:?}, Services: {:?}",
            self.get_server_proxy(),
            self.get_service_proxies(),
        );
    }

    pub fn publish_service(
        bt_facade: &RwLock<BluetoothFacade>,
        mut service_info: ServiceInfo,
        local_service_id: String,
    ) -> impl Future<Output = Result<(), Error>> {
        // Set the local peripheral proxy if necessary
        BluetoothFacade::set_server_proxy(&bt_facade);

        // If the unique service_proxy id already exists, reject publishing of service
        if bt_facade.read().service_proxies.contains_key(&local_service_id) {
            fx_log_err!(tag: "publish_service", "Attempted to create service proxy for existing key. {:?}", local_service_id.clone());
            return Left(fready(Err(Sl4fError::new("Proxy key already exists, aborting.").into())));
        }

        // TODO(fxbug.dev/811): Ensure unwrap() safety
        let (service_local, service_remote) = zx::Channel::create().unwrap();
        let service_local = fasync::Channel::from_channel(service_local).unwrap();
        let service_server = fidl::endpoints::ServerEnd::<LocalServiceMarker>::new(service_remote);

        // Otherwise, store the local proxy in map with unique local_service_id string
        let service_proxy = LocalServiceProxy::new(service_local);

        let (delegate_local, delegate_remote) = zx::Channel::create().unwrap();
        let delegate_local = fasync::Channel::from_channel(delegate_local).unwrap();
        let delegate_ptr =
            fidl::endpoints::ClientEnd::<LocalServiceDelegateMarker>::new(delegate_remote);

        bt_facade.write().service_proxies.insert(local_service_id, (service_proxy, delegate_local));

        match &bt_facade.read().server_proxy {
            Some(server) => {
                let pub_fut = server
                    .publish_service(&mut service_info, delegate_ptr, service_server)
                    .map_err(|e| Error::from(e).context("Publishing service error"))
                    .and_then(|status| match status.error {
                        None => fready(Ok(())),
                        Some(e) => fready(Err(Sl4fError::from(*e).into())),
                    });

                Right(pub_fut)
            }
            None => Left(fready(Err(Sl4fError::new("No central proxy created.").into()))),
        }
    }
}
