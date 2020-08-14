// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    fidl_fuchsia_wlan_policy as fidl_policy,
    log::info,
    pin_utils::pin_mut,
    wlan_common::{bss::Protection, mac::Bssid},
    wlan_hw_sim::*,
};

const BSSID: &Bssid = &Bssid(*b"bessid");
const SSID: &[u8] = b"test-ssid";
const AUTHENTICATOR_PASSWORD: &str = "goodpassword";

async fn connect_future(
    wlan_controller: &fidl_policy::ClientControllerProxy,
    listener_stream: &mut fidl_policy::ClientStateUpdatesRequestStream,
    security_type: fidl_policy::SecurityType,
    password: Option<&str>,
) {
    save_network_and_connect(wlan_controller, SSID, security_type, password).await;
    assert_connecting(
        listener_stream,
        fidl_policy::NetworkIdentifier { ssid: SSID.to_vec(), type_: security_type },
    )
    .await;
    assert_failed(
        listener_stream,
        fidl_policy::NetworkIdentifier { ssid: SSID.to_vec(), type_: security_type },
        fidl_policy::DisconnectStatus::CredentialsFailed,
    )
    .await;
    remove_network(wlan_controller, SSID, security_type, password).await;
}

/// Test a client fails to connect to a network if the wrong credential type is
/// provided by the user. In particular, this occurs when a password should have
/// been provided and was not, or vice-versa.
#[fuchsia_async::run_singlethreaded(test)]
async fn connecting_to_aps_with_wrong_credential_types() {
    init_syslog();
    let mut helper = test_utils::TestHelper::begin_test(default_wlantap_config_client()).await;
    let () = loop_until_iface_is_found().await;

    let (wlan_controller, mut listener_stream) = init_client_controller().await;

    // Test a client fails to connect to a network protected by WPA2-PSK if no
    // password is provided. The DisconnectStatus::CredentialsFailed status should be
    // returned by policy.
    {
        let mut authenticator =
            Some(create_wpa2_psk_authenticator(BSSID, SSID, AUTHENTICATOR_PASSWORD));
        let main_future = connect_future(
            &wlan_controller,
            &mut listener_stream,
            fidl_policy::SecurityType::None,
            None,
        );
        pin_mut!(main_future);
        info!("Attempting to connect to a WPA2 network with no password.");
        connect_to_ap(
            main_future,
            &mut helper,
            SSID,
            BSSID,
            &Protection::Wpa2Personal,
            &mut authenticator,
        )
        .await;
        info!("As expected, the connection failed.");
    }

    // Test a client fails to connect to a network protected by WPA1-PSK if no
    // password is provided. The DisconnectStatus::CredentialsFailed status should be
    // returned by policy.
    {
        let mut authenticator =
            Some(create_deprecated_wpa1_psk_authenticator(BSSID, SSID, AUTHENTICATOR_PASSWORD));
        let main_future = connect_future(
            &wlan_controller,
            &mut listener_stream,
            fidl_policy::SecurityType::None,
            None,
        );
        pin_mut!(main_future);
        info!("Attempting to connect to a WPA1 network with no password.");
        connect_to_ap(main_future, &mut helper, SSID, BSSID, &Protection::Wpa1, &mut authenticator)
            .await;
        info!("As expected, the connection failed.");
    }

    // Test a client fails to connect to a network protected by WEP if no
    // password is provided. The DisconnectStatus::CredentialsFailed status should be
    // returned by policy.
    {
        let main_future = connect_future(
            &wlan_controller,
            &mut listener_stream,
            fidl_policy::SecurityType::None,
            None,
        );
        pin_mut!(main_future);
        info!("Attempting to connect to a WEP network with no password.");
        connect_to_ap(main_future, &mut helper, SSID, BSSID, &Protection::Wep, &mut None).await;
        info!("As expected, the connection failed.");
    }

    // Test a client fails to connect to an open network if a
    // password is provided. The DisconnectStatus::CredentialsFailed status should be
    // returned by policy.
    {
        let main_future = connect_future(
            &wlan_controller,
            &mut listener_stream,
            fidl_policy::SecurityType::Wpa2,
            Some("password"),
        );
        pin_mut!(main_future);
        info!("Attempting to connect to an open network with a password.");
        connect_to_ap(main_future, &mut helper, SSID, BSSID, &Protection::Open, &mut None).await;
        info!("As expected, the connection failed.");
    }

    helper.stop().await;
}
