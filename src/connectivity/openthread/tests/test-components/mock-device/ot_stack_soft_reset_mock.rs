// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_syslog::{self as syslog, macros::*},
    ot_test_code::spinel_device_soft_reset::*,
    ot_test_utils::fake_ot_radio_driver_utils::*,
    ot_test_utils::ot_stack_test_utils::*,
};

const OT_PROTOCOL_PATH: &str = "class/ot-radio";

#[fuchsia_async::run_singlethreaded(test)]
#[ignore] // Disable temporarily per fxb/85315
async fn ot_stack_soft_reset_mock() {
    syslog::init_with_tags(&["ot_stack_soft_reset_mock"]).expect("Can't init logger");
    fx_log_info!("test start");

    let ot_stack_proxy = connect_to_ot_stack_mock();

    spinel_device_soft_reset(ot_stack_proxy).await;

    // Remove fake ot device
    let device = get_ot_device_in_isolated_devmgr(OT_PROTOCOL_PATH).await.expect("getting device");
    unbind_device_in_isolated_devmgr(&device).expect("schedule unbind");
    validate_removal_of_device_in_isolated_devmgr(OT_PROTOCOL_PATH)
        .await
        .expect("validate removal of device");

    fx_log_info!("test end");
}
