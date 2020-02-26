// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_lowpan_spinel::DeviceMarker,
    fuchsia_component::client::{launch, launcher},
    fuchsia_syslog::{self as syslog, macros::*},
    ot_test_utils::spinel_device_utils::*,
};

const OT_RADIO_VERSION_RESP: &[u8; 13] =
    &[0x81, 0x06, 0x02, 0x4f, 0x50, 0x45, 0x4E, 0x54, 0x48, 0x52, 0x45, 0x41, 0x44];
const OT_RADIO_VERSION_REQ: &[u8; 3] = &[0x81, 0x02, 0x02];
const OT_RADIO_RESET_EVENT: &[u8; 3] = &[0x80, 0x06, 0x0];
const OT_RADIO_TX_ALLOWANCE_INIT_VAL: u32 = 4;
const OT_RADIO_RX_ALLOWANCE_INIT_VAL: u32 = 4;
const OT_RADIO_RX_ALLOWANCE_INC_VAL: u32 = 2;
const OT_RADIO_QUERY_NUM: u32 = 128;
const OT_RADIO_GET_FRAME_AND_TX_ALLOWANCE: u32 = 2;
const OT_RADIO_GET_FRAME: u32 = 1;

#[fuchsia_async::run_singlethreaded(test)]
async fn ot_stack_ncp_ver_query() {
    syslog::init_with_tags(&["ot_stack_ncp_ver_query"]).expect("Can't init logger");
    fx_log_info!("test start");
    let server_url = "fuchsia-pkg://fuchsia.com/ot-stack#meta/ot-stack.cmx".to_string();
    let arg = Some(vec!["/dev/class/ot-radio/000".to_string()]);
    let launcher = launcher().expect("Failed to open launcher service");
    let app = launch(&launcher, server_url, arg).expect("Failed to launch ot-stack service");
    let ot_stack_proxy =
        app.connect_to_service::<DeviceMarker>().expect("Failed to connect to ot-stack service");
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
    assert!(rx_frame[3] < 0x80);
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
