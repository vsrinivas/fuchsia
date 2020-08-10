// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_control::{self as fctrl, ControlRequest, ControlRequestStream},
    fidl_fuchsia_bluetooth_sys as fsys,
    fuchsia_bluetooth::{
        bt_fidl_status,
        types::{HostId, PeerId},
    },
    fuchsia_syslog::{fx_log_info, fx_log_warn},
    futures::prelude::*,
    std::sync::Arc,
};

use crate::{
    host_dispatcher::*,
    types::{self, status_response},
};

struct ControlSession {
    discovery_token: Option<Arc<DiscoverySession>>,
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

fn parse_peer_id(id: &str) -> Result<PeerId, types::Error> {
    id.parse::<PeerId>()
        .map_err(|_| types::Error::InternalError(format_err!("invalid peer ID: {}", id)))
}

async fn handle_connect(hd: HostDispatcher, device_id: &str) -> types::Result<()> {
    hd.connect(parse_peer_id(device_id)?).await.map_err(types::Error::from)
}

async fn handle_forget(hd: HostDispatcher, device_id: &str) -> types::Result<()> {
    hd.forget(parse_peer_id(device_id)?).await.map_err(types::Error::from)
}

async fn handle_disconnect(hd: HostDispatcher, device_id: &str) -> types::Result<()> {
    hd.disconnect(parse_peer_id(device_id)?).await.map_err(types::Error::from)
}

fn input_cap_to_sys(ioc: fctrl::InputCapabilityType) -> fsys::InputCapability {
    match ioc {
        fctrl::InputCapabilityType::None => fsys::InputCapability::None,
        fctrl::InputCapabilityType::Confirmation => fsys::InputCapability::Confirmation,
        fctrl::InputCapabilityType::Keyboard => fsys::InputCapability::Keyboard,
    }
}

fn output_cap_to_sys(ioc: fctrl::OutputCapabilityType) -> fsys::OutputCapability {
    match ioc {
        fctrl::OutputCapabilityType::None => fsys::OutputCapability::None,
        fctrl::OutputCapabilityType::Display => fsys::OutputCapability::Display,
    }
}

async fn handler(
    hd: HostDispatcher,
    session: &mut ControlSession,
    event: ControlRequest,
) -> fidl::Result<()> {
    match event {
        ControlRequest::Connect { device_id, responder } => {
            let result = handle_connect(hd, &device_id).await;
            responder.send(&mut status_response(result))
        }
        ControlRequest::Pair { id, options, responder } => {
            let result = hd.pair(id.into(), options.into()).await;
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
            hd.set_io_capability(input_cap_to_sys(input), output_cap_to_sys(output));
            Ok(())
        }
        ControlRequest::Forget { device_id, responder } => {
            let result = handle_forget(hd, &device_id).await;
            responder.send(&mut status_response(result))
        }
        ControlRequest::Disconnect { device_id, responder } => {
            let result = handle_disconnect(hd, &device_id).await;
            responder.send(&mut status_response(result))
        }
        ControlRequest::GetKnownRemoteDevices { responder } => {
            let mut devices: Vec<_> =
                hd.get_peers().into_iter().map(fctrl::RemoteDevice::from).collect();
            responder.send(&mut devices.iter_mut())
        }
        ControlRequest::IsBluetoothAvailable { responder } => {
            let is_available = hd.get_active_host_info().is_some();
            responder.send(is_available)
        }
        ControlRequest::SetPairingDelegate { delegate, responder } => {
            let status = match delegate.map(|d| d.into_proxy()) {
                Some(Ok(proxy)) => hd.set_control_pairing_delegate(Some(proxy)),
                Some(Err(err)) => {
                    fx_log_warn!(
                        "Invalid Pairing Delegate passed to SetPairingDelegate - ignoring: {}",
                        err
                    );
                    false
                }
                None => hd.set_control_pairing_delegate(None),
            };
            responder.send(status)
        }
        ControlRequest::GetAdapters { responder } => {
            let mut adapters: Vec<_> =
                hd.get_adapters().await.into_iter().map(fctrl::AdapterInfo::from).collect();
            responder.send(Some(&mut adapters.iter_mut()))
        }
        ControlRequest::SetActiveAdapter { identifier, responder } => {
            match identifier.parse::<HostId>() {
                Ok(id) => {
                    let result = hd.set_active_host(id);
                    responder.send(&mut status_response(result))
                }
                Err(err) => {
                    fx_log_info!("fuchsia.bluetooth.control.Control client tried to set invalid active adapter: {}", identifier);
                    responder.send(&mut bt_fidl_status!(Failed, format!("{}", err)))
                }
            }
        }
        ControlRequest::GetActiveAdapterInfo { responder } => {
            let host_info = hd.get_active_host_info();
            responder.send(host_info.map(fctrl::AdapterInfo::from).as_mut())
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
