// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_syslog::{self as syslog, macros::*},
    ot_test_utils::{ot_radio_driver_utils::*, spinel_device_utils::*},
};

const OT_RADIO_SOFT_RESET: &[u8; 2] = &[0x81, 0x01];
const OT_RADIO_RESET_EVENT: &[u8; 3] = &[0x80, 0x06, 0x0];
const OT_RADIO_RESET_EVENT_FOURTH_VAL_MAX: u8 = 0x80;
const OT_RADIO_RESET_EVENT_FOURTH_VAL_IDX: usize = 3;
const OT_RADIO_TX_ALLOWANCE_INIT_VAL: u32 = 4;
const OT_RADIO_RX_ALLOWANCE_INIT_VAL: u32 = 4;
const OT_RADIO_RX_ALLOWANCE_INC_VAL: u32 = 2;
const OT_RADIO_QUERY_NUM: u32 = 128;
const OT_RADIO_GET_FRAME_AND_TX_ALLOWANCE: u32 = 2;
const OT_RADIO_GET_FRAME: u32 = 1;
const OT_PROTOCOL_PATH: &str = "/dev/class/ot-radio";

#[fuchsia_async::run_singlethreaded(test)]
async fn ot_radio_soft_reset_test() {
    syslog::init_with_tags(&["ot_radio_soft_reset_test"]).expect("Can't init logger");
    fx_log_info!("test start");

    // Connect to ot-radio device.
    let ot_device_file = get_ot_device_in_devmgr(OT_PROTOCOL_PATH).await.expect("getting device");
    let ot_device_client_ep =
        ot_radio_set_channel(&ot_device_file).await.expect("connecting to driver");
    let ot_device_proxy = ot_device_client_ep.into_proxy().expect("getting device proxy");
    let mut ot_device_event_stream = ot_device_proxy.take_event_stream();

    // Initialize ot-radio device.
    ot_device_proxy.open().await.expect("sending FIDL req").expect("getting FIDL resp");
    let mut ot_device_instance = OtRadioDevice { tx_allowance: 0, rx_allowance: 0 };

    // Get allowance for the test component to send out frames
    ot_device_instance.tx_allowance =
        expect_on_ready_for_send_frame_event(&mut ot_device_event_stream)
            .await
            .expect("receiving frame");
    assert_eq!(ot_device_instance.tx_allowance, OT_RADIO_TX_ALLOWANCE_INIT_VAL);

    // Let driver know that the test component is ready to receive frame
    ot_device_proxy
        .ready_to_receive_frames(OT_RADIO_RX_ALLOWANCE_INIT_VAL)
        .expect("setting receive frame num");
    ot_device_instance.rx_allowance = OT_RADIO_TX_ALLOWANCE_INIT_VAL;

    // Receive a reset event after open() is called.
    let mut rx_frame =
        expect_on_receive_event(&mut ot_device_event_stream).await.expect("receiving event");
    fx_log_info!("received frame {:?}", rx_frame);
    assert_eq!(OT_RADIO_RESET_EVENT, &rx_frame[..OT_RADIO_RESET_EVENT_FOURTH_VAL_IDX]);
    assert!(rx_frame[OT_RADIO_RESET_EVENT_FOURTH_VAL_IDX] < OT_RADIO_RESET_EVENT_FOURTH_VAL_MAX);
    ot_device_instance.rx_allowance -= 1;

    // Send out soft reset frames and validate reset events.
    for i in 0..OT_RADIO_QUERY_NUM {
        let mut tx_frame_iter = OT_RADIO_SOFT_RESET.iter().map(|x| *x);

        ot_device_proxy.send_frame(&mut tx_frame_iter).expect("sending frame");
        ot_device_instance.tx_allowance -= 1;

        if ot_device_instance.tx_allowance & 1 == 0 {
            // If tx_allowance is even, the test component expects to receive an
            // OnReadyForSendFrame event and a frame.
            rx_frame = ot_device_instance
                .expect_one_rx_frame_event(
                    &mut ot_device_event_stream,
                    OT_RADIO_GET_FRAME_AND_TX_ALLOWANCE,
                )
                .await
                .expect("receiving events");
        } else {
            // If tx_allowance is odd, the test component expects to receive a frame.
            rx_frame = ot_device_instance
                .expect_one_rx_frame_event(&mut ot_device_event_stream, OT_RADIO_GET_FRAME)
                .await
                .expect("receiving event");
        }

        if (ot_device_instance.rx_allowance & 1) == 0 {
            // If rx_allowance is even, the test component is ready to receive two more frames.
            ot_device_proxy
                .ready_to_receive_frames(OT_RADIO_RX_ALLOWANCE_INC_VAL)
                .expect("setting receive frame num");
            ot_device_instance.rx_allowance += OT_RADIO_RX_ALLOWANCE_INC_VAL;
        }

        fx_log_info!("received #{} frame {:?}", i, rx_frame);
        assert_eq!(OT_RADIO_RESET_EVENT, &rx_frame[..OT_RADIO_RESET_EVENT_FOURTH_VAL_IDX]);
        assert!(
            rx_frame[OT_RADIO_RESET_EVENT_FOURTH_VAL_IDX] < OT_RADIO_RESET_EVENT_FOURTH_VAL_MAX
        );
    }
    fx_log_info!("received and validated frame");
}
