// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use futures::TryStreamExt as _;
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use std::collections::HashMap;

async fn with_debug<F, Fut>(name: &str, test: F)
where
    F: FnOnce(&netemul::TestRealm<'_>, fidl_fuchsia_net_debug::InterfacesProxy) -> Fut,
    Fut: futures::Future<Output = ()>,
{
    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let realm = sandbox.create_netstack_realm::<Netstack2, _>(name).expect("create realm");

    let debug_control = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect(<fidl_fuchsia_net_debug::InterfacesMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    let () = test(&realm, debug_control).await;
}

async fn with_debug_and_loopback_id<F, Fut>(name: &str, test: F)
where
    F: FnOnce(fidl_fuchsia_net_debug::InterfacesProxy, u64) -> Fut,
    Fut: futures::Future<Output = ()>,
{
    with_debug(name, |realm, debug_control| {
        let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect(<fidl_fuchsia_net_interfaces::StateMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

        let stream = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("create watcher event stream");

        async move {
            let interfaces = fidl_fuchsia_net_interfaces_ext::existing(stream, HashMap::new())
                .await
                .expect("get starting state");

            let mut interfaces = interfaces.into_values();
            let fidl_fuchsia_net_interfaces_ext::Properties {
                id,
                name: _,
                device_class: _,
                online:_,
                addresses:_,
                has_default_ipv4_route:_,
                has_default_ipv6_route:_,
            } = interfaces
                .next()
                .expect("interface properties map unexpectedly does not include loopback");
            assert_eq!(interfaces.next(), None);

            let () = test(debug_control, id).await;
        }
    }).await
}

#[fixture::fixture(with_debug_and_loopback_id)]
#[fuchsia::test]
async fn get_admin_unknown(debug_control: fidl_fuchsia_net_debug::InterfacesProxy, id: u64) {
    // Request unknown NIC ID, expect request channel to be closed.
    let (control, server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
            .expect("create proxy");
    let () = debug_control.get_admin(id + 1, server).expect("get admin failed");
    matches::assert_matches!(
        control.take_event_stream().try_collect::<Vec<_>>().await.as_ref().map(Vec::as_slice),
        // TODO(https://fxbug.dev/8018): Sending epitaphs not supported in Go.
        Ok([])
    );
}

#[fixture::fixture(with_debug_and_loopback_id)]
#[fuchsia::test]
async fn get_mac(debug_control: fidl_fuchsia_net_debug::InterfacesProxy, id: u64) {
    // Loopback doesn't have a meaningful MAC address.
    matches::assert_matches!(
        debug_control.get_mac(id).await,
        Ok(Ok(fidl_fuchsia_net::MacAddress { octets: [0, 0, 0, 0, 0, 0] }))
    );
    // Unknown NIC ID produces an error.
    matches::assert_matches!(
        debug_control.get_mac(id + 1).await,
        Ok(Err(fidl_fuchsia_net_debug::InterfacesGetMacError::NotFound))
    );
}
