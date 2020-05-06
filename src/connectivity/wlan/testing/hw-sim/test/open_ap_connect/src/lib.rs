// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_device::MacRole::Ap,
    fidl_fuchsia_wlan_device_service::DeviceServiceMarker,
    fidl_fuchsia_wlan_sme::{ApConfig, StartApResultCode},
    fidl_fuchsia_wlan_tap::WlantapPhyEvent,
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon::DurationNum,
    futures::channel::oneshot,
    hex,
    std::panic,
    wlan_common::{
        assert_variant,
        channel::{Cbw, Phy},
        mac,
        test_utils::ExpectWithin,
        RadioConfig,
    },
    wlan_hw_sim::*,
};

/// Test WLAN AP implementation by simulating a client that sends out authentication and
/// association *request* frames. Verify AP responds correctly with authentication and
/// association *response* frames, respectively.
#[fuchsia_async::run_singlethreaded(test)]
async fn open_ap_connect() {
    // --- start test data block

    // frame 1 and 3 from ios12.1-connect-open-ap.pcapng
    #[rustfmt::skip]
        const AUTH_REQ_HEX: &str = "b0003a0170f11c052d7fdca90435e58c70f11c052d7ff07d000001000000dd0b0017f20a00010400000000dd09001018020000100000ed7895e7";
    let auth_req = hex::decode(AUTH_REQ_HEX).expect("fail to parse auth req hex");

    #[rustfmt::skip]
        const ASSOC_REQ_HEX: &str = "00003a0170f11c052d7fdca90435e58c70f11c052d7f007e210414000014465543485349412d544553542d4b4945542d4150010882848b962430486c32040c121860210202142402010ddd0b0017f20a00010400000000dd09001018020000100000debda9bb";
    let assoc_req = hex::decode(ASSOC_REQ_HEX).expect("fail to parse assoc req hex");
    // -- end test data block

    // Connect to WLAN device service and start watching for new device
    let wlan_service =
        connect_to_service::<DeviceServiceMarker>().expect("Failed to connect to wlan service");

    // Create wlantap PHY
    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_ap()).await;

    // Wait until iface is created from wlantap PHY
    let iface_id = get_first_matching_iface_id(&wlan_service, |iface| iface.role == Ap)
        .expect_within(5.seconds(), "No AP iface found in time")
        .await;

    let sme = get_ap_sme(&wlan_service, iface_id)
        .expect_within(5.seconds(), "timeout retrieving ap sme")
        .await;

    // Stop AP in case it was already started
    let () = sme.stop().await.expect("stopping any existing AP");

    // Start AP
    let mut config = ApConfig {
        ssid: String::from("fuchsia").into_bytes(),
        password: vec![],
        radio_cfg: RadioConfig::new(Phy::Ht, Cbw::Cbw20, CHANNEL.primary).to_fidl(),
    };
    let result_code = sme.start(&mut config).await.expect("expect start ap result code");
    assert_eq!(result_code, StartApResultCode::Success);

    // (client->ap) send a mock auth req
    let proxy = helper.proxy();
    proxy.rx(0, &auth_req, &mut create_rx_info(&CHANNEL, 0)).expect("cannot send auth req frame");

    // (ap->client) verify auth response frame was sent
    verify_auth_resp(&mut helper).await;

    // (client->ap) send a mock assoc req
    let proxy = helper.proxy();
    proxy.rx(0, &assoc_req, &mut create_rx_info(&CHANNEL, 0)).expect("cannot send assoc req frame");

    // (ap->client) verify assoc response frame was sent
    verify_assoc_resp(&mut helper).await;
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
            5.seconds(),
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
            5.seconds(),
            "waiting for association response",
            event_handler,
            receiver,
        )
        .await
        .unwrap_or_else(|oneshot::Canceled| panic!());
}
