// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_debugdata::PublisherMarker,
    fuchsia_component::client::connect_to_protocol_at_path, fuchsia_zircon as zx,
};

const NUM_VMOS: usize = 3250;
const VMO_SIZE: usize = 4096;
const PUBLISHER_PATH: &str = "/svc/fuchsia.debugdata.PublisherForTest";

#[fuchsia::test]
async fn publish_debug_data() {
    let vmo_contents = "a".repeat(VMO_SIZE);

    let publisher = connect_to_protocol_at_path::<PublisherMarker>(PUBLISHER_PATH).unwrap();
    for i in 0..NUM_VMOS {
        let (_vmo_server, vmo_token) = zx::EventPair::create().unwrap();
        let data = zx::Vmo::create(1024).unwrap();
        data.write(vmo_contents.as_bytes(), 0).expect("write to VMO");
        data.set_content_size(&(vmo_contents.len() as u64)).expect("set VMO content size");
        let name = format!("data_sink_{}", i);
        publisher.publish(&name, data, vmo_token).expect("The connection should not die");
    }
}
