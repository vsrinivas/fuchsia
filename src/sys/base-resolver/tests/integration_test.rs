// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::create_proxy,
    fidl_fuchsia_sys2::{ChildRef, RealmMarker},
    fidl_test_ping::PingMarker,
    fuchsia_async as fasync,
    fuchsia_component::client::{connect_to_protocol_at_dir_root, connect_to_service},
};

#[fasync::run_singlethreaded(test)]
async fn base_resolver_test() {
    let realm =
        connect_to_service::<RealmMarker>().expect("failed to connect to fuchsia.sys2.Realm");
    let (exposed_dir, server_end) = create_proxy().expect("failed to create proxy");
    realm
        .bind_child(&mut ChildRef { name: "component".into(), collection: None }, server_end)
        .await
        .expect("failed to call bind child FIDL")
        .expect("failed to bind child");
    let ping = connect_to_protocol_at_dir_root::<PingMarker>(&exposed_dir)
        .expect("failed to connect to Ping protocol");
    assert_eq!(ping.ping("ping").await.expect("Ping FIDL call failed"), "ping pong");
}
