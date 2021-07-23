// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::ProtocolMarker, fidl_fuchsia_debugdata::DebugDataMarker,
    fuchsia_component::client::connect_to_protocol, fuchsia_zircon_status as zx_status,
    matches::assert_matches,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn can_connect_to_debug_data_service() {
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
}
