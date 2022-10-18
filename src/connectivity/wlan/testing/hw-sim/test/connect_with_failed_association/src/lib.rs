// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_tap::{self as wlantap, WlantapPhyProxy},
    fuchsia_zircon::DurationNum,
    ieee80211::{Bssid, Ssid},
    pin_utils::pin_mut,
    wlan_common::{
        bss::Protection,
        channel::{Cbw, Channel},
        mac,
    },
    wlan_hw_sim::*,
};

const BSSID: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f]);

fn build_event_handler<'a>(
    ssid: &'a Ssid,
    bssid: Bssid,
    phy: &'a WlantapPhyProxy,
) -> impl FnMut(wlantap::WlantapPhyEvent) + 'a {
    EventHandlerBuilder::new()
        .on_start_scan(ScanResults::new(
            &phy,
            vec![BeaconInfo {
                channel: Channel::new(1, Cbw::Cbw20),
                bssid,
                ssid: ssid.clone(),
                protection: Protection::Wpa2Personal,
                rssi_dbm: -30,
                beacon_or_probe: BeaconOrProbeResp::Beacon,
            }],
        ))
        .on_tx(MatchTx::new().on_mgmt(move |frame: &Vec<u8>| {
            match mac::MacFrame::parse(&frame[..], false) {
                Some(mac::MacFrame::Mgmt { mgmt_hdr, body, .. }) => {
                    match mac::MgmtBody::parse({ mgmt_hdr.frame_ctrl }.mgmt_subtype(), body) {
                        Some(mac::MgmtBody::Authentication { .. }) => {
                            send_open_authentication_success(
                                &Channel::new(1, Cbw::Cbw20),
                                &bssid,
                                &phy,
                            )
                            .expect("Error sending fake authentication frame.");
                        }
                        Some(mac::MgmtBody::AssociationReq { .. }) => {
                            send_association_response(
                                &Channel::new(1, Cbw::Cbw20),
                                &bssid,
                                fidl_ieee80211::StatusCode::RefusedTemporarily.into(),
                                &phy,
                            )
                            .expect("Error sending fake association response with failure");
                        }
                        _ => (),
                    }
                }
                _ => (),
            }
        }))
        .build()
}

async fn save_network_and_await_failed_connection(
    client_controller: &mut fidl_policy::ClientControllerProxy,
    client_state_update_stream: &mut fidl_policy::ClientStateUpdatesRequestStream,
) {
    save_network(
        client_controller,
        &AP_SSID,
        fidl_policy::SecurityType::None,
        password_or_psk_to_policy_credential::<String>(None),
    )
    .await;
    let network_identifier = fidl_policy::NetworkIdentifier {
        ssid: AP_SSID.to_vec(),
        type_: fidl_policy::SecurityType::None,
    };
    await_failed(
        client_state_update_stream,
        network_identifier.clone(),
        fidl_policy::DisconnectStatus::ConnectionFailed,
    )
    .await;
}

/// Test a client connect attempt fails if the association response contains a status code that is
/// not success.
#[fuchsia_async::run_singlethreaded(test)]
async fn connect_with_failed_association() {
    init_syslog();

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found(&mut helper).await;

    let (mut client_controller, mut client_state_update_stream) =
        wlan_hw_sim::init_client_controller().await;
    let save_network_fut = save_network_and_await_failed_connection(
        &mut client_controller,
        &mut client_state_update_stream,
    );
    pin_mut!(save_network_fut);

    let proxy = helper.proxy();
    let () = helper
        .run_until_complete_or_timeout(
            240.seconds(),
            format!("connecting to {} ({:02X?})", AP_SSID.to_string_not_redactable(), BSSID),
            build_event_handler(&AP_SSID, BSSID, &proxy),
            save_network_fut,
        )
        .await;

    helper.stop().await;
}
