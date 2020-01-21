// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_common::WlanChan,
    fidl_fuchsia_wlan_device_service::DeviceServiceMarker,
    fidl_fuchsia_wlan_sme::StartApResultCode,
    fidl_fuchsia_wlan_tap::{WlantapPhyEvent, WlantapPhyProxy},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::DurationNum,
    futures::{channel::oneshot, poll, StreamExt},
    std::panic,
    wlan_common::{assert_variant, data_writer, ie, mac, mgmt_writer},
    wlan_hw_sim::*,
};

fn send_probe_req(
    channel: &WlanChan,
    bss_id: &mac::Bssid,
    proxy: &WlantapPhyProxy,
) -> Result<(), anyhow::Error> {
    let mut frame_buf = vec![];

    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::MGMT)
        .with_mgmt_subtype(mac::MgmtSubtype::PROBE_REQ);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(123);
    mgmt_writer::write_mgmt_hdr(
        &mut frame_buf,
        mgmt_writer::mgmt_hdr_to_ap(frame_ctrl, *bss_id, CLIENT_MAC_ADDR, seq_ctrl),
        None,
    )?;

    ie::write_ssid(&mut frame_buf, b"fuchsia")?;
    // tx_vec_idx:                                _     _     _   129   130     _   131   132
    ie::write_supported_rates(&mut frame_buf, &[0x82, 0x84, 0x8b, 0x0c, 0x12, 0x96, 0x18, 0x24])?;
    // tx_vec_idx:                                133 134 basic_135  136
    ie::write_ext_supported_rates(&mut frame_buf, &[48, 72, 128 + 96, 108])?;

    proxy.rx(0, &mut frame_buf.into_iter(), &mut create_rx_info(channel, 0))?;
    Ok(())
}

fn send_power_mgmt_frame(
    channel: &WlanChan,
    bss_id: &mac::Bssid,
    proxy: &WlantapPhyProxy,
    power_mgmt: mac::PowerState,
) -> Result<(), anyhow::Error> {
    let mut frame_buf = vec![];

    let frame_ctrl = mac::FrameControl(0)
        .with_frame_type(mac::FrameType::DATA)
        .with_to_ds(true)
        .with_power_mgmt(power_mgmt);
    let seq_ctrl = mac::SequenceControl(0).with_seq_num(123);
    data_writer::write_data_hdr(
        &mut frame_buf,
        mac::FixedDataHdrFields {
            frame_ctrl,
            duration: 0,
            addr1: bss_id.0,
            addr2: CLIENT_MAC_ADDR,
            addr3: bss_id.0,
            seq_ctrl,
        },
        mac::OptionalDataHdrFields { qos_ctrl: None, addr4: None, ht_ctrl: None },
    )?;

    proxy.rx(0, &mut frame_buf.into_iter(), &mut create_rx_info(channel, 0))?;
    Ok(())
}

/// Test WLAN AP implementation by simulating a client that sends out authentication and
/// association *request* frames. Verify AP responds correctly with authentication and
/// association *response* frames, respectively.
#[fuchsia_async::run_singlethreaded(test)]
async fn ap_power_save() {
    // Connect to WLAN device service and start watching for new device
    let wlan_service =
        connect_to_service::<DeviceServiceMarker>().expect("Failed to connect to wlan service");
    let mut dc = CreateDeviceHelper::new(&wlan_service);

    // Create wlantap PHY
    let (mut helper, iface_id) =
        dc.create_device(default_wlantap_config_ap()).await.expect("create ap");
    let proxy = helper.proxy();

    // Start AP
    let sme = get_ap_sme(&wlan_service, iface_id).await;
    let mut config = default_ap_config();
    let result_code = sme.start(&mut config).await.expect("expect start ap result code");
    assert_eq!(result_code, StartApResultCode::Success);

    send_client_authentication(&mut vec![], &CHANNEL, &AP_MAC_ADDR, &proxy)
        .expect("send_client_authentication");
    verify_auth_resp(&mut helper).await;

    send_association_request(&mut vec![], &CHANNEL, &AP_MAC_ADDR, &proxy)
        .expect("send_association_request");
    verify_assoc_resp(&mut helper).await;

    let mut eth_client = create_eth_client(&AP_MAC_ADDR.0)
        .await
        .expect(&format!("creating ethernet client: {:?}", &AP_MAC_ADDR.0))
        .expect("eth client");

    send_fake_eth_frame(CLIENT_MAC_ADDR, ETH_DST_MAC, b"test1", &mut eth_client).await;
    let _ = poll!(eth_client.get_stream().next());
    verify_data(&mut helper, &[170, 170, 3, 0, 0, 0, 8, 0, 116, 101, 115, 116, 49][..]).await;

    send_power_mgmt_frame(&CHANNEL, &AP_MAC_ADDR, &proxy, mac::PowerState::DOZE)
        .expect("send_power_mgmt_frame");

    // Send a probe request to synchronize after dozing (this is a hack):
    //
    // Test "Client"       AP
    //      |              |
    //      |------------->|
    //      | Doze frame   |
    //      |              |
    //      |------------->|
    //      | Probe Req    |
    //      |              |
    //      |<-------------|
    //      | Probe Resp   |
    //      | (syncs here) |
    //
    // TODO: Make WlantapPhyProxy.Rx sync.
    send_probe_req(&CHANNEL, &AP_MAC_ADDR, &proxy).expect("send_probe_req");
    let (sender, receiver) = oneshot::channel::<()>();
    let mut sender = Some(sender);
    helper
        .run_until_complete_or_timeout(
            std::i64::MAX.nanos(),
            "wait for probe response",
            move |event| match event {
                WlantapPhyEvent::Tx { .. } => {
                    sender.take().map(|s| s.send(()));
                }
                _ => {}
            },
            receiver,
        )
        .await
        .unwrap_or_else(|oneshot::Canceled| panic!());

    send_fake_eth_frame(CLIENT_MAC_ADDR, ETH_DST_MAC, b"test2", &mut eth_client).await;
    let _ = poll!(eth_client.get_stream().next());

    send_fake_eth_frame(CLIENT_MAC_ADDR, ETH_DST_MAC, b"test3", &mut eth_client).await;
    let _ = poll!(eth_client.get_stream().next());

    // Put the client into awake mode.
    send_power_mgmt_frame(&CHANNEL, &AP_MAC_ADDR, &proxy, mac::PowerState::AWAKE)
        .expect("send_power_mgmt_frame");

    let counter = std::sync::Arc::new(std::sync::Mutex::new(0));
    let counter_clone = counter.clone();

    let (sender, receiver) = oneshot::channel::<()>();
    let mut sender = Some(sender);
    helper
        .run_until_complete_or_timeout(
            std::i64::MAX.nanos(),
            "wait for frame",
            move |event| match event {
                WlantapPhyEvent::Tx { args } => {
                    if let Some(mac::MacFrame::Data { fixed_fields, .. }) =
                        mac::MacFrame::parse(&args.packet.data[..], false)
                    {
                        let mut n = counter_clone.lock().unwrap();
                        *n += 1;

                        if *n == 2 {
                            // Second frame, last frame.
                            assert!(!{ fixed_fields.frame_ctrl }.more_data());
                            sender.take().map(|s| s.send(()));
                        } else {
                            // First frame, should be marked with more_data as a second frame
                            // follows.
                            assert!({ fixed_fields.frame_ctrl }.more_data());
                        }
                    }
                }
                _ => {}
            },
            receiver,
        )
        .await
        .unwrap_or_else(|_| panic!());
}

