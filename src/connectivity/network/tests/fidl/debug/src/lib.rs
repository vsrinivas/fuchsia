// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use futures::TryStreamExt as _;
use netstack_testing_common::realms::{Netstack2, TestSandboxExt as _};
use std::collections::HashMap;

#[fuchsia_async::run_singlethreaded(test)]
async fn get_admin_unknown() {
    let name = "debug_interfaces_get_admin_unknown";

    let sandbox = netemul::TestSandbox::new().expect("create sandbox");
    let (realm, _) = sandbox
        .new_netstack::<Netstack2, fidl_fuchsia_net_stack::StackMarker, _>(name)
        .expect("create realm");

    let interface_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect(<fidl_fuchsia_net_interfaces::StateMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(
        fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interface_state)
            .expect("create watcher event stream"),
        HashMap::new(),
    )
    .await
    .expect("get starting state");
    assert_eq!(interfaces.len(), 1);
    let id = interfaces
        .keys()
        .next()
        .expect("interface properties map unexpectedly does not include loopback");

    let debug_control = realm
        .connect_to_protocol::<fidl_fuchsia_net_debug::InterfacesMarker>()
        .expect(<fidl_fuchsia_net_debug::InterfacesMarker as fidl::endpoints::DiscoverableProtocolMarker>::PROTOCOL_NAME);

    {
        // Request unknown NIC ID, expect request channel to be closed.
        let (control, server) =
            fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces_admin::ControlMarker>()
                .expect("create proxy");
        let () = debug_control.get_admin(*id + 1, server).expect("get admin failed");
        matches::assert_matches!(
            control.take_event_stream().try_collect::<Vec<_>>().await.as_ref().map(Vec::as_slice),
            // TODO(https://fxbug.dev/76695): Sending epitaphs not supported in Go.
            Ok([])
        );
    }
}
