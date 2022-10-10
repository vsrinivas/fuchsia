// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl_fuchsia_wlan_policy as fidl_policy,
    ieee80211::{Bssid, Ssid},
    pin_utils::pin_mut,
    tracing::info,
    wlan_common::{
        bss::Protection,
        ie::rsn::cipher::{Cipher, CIPHER_CCMP_128, CIPHER_TKIP},
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
    ssid: &Ssid,
    password: &str,
    expected_failure: fidl_policy::DisconnectStatus,
) {
    save_network(
        client_controller,
        ssid,
        security_type,
        password_or_psk_to_policy_credential(Some(password)),
    )
    .await;
    let network_identifier =
        fidl_policy::NetworkIdentifier { ssid: ssid.to_vec(), type_: security_type };
    await_failed(client_state_update_stream, network_identifier.clone(), expected_failure).await;
    remove_network(
        client_controller,
        ssid,
        security_type,
        password_or_psk_to_policy_credential(Some(password)),
    )
    .await;
}

async fn run_bad_password_test(
    client_controller: &fidl_policy::ClientControllerProxy,
    client_state_update_stream: &mut fidl_policy::ClientStateUpdatesRequestStream,
    test_helper: &mut test_utils::TestHelper,
    bssid: &Bssid,
    ssid: &Ssid,
    authenticator_pass: &str,
    suplicant_pass: &str,
    cipher: Cipher,
    protection: Protection,
    policy_security_type: fidl_policy::SecurityType,
    expected_failure: fidl_policy::DisconnectStatus,
    timeout: Option<i64>,
) {
    let mut authenticator =
        Some(create_authenticator(bssid, ssid, authenticator_pass, cipher, protection, protection));

    let main_fut = connect_future(
        client_controller,
        client_state_update_stream,
        policy_security_type,
        ssid,
        suplicant_pass,
        expected_failure,
    );
    pin_mut!(main_fut);
    info!(
        "Attempting to connect to a network with {:?} security using the wrong password.",
        protection
    );
    connect_to_ap(
        main_fut,
        test_helper,
        ssid,
        bssid,
        &protection,
        &mut authenticator,
        &mut Some(wlan_rsn::rsna::UpdateSink::default()),
        timeout,
    )
    .await;
    info!("As expected, the connection failed.")
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
    run_bad_password_test(
        &client_controller,
        &mut client_state_update_stream,
        &mut helper,
        BSSID,
        &Ssid::try_from("wpa3network").unwrap(),
        AUTHENTICATOR_PASSWORD,
        SUPPLICANT_PASSWORD,
        CIPHER_CCMP_128,
        Protection::Wpa3Personal,
        fidl_policy::SecurityType::Wpa3,
        fidl_policy::DisconnectStatus::ConnectionFailed,
        Some(60),
    )
    .await;

    // Test a client fails to connect to a network protected by WPA2-PSK if the wrong
    // password is provided. The DisconnectStatus::CredentialsFailed status should be
    // returned by policy.
    run_bad_password_test(
        &client_controller,
        &mut client_state_update_stream,
        &mut helper,
        BSSID,
        &Ssid::try_from("wpa2network").unwrap(),
        AUTHENTICATOR_PASSWORD,
        SUPPLICANT_PASSWORD,
        CIPHER_CCMP_128,
        Protection::Wpa2Personal,
        fidl_policy::SecurityType::Wpa2,
        fidl_policy::DisconnectStatus::CredentialsFailed,
        None,
    )
    .await;

    // Test a client fails to connect to a network protected by WPA1-PSK if the wrong
    // password is provided. The DisconnectStatus::CredentialsFailed status should be
    // returned by policy.
    run_bad_password_test(
        &client_controller,
        &mut client_state_update_stream,
        &mut helper,
        BSSID,
        &Ssid::try_from("wpa1network").unwrap(),
        AUTHENTICATOR_PASSWORD,
        SUPPLICANT_PASSWORD,
        CIPHER_TKIP,
        Protection::Wpa1,
        fidl_policy::SecurityType::Wpa,
        fidl_policy::DisconnectStatus::CredentialsFailed,
        None,
    )
    .await;

    helper.stop().await;
}
