// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_syslog::{self as syslog, macros::*},
    ot_test_code::spinel_device_ncp_ver_query::*,
    ot_test_utils::ot_stack_test_utils::*,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn ot_stack_ncp_ver_query_mock() {
    syslog::init_with_tags(&["ot_stack_ncp_ver_query_mock"]).expect("Can't init logger");
    fx_log_info!("test start");
    let ot_stack_proxy = connect_to_ot_stack_mock();
    spinel_device_ncp_ver_query(ot_stack_proxy).await;
    fx_log_info!("test end");
}
