// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl_fuchsia_wlan_policy as fidl_policy, fidl_fuchsia_wlan_tap as wlantap,
    fuchsia_zircon::DurationNum,
    log::info,
    pin_utils::pin_mut,
    wlan_common::{bss::Protection, mac::Bssid},
    wlan_hw_sim::*,
};

const BSSID: &Bssid = &Bssid(*b"wpa2ok");
const SSID: &[u8] = b"wpa2ssid";
const AUTHENTICATOR_PASSWORD: &str = "goodpassword";
const SUPPLICANT_PASSWORD: &str = "badpassword";

async fn doomed_connect() -> fidl_policy::ClientStateSummary {
    let (wlan_controller, mut update_listener) = init_client_controller().await;

    info!("Saving network. SSID: {:?}, Password: {:?}", SSID, SUPPLICANT_PASSWORD);
    let network_config = create_wpa2_network_config(SSID, SUPPLICANT_PASSWORD);
    wlan_controller
        .save_network(network_config)
        .await
        .expect("save_network future failed")
        .expect("client controller failed to save network");

    // The next update in the queue should be "Connecting".
    let update = get_update_from_client_listener(&mut update_listener).await;
    assert_eq!(
        update.networks.unwrap(),
        vec![fidl_policy::NetworkState {
            id: Some(fidl_policy::NetworkIdentifier {
                ssid: SSID.to_vec(),
                type_: fidl_policy::SecurityType::Wpa2,
            }),
            state: Some(fidl_policy::ConnectionState::Connecting),
            status: None
        }]
    );
    info!("Connecting to SSID: {:?}", SSID);

    get_update_from_client_listener(&mut update_listener).await
}

/// Test a client fails to connect to a network protected by WPA2-PSK if the wrong
/// password is provided. The DisconnectStatus::CredentialsFailed status should be
/// returned by policy.
#[fuchsia_async::run_singlethreaded(test)]
async fn connect_with_bad_password() {
    init_syslog();

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let phy = helper.proxy();

    let mut authenticator =
        Some(create_wpa2_psk_authenticator(BSSID, SSID, AUTHENTICATOR_PASSWORD));
    let protection = Protection::Wpa2Personal;

    let doomed_connect_fut = doomed_connect();
    pin_mut!(doomed_connect_fut);

    let last_update = helper
        .run_until_complete_or_timeout(
            240.seconds(),
            format!("connecting to {} ({:02X?})", String::from_utf8_lossy(SSID), BSSID),
            EventHandlerBuilder::new()
                .on_set_channel(|args: &wlantap::SetChannelArgs| {
                    handle_set_channel_event(args, &phy, SSID, BSSID, &protection);
                })
                .on_tx(|args: &wlantap::TxArgs| {
                    handle_tx_event(args, &phy, BSSID, &mut authenticator);
                })
                .build(),
            doomed_connect_fut,
        )
        .await;

    // The last update in the queue should be "Failed" with a "CredentialsFailed" status.
    assert_eq!(
        last_update.networks.as_ref().unwrap(),
        &vec![fidl_policy::NetworkState {
            id: Some(fidl_policy::NetworkIdentifier {
                ssid: SSID.to_vec(),
                type_: fidl_policy::SecurityType::Wpa2,
            }),
            state: Some(fidl_policy::ConnectionState::Failed),
            status: Some(fidl_policy::DisconnectStatus::CredentialsFailed)
        }]
    );
    info!(
        "As expected, failed to connect due to {:?}.",
        last_update.networks.unwrap()[0].status.unwrap()
    );

    helper.stop().await;
}
