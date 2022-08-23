// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl_fuchsia_wlan_policy as fidl_policy,
    ieee80211::Bssid,
    pin_utils::pin_mut,
    tracing::info,
    wlan_common::{
        bss::Protection,
        ie::rsn::cipher::{CIPHER_CCMP_128, CIPHER_TKIP},
    },
    wlan_hw_sim::*,
    wlan_rsn,
};

const BSSID: &Bssid = &Bssid(*b"wpa2ok");
const AUTHENTICATOR_PASSWORD: &str = "goodpassword";
const SUPPLICANT_PASSWORD: &str = "badpassword";

async fn connect_future(
    client_controller: &fidl_policy::ClientControllerProxy,
    client_state_update_stream: &mut fidl_policy::ClientStateUpdatesRequestStream,
    security_type: fidl_policy::SecurityType,
    password: &str,
    expected_failure: fidl_policy::DisconnectStatus,
) {
    save_network(
        client_controller,
        &AP_SSID,
        security_type,
        password_or_psk_to_policy_credential(Some(password)),
    )
    .await;
    let network_identifier =
        fidl_policy::NetworkIdentifier { ssid: AP_SSID.to_vec(), type_: security_type };
    await_failed(client_state_update_stream, network_identifier.clone(), expected_failure).await;
    remove_network(
        client_controller,
        &AP_SSID,
        security_type,
        password_or_psk_to_policy_credential(Some(password)),
    )
    .await;
}

/// Test a client fails to connect to a network if the wrong credential type is
/// provided by the user. In particular, this occurs when a password should have
/// been provided and was not, or vice-versa.
#[fuchsia_async::run_singlethreaded(test)]
async fn connect_with_bad_password() {
    init_syslog();
    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found(&mut helper).await;

    let (client_controller, mut client_state_update_stream) = init_client_controller().await;

    // Test a client fails to connect to a network protected by WPA3-Personal if the wrong
    // password is provided. The DisconnectStatus::CredentialsFailed status should be
    // returned by policy.
    {
        let mut authenticator = Some(create_authenticator(
            BSSID,
            &AP_SSID,
            AUTHENTICATOR_PASSWORD,
            CIPHER_CCMP_128,
            Protection::Wpa3Personal,
            Protection::Wpa3Personal,
        ));
        let main_future = connect_future(
            &client_controller,
            &mut client_state_update_stream,
            fidl_policy::SecurityType::Wpa3,
            SUPPLICANT_PASSWORD,
            // TODO(fxbug.dev/60912): We can't yet detect incorrect WPA3 passwords
            fidl_policy::DisconnectStatus::ConnectionFailed,
        );
        pin_mut!(main_future);
        info!("Attempting to connect to a WPA3 Personal network with wrong password.");
        connect_to_ap(
            main_future,
            &mut helper,
            &AP_SSID,
            BSSID,
            &Protection::Wpa3Personal,
            &mut authenticator,
            &mut Some(wlan_rsn::rsna::UpdateSink::default()),
            // With WPA3, each connection attempt with a bad password takes ~8s to be
            // rejected. Since Policy does four attempts, we need more than the default
            // 30 second timeout here.
            Some(60),
        )
        .await;
        info!("As expected, the connection failed.");
    }

    // Test a client fails to connect to a network protected by WPA2-PSK if the wrong
    // password is provided. The DisconnectStatus::CredentialsFailed status should be
    // returned by policy.
    {
        let mut authenticator = Some(create_authenticator(
            BSSID,
            &AP_SSID,
            AUTHENTICATOR_PASSWORD,
            CIPHER_CCMP_128,
            Protection::Wpa2Personal,
            Protection::Wpa2Personal,
        ));
        let main_future = connect_future(
            &client_controller,
            &mut client_state_update_stream,
            fidl_policy::SecurityType::Wpa2,
            SUPPLICANT_PASSWORD,
            fidl_policy::DisconnectStatus::CredentialsFailed,
        );
        pin_mut!(main_future);
        info!("Attempting to connect to a WPA2 Personal network with wrong password.");
        connect_to_ap(
            main_future,
            &mut helper,
            &AP_SSID,
            BSSID,
            &Protection::Wpa2Personal,
            &mut authenticator,
            &mut Some(wlan_rsn::rsna::UpdateSink::default()),
            None,
        )
        .await;
        info!("As expected, the connection failed.");
    }

    // Test a client fails to connect to a network protected by WPA1-PSK if the wrong
    // password is provided. The DisconnectStatus::CredentialsFailed status should be
    // returned by policy.
    {
        let mut authenticator = Some(create_authenticator(
            BSSID,
            &AP_SSID,
            AUTHENTICATOR_PASSWORD,
            CIPHER_TKIP,
            Protection::Wpa1,
            Protection::Wpa1,
        ));
        let main_future = connect_future(
            &client_controller,
            &mut client_state_update_stream,
            fidl_policy::SecurityType::Wpa,
            SUPPLICANT_PASSWORD,
            fidl_policy::DisconnectStatus::CredentialsFailed,
        );
        pin_mut!(main_future);
        info!("Attempting to connect to a WPA1 Personal network with wrong password.");
        connect_to_ap(
            main_future,
            &mut helper,
            &AP_SSID,
            BSSID,
            &Protection::Wpa1,
            &mut authenticator,
            &mut Some(wlan_rsn::rsna::UpdateSink::default()),
            None,
        )
        .await;
        info!("As expected, the connection failed.");
    }

    helper.stop().await;
}
