// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_wlan_policy as fidl_policy, ieee80211::Bssid, log::info,
    wlan_common::bss::Protection, wlan_hw_sim::*,
};

/// Test connections against all modern (non-WEP/WPA1) BSS protection types.
#[fuchsia_async::run_singlethreaded(test)]
async fn connect_to_modern_wpa_network() {
    init_syslog();

    let bss = Bssid(*b"wpaok!");
    let password = Some("wpaisgood");

    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let combinations = vec![
        // TODO(fxbug.dev/101516): allow simulation for tkip
        // (Protection::Wpa1Wpa2PersonalTkipOnly, fidl_policy::SecurityType::Wpa),
        // (Protection::Wpa1Wpa2PersonalTkipOnly, fidl_policy::SecurityType::Wpa2),
        // (Protection::Wpa2PersonalTkipOnly, fidl_policy::SecurityType::Wpa),
        // (Protection::Wpa2PersonalTkipOnly, fidl_policy::SecurityType::Wpa2),
        (Protection::Wpa1Wpa2Personal, fidl_policy::SecurityType::Wpa),
        (Protection::Wpa1Wpa2Personal, fidl_policy::SecurityType::Wpa2),
        (Protection::Wpa2Personal, fidl_policy::SecurityType::Wpa),
        (Protection::Wpa2Personal, fidl_policy::SecurityType::Wpa2),
        // TODO(fxbug.dev/85817): reenable credential upgrading
        // (Protection::Wpa2Wpa3Personal, fidl_policy::SecurityType::Wpa),
        (Protection::Wpa2Wpa3Personal, fidl_policy::SecurityType::Wpa2),
        (Protection::Wpa2Wpa3Personal, fidl_policy::SecurityType::Wpa3),
        // TODO(fxbug.dev/85817): reenable credential upgrading
        // (Protection::Wpa3Personal, fidl_policy::SecurityType::Wpa),
        (Protection::Wpa3Personal, fidl_policy::SecurityType::Wpa2),
        (Protection::Wpa3Personal, fidl_policy::SecurityType::Wpa3),
    ];

    for (bss_protection, policy_security_type) in combinations {
        info!(
            "Starting connection test for {} with Policy security {:?}",
            bss_protection, policy_security_type
        );
        let () = connect_with_security_type(
            &mut helper,
            &AP_SSID,
            &bss,
            password,
            bss_protection,
            policy_security_type,
        )
        .await;

        // Remove the network and await disconnection
        let (client_controller, mut client_state_update_stream) =
            wlan_hw_sim::init_client_controller().await;
        remove_network(
            &client_controller,
            &AP_SSID,
            policy_security_type,
            password_to_policy_credential(password),
        )
        .await;
        wait_until_client_state(&mut client_state_update_stream, |update| {
            has_ssid_and_state(update, &AP_SSID, fidl_policy::ConnectionState::Disconnected)
        })
        .await;
    }
    helper.stop().await;
}
