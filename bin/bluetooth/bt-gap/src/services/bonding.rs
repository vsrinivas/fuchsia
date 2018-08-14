// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::host_dispatcher::HostDispatcher;
use fidl;
use fidl::endpoints2::RequestStream;
use fidl_fuchsia_bluetooth;
use fidl_fuchsia_bluetooth_control::{BondingRequest, BondingRequestStream};
use fuchsia_async;
use fuchsia_bluetooth::bt_fidl_status;
use fuchsia_syslog::{fx_log, fx_log_info};
use futures::{Future, TryFutureExt, TryStreamExt};
use parking_lot::RwLock;
use std::sync::Arc;

pub fn start_bonding_service(
    hd: Arc<RwLock<HostDispatcher>>, channel: fuchsia_async::Channel,
) -> impl Future<Output = Result<(), fidl::Error>> {
    let stream = BondingRequestStream::from_channel(channel);
    let hd = hd.clone();
    hd.write().bonding_events = Some(stream.control_handle());
    stream.try_for_each(move |evt| {
        let BondingRequest::AddBondedDevices {
            local_id,
            bonds,
            responder,
        } = evt;
        fx_log_info!("Add Bonded devices for {:?}", local_id);
        HostDispatcher::get_active_adapter(hd.clone()).map_ok(move |host_device| {
            if let Some(ref host_device) = host_device {
                host_device.read().restore_bonds(bonds);
            }
            responder.send(&mut bt_fidl_status!()).unwrap()
        })
    })
}
