// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_debugdata::PublisherMarker,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    std::thread::sleep,
    std::time::Duration,
    zx::{AsHandleRef, HandleBased},
};

#[fuchsia::test]
async fn can_connect_to_publisher_service() {
    // Here we want to test that we can find and connect to publisher service. As the service does
    // not return anything, the only way to do that is first wait for vmo_handle to close and then
    // send a request again to make sure that fidl connection still works.
    for i in 0..2 {
        let publisher = connect_to_protocol::<PublisherMarker>().unwrap();
        let (vmo_server, vmo_token) = zx::EventPair::create().unwrap();
        let data = zx::Vmo::create(10).unwrap();
        let vmo_handle = data.duplicate_handle(zx::Rights::SAME_RIGHTS).unwrap();
        let name = format!("data_sink_{}", i);
        publisher.publish(&name, data, vmo_token).expect("The connection should not die");
        drop(vmo_server);
        // wait for vmo handle to close.
        loop {
            let count = vmo_handle.count_info().unwrap();
            if count.handle_count != 1 {
                sleep(Duration::from_millis(1));
            } else {
                break;
            };
        }
    }
}
