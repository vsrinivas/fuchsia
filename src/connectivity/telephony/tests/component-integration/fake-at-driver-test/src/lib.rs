// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_syslog::{self as syslog, macros::*},
    qmi as tel_ctrl,
    tel_dev::{at_test_common::*, component_test::*},
};

// Tests that creating and destroying a fake at device, binds and unbinds the fake at driver.
#[fuchsia_async::run_singlethreaded(test)]
async fn fake_at_driver_test() {
    syslog::init_with_tags(&["fake-at-driver-test"]).expect("Can't init logger");
    const TEL_PATH: &str = "class/at-transport";
    let at_device =
        get_fake_device_in_isolated_devmgr(TEL_PATH).await.expect("getting fake device");
    let ctrl_channel =
        tel_ctrl::connect_transport_device(&at_device).await.expect("connecting to driver");
    ctrl_channel.write(AT_CMD_REQ_ATD, &mut Vec::new()).expect("sending AT msg");
    let at_msg_vec = read_next_msg_from_channel(&ctrl_channel).expect("receiving AT msg");
    assert_eq!(&at_msg_vec, &AT_CMD_RESP_NO_CARRIER.to_vec());
    fx_log_info!("received and verified responses");
    unbind_fake_device(&at_device).expect("removing fake device");
    fx_log_info!("unbinded device");
    validate_removal_of_fake_device(TEL_PATH).await.expect("validate removal of device");
}
