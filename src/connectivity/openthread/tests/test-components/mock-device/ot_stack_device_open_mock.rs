// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_lowpan_spinel::DeviceMarker,
    fuchsia_component::client::connect_to_service,
    fuchsia_syslog::{self as syslog, macros::*},
};

#[fuchsia_async::run_singlethreaded(test)]
async fn ot_stack_device_open_c() {
    syslog::init_with_tags(&["ot_stack_device_open_c"]).expect("Can't init logger");
    fx_log_info!("test starts");
    // calling this through ot-stack
    let ot_stack_proxy =
        connect_to_service::<DeviceMarker>().expect("Failed to connect to ot-stack service");
    fx_log_info!("sending FIDL cmd");
    ot_stack_proxy.open().await.expect("sending FIDL req").expect("getting FIDL resp");
    fx_log_info!("test ends");
}
