// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    async_helpers::hanging_get::asynchronous as hanging_get,
    fidl::endpoints,
    fidl_fuchsia_bluetooth_host::{HostMarker, HostRequest},
    fidl_fuchsia_bluetooth_sys::AccessMarker,
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::{
        pairing_options::{BondableMode, PairingOptions, SecurityLevel},
        Address, HostId, PeerId, Technology,
    },
    futures::{future, stream::TryStreamExt},
    matches::assert_matches,
    parking_lot::RwLock,
    std::{collections::HashMap, path::Path, sync::Arc},
};

use crate::{
    host_device::{self, HostDevice},
    host_dispatcher,
    services::access,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn test_pair() -> Result<(), Error> {
    let watch_peers_broker = hanging_get::HangingGetBroker::new(
        HashMap::new(),
        |_, _| true,
        hanging_get::DEFAULT_CHANNEL_SIZE,
    );
    let watch_hosts_broker = hanging_get::HangingGetBroker::new(
        Vec::new(),
        |_, _| true,
        hanging_get::DEFAULT_CHANNEL_SIZE,
    );

    let dispatcher = host_dispatcher::test::make_test_dispatcher(
        watch_peers_broker.new_publisher(),
        watch_peers_broker.new_registrar(),
        watch_hosts_broker.new_publisher(),
        watch_hosts_broker.new_registrar(),
    )?;

    // This needs to be processed so we can start up the Access service
    fasync::Task::spawn(watch_peers_broker.run()).detach();

    let (host_proxy, host_server) = endpoints::create_proxy_and_stream::<HostMarker>()?;

    let address = Address::Public([1, 2, 3, 4, 5, 6]);
    let host_id = HostId(42);
    let host_device = HostDevice::mock(host_id, address, Path::new("/dev/host"), host_proxy);
    dispatcher.add_test_host(host_id, host_device);

    let (client, server) = endpoints::create_proxy_and_stream::<AccessMarker>()?;
    let run_access = access::run(dispatcher, server);

    // The parameters to send to access.Pair()
    let req_id = PeerId(128);
    let req_opts = PairingOptions {
        transport: Technology::LE,
        le_security_level: SecurityLevel::Authenticated,
        bondable: BondableMode::NonBondable,
    };

    let req_opts_ = req_opts.clone();

    let make_request = async move {
        let response = client.pair(&mut req_id.into(), req_opts_.into()).await;
        assert_matches!(response, Ok(Ok(())));
        // This terminating will drop the access client, which causest the access stream to
        // terminate. This will cause run_access to terminate which drops the host dispatcher, which
        // closes the host channel and will cause run_host to terminate
        Ok(())
    };

    let run_host = async move {
        host_server.try_for_each(move |req| {
            assert_matches!(&req, HostRequest::Pair { id, options, responder: _ } if *id == req_id.into() && PairingOptions::from(options) == req_opts);
            if let HostRequest::Pair { id: _, options: _, responder } = req {
                assert_matches!(responder.send(&mut Ok(())), Ok(()));
            }
            future::ok(())
        }).await.map_err(|e| e.into())
    };

    let r = future::try_join3(make_request, run_host, run_access).await.map(|_: ((), (), ())| ());
    println!("{:?}", r);
    r
}

// Test that we can start discovery on a host then migrate that discovery session onto a different
// host when the original host is deactivated
#[fuchsia_async::run_singlethreaded(test)]
async fn test_discovery_over_adapter_change() -> Result<(), Error> {
    // Create mock host dispatcher
    let hd = host_dispatcher::test::make_simple_test_dispatcher()?;

    // Add Host #1 to dispatcher and make active
    let (host_proxy_1, host_server_1) = endpoints::create_proxy_and_stream::<HostMarker>()?;
    let active_host_path = Path::new("/dev/host1");
    let addr = Address::Public([0, 0, 0, 0, 0, 1]);
    let host_1 = HostDevice::mock(HostId(1), addr, active_host_path, host_proxy_1).as_inactive();
    let host_info_1 = Arc::new(RwLock::new(host_1.info()));

    hd.add_test_host(HostId(1), host_1.clone());
    hd.set_active_host(HostId(1))?;

    // Add Host #2 to dispatcher
    let (host_proxy_2, host_server_2) = endpoints::create_proxy_and_stream::<HostMarker>()?;
    let addr = Address::Public([0, 0, 0, 0, 0, 2]);
    let inactive_host_path = Path::new("/dev/host2");
    let host_2 = HostDevice::mock(HostId(2), addr, inactive_host_path, host_proxy_2).as_inactive();
    let host_info_2 = Arc::new(RwLock::new(host_2.info()));
    hd.add_test_host(HostId(2), host_2.clone());

    // Create access server future
    let (access_client, access_server) = endpoints::create_proxy_and_stream::<AccessMarker>()?;
    let run_access = access::run(hd.clone(), access_server);

    // Create access client future
    let (discovery_session, discovery_session_server) = endpoints::create_proxy()?;
    let run_client = async move {
        // Request discovery on active Host #1
        let response = access_client.start_discovery(discovery_session_server).await;
        assert_matches!(response, Ok(Ok(())));

        // Assert that Host #1 is now marked as discovering
        host_1.clone().refresh_test_host_info().await.expect("did not receive Host #1 info update");
        let is_discovering = host_1.info().discovering.clone();
        assert!(is_discovering);

        // Deactivate Host #1
        hd.rm_adapter(active_host_path).await;

        // Assert that Host #2 is now marked as discovering
        host_2.clone().refresh_test_host_info().await.expect("did not receive Host #2 info update");
        let is_discovering = host_2.info().discovering.clone();
        assert!(is_discovering);

        // Drop discovery session, which contains an internal reference to the dispatcher state,
        // so that the other futures may terminate. Then, assert Host #2 stops discovering.
        drop(discovery_session);

        // TODO(fxbug.dev/59420): Remove the double refresh once the cause is understood and fixed
        host_2.clone().refresh_test_host_info().await.expect("did not receive Host #2 info update");
        host_2.clone().refresh_test_host_info().await.expect("did not receive Host #2 info update");
        let is_discovering = host_2.info().discovering.clone();
        assert!(!is_discovering);

        Ok(())
    };

    future::try_join4(
        run_client,
        run_access,
        host_device::test::run_discovery_host_server(host_server_1, host_info_1),
        host_device::test::run_discovery_host_server(host_server_2, host_info_2),
    )
    .await
    .map(|_: ((), (), (), ())| ())
}
