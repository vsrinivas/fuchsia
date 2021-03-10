// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_tap::{self as wlantap, WlantapPhyProxy},
    fuchsia_zircon::DurationNum,
    pin_utils::pin_mut,
    wlan_common::mac::{self, Bssid},
    wlan_hw_sim::*,
};

const BSSID: Bssid = Bssid([0x62, 0x73, 0x73, 0x66, 0x6f, 0x6f]);
const SSID: &[u8] = b"open";

fn build_event_handler(
    ssid: Vec<u8>,
    bssid: Bssid,
    phy: &WlantapPhyProxy,
) -> impl FnMut(wlantap::WlantapPhyEvent) + '_ {
    EventHandlerBuilder::new()
        .on_set_channel(
            Beacon::send_on_primary_channel(CHANNEL.primary, &phy).bssid(bssid).ssid(ssid),
        )
        .on_tx(MatchTx::new().on_mgmt(move |frame: &Vec<u8>| {
            match mac::MacFrame::parse(&frame[..], false) {
                Some(mac::MacFrame::Mgmt { mgmt_hdr, body, .. }) => {
                    match mac::MgmtBody::parse({ mgmt_hdr.frame_ctrl }.mgmt_subtype(), body) {
                        Some(mac::MgmtBody::Authentication { .. }) => {
                            send_open_authentication_success(&CHANNEL, &bssid, &phy)
                                .expect("Error sending fake authentication frame.");
                        }
                        Some(mac::MgmtBody::AssociationReq { .. }) => {
                            send_association_response(
                                &CHANNEL,
                                &bssid,
                                mac::StatusCode::REFUSED_TEMPORARILY,
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

async fn save_network_and_await_connection(
    wlan_controller: &mut fidl_policy::ClientControllerProxy,
    mut update_listener: &mut fidl_policy::ClientStateUpdatesRequestStream,
) {
    let network_config =
        fidl_policy::NetworkConfig::from(NetworkConfigBuilder::open().ssid(&SSID.to_vec()));
    wlan_controller
        .save_network(network_config)
        .await
        .expect("requesting network save")
        .expect("failed while saving network");

    // The next update the queue should be "Connecting"
    let expected_network_id = fidl_policy::NetworkIdentifier {
        ssid: SSID.to_vec(),
        type_: fidl_policy::SecurityType::None,
    };

    let update = wlan_hw_sim::get_update_from_client_listener(&mut update_listener).await;

    assert_eq!(
        update.networks.unwrap(),
        vec![fidl_policy::NetworkState {
            id: Some(expected_network_id.clone()),
            state: Some(fidl_policy::ConnectionState::Connecting),
            status: None,
            ..fidl_policy::NetworkState::EMPTY
        }]
    );

    // The next update the queue should be "Failed"
    let update = get_update_from_client_listener(&mut update_listener).await;
    assert_eq!(
        update.networks.unwrap(),
        vec![fidl_policy::NetworkState {
            id: Some(expected_network_id),
            state: Some(fidl_policy::ConnectionState::Failed),
            status: Some(fidl_policy::DisconnectStatus::ConnectionFailed),
            ..fidl_policy::NetworkState::EMPTY
        }]
    );
}

/// Test a client connect attempt fails if the association response contains a status code that is
/// not success.
#[fuchsia_async::run_singlethreaded(test)]
async fn connect_with_failed_association() {
    init_syslog();

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let (mut wlan_controller, mut update_listener) = wlan_hw_sim::init_client_controller().await;
    let save_network_fut =
        save_network_and_await_connection(&mut wlan_controller, &mut update_listener);
    pin_mut!(save_network_fut);

    let proxy = helper.proxy();
    let () = helper
        .run_until_complete_or_timeout(
            240.seconds(),
            format!("connecting to {} ({:02X?})", String::from_utf8_lossy(SSID), BSSID),
            build_event_handler(SSID.to_vec(), BSSID, &proxy),
            save_network_fut,
        )
        .await;

    helper.stop().await;
}
