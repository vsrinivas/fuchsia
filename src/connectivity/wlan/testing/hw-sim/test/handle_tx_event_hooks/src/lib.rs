// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_tap::{WlantapPhyEvent, WlantapPhyProxy},
    fuchsia_zircon::prelude::*,
    futures::channel::oneshot,
    pin_utils::pin_mut,
    wlan_common::{
        assert_variant,
        bss::Protection,
        format::SsidFmt as _,
        mac::{self, Bssid},
    },
    wlan_hw_sim::*,
    wlan_rsn::{
        self,
        key::exchange::Key,
        rsna::{SecAssocStatus, SecAssocUpdate},
    },
};

fn handle_phy_event(
    event: &WlantapPhyEvent,
    phy: &WlantapPhyProxy,
    ssid: &[u8],
    bssid: &mac::Bssid,
    protection: &Protection,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
    update_sink: &mut Option<wlan_rsn::rsna::UpdateSink>,
    all_auth_updates: &mut Vec<SecAssocUpdate>,
    esssa_established_sender_ptr: &mut Option<oneshot::Sender<()>>,
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
            update_sink,
            |authenticator,
             update_sink,
             channel,
             bssid,
             phy,
             ready_for_sae_frames,
             ready_for_eapol_frames| {
                all_auth_updates.append(&mut update_sink.to_vec());
                process_tx_auth_updates(
                    authenticator,
                    update_sink,
                    channel,
                    bssid,
                    phy,
                    ready_for_sae_frames,
                    ready_for_eapol_frames,
                )?;

                // After updates are processed, the update sink can be drained. A WPA2 EAPOL
                // exchange does not require persisting updates in the update_sink
                // since all TxEapolFrame updates appear after association. Draining
                // the update_sink avoids adding entries to all_auth_updates twice.
                for update in update_sink.drain(..) {
                    if let SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished) = update {
                        if let Some(esssa_established_sender) = esssa_established_sender_ptr.take()
                        {
                            esssa_established_sender.send(()).map_err(|e| {
                                format_err!(
                                    "Unable to send confirmation EssSa establishment: {:?}",
                                    e
                                )
                            })?;
                        }
                    }
                }
                Ok(())
            },
        ),
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

    let connect_to_network_fut =
        save_network_and_wait_until_connected(SSID, fidl_policy::SecurityType::Wpa2, passphrase);
    pin_mut!(connect_to_network_fut);

    // Validate the connect request.
    let mut authenticator = passphrase.map(|p| create_wpa2_psk_authenticator(&BSSID, SSID, p));
    let mut update_sink = Some(wlan_rsn::rsna::UpdateSink::default());
    let protection = Protection::Wpa2Personal;

    let (esssa_established_sender, esssa_established_receiver) = oneshot::channel();
    let mut esssa_established_sender_ptr = Some(esssa_established_sender);
    let mut all_auth_updates: Vec<SecAssocUpdate> = vec![];

    helper
        .run_until_complete_or_timeout(
            30.seconds(),
            format!("connecting to {} ({:02X?})", SSID.to_ssid_string_not_redactable(), BSSID),
            |event| {
                handle_phy_event(
                    &event,
                    &phy,
                    SSID,
                    &BSSID,
                    &protection,
                    &mut authenticator,
                    &mut update_sink,
                    &mut all_auth_updates,
                    &mut esssa_established_sender_ptr,
                );
            },
            connect_to_network_fut,
        )
        .await;

    esssa_established_receiver
        .await
        .expect("waiting for confirmation of EssSa established for authenticator");

    // The process_auth_update hook for this test collects all of the updates that appear
    // in an update_sink while connecting to a WPA2 AP.
    assert_variant!(&all_auth_updates[0], SecAssocUpdate::Status(SecAssocStatus::PmkSaEstablished));
    assert_variant!(&all_auth_updates[1], SecAssocUpdate::TxEapolKeyFrame { .. });
    assert_variant!(&all_auth_updates[2], SecAssocUpdate::TxEapolKeyFrame { .. });
    assert_variant!(&all_auth_updates[3], SecAssocUpdate::Key(Key::Ptk(..)));
    assert_variant!(&all_auth_updates[4], SecAssocUpdate::Key(Key::Gtk(..)));
    assert_variant!(&all_auth_updates[5], SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished));

    helper.stop().await;
}
