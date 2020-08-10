// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_host::{HostControlHandle, HostMarker, HostRequest, HostRequestStream},
    fidl_fuchsia_bluetooth_sys::{AccessMarker, HostInfo as FidlHostInfo, TechnologyType},
    fuchsia_bluetooth::{
        inspect::{placeholder_node, Inspectable},
        types::{Address, BondingData, HostId, HostInfo, Peer, PeerId},
    },
    futures::{
        future, join,
        stream::{StreamExt, TryStreamExt},
    },
    matches::assert_matches,
    parking_lot::RwLock,
    std::{path::PathBuf, sync::Arc},
};

use crate::{host_device, host_dispatcher, services::access};

// An impl that ignores all events
impl host_device::HostListener for () {
    type PeerUpdatedFut = future::Ready<()>;
    fn on_peer_updated(&mut self, _peer: Peer) -> Self::PeerRemovedFut {
        future::ready(())
    }

    type PeerRemovedFut = future::Ready<()>;
    fn on_peer_removed(&mut self, _id: PeerId) -> Self::PeerRemovedFut {
        future::ready(())
    }

    type HostBondFut = future::Ready<Result<(), anyhow::Error>>;
    fn on_new_host_bond(&mut self, _data: BondingData) -> Self::HostBondFut {
        future::ok(())
    }

    type HostInfoFut = future::Ready<Result<(), anyhow::Error>>;
    fn on_host_updated(&mut self, _info: HostInfo) -> Self::HostInfoFut {
        future::ok(())
    }
}

// Create a HostDevice with a fake channel, set local name and check it is updated
#[fuchsia_async::run_singlethreaded(test)]
async fn host_device_set_local_name() -> Result<(), Error> {
    let (client, server) = fidl::endpoints::create_proxy_and_stream::<HostMarker>()?;

    let info = HostInfo {
        id: HostId(1),
        technology: TechnologyType::DualMode,
        address: Address::Public([0, 0, 0, 0, 0, 0]),
        local_name: None,
        active: false,
        discoverable: false,
        discovering: false,
    };
    let host = Arc::new(RwLock::new(host_device::HostDevice::new(
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
            responder.send(&mut Ok(()))?;
            Ok(())
        }
        _ => Err(format_err!("Unexpected!")),
    });
    let (set_name_result, expect_result) = join!(set_name, expect_fidl);
    let _ = set_name_result.expect("failed to set name");
    let _ = expect_result.expect("FIDL result unsatisfied");

    refresh_host(host.clone(), server.clone(), info.read().clone()).await;
    let host_name = host.read().get_info().local_name.clone();
    println!("name: {:?}", host_name);
    assert!(host_name == Some(name));
    Ok(())
}

// Test that we can establish a host discovery session, then stop discovery on the host when
// the session token is dropped
#[fuchsia_async::run_singlethreaded(test)]
async fn test_discovery_session() -> Result<(), Error> {
    let (client, server) = fidl::endpoints::create_proxy_and_stream::<HostMarker>()?;

    let info = HostInfo {
        id: HostId(1),
        technology: TechnologyType::DualMode,
        address: Address::Public([0, 0, 0, 0, 0, 0]),
        local_name: None,
        active: false,
        discoverable: false,
        discovering: false,
    };

    let host = Arc::new(RwLock::new(host_device::HostDevice::new(
        PathBuf::from("/dev/class/bt-host/test"),
        client,
        Inspectable::new(info.clone(), placeholder_node()),
    )));

    let info = Arc::new(RwLock::new(info));
    let server = Arc::new(RwLock::new(server));

    // Simulate request to establish discovery session
    let establish_discovery_session = host_device::HostDevice::establish_discovery_session(&host);
    let expect_fidl = expect_call(server.clone(), |_, e| match e {
        HostRequest::StartDiscovery { responder } => {
            info.write().discovering = true;
            responder.send(&mut Ok(()))?;
            Ok(())
        }
        _ => Err(format_err!("Unexpected!")),
    });

    let (discovery_result, expect_result) = join!(establish_discovery_session, expect_fidl);
    let session = discovery_result.expect("did not receive discovery session token");
    let _ = expect_result.expect("FIDL result unsatisfied");

    // Assert that host is now marked as discovering
    refresh_host(host.clone(), server.clone(), info.read().clone()).await;
    let is_discovering = host.read().get_info().discovering.clone();
    assert!(is_discovering);

    // Simulate drop of discovery session
    let expect_fidl = expect_call(server.clone(), |_, e| match e {
        HostRequest::StopDiscovery { control_handle: _ } => {
            info.write().discovering = false;
            Ok(())
        }
        _ => Err(format_err!("Unexpected!")),
    });
    std::mem::drop(session);
    expect_fidl.await.expect("FIDL result unsatisfied");

    // Assert that host is no longer marked as discovering
    refresh_host(host.clone(), server.clone(), info.read().clone()).await;
    let is_discovering = host.read().get_info().discovering.clone();
    assert!(!is_discovering);

    Ok(())
}

