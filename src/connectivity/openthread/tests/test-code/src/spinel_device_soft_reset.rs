// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::*, fidl_fuchsia_lowpan_spinel::DeviceProxy, fuchsia_syslog::macros::*,
    ot_test_utils::spinel_device_utils::*,
};

const OT_RADIO_QUERY_NUM: u32 = 128;

pub async fn spinel_device_soft_reset(device_proxy: DeviceProxy) {
    let mut spinel_device_client = LoWPANSpinelDeviceClientImpl::new(device_proxy);

    spinel_device_client.open_and_init_device().await;

    for i in 0..OT_RADIO_QUERY_NUM {
        spinel_device_client.send_one_frame(OT_RADIO_SOFT_RESET).await.expect("sending frame");
        let rx_frame = spinel_device_client.receive_one_frame().await.expect("receiving frame");
        fx_log_info!("received #{} frame {:?}", i, rx_frame);
        validate_reset_frame(&rx_frame);
    }
    fx_log_info!("received and validated frame");
}
