// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_debugdata::PublisherMarker, fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
};

#[fuchsia::test]
async fn can_connect_to_publisher_service() {
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
