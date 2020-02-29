// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_lowpan_spinel::{DeviceMarker, DeviceProxy},
    fuchsia_component::client::connect_to_service,
    fuchsia_component::client::{launch, launcher, App},
};

pub fn connect_to_ot_stack_real() -> (App, DeviceProxy) {
    let server_url = "fuchsia-pkg://fuchsia.com/ot-stack#meta/ot-stack.cmx".to_string();
    let arg = Some(vec!["/dev/class/ot-radio/000".to_string()]);
    let launcher = launcher().expect("Failed to open launcher service");
    let app = launch(&launcher, server_url, arg).expect("Failed to launch ot-stack service");
    let ot_stack_proxy =
        app.connect_to_service::<DeviceMarker>().expect("Failed to connect to ot-stack service");
    (app, ot_stack_proxy)
}

pub fn connect_to_ot_stack_mock() -> DeviceProxy {
    connect_to_service::<DeviceMarker>().expect("Failed to connect to ot-stack service")
}
