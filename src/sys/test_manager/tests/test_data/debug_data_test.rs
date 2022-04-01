// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    assert_matches::assert_matches,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_debugdata::{DebugDataMarker, PublisherMarker},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx, fuchsia_zircon_status as zx_status,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn can_connect_to_publisher_service() {
    let debug_data = connect_to_protocol::<DebugDataMarker>().unwrap();
    let error = debug_data
        .load_config("non_existent_config")
        .await
        .expect_err("the connection should have died");

    assert_matches!(
        error,
        fidl::Error::ClientChannelClosed {
            status: zx_status::Status::NOT_SUPPORTED,
            protocol_name: DebugDataMarker::NAME
        }
    );

    // Here we want to test that we can find and connect to publisher service. As the service does
    // not return anything the only way to do that is to call Publish again and again to make sure
    // that connection is not closed.
    // There is no other way to test correctness.
    let publisher = connect_to_protocol::<PublisherMarker>().unwrap();
    for i in 0..1000 {
        let (_vmo_server, vmo_token) = zx::EventPair::create().unwrap();
        let data = zx::Vmo::create(1024).unwrap();
        let name = format!("data_sink_{}", i);
        publisher.publish(&name, data, vmo_token).expect("The connection should not die");
    }
}
