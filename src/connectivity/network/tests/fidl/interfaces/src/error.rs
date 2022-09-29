// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! FIDL tests that will emit error log lines.

#![cfg(test)]

use std::collections::HashMap;

use fidl::endpoints::Proxy as _;
use fuchsia_zircon as zx;
use net_declare::fidl_subnet;
use netstack_testing_common::realms::{Netstack, NetstackVersion, TestSandboxExt as _};
use netstack_testing_macros::variants_test;

#[variants_test]
async fn interfaces_watcher_after_invalid_state_request<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("failed to create netstack");

    let interfaces_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("failed to connect fuchsia.net.interfaces/State");
    let stream = fidl_fuchsia_net_interfaces_ext::event_stream_from_state(&interfaces_state)
        .expect("get interface event stream");

    // Writes some garbage into the channel and verify an error on the State
    // doesn't cause trouble using an obtained Watcher.
    let () = interfaces_state
        .as_channel()
        .write(&[1, 2, 3, 4, 5, 6], &mut [])
        .expect("failed to write garbage to channel");

    // The channel to the State protocol should be closed by the server.
    assert_eq!(interfaces_state.on_closed().await, Ok(zx::Signals::CHANNEL_PEER_CLOSED));

    // But we should still be able to use the already obtained watcher.
    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(stream, HashMap::new())
        .await
        .expect("failed to collect interfaces");
    let expected = match N::VERSION {
        NetstackVersion::Netstack3 | NetstackVersion::Netstack2 => std::iter::once((
            1,
            fidl_fuchsia_net_interfaces_ext::Properties {
                id: 1,
                name: "lo".to_owned(),
                device_class: fidl_fuchsia_net_interfaces::DeviceClass::Loopback(
                    fidl_fuchsia_net_interfaces::Empty,
                ),
                online: true,
                addresses: vec![
                    fidl_fuchsia_net_interfaces_ext::Address {
                        addr: fidl_subnet!("127.0.0.1/8"),
                        valid_until: zx::sys::ZX_TIME_INFINITE,
                    },
                    fidl_fuchsia_net_interfaces_ext::Address {
                        addr: fidl_subnet!("::1/128"),
                        valid_until: zx::sys::ZX_TIME_INFINITE,
                    },
                ],
                has_default_ipv4_route: false,
                has_default_ipv6_route: false,
            },
        ))
        .collect(),
        NetstackVersion::ProdNetstack2 => panic!("unexpected netstack version"),
        NetstackVersion::Netstack2WithFastUdp => panic!("unexpected netstack version"),
    };
    assert_eq!(interfaces, expected);
}
