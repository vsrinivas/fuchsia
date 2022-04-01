// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::{endpoints::create_proxy, AsHandleRef},
    fidl_fuchsia_debugdata::{DebugDataMarker, DebugDataVmoTokenMarker, PublisherMarker},
    fuchsia_component::client::connect_to_protocol_at_path,
    fuchsia_zircon as zx,
    std::ffi::CStr,
};

const DEPRECATED_VMO_CONTENTS: &[u8] = b"Deprecated debug data from test\n";
const VMO_CONTENTS: &[u8] = b"Debug data from test\n";
const DEPRECATED_VMO_NAME: &[u8] = b"vmo_name\0";
const VMO_NAME: &[u8] = b"Deprecated vmo_name\0";
const DEPRECATED_VMO_DATA_SINK: &str = "Deprecated data_sink";
const VMO_DATA_SINK: &str = "data_sink";
const DEBUG_DATA_PATH: &str = "/svc/fuchsia.debugdata.DebugDataForTest";
const PUBLISHER_PATH: &str = "/svc/fuchsia.debugdata.PublisherForTest";

#[fuchsia::test]
async fn publish_debug_data() {
    let debug_data = connect_to_protocol_at_path::<DebugDataMarker>(DEBUG_DATA_PATH).unwrap();
    let vmo = zx::Vmo::create(1024).unwrap();
    vmo.write(DEPRECATED_VMO_CONTENTS, 0).expect("write to VMO");
    vmo.set_content_size(&(DEPRECATED_VMO_CONTENTS.len() as u64)).expect("set VMO content size");
    vmo.set_name(CStr::from_bytes_with_nul(DEPRECATED_VMO_NAME).unwrap()).expect("set VMO name");
    let (vmo_token, vmo_server) = create_proxy::<DebugDataVmoTokenMarker>().unwrap();
    debug_data.publish(DEPRECATED_VMO_DATA_SINK, vmo, vmo_server).expect("Publish debugdata");
    drop(vmo_token);

    let publish_data = connect_to_protocol_at_path::<PublisherMarker>(PUBLISHER_PATH).unwrap();
    let vmo = zx::Vmo::create(1024).unwrap();
    vmo.write(VMO_CONTENTS, 0).expect("write to VMO");
    vmo.set_content_size(&(VMO_CONTENTS.len() as u64)).expect("set VMO content size");
    vmo.set_name(CStr::from_bytes_with_nul(VMO_NAME).unwrap()).expect("set VMO name");
    let (vmo_token, vmo_server) = zx::EventPair::create().unwrap();
    publish_data.publish(VMO_DATA_SINK, vmo, vmo_server).expect("Publish debugdata");
    drop(vmo_token);
}
