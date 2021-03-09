// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_policy as wlan_policy,
    fidl_fuchsia_wlan_tap::{WlantapPhyEvent, WlantapPhyProxy},
    fuchsia_zircon::prelude::*,
    pin_utils::pin_mut,
    wlan_common::{
        assert_variant,
        bss::Protection,
        mac::{self, Bssid},
    },
    wlan_hw_sim::*,
    wlan_rsn::{
        self,
        key::exchange::Key,
        rsna::{SecAssocStatus, SecAssocUpdate},
    },
};

async fn test_actions(ssid: &'static [u8], passphrase: Option<&str>) {
    // Connect to the client policy service and get a client controller.
    let (client_controller, mut update_listener) = init_client_controller().await;

    // Store the config that was just passed in.
    let network_config = match passphrase {
        Some(passphrase) => NetworkConfigBuilder::protected(
            wlan_policy::SecurityType::Wpa2,
            &passphrase.as_bytes().to_vec(),
        ),
        None => NetworkConfigBuilder::open(),
    }
    .ssid(&ssid.to_vec());

    let result = client_controller
        .save_network(wlan_policy::NetworkConfig::from(network_config.clone()))
        .await
        .expect("saving network config");
    assert!(result.is_ok());

    // Issue a connect request.
    let mut network_id = wlan_policy::NetworkConfig::from(network_config).id.unwrap();
    client_controller.connect(&mut network_id).await.expect("connecting");

    // Wait until the policy layer indicates that the client has successfully connected.
    wait_until_client_state(&mut update_listener, |update| {
        has_ssid_and_state(update, ssid, wlan_policy::ConnectionState::Connected)
    })
    .await;
}

fn handle_phy_event(
    event: &WlantapPhyEvent,
    phy: &WlantapPhyProxy,
    ssid: &[u8],
    bssid: &mac::Bssid,
    protection: &Protection,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
    all_the_updates_sink: &mut wlan_rsn::rsna::UpdateSink,
) {
    match event {
        WlantapPhyEvent::SetChannel { args } => {
            handle_set_channel_event(&args, phy, ssid, bssid, protection)
        }
        WlantapPhyEvent::Tx { args } => {
            handle_tx_event(&args, phy, bssid, authenticator, |_, update_sink, _, _, _| {
                all_the_updates_sink.append(&mut update_sink.to_vec());
            })
        }
        _ => (),
    }
}

/// Test a client can connect to a network protected by WPA2-PSK by simulating an AP that
/// authenticates, associates, as well as initiating and completing EAPOL exchange.
/// In this test, no data is being sent after the link becomes up.
#[fuchsia_async::run_singlethreaded(test)]
async fn handle_tx_event_hooks() {
    init_syslog();

    const BSSID: Bssid = Bssid(*b"wpa2ok");
    const SSID: &[u8] = b"wpa2ssid";
    let passphrase = Some("wpa2good");

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let phy = helper.proxy();

    let test_actions_fut = test_actions(SSID, passphrase);
    pin_mut!(test_actions_fut);

    // Validate the connect request.
    let mut authenticator = passphrase.map(|p| create_wpa2_psk_authenticator(&BSSID, SSID, p));
    let protection = Protection::Wpa2Personal;

    let mut all_the_updates_sink = wlan_rsn::rsna::UpdateSink::default();

    helper
        .run_until_complete_or_timeout(
            30.seconds(),
            format!("connecting to {} ({:02X?})", String::from_utf8_lossy(SSID), BSSID),
            |event| {
                handle_phy_event(
                    &event,
                    &phy,
                    SSID,
                    &BSSID,
                    &protection,
                    &mut authenticator,
                    &mut all_the_updates_sink,
                );
            },
            test_actions_fut,
        )
        .await;

    // The post_process_auth_update hook for this test collects all of the updates that appear
    // in an update_sink while connecting to a WPA2 AP.
    assert_variant!(
        &all_the_updates_sink[0],
        SecAssocUpdate::Status(SecAssocStatus::PmkSaEstablished)
    );
    assert_variant!(&all_the_updates_sink[1], SecAssocUpdate::TxEapolKeyFrame(..));
    assert_variant!(&all_the_updates_sink[2], SecAssocUpdate::TxEapolKeyFrame(..));
    assert_variant!(&all_the_updates_sink[3], SecAssocUpdate::Key(Key::Ptk(..)));
    assert_variant!(&all_the_updates_sink[4], SecAssocUpdate::Key(Key::Gtk(..)));
    assert_variant!(
        &all_the_updates_sink[5],
        SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished)
    );

    helper.stop().await;
}
