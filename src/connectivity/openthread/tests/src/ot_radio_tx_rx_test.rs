// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_syslog::{self as syslog, macros::*},
    ot_test_utils::{ot_radio_driver_utils::*, spinel_device_utils::*},
};

const OT_RADIO_VERSION_RESP: &[u8; 13] =
    &[0x81, 0x06, 0x02, 0x4f, 0x50, 0x45, 0x4E, 0x54, 0x48, 0x52, 0x45, 0x41, 0x44];
const OT_RADIO_VERSION_REQ: &[u8; 3] = &[0x81, 0x02, 0x02];
const OT_RADIO_RESET_EVENT: &[u8; 3] = &[0x80, 0x06, 0x0];
const OT_RADIO_TX_ALLOWANCE_INIT_VAL: u32 = 4;
const OT_RADIO_RX_ALLOWANCE_INIT_VAL: u32 = 4;
const OT_PROTOCOL_PATH: &str = "/dev/class/ot-radio";

#[fuchsia_async::run_singlethreaded(test)]
async fn ot_radio_tx_rx_test() {
    syslog::init_with_tags(&["ot_radio_tx_rx_test"]).expect("Can't init logger");
    fx_log_info!("test start");
    let ot_device_file = get_ot_device_in_devmgr(OT_PROTOCOL_PATH).await.expect("getting device");
    let ot_device_client_ep =
        ot_radio_set_channel(&ot_device_file).await.expect("connecting to driver");
    let ot_device_proxy = ot_device_client_ep.into_proxy().expect("getting device proxy");
    let mut ot_device_event_stream = ot_device_proxy.take_event_stream();
    ot_device_proxy.open().await.expect("sending FIDL req").expect("getting FIDL resp");
    let tx_allowance: u32 = expect_on_ready_for_send_frame_event(&mut ot_device_event_stream)
        .await
        .expect("receiving frame");
    assert_eq!(tx_allowance, OT_RADIO_TX_ALLOWANCE_INIT_VAL);
    ot_device_proxy
        .ready_to_receive_frames(OT_RADIO_RX_ALLOWANCE_INIT_VAL)
        .expect("setting receive frame num");
    let mut tx_frame_iter = OT_RADIO_VERSION_REQ.iter().map(|x| *x);
    ot_device_proxy.send_frame(&mut tx_frame_iter).expect("sending frame");
    let mut rx_frame =
        expect_on_receive_event(&mut ot_device_event_stream).await.expect("receiving frame");
    println!("received frame {:?}", rx_frame);
    assert_eq!(OT_RADIO_RESET_EVENT, &rx_frame[..3]);
    assert!(rx_frame[3] < 0x80);
    rx_frame = expect_on_receive_event(&mut ot_device_event_stream).await.expect("receiving frame");
    println!("received frame {:?}", rx_frame);
    assert_eq!(OT_RADIO_VERSION_RESP, &rx_frame[..13]);
    fx_log_info!("received and validated frame");
    unbind_device(&ot_device_file).expect("removing device");
    fx_log_info!("unbinded device");
    validate_removal_of_device(OT_PROTOCOL_PATH).await.expect("validate removal of device");
}
