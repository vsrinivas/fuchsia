// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl::{encoding::OutOfLine, endpoints::RequestStream},
    fidl_fuchsia_bluetooth_control::{ControlRequest, ControlRequestStream},
    fuchsia_bluetooth::bt_fidl_status,
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

    while let Some(event) = await!(stream.next()) {
        await!(handler(hd.clone(), &mut session, event?))?;
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
            let result = await!(hd.connect(device_id));
            responder.send(&mut status_response(result))
        }
        ControlRequest::SetDiscoverable { discoverable, responder } => {
            let mut resp = if discoverable {
                match await!(hd.set_discoverable()) {
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
            let result = await!(hd.forget(device_id));
            responder.send(&mut status_response(result))
        }
        ControlRequest::Disconnect { device_id, responder } => {
            // TODO work with classic as well
            let result = await!(hd.disconnect(device_id));
            responder.send(&mut status_response(result))
        }
        ControlRequest::GetKnownRemoteDevices { responder } => {
            let mut devices = hd.get_remote_devices();
            responder.send(&mut devices.iter_mut())
        }
        ControlRequest::IsBluetoothAvailable { responder } => {
            let is_available = hd.get_active_adapter_info().is_some();
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
            let mut adapters = await!(hd.get_adapters());
            responder.send(Some(&mut adapters.iter_mut()))
        }
        ControlRequest::SetActiveAdapter { identifier, responder } => {
            let result = hd.set_active_adapter(identifier.clone());
            responder.send(&mut status_response(result))
        }
        ControlRequest::GetActiveAdapterInfo { responder } => {
            let mut adap = hd.get_active_adapter_info();
            responder.send(adap.as_mut().map(OutOfLine))
        }
        ControlRequest::RequestDiscovery { discovery, responder } => {
            let mut resp = if discovery {
                match await!(hd.start_discovery()) {
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
            let result = await!(hd.set_name(name));
            responder.send(&mut status_response(result))
        }
        ControlRequest::SetDeviceClass { device_class, responder } => {
            let result = await!(hd.set_device_class(device_class));
            responder.send(&mut status_response(result))
        }
    }
}
