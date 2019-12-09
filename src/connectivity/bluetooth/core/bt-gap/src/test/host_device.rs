// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{err_msg, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_host::{HostControlHandle, HostMarker, HostRequest, HostRequestStream},
    fidl_fuchsia_bluetooth_sys::{HostInfo as FidlHostInfo, TechnologyType},
    fuchsia_bluetooth::{
        bt_fidl_status,
        inspect::{placeholder_node, Inspectable},
        types::{Address, BondingData, HostInfo, Peer},
    },
    futures::{future, join, stream::StreamExt},
    parking_lot::RwLock,
    std::path::PathBuf,
    std::sync::Arc,
};

use crate::{
    host_device::{refresh_host_info, HostDevice, HostListener},
    test::create_fidl_endpoints,
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

    let info = HostInfo {
        id: fidl_fuchsia_bluetooth::Id { value: 1 },
        technology: TechnologyType::DualMode,
        address: Address::Public([0, 0, 0, 0, 0, 0]),
        local_name: None,
        active: false,
        discoverable: false,
        discovering: false,
    };
    let host = Arc::new(RwLock::new(HostDevice::new(
        PathBuf::from("/dev/class/bt-host/test"),
        client,
        Inspectable::new(info.clone(), placeholder_node()),
    )));
    let name = "EXPECTED_NAME".to_string();

    let info = Arc::new(RwLock::new(info));
    let server = Arc::new(RwLock::new(server));

    // Assign a name and verify that that it gets written to the bt-host over FIDL.
    let set_name = host.write().set_name(name.clone());
    let expect_fidl = expect_call(server.clone(), |_, e| match e {
        HostRequest::SetLocalName { local_name, responder } => {
            info.write().local_name = Some(local_name);
            responder.send(&mut bt_fidl_status!())?;
            Ok(())
        }
        _ => Err(err_msg("Unexpected!")),
    });
    let (set_name_result, expect_result) = join!(set_name, expect_fidl);
    let _ = set_name_result.expect("failed to set name");
    let _ = expect_result.expect("FIDL result unsatisfied");

    let refresh = refresh_host_info(host.clone());
    let expect_fidl = expect_call(server.clone(), |_, e| match e {
        HostRequest::WatchState { responder } => {
            responder.send(FidlHostInfo::from(info.read().clone()))?;
            Ok(())
        }
        _ => Err(err_msg("Unexpected!")),
    });
    let (refresh_result, expect_result) = join!(refresh, expect_fidl);
    let _ = refresh_result.expect("did not receive HostInfo update");
    let _ = expect_result.expect("FIDL result unsatisfied");

    let host_name = host.read().get_info().local_name.clone();
    println!("name: {:?}", host_name);
    assert!(host_name == Some(name));
    Ok(())
}

// TODO(39373): Add host.fidl emulation to bt-fidl-mocks and use that instead.
async fn expect_call<F>(stream: Arc<RwLock<HostRequestStream>>, f: F) -> Result<(), Error>
where
    F: FnOnce(Arc<HostControlHandle>, HostRequest) -> Result<(), Error>,
{
    let control_handle = Arc::new(stream.read().control_handle());
    let mut stream = stream.write();
    if let Some(event) = stream.next().await {
        let event = event?;
        f(control_handle, event)
    } else {
        Err(err_msg("No event received"))
    }
}
