// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::*, fidl_fuchsia_lowpan_spinel::DeviceProxy, fuchsia_syslog::macros::*,
    ot_test_utils::spinel_device_utils::*,
};

pub async fn ot_stack_ncp_ver_query(ot_stack_proxy: DeviceProxy) {
    let mut ot_stack_event_stream = ot_stack_proxy.take_event_stream();
    ot_stack_proxy.open().await.expect("sending FIDL req").expect("getting FIDL resp");

    let mut ot_stack_instance = OtRadioDevice { tx_allowance: 0, rx_allowance: 0 };
    ot_stack_instance.tx_allowance =
        expect_on_ready_for_send_frame_event(&mut ot_stack_event_stream)
            .await
            .expect("receiving frame");
    assert_eq!(ot_stack_instance.tx_allowance, OT_RADIO_TX_ALLOWANCE_INIT_VAL);

    ot_stack_proxy
        .ready_to_receive_frames(OT_RADIO_RX_ALLOWANCE_INIT_VAL)
        .expect("setting receive frame num");
    ot_stack_instance.rx_allowance = OT_RADIO_TX_ALLOWANCE_INIT_VAL;

    let mut rx_frame =
        expect_on_receive_event(&mut ot_stack_event_stream).await.expect("receiving event");
    fx_log_info!("received frame {:?}", rx_frame);
    assert_eq!(OT_RADIO_RESET_EVENT, &rx_frame[..3]);
    assert!(rx_frame[3] & 0xF0 == 0x70);
    ot_stack_instance.rx_allowance -= 1;
    for i in 0..OT_RADIO_QUERY_NUM {
        let mut tx_frame_iter = OT_RADIO_VERSION_REQ.iter().map(|x| *x);
        ot_stack_proxy.send_frame(&mut tx_frame_iter).expect("sending frame");

        ot_stack_instance.tx_allowance -= 1;
        fx_log_info!("ot_stack_instance.tx_allowance {}", ot_stack_instance.tx_allowance);
        if ot_stack_instance.tx_allowance & 1 == 0 {
            rx_frame = ot_stack_instance
                .expect_one_rx_frame_event(
                    &mut ot_stack_event_stream,
                    OT_RADIO_GET_FRAME_AND_TX_ALLOWANCE,
                )
                .await
                .expect("receiving events");
        } else {
            rx_frame = ot_stack_instance
                .expect_one_rx_frame_event(&mut ot_stack_event_stream, OT_RADIO_GET_FRAME)
                .await
                .expect("receiving event");
        }
        fx_log_info!("ot_stack_instance.rx_allowance {}", ot_stack_instance.rx_allowance);
        if (ot_stack_instance.rx_allowance & 1) == 0 {
            ot_stack_proxy
                .ready_to_receive_frames(OT_RADIO_RX_ALLOWANCE_INC_VAL)
                .expect("setting receive frame num");
            ot_stack_instance.rx_allowance += OT_RADIO_RX_ALLOWANCE_INC_VAL;
        }
        fx_log_info!("received #{} frame {:?}", i, rx_frame);
        assert_eq!(OT_RADIO_VERSION_RESP, &rx_frame[..13]);
    }
    fx_log_info!("received and validated frame");
}
