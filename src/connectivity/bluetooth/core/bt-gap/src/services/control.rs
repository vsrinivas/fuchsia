// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_control::{self as control, ControlRequest, ControlRequestStream},
    fuchsia_bluetooth::{bt_fidl_status, types::PeerId},
    fuchsia_syslog::fx_log_warn,
    futures::prelude::*,
    std::sync::Arc,
};

use crate::{host_dispatcher::*, types::status_response};

struct ControlSession {
    discovery_token: Option<Arc<DiscoveryRequestToken>>,
    discoverable_token: Option<Arc<DiscoverableRequestToken>>,
}

impl ControlSession {
    fn new() -> ControlSession {
        ControlSession { discovery_token: None, discoverable_token: None }
    }
}

/// Build the ControlImpl to interact with fidl messages
/// State is stored in the HostDispatcher object
pub async fn start_control_service(
    hd: HostDispatcher,
    mut stream: ControlRequestStream,
) -> Result<(), Error> {
    let event_listener = Arc::new(stream.control_handle());
    hd.add_event_listener(Arc::downgrade(&event_listener));
    let mut session = ControlSession::new();

    while let Some(event) = stream.next().await {
        handler(hd.clone(), &mut session, event?).await?;
    }
    // event_listener will now be dropped, closing the listener
    Ok(())
}

async fn handler(
    mut hd: HostDispatcher,
    session: &mut ControlSession,
    event: ControlRequest,
) -> fidl::Result<()> {
    match event {
        ControlRequest::Connect { device_id, responder } => {
            let result = hd.connect(device_id).await;
            responder.send(&mut status_response(result))
        }
        ControlRequest::Pair { id, options, responder } => {
            let result = hd.pair(id, options).await;
            responder.send(&mut status_response(result))
        }
        ControlRequest::SetDiscoverable { discoverable, responder } => {
            let mut resp = if discoverable {
                match hd.set_discoverable().await {
                    Ok(token) => {
                        session.discoverable_token = Some(token);
                        bt_fidl_status!()
                    }
                    Err(err) => err.as_status(),
                }
            } else {
                session.discoverable_token = None;
                bt_fidl_status!()
            };
            responder.send(&mut resp)
        }
        ControlRequest::SetIoCapabilities { input, output, control_handle: _ } => {
            hd.set_io_capability(input, output);
            Ok(())
        }
        ControlRequest::Forget { device_id, responder } => {
            let peer_id = device_id
                .parse::<PeerId>()
                .map_err(|_| err_msg(format!("Invalid peer identifier: {}", device_id)));
            let result = match peer_id {
                Ok(peer_id) => hd.forget(peer_id).await,
                Err(e) => Err(e.into()),
            };
            responder.send(&mut status_response(result))
        }
        ControlRequest::Disconnect { device_id, responder } => {
            let result = hd.disconnect(device_id).await;
            responder.send(&mut status_response(result))
        }
        ControlRequest::GetKnownRemoteDevices { responder } => {
            let mut devices: Vec<_> =
                hd.get_peers().into_iter().map(control::RemoteDevice::from).collect();
            responder.send(&mut devices.iter_mut())
        }
        ControlRequest::IsBluetoothAvailable { responder } => {
            let is_available = hd.get_active_host_info().is_some();
            responder.send(is_available)
        }
        ControlRequest::SetPairingDelegate { delegate, responder } => {
            let status = match delegate.map(|d| d.into_proxy()) {
                Some(Ok(proxy)) => hd.set_pairing_delegate(Some(proxy)),
                Some(Err(err)) => {
                    fx_log_warn!(
                        "Invalid Pairing Delegate passed to SetPairingDelegate - ignoring: {}",
                        err
                    );
                    false
                }
                None => hd.set_pairing_delegate(None),
            };
            responder.send(status)
        }
        ControlRequest::GetAdapters { responder } => {
            let mut adapters: Vec<_> =
                hd.get_adapters().await.into_iter().map(control::AdapterInfo::from).collect();
            responder.send(Some(&mut adapters.iter_mut()))
        }
        ControlRequest::SetActiveAdapter { identifier, responder } => {
            let result = hd.set_active_adapter(identifier.clone());
            responder.send(&mut status_response(result))
        }
        ControlRequest::GetActiveAdapterInfo { responder } => {
            let host_info = hd.get_active_host_info();
            responder.send(host_info.map(control::AdapterInfo::from).as_mut())
        }
        ControlRequest::RequestDiscovery { discovery, responder } => {
            let mut resp = if discovery {
                match hd.start_discovery().await {
                    Ok(token) => {
                        session.discovery_token = Some(token);
                        bt_fidl_status!()
                    }
                    Err(err) => err.as_status(),
                }
            } else {
                session.discovery_token = None;
                bt_fidl_status!()
            };
            responder.send(&mut resp)
        }
        ControlRequest::SetName { name, responder } => {
            let result = hd.set_name(name.unwrap_or("".to_string())).await;
            responder.send(&mut status_response(result))
        }
        ControlRequest::SetDeviceClass { device_class, responder } => {
            let device_class = fidl_fuchsia_bluetooth::DeviceClass { value: device_class.value };
            let result = hd.set_device_class(device_class).await;
            responder.send(&mut status_response(result))
        }
    }
}
