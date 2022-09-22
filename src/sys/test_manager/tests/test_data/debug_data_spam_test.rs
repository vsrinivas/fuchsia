// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_debugdata::PublisherMarker, fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
};

async fn publish_debug_data(num_vmos: usize, vmo_size: usize) {
    let vmo_contents = "a".repeat(vmo_size);
    let publisher = connect_to_protocol::<PublisherMarker>().unwrap();
    for i in 0..num_vmos {
        let (_vmo_server, vmo_token) = zx::EventPair::create().unwrap();
        let data = zx::Vmo::create(vmo_size as u64).unwrap();
        data.write(vmo_contents.as_bytes(), 0).expect("write to VMO");
        data.set_content_size(&(vmo_contents.len() as u64)).expect("set VMO content size");
        let name = format!("data_sink_{}", i);
        publisher.publish(&name, data, vmo_token).expect("The connection should not die");
    }
    // TODO(fxbug.dev/76579): remove timeout once routing requests are guaranteed delivery
    // before component destruction.
    fuchsia_async::Timer::new(std::time::Duration::from_secs(5)).await;
}

#[fuchsia::test]
async fn many_small_vmos() {
    const NUM_VMOS: usize = 3250;
    const VMO_SIZE: usize = 1024 * 4; // 4KiB
    publish_debug_data(NUM_VMOS, VMO_SIZE).await;
}

#[fuchsia::test]
async fn few_large_vmos() {
    const NUM_VMOS: usize = 2;
    const VMO_SIZE: usize = 1024 * 1024 * 400; // 400 MiB
    publish_debug_data(NUM_VMOS, VMO_SIZE).await;
}
