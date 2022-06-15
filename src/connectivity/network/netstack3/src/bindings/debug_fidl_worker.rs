// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A Netstack3 worker to serve fuchsia.net.debug.Interfaces API requests.

use fidl::endpoints::{ProtocolMarker as _, ServerEnd};
use fidl_fuchsia_net_debug as fnet_debug;
use fidl_fuchsia_net_interfaces_admin as fnet_interfaces_admin;
use fuchsia_zircon as zx;
use futures::TryStreamExt as _;
use tracing::{debug, error, warn};

use crate::bindings::{devices::LOOPBACK_MAC, util::IntoFidl, DeviceSpecificInfo, Netstack};

// Serve a stream of fuchsia.net.debug.Interfaces API requests for a single
// channel (e.g. a single client connection).
pub(crate) async fn serve(
    ns: Netstack,
    rs: fnet_debug::InterfacesRequestStream,
) -> Result<(), fidl::Error> {
    debug!(protocol = fnet_debug::InterfacesMarker::DEBUG_NAME, "serving");
    rs.try_for_each(|req| async {
        match req {
            fnet_debug::InterfacesRequest::GetAdmin { id, control, control_handle: _ } => {
                handle_get_admin(id, control).await;
            }
            fnet_debug::InterfacesRequest::GetMac { id, responder } => {
                responder_send!(responder, &mut handle_get_mac(ns.clone(), id).await);
            }
        }
        Ok(())
    })
    .await
}

async fn handle_get_admin(
    interface_id: u64,
    control: ServerEnd<fnet_interfaces_admin::ControlMarker>,
) {
    debug!(interface_id, "handling fuchsia.net.debug.Interfaces::GetAdmin");
    control.close_with_epitaph(zx::Status::NOT_SUPPORTED).unwrap_or_else(|e| {
        if e.is_closed() {
            debug!(err = ?e, "control handle closed before sending epitaph")
        } else {
            error!(err = ?e, "failed to send epitaph")
        }
    });
    warn!("TODO(https://fxbug.dev/88797): fuchsia.net.debug/Interfaces not implemented");
}

async fn handle_get_mac(ns: Netstack, interface_id: u64) -> fnet_debug::InterfacesGetMacResult {
    debug!(interface_id, "handling fuchsia.net.debug.Interfaces::GetMac");
    let ctx = ns.ctx.lock().await;
    ctx.non_sync_ctx
        .devices
        .get_device(interface_id)
        .ok_or(fnet_debug::InterfacesGetMacError::NotFound)
        .map(|device_info| {
            let mac = match device_info.info() {
                DeviceSpecificInfo::Loopback(_) => LOOPBACK_MAC,
                DeviceSpecificInfo::Ethernet(info) => info.mac.into(),
                DeviceSpecificInfo::Netdevice(info) => info.mac.into(),
            };
            Some(Box::new(mac.into_fidl()))
        })
}
