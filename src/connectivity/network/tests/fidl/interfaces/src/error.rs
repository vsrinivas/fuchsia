// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! FIDL tests that will emit error log lines.

#![cfg(test)]

use std::collections::HashMap;

use fidl::endpoints::ProtocolMarker as _;
use fidl::endpoints::Proxy as _;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use net_declare::fidl_subnet;
use netstack_testing_common::realms::{Netstack, Netstack3, NetstackVersion, TestSandboxExt as _};
use netstack_testing_macros::variants_test;

#[variants_test]
async fn interfaces_watcher_after_invalid_state_request<N: Netstack>(name: &str) {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = sandbox.create_netstack_realm::<N, _>(name).expect("failed to create netstack");

    let interfaces_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("failed to connect fuchsia.net.interfaces/State");
    let (watcher, server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
            .expect("failed to create watcher proxy");
    let () = interfaces_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, server)
        .expect("failed to get watcher");

    // Writes some garbage into the channel and verify an error on the State
    // doesn't cause trouble using an obtained Watcher.
    let () = interfaces_state
        .as_channel()
        .write(&[1, 2, 3, 4, 5, 6], &mut [])
        .expect("failed to write garbage to channel");

    // The channel to the State protocol should be closed by the server.
    assert_eq!(
        fasync::OnSignals::new(interfaces_state.as_channel(), zx::Signals::CHANNEL_PEER_CLOSED)
            .await,
        Ok(zx::Signals::CHANNEL_PEER_CLOSED),
    );

    // But we should still be able to use the already obtained watcher.
    let stream = fidl_fuchsia_net_interfaces_ext::event_stream(watcher);
    let interfaces = fidl_fuchsia_net_interfaces_ext::existing(stream, HashMap::new())
        .await
        .expect("failed to collect interfaces");
    // TODO(https://fxbug.dev/72378): N3 doesn't support loopback devices yet.
    let expected = match N::VERSION {
        NetstackVersion::Netstack2 => std::iter::once((
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
        NetstackVersion::Netstack3 => HashMap::new(),
    };
    assert_eq!(interfaces, expected);
}

// TODO(https://fxbug.dev/75553): Rewrite this test and make it a variants test
// once we support hanging-get in N3.
#[fasync::run_singlethreaded(test)]
async fn interfaces_watcher_hanging_get() {
    let sandbox = netemul::TestSandbox::new().expect("failed to create sandbox");
    let realm = sandbox
        .create_netstack_realm::<Netstack3, _>("interfaces_watcher_hanging_get")
        .expect("failed to create netstack");

    let interfaces_state = realm
        .connect_to_protocol::<fidl_fuchsia_net_interfaces::StateMarker>()
        .expect("failed to connect fuchsia.net.interfaces/State");
    let (watcher, server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_net_interfaces::WatcherMarker>()
            .expect("failed to create watcher proxy");
    let () = interfaces_state
        .get_watcher(fidl_fuchsia_net_interfaces::WatcherOptions::EMPTY, server)
        .expect("failed to get watcher");

    assert_eq!(
        watcher.watch().await.expect("failed to get idle event"),
        fidl_fuchsia_net_interfaces::Event::Idle(fidl_fuchsia_net_interfaces::Empty)
    );

    // Try hanging-get twice, and we should get NOT_SUPPORTED back.
    for _ in 0..2 {
        matches::assert_matches!(
            watcher.watch().await,
            Err(fidl::Error::ClientChannelClosed {
                status: zx::Status::NOT_SUPPORTED,
                protocol_name: fidl_fuchsia_net_interfaces::WatcherMarker::DEBUG_NAME,
            })
        );
    }
}
