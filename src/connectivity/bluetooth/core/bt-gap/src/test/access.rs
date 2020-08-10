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

use crate::{host_device, host_dispatcher, services::access};

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
    let path = Path::new("/dev/host");
    let host_device = host_device::test::new_mock(host_id, address, path, host_proxy);
    dispatcher.add_test_host(host_id, Arc::new(RwLock::new(host_device)));

    let (client, server) = fidl::endpoints::create_proxy_and_stream::<AccessMarker>()?;
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