async fn verify_auth_resp(helper: &mut test_utils::TestHelper) {
    let (sender, receiver) = oneshot::channel::<()>();
    let mut sender = Some(sender);
    let event_handler = move |event| match event {
        WlantapPhyEvent::Tx { args } => {
            if let Some(mac::MacFrame::Mgmt { mgmt_hdr, body, .. }) =
                mac::MacFrame::parse(&args.packet.data[..], false)
            {
                assert_variant!(
                    mac::MgmtBody::parse({ mgmt_hdr.frame_ctrl }.mgmt_subtype(), body),
                    Some(mac::MgmtBody::Authentication { auth_hdr, .. }) => {
                        assert_eq!({ auth_hdr.status_code }, mac::StatusCode::SUCCESS);
                        sender.take().map(|s| s.send(()));
                    },
                    "expected authentication frame"
                );
            }
        }
        _ => {}
    };
    helper
        .run_until_complete_or_timeout(
            std::i64::MAX.nanos(),
            "waiting for authentication response",
            event_handler,
            receiver,
        )
        .await
        .unwrap_or_else(|oneshot::Canceled| panic!());
}

async fn verify_assoc_resp(helper: &mut test_utils::TestHelper) {
    let (sender, receiver) = oneshot::channel::<()>();
    let mut sender = Some(sender);
    let event_handler = move |event| match event {
        WlantapPhyEvent::Tx { args } => {
            if let Some(mac::MacFrame::Mgmt { mgmt_hdr, body, .. }) =
                mac::MacFrame::parse(&args.packet.data[..], false)
            {
                match mac::MgmtBody::parse({ mgmt_hdr.frame_ctrl }.mgmt_subtype(), body) {
                    Some(mac::MgmtBody::AssociationResp { assoc_resp_hdr, .. }) => {
                        assert_eq!({ assoc_resp_hdr.status_code }, mac::StatusCode::SUCCESS);
                        sender.take().map(|s| s.send(()));
                    }
                    Some(mac::MgmtBody::Unsupported { subtype })
                        if subtype == mac::MgmtSubtype::ACTION => {}
                    other => {
                        // We might still be servicing things from the channel after the initial
                        // association frame but if the sender has not been taken, then we haven't
                        // serviced an association frame at all.
                        if sender.is_some() {
                            panic!("expected association response frame, got {:?}", other);
                        }
                    }
                }
            }
        }
        _ => {}
    };
    helper
        .run_until_complete_or_timeout(
            std::i64::MAX.nanos(),
            "waiting for association response",
            event_handler,
            receiver,
        )
        .await
        .unwrap_or_else(|oneshot::Canceled| panic!());
}

async fn verify_data(helper: &mut test_utils::TestHelper, expected_body: &[u8]) {
    let (sender, receiver) = oneshot::channel::<()>();
    let mut sender = Some(sender);
    let event_handler = move |event| match event {
        WlantapPhyEvent::Tx { args } => {
            if let Some(mac::MacFrame::Data { body, .. }) =
                mac::MacFrame::parse(&args.packet.data[..], false)
            {
                assert_eq!(body, expected_body);
                sender.take().map(|s| s.send(()));
            }
        }
        _ => {}
    };
    helper
        .run_until_complete_or_timeout(
            std::i64::MAX.nanos(),
            "waiting for data frame",
            event_handler,
            receiver,
        )
        .await
        .unwrap_or_else(|oneshot::Canceled| panic!());
}
