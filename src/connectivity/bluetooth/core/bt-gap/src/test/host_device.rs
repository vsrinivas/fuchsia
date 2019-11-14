// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, format_err, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_control::{AdapterState, TechnologyType},
    fidl_fuchsia_bluetooth_host::{HostControlHandle, HostMarker, HostRequest, HostRequestStream},
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_bluetooth::{
        bt_fidl_status,
        inspect::{placeholder_node, Inspectable},
        types::{AdapterInfo, Address, BondingData, Peer},
    },
    fuchsia_zircon::DurationNum,
    futures::{
        FutureExt,
        future::{self, join3},
        stream::StreamExt,
    },
    parking_lot::RwLock,
    std::path::PathBuf,
    std::sync::Arc,
};

use crate::{
    host_device::{handle_events, HostDevice, HostListener},
    test::create_fidl_endpoints,
    types,
};

// An impl that ignores all events
impl HostListener for () {
    fn on_peer_updated(&mut self, _peer: Peer) {}
    fn on_peer_removed(&mut self, _identifier: String) {}
    type HostBondFut = future::Ready<Result<(), failure::Error>>;
    fn on_new_host_bond(&mut self, _data: BondingData) -> Self::HostBondFut {
        future::ok(())
    }
}

// Create a HostDevice with a fake channel, set local name and check it is updated
#[fuchsia_async::run_singlethreaded(test)]
async fn host_device_set_local_name() -> Result<(), Error> {
    let (client, server) = create_fidl_endpoints::<HostMarker>()?;

    let info = AdapterInfo::new(
        "foo".to_string(),
        TechnologyType::DualMode,
        Address::Public([0, 0, 0, 0, 0, 0]),
        None,
    );
    let info = Inspectable::new(info, placeholder_node());
    let host = Arc::new(RwLock::new(HostDevice::new(
        PathBuf::from("/dev/class/bt-host/test"),
        client,
        info,
    )));
    let name = "EXPECTED_NAME".to_string();

    let run_test = join3(
        // Call set_name
        host.write().set_name(name.clone()),
        // Respond with the new name
        expect_call(server, |listener, e| match e {
            HostRequest::SetLocalName { local_name, responder } => {
                let mut state = AdapterState {
                    local_name: Some(local_name),
                    discovering: None,
                    discoverable: None,
                    local_service_uuids: None,
                };
                listener.send_on_adapter_state_changed(&mut state)?;
                responder.send(&mut bt_fidl_status!())?;
                Ok(())
            }
            _ => Err(err_msg("Unexpected!")),
        }),
        // Receive the name update
        handle_events((), host.clone()),
    );

    let timeout = 5.seconds();
    run_test
        .map(|r| {
            r.0.map_err(types::Error::as_failure)
                .and(r.1)
                .and(r.2.map_err(types::Error::as_failure))
        })
        .on_timeout(timeout.after_now(), move || Err(format_err!("Timed out")))
        .await?;

    let host_name =
        host.read().get_info().state.as_ref().and_then(|s| s.local_name.as_ref().cloned());
    assert!(host_name == Some(name));
    Ok(())
}

async fn expect_call<F>(mut stream: HostRequestStream, f: F) -> Result<(), Error>
where
    F: FnOnce(Arc<HostControlHandle>, HostRequest) -> Result<(), Error>,
{
    let control_handle = Arc::new(stream.control_handle());
    if let Some(event) = stream.next().await {
        let event = event?;
        f(control_handle, event)
    } else {
        Err(err_msg("No event received"))
    }
}
