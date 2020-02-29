// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_syslog::{self as syslog, macros::*},
    ot_test_code::ot_stack_ncp_ver_query::*,
    ot_test_utils::ot_stack_test_utils::*,
};

#[fuchsia_async::run_singlethreaded(test)]
async fn ot_stack_ncp_ver_query_real() {
    syslog::init_with_tags(&["ot_stack_ncp_ver_query"]).expect("Can't init logger");
    fx_log_info!("test start");
    let (_app, ot_stack_proxy) = connect_to_ot_stack_real();
    ot_stack_ncp_ver_query(ot_stack_proxy).await;
    fx_log_info!("test end");
}
