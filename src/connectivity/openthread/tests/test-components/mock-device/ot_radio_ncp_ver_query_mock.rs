// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_syslog::{self as syslog, macros::*},
    ot_test_code::spinel_device_ncp_ver_query::*,
    ot_test_utils::fake_ot_radio_driver_utils::*,
};

const OT_PROTOCOL_PATH: &str = "class/ot-radio";

#[fuchsia_async::run_singlethreaded(test)]
async fn ot_radio_ncp_ver_query_mock() {
    syslog::init_with_tags(&["ot_radio_ncp_ver_query_mock"]).expect("Can't init logger");
    fx_log_info!("test start");

    // Get the proxy from the ot-radio device.
    let ot_device_proxy = get_device_proxy_from_isolated_devmgr(OT_PROTOCOL_PATH)
        .await
        .expect("getting device proxy");

    // Run the test.
    spinel_device_ncp_ver_query(ot_device_proxy).await;

    fx_log_info!("test end");
}
