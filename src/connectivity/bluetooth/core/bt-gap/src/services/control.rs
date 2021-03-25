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
    futures::prelude::*,
    log::{info, warn},
    std::sync::Arc,
};

use crate::{
    host_device::HostDiscoverableSession,
    host_dispatcher::*,
    services::pairing::PairingDelegate,
    types::{self, status_response},
};

#[derive(Default)]
struct ControlSession {
    /// Token held when the control client has requested discovery.
    /// Disables discovery request when dropped.
    _discovery_token: Option<Arc<DiscoverySession>>,
    /// Token held when the control client has requested discoverability.
    /// Disables discoverability when dropped.
    _discoverable_token: Option<Arc<HostDiscoverableSession>>,
    delegate: Option<PairingDelegate>,
    // Cached pairing options set via this session. Since Control allows setting IO capabilities
    // separately to pairing delegate, we cache all three locally and then update the host
    // dispatchers pairing dispatcher whenever we have all three.
    input: Option<fsys::InputCapability>,
    output: Option<fsys::OutputCapability>,
}

impl ControlSession {
    // Set the pairing delegate if we have all the required settings
    // Returns true if the pairing delegate was successfully set
    fn maybe_set_pairing_delegate(&self, hd: HostDispatcher) -> bool {
        if let (Some(delegate), Some(input), Some(output)) =
            (&self.delegate, self.input, self.output)
        {
            hd.set_pairing_delegate(delegate.clone(), input, output)
        } else {
            false
        }
    }
}

/// Build the ControlImpl to interact with fidl messages
/// State is stored in the HostDispatcher object
pub async fn run(hd: HostDispatcher, mut stream: ControlRequestStream) -> Result<(), Error> {
    let event_listener = Arc::new(stream.control_handle());
    hd.add_event_listener(Arc::downgrade(&event_listener));
    let mut session = ControlSession::default();

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

pub(crate) fn input_cap_to_sys(ioc: fctrl::InputCapabilityType) -> fsys::InputCapability {
    match ioc {
        fctrl::InputCapabilityType::None => fsys::InputCapability::None,
        fctrl::InputCapabilityType::Confirmation => fsys::InputCapability::Confirmation,
        fctrl::InputCapabilityType::Keyboard => fsys::InputCapability::Keyboard,
    }
}

pub(crate) fn output_cap_to_sys(ioc: fctrl::OutputCapabilityType) -> fsys::OutputCapability {
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
                        session._discoverable_token = Some(token);
                        bt_fidl_status!()
                    }
                    Err(err) => err.as_status(),
                }
            } else {
                session._discoverable_token = None;
                bt_fidl_status!()
            };
            responder.send(&mut resp)
        }
        ControlRequest::SetIoCapabilities { input, output, control_handle: _ } => {
            session.input = Some(input_cap_to_sys(input));
            session.output = Some(output_cap_to_sys(output));
            let _ignore_result = session.maybe_set_pairing_delegate(hd);
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
            let is_available = hd.active_host().await.is_some();
            responder.send(is_available)
        }
        ControlRequest::SetPairingDelegate { delegate, responder } => {
            match delegate.map(|d| d.into_proxy()).transpose() {
                Err(err) => {
                    warn!(
                        "Invalid Pairing Delegate passed to SetPairingDelegate - ignoring: {}",
                        err
                    );
                    responder.send(false)
                }
                Ok(None) => {
                    hd.clear_pairing_delegate();
                    session.delegate = None;
                    responder.send(true)
                }
                Ok(Some(delegate)) => {
                    session.delegate = Some(PairingDelegate::Control(delegate));
                    let success = session.maybe_set_pairing_delegate(hd);
                    responder.send(success)
                }
            }
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
                    info!("fuchsia.bluetooth.control.Control client tried to set invalid active adapter: {}", identifier);
                    responder.send(&mut bt_fidl_status!(Failed, format!("{}", err)))
                }
            }
        }
        ControlRequest::GetActiveAdapterInfo { responder } => {
            let host = hd.active_host().await;
            responder.send(host.map(|host| fctrl::AdapterInfo::from(host.info())).as_mut())
        }
        ControlRequest::RequestDiscovery { discovery, responder } => {
            let mut resp = if discovery {
                match hd.start_discovery().await {
                    Ok(token) => {
                        session._discovery_token = Some(token);
                        bt_fidl_status!()
                    }
                    Err(err) => err.as_status(),
                }
            } else {
                session._discovery_token = None;
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
