// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_tap::{WlantapPhyEvent, WlantapPhyProxy},
    fuchsia_zircon::prelude::*,
    futures::{channel::oneshot, join},
    ieee80211::{Bssid, Ssid},
    pin_utils::pin_mut,
    wlan_common::{
        assert_variant_at_idx,
        bss::Protection,
        channel::{Cbw, Channel},
        ie::rsn::cipher::CIPHER_CCMP_128,
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
    ssid: &Ssid,
    bssid: &Bssid,
    protection: &Protection,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
    update_sink: &mut Option<wlan_rsn::rsna::UpdateSink>,
    sec_assoc_update_trace: &mut Vec<SecAssocUpdate>,
    esssa_established_sender: &mut Option<oneshot::Sender<()>>,
) {
    match event {
        WlantapPhyEvent::StartScan { args } => handle_start_scan_event(
            &args,
            phy,
            &Beacon {
                channel: Channel::new(1, Cbw::Cbw20),
                bssid: bssid.clone(),
                ssid: ssid.clone(),
                protection: protection.clone(),
                rssi_dbm: -30,
            },
        ),
        WlantapPhyEvent::Tx { args } => handle_tx_event(
            &args,
            phy,
            ssid,
            bssid,
            protection,
            authenticator,
            update_sink,
            |authenticator,
             update_sink,
             channel,
             bssid,
             phy,
             ready_for_sae_frames,
             ready_for_eapol_frames| {
                sec_assoc_update_trace.append(&mut update_sink.to_vec());
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
                // the update_sink avoids adding entries to sec_assoc_update_trace twice.
                for update in update_sink.drain(..) {
                    if matches!(update, SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished)) {
                        if let Some(esssa_established_sender) = esssa_established_sender.take() {
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
    let password = Some("wpa2good");

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found(&mut helper).await;

    let phy = helper.proxy();

    // Validate the connect request.
    let mut authenticator = password.map(|p| {
        create_authenticator(
            &BSSID,
            &AP_SSID,
            p,
            CIPHER_CCMP_128,
            Protection::Wpa2Personal,
            Protection::Wpa2Personal,
        )
    });
    let mut update_sink = Some(wlan_rsn::rsna::UpdateSink::default());
    let protection = Protection::Wpa2Personal;

    let (esssa_established_sender, esssa_established_receiver) = oneshot::channel();
    let mut esssa_established_sender = Some(esssa_established_sender);
    let mut sec_assoc_update_trace: Vec<SecAssocUpdate> = vec![];

    // Wait for policy to indicate client connected AND the authenticator to indicate
    // EssSa established.
    let connect_to_network_fut = async {
        join!(
            save_network_and_wait_until_connected(
                &AP_SSID,
                fidl_policy::SecurityType::Wpa2,
                password_or_psk_to_policy_credential(password)
            ),
            async {
                esssa_established_receiver
                    .await
                    .expect("waiting for confirmation of EssSa established for authenticator")
            }
        )
    };
    pin_mut!(connect_to_network_fut);

    helper
        .run_until_complete_or_timeout(
            30.seconds(),
            format!("connecting to {} ({:02X?})", AP_SSID.to_string_not_redactable(), BSSID),
            |event| {
                handle_phy_event(
                    &event,
                    &phy,
                    &AP_SSID,
                    &BSSID,
                    &protection,
                    &mut authenticator,
                    &mut update_sink,
                    &mut sec_assoc_update_trace,
                    &mut esssa_established_sender,
                );
            },
            connect_to_network_fut,
        )
        .await;

    // The process_auth_update hook for this test collects all of the updates that appear
    // in an update_sink while connecting to a WPA2 AP. If association succeeds, then
    // the last six updates should be in the order asserted below.
    let n = sec_assoc_update_trace.len();
    assert!(
        n >= 6,
        "There should be at least 6 updates in a successful association:\n{:#?}",
        sec_assoc_update_trace
    );

    assert_variant_at_idx!(
        sec_assoc_update_trace,
        n - 6,
        SecAssocUpdate::Status(SecAssocStatus::PmkSaEstablished)
    );
    assert_variant_at_idx!(sec_assoc_update_trace, n - 5, SecAssocUpdate::TxEapolKeyFrame { .. });
    assert_variant_at_idx!(sec_assoc_update_trace, n - 4, SecAssocUpdate::TxEapolKeyFrame { .. });
    assert_variant_at_idx!(sec_assoc_update_trace, n - 3, SecAssocUpdate::Key(Key::Ptk(..)));
    assert_variant_at_idx!(sec_assoc_update_trace, n - 2, SecAssocUpdate::Key(Key::Gtk(..)));
    assert_variant_at_idx!(
        &sec_assoc_update_trace,
        n - 1,
        SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished)
    );

    helper.stop().await;
}
