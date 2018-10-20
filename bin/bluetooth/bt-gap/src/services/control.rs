// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::host_dispatcher::*;
use failure::Error;
use fidl::encoding::OutOfLine;
use fidl::endpoints::RequestStream;
use fidl_fuchsia_bluetooth;
use fidl_fuchsia_bluetooth_control::{ControlRequest, ControlRequestStream};
use fuchsia_async as fasync;
use fuchsia_bluetooth::bt_fidl_status;
use futures::prelude::*;
use std::sync::Arc;

struct ControlSession {
    discovery_token: Option<Arc<DiscoveryRequestToken>>,
    discoverable_token: Option<Arc<DiscoverableRequestToken>>
}

impl ControlSession {
    fn new() -> ControlSession {
        ControlSession { discovery_token: None, discoverable_token: None }
    }
}

/// Build the ControlImpl to interact with fidl messages
/// State is stored in the HostDispatcher object
pub async fn start_control_service(mut hd: HostDispatcher, chan: fasync::Channel) -> Result<(), Error> {
    let mut stream = ControlRequestStream::from_channel(chan);
    hd.add_event_listener(stream.control_handle());
    let mut session = ControlSession::new();

    while let Some(event) = await!(stream.next()) {
        await!(handler(hd.clone(), &mut session, event?))?;
    }
    Ok(())
}

async fn handler(
    mut hd: HostDispatcher, session: &mut ControlSession, event: ControlRequest,
) -> fidl::Result<()> {
    match event {
        ControlRequest::Connect { device_id, responder } => {
            let mut status = await!(hd.connect(device_id))?;
            responder.send(&mut status)
        }
        ControlRequest::SetDiscoverable { discoverable, responder } => {
            let (mut resp, token) = if discoverable {
                await!(hd.set_discoverable())?
            } else {
                (bt_fidl_status!(), None)
            };
            session.discoverable_token = token;
            responder.send(&mut resp)
        }
        ControlRequest::SetIoCapabilities { input, output, control_handle: _ } => {
            hd.set_io_capability(input, output);
            Ok(())
        }
        ControlRequest::Forget { device_id, responder } => {
            let mut status = await!(hd.forget(device_id))?;
            responder.send(&mut status)
        }
        ControlRequest::Disconnect { device_id, responder } => {
            // TODO work with classic as well
            let mut status = await!(hd.disconnect(device_id))?;
            responder.send(&mut status)
        }
        ControlRequest::GetKnownRemoteDevices { responder } => {
            let mut devices = hd.get_remote_devices();
            responder.send(&mut devices.iter_mut())
        }
        ControlRequest::IsBluetoothAvailable { responder } => {
            let is_available = hd.get_active_adapter_info().is_some();
            let _ = responder.send(is_available);
            Ok(())
        }
        ControlRequest::SetPairingDelegate { delegate, responder } => {
            let mut status = match delegate.map(|d| d.into_proxy()) {
                Some(Ok(proxy)) => hd.set_pairing_delegate(Some(proxy)),
                Some(Err(_ignored)) => return Ok(()), // TODO - should we return this error?
                None => hd.set_pairing_delegate(None)
            };
            let _ = responder.send(status);
            Ok(())
        }
        ControlRequest::GetAdapters { responder } => {
            let mut resp = await!(hd.get_adapters())?;
            responder.send(Some(&mut resp.iter_mut()))
        }
        ControlRequest::SetActiveAdapter { identifier, responder } => {
            let mut success = hd.set_active_adapter(identifier.clone());
            let _ = responder.send(&mut success);
            Ok(())
        }
        ControlRequest::GetActiveAdapterInfo { responder } => {
            let mut adap = hd.get_active_adapter_info();
            let _ = responder.send(adap.as_mut().map(OutOfLine));
            Ok(())
        }
        ControlRequest::RequestDiscovery { discovery, responder } => {
            let (mut resp, token) = if discovery {
                await!(hd.start_discovery())?
            } else {
                (bt_fidl_status!(), None)
            };
            session.discovery_token = token;
            responder.send(&mut resp)
        }
        ControlRequest::SetName { name, responder } => {
            let mut resp = await!(hd.set_name(name))?;
            responder.send(&mut resp)
        }
    }
}