// Test that we can start discovery on a host then migrate that discovery session onto a different
// host when the original host is deactivated
#[fuchsia_async::run_singlethreaded(test)]
async fn test_discovery_over_adapter_change() -> Result<(), Error> {
    // Create mock host dispatcher
    let hd = host_dispatcher::test::make_simple_test_dispatcher()?;

    // Add Host #1 to dispatcher and make active
    let host_id = HostId(1);
    let active_host_path = PathBuf::from("/dev/host1");
    let host_info_1 = HostInfo {
        id: host_id,
        technology: TechnologyType::DualMode,
        address: Address::Public([0, 0, 0, 0, 0, 1]),
        local_name: None,
        active: false,
        discoverable: false,
        discovering: false,
    };
    let (host_proxy_1, host_server_1) = fidl::endpoints::create_proxy_and_stream::<HostMarker>()?;
    let host_1 = Arc::new(RwLock::new(host_device::HostDevice::new(
        active_host_path.clone(),
        host_proxy_1,
        Inspectable::new(host_info_1.clone(), placeholder_node()),
    )));
    let host_info_1 = Arc::new(RwLock::new(host_info_1));
    hd.add_test_host(host_id, host_1.clone());
    hd.set_active_host(host_id)?;

    // Add Host #2 to dispatcher
    let host_id = HostId(2);
    let host_info_2 = HostInfo {
        id: host_id,
        technology: TechnologyType::DualMode,
        address: Address::Public([0, 0, 0, 0, 0, 2]),
        local_name: None,
        active: false,
        discoverable: false,
        discovering: false,
    };
    let (host_proxy_2, host_server_2) = fidl::endpoints::create_proxy_and_stream::<HostMarker>()?;
    let host_2 = Arc::new(RwLock::new(host_device::HostDevice::new(
        PathBuf::from("/dev/host2"),
        host_proxy_2,
        Inspectable::new(host_info_2.clone(), placeholder_node()),
    )));
    let host_info_2 = Arc::new(RwLock::new(host_info_2));
    hd.add_test_host(host_id, host_2.clone());

    // Create access server future
    let (access_client, access_server) =
        fidl::endpoints::create_proxy_and_stream::<AccessMarker>()?;
    let run_access = access::run(hd.clone(), access_server);

    // Create access client future
    let (discovery_session, discovery_session_server) = fidl::endpoints::create_proxy()?;
    let run_client = async move {
        // Request discovery on active Host #1
        let response = access_client.start_discovery(discovery_session_server).await;
        assert_matches!(response, Ok(Ok(())));

        // Assert that Host #1 is now marked as discovering
        host_device::refresh_host_info(host_1.clone())
            .await
            .expect("did not receive Host #1 info update");
        let is_discovering = host_1.read().get_info().discovering.clone();
        assert!(is_discovering);

        // Deactivate Host #1
        hd.rm_adapter(&active_host_path).await;

        // Assert that Host #2 is now marked as discovering
        host_device::refresh_host_info(host_2.clone())
            .await
            .expect("did not receive Host #2 info update");
        let is_discovering = host_2.read().get_info().discovering.clone();
        assert!(is_discovering);

        // Drop discovery session, which contains an internal reference to the dispatcher state,
        // so that the other futures may terminate
        drop(discovery_session);
        Ok(())
    };

    future::try_join4(
        run_client,
        run_access,
        run_discovery_host_server(host_server_1, host_info_1),
        run_discovery_host_server(host_server_2, host_info_2),
    )
    .await
    .map(|_: ((), (), (), ())| ())
}

// Runs a HostRequestStream that handles StartDiscovery and WatchState requests
async fn run_discovery_host_server(
    server: HostRequestStream,
    host_info: Arc<RwLock<HostInfo>>,
) -> Result<(), Error> {
    server
        .try_for_each(move |req| {
            // Set discovery field of host info
            if let HostRequest::StartDiscovery { responder } = req {
                host_info.write().discovering = true;
                assert_matches!(responder.send(&mut Ok(())), Ok(()));
            }
            // Update host with current info state
            else if let HostRequest::WatchState { responder } = req {
                assert_matches!(
                    responder.send(FidlHostInfo::from(host_info.read().clone())),
                    Ok(())
                );
            }

            future::ok(())
        })
        .await
        .map_err(|e| e.into())
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
        Err(format_err!("No event received"))
    }
}

// Updates host with new info
async fn refresh_host(
    host: Arc<RwLock<host_device::HostDevice>>,
    server: Arc<RwLock<HostRequestStream>>,
    info: HostInfo,
) {
    let refresh = host_device::refresh_host_info(host);
    let expect_fidl = expect_call(server, |_, e| match e {
        HostRequest::WatchState { responder } => {
            responder.send(FidlHostInfo::from(info))?;
            Ok(())
        }
        _ => Err(format_err!("Unexpected!")),
    });

    let (refresh_result, expect_result) = join!(refresh, expect_fidl);
    let _ = refresh_result.expect("did not receive HostInfo update");
    let _ = expect_result.expect("FIDL result unsatisfied");
}
