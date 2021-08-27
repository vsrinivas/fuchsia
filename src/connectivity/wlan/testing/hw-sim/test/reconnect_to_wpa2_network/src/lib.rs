// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl_fuchsia_wlan_policy::{self as fidl_policy},
    fidl_fuchsia_wlan_tap::{WlantapPhyEvent, WlantapPhyProxy},
    fuchsia_async::Task,
    fuchsia_zircon::prelude::*,
    futures::channel::oneshot,
    ieee80211::{Bssid, Ssid},
    pin_utils::pin_mut,
    wlan_common::{bss::Protection, mac},
    wlan_hw_sim::*,
    wlan_rsn::{
        self,
        rsna::{SecAssocStatus, SecAssocUpdate},
    },
};

async fn test_actions(
    ssid: &'static Ssid,
    password: Option<&str>,
    second_association_confirm_receiver: oneshot::Receiver<()>,
) {
    // Connect to the client policy service and get a client controller.
    let (_client_controller, mut client_state_update_stream) =
        save_network_and_wait_until_connected(ssid, fidl_policy::SecurityType::Wpa2, password)
            .await;

    let t = Task::spawn(async move {
        // Check updates from the policy. If we see disconnect, panic.
        wait_until_client_state(&mut client_state_update_stream, |update| {
            has_ssid_and_state(update, ssid, fidl_policy::ConnectionState::Disconnected)
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
    ssid: &Ssid,
    bssid: &Bssid,
    protection: &Protection,
    authenticator: &mut Option<wlan_rsn::Authenticator>,
    update_sink: &mut Option<wlan_rsn::rsna::UpdateSink>,
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
            update_sink,
            |authenticator,
             update_sink,
             channel,
             bssid,
             phy,
             ready_for_sae_frames,
             ready_for_eapol_frames| {
                process_tx_auth_updates(
                    authenticator,
                    update_sink,
                    channel,
                    bssid,
                    phy,
                    ready_for_sae_frames,
                    ready_for_eapol_frames,
                )?;

                // TODO(fxbug.dev/69580): Use Vec::drain_filter instead.
                let mut i = 0;
                while i < update_sink.len() {
                    if let SecAssocUpdate::Status(SecAssocStatus::EssSaEstablished) =
                        &update_sink[i]
                    {
                        if !*first_association_complete {
                            send_disassociate(
                                &channel,
                                bssid,
                                mac::ReasonCode::NO_MORE_STAS,
                                &phy,
                            )?;
                            authenticator.reset();
                            *first_association_complete = true;
                            return Ok(());
                        }
                        if let Some(second_association_confirm_sender) =
                            second_association_confirm_sender_wrapper.take()
                        {
                            second_association_confirm_sender.send(()).map_err(|e| {
                                format_err!(
                                    "Unable to send confirmation of second association: {:?}",
                                    e
                                )
                            })?;
                        }
                        update_sink.remove(i);
                        return Ok(());
                    } else {
                        i += 1;
                    }
                }
                Ok(())
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
    let passphrase = Some("wpa2good");

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let phy = helper.proxy();

    // Initialize state variables to track progress of asynchronous test
    let mut first_association_complete = false;
    let (second_association_confirm_sender, second_association_confirm_receiver) =
        oneshot::channel();
    let mut second_association_confirm_sender_wrapper = Some(second_association_confirm_sender);

    let test_actions_fut = test_actions(&AP_SSID, passphrase, second_association_confirm_receiver);
    pin_mut!(test_actions_fut);

    // Validate the connect request.
    let mut authenticator = passphrase.map(|p| create_wpa2_psk_authenticator(&BSSID, &AP_SSID, p));
    let mut update_sink = match authenticator {
        Some(_) => Some(wlan_rsn::rsna::UpdateSink::default()),
        None => None,
    };
    let protection = Protection::Wpa2Personal;

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
                    &mut first_association_complete,
                    &mut second_association_confirm_sender_wrapper,
                );
            },
            test_actions_fut,
        )
        .await;

    helper.stop().await;
}
