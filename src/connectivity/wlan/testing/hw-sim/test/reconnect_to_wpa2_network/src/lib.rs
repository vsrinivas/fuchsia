// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_policy::{self as wlan_policy},
    fidl_fuchsia_wlan_tap::{WlantapPhyEvent, WlantapPhyProxy},
    fuchsia_async::Task,
    fuchsia_zircon::prelude::*,
    futures::channel::oneshot,
    pin_utils::pin_mut,
    wlan_common::{
        bss::Protection,
        mac::{self, Bssid},
    },
    wlan_hw_sim::*,
    wlan_rsn::{
        self,
        rsna::{SecAssocStatus, SecAssocUpdate},
    },
};

async fn test_actions(
    ssid: &'static [u8],
    passphrase: Option<&str>,
    second_association_confirm_receiver: oneshot::Receiver<()>,
) {
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

    let t = Task::spawn(async move {
        // Check updates from the policy. If we see disconnect, panic.
        wait_until_client_state(&mut update_listener, |update| {
            has_ssid_and_state(update, ssid, wlan_policy::ConnectionState::Disconnected)
        })
        .await;
        panic!("Policy saw a disconnect!");
    });

    second_association_confirm_receiver
        .await
        .expect("waiting for confirmation of second association");
    drop(t);
}

fn handle_phy_event(
    event: &WlantapPhyEvent,
    phy: &WlantapPhyProxy,
    ssid: &[u8],
    bssid: &mac::Bssid,
    protection: &Protection,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
    first_association_complete: &mut bool,
    second_association_confirm_sender_wrapper: &mut Option<oneshot::Sender<()>>,
) {
    match event {
        WlantapPhyEvent::SetChannel { args } => {
            handle_set_channel_event(&args, phy, ssid, bssid, protection)
        }
        WlantapPhyEvent::Tx { args } => handle_tx_event(
            &args,
            phy,
            bssid,
            authenticator,
            |authenticator, updates, channel, bssid, phy| {
                for update in updates {
                    if let SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished) = update {
                        if !*first_association_complete {
                            send_disassociate(&channel, bssid, mac::ReasonCode::NO_MORE_STAS, &phy)
                                .expect("Error sending disassociation frame.");
                            authenticator.reset();
                            *first_association_complete = true;
                            return;
                        }
                        if let Some(_) = second_association_confirm_sender_wrapper {
                            let second_association_confirm_sender =
                                second_association_confirm_sender_wrapper.take().unwrap();
                            second_association_confirm_sender.send(()).expect(
                                "second association complete, sending message to other future",
                            );
                        }
                        return;
                    }
                }
            },
        ),
        _ => (),
    }
}

/// Test a client can reconnect to a network protected by WPA2-PSK
/// after being disassociated without the policy layer noticing. SME
/// is capable of rebuilding the link on its own when the AP
/// disassociates the client. In this test, no data is being sent
/// after the link becomes up.
#[fuchsia_async::run_singlethreaded(test)]
async fn reconnect_to_wpa2_network() {
    init_syslog();

    const BSSID: Bssid = Bssid(*b"wpa2ok");
    const SSID: &[u8] = b"wpa2ssid";
    let passphrase = Some("wpa2good");

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let phy = helper.proxy();

    // Initialize state variables to track progress of asynchronous test
    let mut first_association_complete = false;
    let (second_association_confirm_sender, second_association_confirm_receiver) =
        oneshot::channel();
    let mut second_association_confirm_sender_wrapper = Some(second_association_confirm_sender);

    let test_actions_fut = test_actions(SSID, passphrase, second_association_confirm_receiver);
    pin_mut!(test_actions_fut);

    // Validate the connect request.
    let mut authenticator = passphrase.map(|p| create_wpa2_psk_authenticator(&BSSID, SSID, p));
    let protection = Protection::Wpa2Personal;

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
                    &mut first_association_complete,
                    &mut second_association_confirm_sender_wrapper,
                );
            },
            test_actions_fut,
        )
        .await;

    helper.stop().await;
}
