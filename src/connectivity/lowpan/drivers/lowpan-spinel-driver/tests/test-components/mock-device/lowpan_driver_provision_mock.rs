// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_syslog::{self as syslog, macros::*},
    lowpan_test_code::lowpan_driver_provision_test::*,
    lowpan_test_utils::lowpan_driver_test_utils::*,
    ot_test_utils::fake_ot_radio_driver_utils::*,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn lowpan_driver_provision_mock() {
    syslog::init_with_tags(&["lowpan_driver_provision_mock"]).expect("Can't init logger");
    fx_log_info!("test start");

    let driver = lowpan_driver_init().await;

    lowpan_driver_provision_test().await;

    lowpan_driver_deinit(driver).await;

    ot_radio_deinit().await;

    fx_log_info!("test end");
}
