// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_syslog::{self as syslog, macros::*},
    ot_test_utils::{fake_ot_radio_driver_utils::*, ot_radio_driver_utils::*},
};

const OT_PROTOCOL_PATH: &str = "class/ot-radio";

#[fuchsia_async::run_singlethreaded(test)]
async fn ot_fake_driver_open() {
    syslog::init_with_tags(&["ot_fake_driver_open"]).expect("Can't init logger");
    fx_log_info!("test starts");
    let ot_device_file =
        get_ot_device_in_isolated_devmgr(OT_PROTOCOL_PATH).await.expect("getting device");
    let ot_device_client_ep =
        ot_radio_set_channel(&ot_device_file).await.expect("connecting to driver");
    let ot_device_proxy = ot_device_client_ep.into_proxy().expect("getting device proxy");
    ot_device_proxy.open().await.expect("sending FIDL req").expect("getting FIDL resp");
    fx_log_info!("test ends");
}
