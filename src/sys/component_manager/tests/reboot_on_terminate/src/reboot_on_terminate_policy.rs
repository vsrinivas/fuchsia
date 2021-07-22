// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fidl_examples_routing_echo as fecho, fidl_fidl_test_components as ftest,
    fidl_fuchsia_sys2 as fsys, fuchsia_component::client, fuchsia_zircon as zx, futures::StreamExt,
    log::*, matches::assert_matches,
};

#[fuchsia::component]
async fn main() {
    info!("start");

    // Expect an ACCESS_DENIED epitaph due to failed policy check.
    let realm_proxy = client::connect_to_protocol::<fsys::RealmMarker>().unwrap();
    let (exposed_dir, server_end) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
    realm_proxy
        .open_exposed_dir(
            &mut fsys::ChildRef { name: "critical_child_not_allowlisted".into(), collection: None },
            server_end,
        )
        .await
        .unwrap()
        .unwrap();
    let echo = client::connect_to_protocol_at_dir_root::<fecho::EchoMarker>(&exposed_dir).unwrap();
    let mut stream = echo.take_event_stream();
    assert_matches!(stream.next().await, Some(Err(fidl::Error::ClientChannelClosed { status, ..}))
        if status == zx::Status::ACCESS_DENIED);

    let trigger = client::connect_to_protocol::<ftest::TriggerMarker>().unwrap();
    trigger.run().await.unwrap();
}
