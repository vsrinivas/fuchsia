// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_lowpan_spinel::DeviceMarker,
    fuchsia_component::client::{launch, launcher},
    fuchsia_syslog::{self as syslog, macros::*},
};

#[fuchsia_async::run_singlethreaded(test)]
async fn ot_radio_device_open() {
    syslog::init_with_tags(&["ot_radio_device_open"]).expect("Can't init logger");
    fx_log_info!("test starts");
    // calling this through ot-stack
    let server_url = "fuchsia-pkg://fuchsia.com/ot-stack#meta/ot-stack.cmx".to_string();
    let arg = Some(vec!["/dev/class/ot-radio/000".to_string()]);
    let launcher = launcher().expect("Failed to open launcher service");
    let app = launch(&launcher, server_url, arg).expect("Failed to launch ot-stack service");
    let ot_stack_proxy =
        app.connect_to_service::<DeviceMarker>().expect("Failed to connect to ot-stack service");
    fx_log_info!("sending FIDL cmd");
    ot_stack_proxy.open().await.expect("sending FIDL req").expect("getting FIDL resp");
    fx_log_info!("received FIDL cmd");
    fx_log_info!("test ends");
}
