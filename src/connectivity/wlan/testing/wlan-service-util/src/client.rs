// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints;
use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_common::WlanMacRole;
use fidl_fuchsia_wlan_common_security as fidl_security;
use fidl_fuchsia_wlan_device_service::DeviceMonitorProxy;
use fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211;
use fidl_fuchsia_wlan_internal as fidl_internal;
use fidl_fuchsia_wlan_sme as fidl_sme;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon as zx;
use futures::stream::TryStreamExt;
use ieee80211::Ssid;
use std::convert::TryFrom;
use wlan_common::{
    bss::{BssDescription, Protection},
    security::{
        wep::WepKey,
        wpa::credential::{Passphrase, Psk},
        SecurityError,
    },
};

type WlanService = DeviceMonitorProxy;

/// Context for negotiating an `Authentication` (security protocol and credentials).
///
/// This ephemeral type joins a BSS description with credential data to negotiate an
/// `Authentication`. See the `TryFrom` implementation below.
#[derive(Clone, Debug)]
struct SecurityContext {
    pub bss: BssDescription,
    pub unparsed_credential_bytes: Vec<u8>,
}

/// Negotiates an `Authentication` from security information (given credentials and a BSS
/// description). The security protocol is based on the protection information described by the BSS
/// description. This is used to parse and validate the given credentials.
///
/// This is necessary, because the service utilities communicate directly with SME, which requires
/// more detailed information than the Policy layer.
impl TryFrom<SecurityContext> for fidl_security::Authentication {
    type Error = SecurityError;

    fn try_from(context: SecurityContext) -> Result<Self, SecurityError> {
        let SecurityContext { bss, unparsed_credential_bytes } = context;
        fn parse_wpa_credentials(
            unparsed_credential_bytes: Vec<u8>,
        ) -> Result<fidl_security::Credentials, SecurityError> {
            // Interpret credentials as a PSK first. This format is more specific.
            if let Ok(psk) = Psk::try_from(unparsed_credential_bytes.as_slice()) {
                Ok(fidl_security::Credentials::Wpa(fidl_security::WpaCredentials::Psk(psk.into())))
            } else {
                let passphrase = Passphrase::try_from(unparsed_credential_bytes)?;
                Ok(fidl_security::Credentials::Wpa(fidl_security::WpaCredentials::Passphrase(
                    passphrase.into(),
                )))
            }
        }

        match bss.protection() {
            // Unsupported.
            // TODO(fxbug.dev/92693): Implement conversions for WPA Enterprise.
            Protection::Unknown | Protection::Wpa2Enterprise | Protection::Wpa3Enterprise => {
                Err(SecurityError::Unsupported)
            }
            Protection::Open => {
                if unparsed_credential_bytes.len() == 0 {
                    Ok(fidl_security::Authentication {
                        protocol: fidl_security::Protocol::Open,
                        credentials: None,
                    })
                } else {
                    Err(SecurityError::Incompatible)
                }
            }
            Protection::Wep => WepKey::parse(unparsed_credential_bytes.as_slice())
                .map(|key| fidl_security::Authentication {
                    protocol: fidl_security::Protocol::Wep,
                    credentials: Some(Box::new(fidl_security::Credentials::Wep(
                        fidl_security::WepCredentials { key: key.into() },
                    ))),
                })
                .map_err(From::from),
            Protection::Wpa1 => {
                parse_wpa_credentials(unparsed_credential_bytes).map(|credentials| {
                    fidl_security::Authentication {
                        protocol: fidl_security::Protocol::Wpa1,
                        credentials: Some(Box::new(credentials)),
                    }
                })
            }
            Protection::Wpa1Wpa2PersonalTkipOnly
            | Protection::Wpa1Wpa2Personal
            | Protection::Wpa2PersonalTkipOnly
            | Protection::Wpa2Personal => {
                parse_wpa_credentials(unparsed_credential_bytes).map(|credentials| {
                    fidl_security::Authentication {
                        protocol: fidl_security::Protocol::Wpa2Personal,
                        credentials: Some(Box::new(credentials)),
                    }
                })
            }
            Protection::Wpa2Wpa3Personal => {
                // Use WPA2 for transitional networks when a PSK is supplied.
                if let Ok(psk) = Psk::try_from(unparsed_credential_bytes.as_slice()) {
                    Ok(fidl_security::Authentication {
                        protocol: fidl_security::Protocol::Wpa2Personal,
                        credentials: Some(Box::new(fidl_security::Credentials::Wpa(
                            fidl_security::WpaCredentials::Psk(psk.into()),
                        ))),
                    })
                } else {
                    Passphrase::try_from(unparsed_credential_bytes)
                        .map(|passphrase| fidl_security::Authentication {
                            protocol: fidl_security::Protocol::Wpa3Personal,
                            credentials: Some(Box::new(fidl_security::Credentials::Wpa(
                                fidl_security::WpaCredentials::Passphrase(passphrase.into()),
                            ))),
                        })
                        .map_err(From::from)
                }
            }
            Protection::Wpa3Personal => Passphrase::try_from(unparsed_credential_bytes)
                .map(|passphrase| fidl_security::Authentication {
                    protocol: fidl_security::Protocol::Wpa3Personal,
                    credentials: Some(Box::new(fidl_security::Credentials::Wpa(
                        fidl_security::WpaCredentials::Passphrase(passphrase.into()),
                    ))),
                })
                .map_err(From::from),
        }
    }
}

pub async fn get_sme_proxy(
    wlan_svc: &WlanService,
    iface_id: u16,
) -> Result<fidl_sme::ClientSmeProxy, Error> {
    let (sme_proxy, sme_remote) = endpoints::create_proxy()?;
    let result = wlan_svc
        .get_client_sme(iface_id, sme_remote)
        .await
        .context("error sending GetClientSme request")?;
    match result {
        Ok(()) => Ok(sme_proxy),
        Err(e) => Err(format_err!(
            "Failed to get client sme proxy for interface id {} with error {}",
            iface_id,
            e
        )),
    }
}

pub async fn get_first_sme(wlan_svc: &WlanService) -> Result<fidl_sme::ClientSmeProxy, Error> {
    let iface_id = super::get_first_iface(wlan_svc, WlanMacRole::Client)
        .await
        .context("failed to get iface")?;
    get_sme_proxy(&wlan_svc, iface_id).await
}

pub async fn connect(
    iface_sme_proxy: &fidl_sme::ClientSmeProxy,
    target_ssid: Ssid,
    target_pwd: Vec<u8>,
    target_bss_desc: fidl_internal::BssDescription,
) -> Result<bool, Error> {
    let (connection_proxy, connection_remote) = endpoints::create_proxy()?;

    // Create a `ConnectRequest` using the given network information.
    // Negotiate a security protocol based on the given credential and BSS metadata. Note that the
    // resulting `Authentication` may be invalid.
    let authentication = fidl_security::Authentication::try_from(SecurityContext {
        bss: BssDescription::try_from(target_bss_desc.clone())?,
        unparsed_credential_bytes: target_pwd,
    })?;
    let mut req = fidl_sme::ConnectRequest {
        ssid: target_ssid.clone().into(),
        bss_description: target_bss_desc,
        authentication,
        deprecated_scan_type: fidl_common::ScanType::Passive,
        multiple_bss_candidates: false, // only used for metrics, select an arbitrary value
    };

    let _result = iface_sme_proxy.connect(&mut req, Some(connection_remote))?;

    let connection_result_code = handle_connect_transaction(connection_proxy).await?;

    if !matches!(connection_result_code, fidl_ieee80211::StatusCode::Success) {
        fx_log_err!("Failed to connect to network: {:?}", connection_result_code);
        return Ok(false);
    }

    let client_status_response =
        iface_sme_proxy.status().await.context("failed to check status from sme_proxy")?;
    Ok(match client_status_response {
        fidl_sme::ClientStatusResponse::Connected(serving_ap_info) => {
            if serving_ap_info.ssid != target_ssid {
                fx_log_err!(
                    "Connected to wrong network: {:?}. Expected: {:?}.",
                    serving_ap_info.ssid.as_slice(),
                    target_ssid
                );
                false
            } else {
                true
            }
        }
        fidl_sme::ClientStatusResponse::Connecting(_) | fidl_sme::ClientStatusResponse::Idle(_) => {
            fx_log_err!(
                "Unexpected status {:?} after {:?}",
                client_status_response,
                connection_result_code
            );
            false
        }
    })
}

async fn handle_connect_transaction(
    connect_transaction: fidl_sme::ConnectTransactionProxy,
) -> Result<fidl_ieee80211::StatusCode, Error> {
    let mut event_stream = connect_transaction.take_event_stream();
    let mut result_code = fidl_ieee80211::StatusCode::RefusedReasonUnspecified;

    if let Some(evt) = event_stream
        .try_next()
        .await
        .context("failed to receive connect result before the channel was closed")?
    {
        match evt {
            fidl_sme::ConnectTransactionEvent::OnConnectResult { result } => {
                result_code = result.code;
            }
            other => {
                return Err(format_err!(
                    "Expected ConnectTransactionEvent::OnConnectResult event, got {:?}",
                    other
                ))
            }
        }
    }

    Ok(result_code)
}

pub async fn disconnect(iface_sme_proxy: &fidl_sme::ClientSmeProxy) -> Result<(), Error> {
    iface_sme_proxy
        .disconnect(fidl_sme::UserDisconnectReason::WlanServiceUtilTesting)
        .await
        .context("failed to trigger disconnect")?;

    // check the status and ensure we are not connected to or connecting to anything
    let client_status_response =
        iface_sme_proxy.status().await.context("failed to check status from sme_proxy")?;
    match client_status_response {
        fidl_sme::ClientStatusResponse::Connected(_)
        | fidl_sme::ClientStatusResponse::Connecting(_) => {
            Err(format_err!("Disconnect confirmation failed: {:?}", client_status_response))
        }
        fidl_sme::ClientStatusResponse::Idle(_) => Ok(()),
    }
}

pub async fn disconnect_all(wlan_svc: &WlanService) -> Result<(), Error> {
    let wlan_iface_ids =
        super::get_iface_list(wlan_svc).await.context("Connect: failed to get wlan iface list")?;

    let mut error_msg = format!("");
    for iface_id in wlan_iface_ids {
        let result = wlan_svc.query_iface(iface_id).await.context("querying iface info")?;

        match result {
            Ok(query_info) => {
                if query_info.role == WlanMacRole::Client {
                    let sme_proxy = get_sme_proxy(&wlan_svc, iface_id)
                        .await
                        .context("Disconnect all: failed to get iface sme proxy")?;
                    if let Err(e) = disconnect(&sme_proxy).await {
                        error_msg =
                            format!("{}Error disconnecting iface {}: {}\n", error_msg, iface_id, e);
                        fx_log_err!("disconnect_all: disconnect err on iface {}: {}", iface_id, e);
                    }
                }
            }
            Err(zx::sys::ZX_ERR_NOT_FOUND) => {
                error_msg = format!("{}no query response on iface {}\n", error_msg, iface_id);
                fx_log_err!("disconnect_all: iface query empty on iface {}", iface_id);
            }
            Err(e) => {
                error_msg = format!("{}failed querying iface {}: {}\n", error_msg, iface_id, e);
                fx_log_err!("disconnect_all: query err on iface {}: {}", iface_id, e);
            }
        }
    }
    if error_msg.is_empty() {
        Ok(())
    } else {
        Err(format_err!("{}", error_msg))
    }
}

pub async fn passive_scan(
    iface_sme_proxy: &fidl_sme::ClientSmeProxy,
) -> Result<Vec<fidl_sme::ScanResult>, Error> {
    iface_sme_proxy
        .scan(&mut fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {}))
        .await
        .context("error sending scan request")?
        .map_err(|scan_error_code| format_err!("Scan error: {:?}", scan_error_code))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::*,
        fidl::endpoints::RequestStream,
        fidl_fuchsia_wlan_common as fidl_common,
        fidl_fuchsia_wlan_common_security as fidl_security,
        fidl_fuchsia_wlan_device_service::{
            self as wlan_service, DeviceMonitorMarker, DeviceMonitorProxy, DeviceMonitorRequest,
            DeviceMonitorRequestStream,
        },
        fidl_fuchsia_wlan_sme::{
            ClientSmeMarker, ClientSmeRequest, ClientSmeRequestStream, Protection,
        },
        fuchsia_async::TestExecutor,
        futures::stream::{StreamExt, StreamFuture},
        futures::task::Poll,
        ieee80211::Ssid,
        pin_utils::pin_mut,
        rand::Rng as _,
        std::convert::{TryFrom, TryInto},
        wlan_common::{
            assert_variant,
            channel::{Cbw, Channel},
            fake_fidl_bss_description,
        },
    };

    fn generate_random_wpa2_bss_description() -> fidl_fuchsia_wlan_internal::BssDescription {
        let mut rng = rand::thread_rng();
        fidl_fuchsia_wlan_internal::BssDescription {
            bssid: (0..6).map(|_| rng.gen::<u8>()).collect::<Vec<u8>>().try_into().unwrap(),
            beacon_period: rng.gen::<u16>(),
            rssi_dbm: rng.gen::<i8>(),
            channel: fidl_common::WlanChannel {
                primary: rng.gen::<u8>(),
                cbw: match rng.gen_range(0..5) {
                    0 => fidl_common::ChannelBandwidth::Cbw20,
                    1 => fidl_common::ChannelBandwidth::Cbw40,
                    2 => fidl_common::ChannelBandwidth::Cbw40Below,
                    3 => fidl_common::ChannelBandwidth::Cbw80,
                    4 => fidl_common::ChannelBandwidth::Cbw160,
                    5 => fidl_common::ChannelBandwidth::Cbw80P80,
                    _ => panic!(),
                },
                secondary80: rng.gen::<u8>(),
            },
            snr_db: rng.gen::<i8>(),
            ..fake_fidl_bss_description!(Wpa2)
        }
    }

    fn extract_sme_server_from_get_client_sme_req_and_respond(
        exec: &mut TestExecutor,
        req_stream: &mut DeviceMonitorRequestStream,
        result: Result<(), zx::Status>,
    ) -> fidl_sme::ClientSmeRequestStream {
        let req = exec.run_until_stalled(&mut req_stream.next());

        let (responder, fake_sme_server) = assert_variant !(
            req,
            Poll::Ready(Some(Ok(DeviceMonitorRequest::GetClientSme{ iface_id:_, sme_server, responder})))
            => (responder, sme_server));

        // now send the response back
        responder
            .send(&mut result.map_err(|e| e.into_raw()))
            .expect("fake sme proxy response: send failed");

        // and return the stream
        // let sme_stream = fake_sme_server.into_stream().expect("sme server stream failed");
        // sme_stream
        fake_sme_server.into_stream().expect("sme server stream failed")
    }

    fn respond_to_get_client_sme_request(
        exec: &mut TestExecutor,
        req_stream: &mut DeviceMonitorRequestStream,
        result: Result<(), zx::Status>,
    ) {
        let req = exec.run_until_stalled(&mut req_stream.next());

        let responder = assert_variant !(
            req,
            Poll::Ready(Some(Ok(DeviceMonitorRequest::GetClientSme{ responder, ..})))
            => responder);

        // now send the response back
        responder
            .send(&mut result.map_err(|e| e.into_raw()))
            .expect("fake sme proxy response: send failed")
    }

    fn respond_to_client_sme_disconnect_request(
        exec: &mut TestExecutor,
        req_stream: &mut ClientSmeRequestStream,
    ) {
        let req = exec.run_until_stalled(&mut req_stream.next());
        let responder = assert_variant !(
            req,
            Poll::Ready(Some(Ok(ClientSmeRequest::Disconnect{ responder, .. })))
            => responder);

        // now send the response back
        responder.send().expect("fake disconnect response: send failed")
    }

    fn respond_to_client_sme_status_request(
        exec: &mut TestExecutor,
        req_stream: &mut ClientSmeRequestStream,
        status: &StatusResponse,
    ) {
        let req = exec.run_until_stalled(&mut req_stream.next());
        let responder = assert_variant !(
            req,
            Poll::Ready(Some(Ok(ClientSmeRequest::Status{ responder})))
            => responder);

        // Send appropriate status response
        match status {
            StatusResponse::Idle => {
                let mut response = fidl_sme::ClientStatusResponse::Idle(fidl_sme::Empty {});
                responder.send(&mut response).expect("Failed to send StatusResponse.");
            }
            StatusResponse::Connected => {
                let serving_ap_info =
                    create_serving_ap_info_using_ssid(Ssid::try_from([1, 2, 3, 4]).unwrap());
                let mut response = fidl_sme::ClientStatusResponse::Connected(serving_ap_info);
                responder.send(&mut response).expect("Failed to send StatusResponse.");
            }
            StatusResponse::Connecting => {
                let mut response = fidl_sme::ClientStatusResponse::Connecting(vec![1, 2, 3, 4]);
                responder.send(&mut response).expect("Failed to send StatusResponse.");
            }
        }
    }

    fn test_get_first_sme(iface_list: &[WlanMacRole]) -> Result<(), Error> {
        let (mut exec, proxy, mut req_stream) =
            crate::tests::setup_fake_service::<DeviceMonitorMarker>();
        let fut = get_first_sme(&proxy);
        pin_mut!(fut);

        let ifaces = (0..iface_list.len() as u16).collect();

        assert!(exec.run_until_stalled(&mut fut).is_pending());
        crate::tests::respond_to_query_iface_list_request(&mut exec, &mut req_stream, ifaces);

        for mac_role in iface_list {
            // iface query response
            assert!(exec.run_until_stalled(&mut fut).is_pending());

            crate::tests::respond_to_query_iface_request(
                &mut exec,
                &mut req_stream,
                *mac_role,
                Some([1, 2, 3, 4, 5, 6]),
            );

            if *mac_role == WlanMacRole::Client {
                // client sme proxy
                assert!(exec.run_until_stalled(&mut fut).is_pending());
                respond_to_get_client_sme_request(&mut exec, &mut req_stream, Ok(()));
                break;
            }
        }

        let _proxy = exec.run_singlethreaded(&mut fut)?;
        Ok(())
    }

    fn test_disconnect_all(iface_list: &[(WlanMacRole, StatusResponse)]) -> Result<(), Error> {
        let (mut exec, proxy, mut req_stream) =
            crate::tests::setup_fake_service::<DeviceMonitorMarker>();
        let fut = disconnect_all(&proxy);
        pin_mut!(fut);

        let ifaces = (0..iface_list.len() as u16).collect();

        assert!(exec.run_until_stalled(&mut fut).is_pending());
        crate::tests::respond_to_query_iface_list_request(&mut exec, &mut req_stream, ifaces);

        for (mac_role, status) in iface_list {
            // iface query response
            assert!(exec.run_until_stalled(&mut fut).is_pending());
            crate::tests::respond_to_query_iface_request(
                &mut exec,
                &mut req_stream,
                *mac_role,
                Some([1, 2, 3, 4, 5, 6]),
            );

            if *mac_role == WlanMacRole::Client {
                // Get the Client SME server (to send the responses for the following 2 SME requests)
                assert!(exec.run_until_stalled(&mut fut).is_pending());
                let mut fake_sme_server_stream =
                    extract_sme_server_from_get_client_sme_req_and_respond(
                        &mut exec,
                        &mut req_stream,
                        Ok(()),
                    );

                // Disconnect
                assert!(exec.run_until_stalled(&mut fut).is_pending());
                respond_to_client_sme_disconnect_request(&mut exec, &mut fake_sme_server_stream);

                assert!(exec.run_until_stalled(&mut fut).is_pending());

                // Send appropriate status response
                respond_to_client_sme_status_request(
                    &mut exec,
                    &mut fake_sme_server_stream,
                    status,
                );
            }
        }
        exec.run_singlethreaded(&mut fut)
    }

    // iface list contains an AP and a client. Test should pass
    #[test]
    fn check_get_client_sme_success() {
        let iface_list: Vec<WlanMacRole> = vec![WlanMacRole::Ap, WlanMacRole::Client];
        test_get_first_sme(&iface_list).expect("expect success but failed");
    }

    // iface list is empty. Test should fail
    #[test]
    fn check_get_client_sme_no_devices() {
        let iface_list: Vec<WlanMacRole> = Vec::new();
        test_get_first_sme(&iface_list).expect_err("expect fail but succeeded");
    }

    // iface list does not contain a client. Test should fail
    #[test]
    fn check_get_client_sme_no_clients() {
        let iface_list: Vec<WlanMacRole> = vec![WlanMacRole::Ap, WlanMacRole::Ap];
        test_get_first_sme(&iface_list).expect_err("expect fail but succeeded");
    }

    // test disconnect_all with a Client and an AP. Test should pass
    // as AP IF will be ignored and Client IF delete should succeed.
    #[test]
    fn check_disconnect_all_client_and_ap_success() {
        let iface_list: Vec<(WlanMacRole, StatusResponse)> = vec![
            (WlanMacRole::Ap, StatusResponse::Idle),
            (WlanMacRole::Client, StatusResponse::Idle),
        ];
        test_disconnect_all(&iface_list).expect("Expect success but failed")
    }

    // test disconnect_all with 2 Clients. Test should pass as both the
    // IFs are clients and both deletes should succeed.
    #[test]
    fn check_disconnect_all_all_clients_success() {
        let iface_list: Vec<(WlanMacRole, StatusResponse)> = vec![
            (WlanMacRole::Client, StatusResponse::Idle),
            (WlanMacRole::Client, StatusResponse::Idle),
        ];
        test_disconnect_all(&iface_list).expect("Expect success but failed");
    }

    // test disconnect_all with 2 Clients, one disconnect failure
    #[test]
    fn check_disconnect_all_all_clients_fail() {
        let iface_list: Vec<(WlanMacRole, StatusResponse)> = vec![
            (WlanMacRole::Ap, StatusResponse::Connected),
            (WlanMacRole::Client, StatusResponse::Connected),
        ];
        test_disconnect_all(&iface_list).expect_err("Expect fail but succeeded");
    }

    // test disconnect_all with no Clients
    #[test]
    fn check_disconnect_all_no_clients_success() {
        let iface_list: Vec<(WlanMacRole, StatusResponse)> =
            vec![(WlanMacRole::Ap, StatusResponse::Idle), (WlanMacRole::Ap, StatusResponse::Idle)];
        test_disconnect_all(&iface_list).expect("Expect success but failed");
    }

    #[test]
    fn list_ifaces_returns_iface_id_vector() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (wlan_monitor, server) = create_wlan_monitor_util();
        let mut next_device_monitor_req = server.into_future();

        let ifaces: Vec<u16> = vec![0, 1, 35, 36];

        let fut = get_iface_list(&wlan_monitor);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_iface_list_response(&mut exec, &mut next_device_monitor_req, ifaces.clone());

        let complete = exec.run_until_stalled(&mut fut);

        let list_response = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected an iface list response"),
        };

        let response = match list_response {
            Ok(response) => response,
            Err(_) => panic!("Expected a valid list response"),
        };

        // now verify the response
        assert_eq!(response, ifaces)
    }

    #[test]
    fn list_ifaces_properly_handles_zero_ifaces() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (wlan_monitor, server) = create_wlan_monitor_util();
        let mut next_device_monitor_req = server.into_future();

        // create the data to use in the response
        let iface_id_list: Vec<u16> = vec![];
        let iface_list_vec = vec![];

        let fut = get_iface_list(&wlan_monitor);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_iface_list_response(&mut exec, &mut next_device_monitor_req, iface_list_vec);

        let complete = exec.run_until_stalled(&mut fut);

        let list_response = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected an iface list response"),
        };

        let response = match list_response {
            Ok(response) => response,
            Err(_) => panic!("Expected a valid list response"),
        };

        // now verify the response
        assert_eq!(response, iface_id_list)
    }

    #[test]
    fn list_phys_returns_iface_id_vector() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (monitor_service, server) = create_wlan_monitor_util();
        let mut next_device_service_req = server.into_future();

        // create the data to use in the response
        let phy_id_list: Vec<u16> = vec![0, 1, 35, 36];
        let mut phy_list_vec = vec![];
        for id in &phy_id_list {
            phy_list_vec.push(*id);
        }

        let fut = get_phy_list(&monitor_service);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_phy_list_response(&mut exec, &mut next_device_service_req, phy_list_vec);

        let complete = exec.run_until_stalled(&mut fut);

        let list_response = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected an phy list response"),
        };

        let response = match list_response {
            Ok(response) => response,
            Err(_) => panic!("Expected a valid list response"),
        };

        // now verify the response
        assert_eq!(response, phy_id_list)
    }

    #[test]
    fn list_phys_properly_handles_zero_phys() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (monitor_service, server) = create_wlan_monitor_util();
        let mut next_device_service_req = server.into_future();

        // create the data to use in the response
        let phy_id_list: Vec<u16> = vec![];
        let phy_list_vec = vec![];

        let fut = get_phy_list(&monitor_service);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_phy_list_response(&mut exec, &mut next_device_service_req, phy_list_vec);

        let complete = exec.run_until_stalled(&mut fut);

        let list_response = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected an phy list response"),
        };

        let response = match list_response {
            Ok(response) => response,
            Err(_) => panic!("Expected a valid list response"),
        };

        // now verify the response
        assert_eq!(response, phy_id_list)
    }

    fn poll_device_monitor_req(
        exec: &mut TestExecutor,
        next_device_monitor_req: &mut StreamFuture<DeviceMonitorRequestStream>,
    ) -> Poll<DeviceMonitorRequest> {
        exec.run_until_stalled(next_device_monitor_req).map(|(req, stream)| {
            *next_device_monitor_req = stream.into_future();
            req.expect("did not expect the DeviceMonitorRequestStream to end")
                .expect("error polling device service request stream")
        })
    }

    fn send_iface_list_response(
        exec: &mut TestExecutor,
        server: &mut StreamFuture<wlan_service::DeviceMonitorRequestStream>,
        mut ifaces: Vec<u16>,
    ) {
        let responder = match poll_device_monitor_req(exec, server) {
            Poll::Ready(DeviceMonitorRequest::ListIfaces { responder }) => responder,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a ListIfaces request"),
        };

        // now send the response back
        let _result = responder.send(&mut ifaces[..]);
    }

    fn send_phy_list_response(
        exec: &mut TestExecutor,
        server: &mut StreamFuture<wlan_service::DeviceMonitorRequestStream>,
        phy_list_vec: Vec<u16>,
    ) {
        let responder = match poll_device_monitor_req(exec, server) {
            Poll::Ready(DeviceMonitorRequest::ListPhys { responder }) => responder,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a ListPhys request"),
        };

        // now send the response back
        let _result = responder.send(&phy_list_vec);
    }

    #[test]
    fn get_client_sme_valid_iface() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (wlan_monitor, server) = create_wlan_monitor_util();
        let mut next_device_monitor_req = server.into_future();

        let fut = get_sme_proxy(&wlan_monitor, 1);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // pass in that we expect this to succeed
        send_sme_proxy_response(&mut exec, &mut next_device_monitor_req, Ok(()));

        let () = match exec.run_until_stalled(&mut fut) {
            Poll::Ready(Ok(_)) => (),
            _ => panic!("Expected a status response"),
        };
    }

    fn send_sme_proxy_response(
        exec: &mut TestExecutor,
        server: &mut StreamFuture<wlan_service::DeviceMonitorRequestStream>,
        result: Result<(), zx::Status>,
    ) {
        let responder = match poll_device_monitor_req(exec, server) {
            Poll::Ready(DeviceMonitorRequest::GetClientSme { responder, .. }) => responder,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a GetClientSme request"),
        };

        // now send the response back
        let _result = responder.send(&mut result.map_err(|e| e.into_raw()));
    }

    #[test]
    fn get_client_sme_invalid_iface() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (wlan_monitor, server) = create_wlan_monitor_util();
        let mut next_device_monitor_req = server.into_future();

        let fut = get_sme_proxy(&wlan_monitor, 1);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // pass in that we expect this to fail with zx::Status::NOT_FOUND
        send_sme_proxy_response(
            &mut exec,
            &mut next_device_monitor_req,
            Err(zx::Status::NOT_FOUND),
        );

        let complete = exec.run_until_stalled(&mut fut);

        match complete {
            Poll::Ready(Err(_)) => (),
            _ => panic!("Expected a status response"),
        };
    }

    #[test]
    fn connect_success_returns_true() {
        let connect_result =
            test_wpa2_connect("TestAp", "password", "TestAp", fidl_ieee80211::StatusCode::Success);
        assert!(connect_result);
    }

    #[test]
    fn connect_failed_returns_false() {
        let connect_result = test_wpa2_connect(
            "TestAp",
            "password",
            "",
            fidl_ieee80211::StatusCode::RefusedReasonUnspecified,
        );
        assert!(!connect_result);
    }

    #[test]
    fn connect_different_ssid_returns_false() {
        let connect_result = test_wpa2_connect(
            "TestAp1",
            "password",
            "TestAp2",
            fidl_ieee80211::StatusCode::Success,
        );
        assert!(!connect_result);
    }

    fn test_wpa2_connect(
        target_ssid: &str,
        password: &str,
        connected_to_ssid: &str,
        result_code: fidl_ieee80211::StatusCode,
    ) -> bool {
        let target_ssid = Ssid::try_from(target_ssid).unwrap();
        let connected_to_ssid = Ssid::try_from(connected_to_ssid).unwrap();

        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut next_client_sme_req = server.into_future();

        let target_password = password.as_bytes();
        let target_bss_desc = generate_random_wpa2_bss_description();

        let fut = connect(
            &client_sme,
            target_ssid.clone(),
            target_password.to_vec(),
            target_bss_desc.clone(),
        );
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // have the request, need to send a response
        send_connect_request_response(
            &mut exec,
            &mut next_client_sme_req,
            &target_ssid,
            fidl_security::Authentication::try_from(SecurityContext {
                bss: BssDescription::try_from(target_bss_desc.clone()).unwrap(),
                unparsed_credential_bytes: target_password.to_vec(),
            })
            .unwrap(),
            result_code,
        );

        // if connection is successful, status is requested to extract ssid
        if result_code == fidl_ieee80211::StatusCode::Success {
            assert!(exec.run_until_stalled(&mut fut).is_pending());
            send_status_response(
                &mut exec,
                &mut next_client_sme_req,
                Some(connected_to_ssid),
                None,
            );
        }

        let complete = exec.run_until_stalled(&mut fut);

        let connection_result = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected a connect response"),
        };

        let returned_bool = match connection_result {
            Ok(response) => response,
            _ => panic!("Expected a valid connection result"),
        };

        returned_bool
    }

    #[test]
    fn connect_properly_passes_network_info_with_password() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut next_client_sme_req = server.into_future();

        let target_ssid = Ssid::try_from("TestAp").unwrap();
        let target_password = "password".as_bytes();
        let target_bss_desc = generate_random_wpa2_bss_description();

        let fut = connect(
            &client_sme,
            target_ssid.clone(),
            target_password.to_vec(),
            target_bss_desc.clone(),
        );
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // verify the connect request info
        verify_connect_request_info(
            &mut exec,
            &mut next_client_sme_req,
            &target_ssid,
            fidl_security::Authentication::try_from(SecurityContext {
                bss: BssDescription::try_from(target_bss_desc.clone()).unwrap(),
                unparsed_credential_bytes: target_password.to_vec(),
            })
            .unwrap(),
            target_bss_desc,
        );
    }

    #[test]
    fn connect_properly_passes_network_info_open() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut next_client_sme_req = server.into_future();

        let target_ssid = Ssid::try_from("TestAp").unwrap();
        let target_password = "".as_bytes();
        let target_bss_desc = fake_fidl_bss_description!(Open);

        let fut = connect(
            &client_sme,
            target_ssid.clone(),
            target_password.to_vec(),
            target_bss_desc.clone(),
        );
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // verify the connect request info
        verify_connect_request_info(
            &mut exec,
            &mut next_client_sme_req,
            &target_ssid,
            fidl_security::Authentication::try_from(SecurityContext {
                bss: BssDescription::try_from(target_bss_desc.clone()).unwrap(),
                unparsed_credential_bytes: vec![],
            })
            .unwrap(),
            target_bss_desc,
        );
    }

    fn verify_connect_request_info(
        exec: &mut TestExecutor,
        server: &mut StreamFuture<ClientSmeRequestStream>,
        expected_ssid: &Ssid,
        expected_authentication: fidl_security::Authentication,
        expected_bss_desc: fidl_internal::BssDescription,
    ) {
        match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Connect { req, .. }) => {
                assert_eq!(expected_ssid, &req.ssid);
                assert_eq!(req.authentication, expected_authentication);
                assert_eq!(req.bss_description, expected_bss_desc);
            }
            _ => panic!("expected a Connect request"),
        }
    }

    fn send_connect_request_response(
        exec: &mut TestExecutor,
        server: &mut StreamFuture<ClientSmeRequestStream>,
        expected_ssid: &Ssid,
        expected_authentication: fidl_security::Authentication,
        connect_result: fidl_ieee80211::StatusCode,
    ) {
        let responder = match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Connect { req, txn, .. }) => {
                assert_eq!(expected_ssid, &req.ssid[..]);
                assert_eq!(req.authentication, expected_authentication);
                txn.expect("expected a Connect transaction channel")
            }
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a Connect request"),
        };
        let connect_transaction = responder
            .into_stream()
            .expect("failed to create a connect transaction stream")
            .control_handle();
        connect_transaction
            .send_on_connect_result(&mut fidl_sme::ConnectResult {
                code: connect_result,
                is_credential_rejected: false,
                is_reconnect: false,
            })
            .expect("failed to send OnConnectResult to ConnectTransaction");
    }

    fn poll_client_sme_request(
        exec: &mut TestExecutor,
        next_client_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
    ) -> Poll<ClientSmeRequest> {
        exec.run_until_stalled(next_client_sme_req).map(|(req, stream)| {
            *next_client_sme_req = stream.into_future();
            req.expect("did not expect the ClientSmeRequestStream to end")
                .expect("error polling client sme request stream")
        })
    }

    fn create_client_sme_proxy() -> (fidl_sme::ClientSmeProxy, ClientSmeRequestStream) {
        let (proxy, server) = endpoints::create_proxy::<ClientSmeMarker>()
            .expect("failed to create sme client channel");
        let server = server.into_stream().expect("failed to create a client sme response stream");
        (proxy, server)
    }

    fn create_wlan_monitor_util() -> (DeviceMonitorProxy, DeviceMonitorRequestStream) {
        let (proxy, server) = endpoints::create_proxy::<DeviceMonitorMarker>()
            .expect("failed to create a wlan_service channel for tests");
        let server = server.into_stream().expect("failed to create a wlan_service response stream");
        (proxy, server)
    }

    enum StatusResponse {
        Idle,
        Connected,
        Connecting,
    }

    #[test]
    fn disconnect_with_empty_status_response() {
        if let Poll::Ready(result) = test_disconnect(StatusResponse::Idle) {
            return assert!(result.is_ok());
        }
        panic!("disconnect did not return a Poll::Ready")
    }

    #[test]
    fn disconnect_fail_because_connected() {
        if let Poll::Ready(result) = test_disconnect(StatusResponse::Connected) {
            return assert!(result.is_err());
        }
        panic!("disconnect did not return a Poll::Ready")
    }

    #[test]
    fn disconnect_fail_because_connecting() {
        if let Poll::Ready(result) = test_disconnect(StatusResponse::Connecting) {
            return assert!(result.is_err());
        }
        panic!("disconnect did not return a Poll::Ready")
    }

    fn test_disconnect(status: StatusResponse) -> Poll<Result<(), Error>> {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut client_sme_req = server.into_future();

        let fut = disconnect(&client_sme);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_disconnect_request_response(&mut exec, &mut client_sme_req);

        assert!(exec.run_until_stalled(&mut fut).is_pending());

        match status {
            StatusResponse::Idle => {
                send_status_response(&mut exec, &mut client_sme_req, None, None)
            }
            StatusResponse::Connected => send_status_response(
                &mut exec,
                &mut client_sme_req,
                Some(Ssid::try_from([1, 2, 3, 4]).unwrap()),
                None,
            ),
            StatusResponse::Connecting => send_status_response(
                &mut exec,
                &mut client_sme_req,
                None,
                Some(Ssid::try_from([1, 2, 3, 4]).unwrap()),
            ),
        }

        exec.run_until_stalled(&mut fut)
    }

    fn send_disconnect_request_response(
        exec: &mut TestExecutor,
        server: &mut StreamFuture<ClientSmeRequestStream>,
    ) {
        let rsp = match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Disconnect { responder, .. }) => responder,
            Poll::Pending => panic!("Expected a DisconnectRequest"),
            _ => panic!("Expected a DisconnectRequest"),
        };
        rsp.send().expect("Failed to send DisconnectResponse.");
    }

    fn create_serving_ap_info_using_ssid(ssid: Ssid) -> fidl_sme::ServingApInfo {
        fidl_sme::ServingApInfo {
            bssid: [0, 1, 2, 3, 4, 5],
            ssid: ssid.into(),
            rssi_dbm: -30,
            snr_db: 10,
            channel: fidl_common::WlanChannel {
                primary: 1,
                cbw: fidl_common::ChannelBandwidth::Cbw20,
                secondary80: 0,
            },
            protection: Protection::Wpa2Personal,
        }
    }

    fn send_status_response(
        exec: &mut TestExecutor,
        server: &mut StreamFuture<ClientSmeRequestStream>,
        connected_to_ssid: Option<Ssid>,
        connecting_to_ssid: Option<Ssid>,
    ) {
        let rsp = match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Status { responder }) => responder,
            Poll::Pending => panic!("Expected a StatusRequest"),
            _ => panic!("Expected a StatusRequest"),
        };

        let mut response = match (connected_to_ssid, connecting_to_ssid) {
            (Some(_), Some(_)) => panic!("SME cannot simultaneously be Connected and Connecting."),
            (Some(ssid), None) => {
                let serving_ap_info = create_serving_ap_info_using_ssid(ssid);
                fidl_sme::ClientStatusResponse::Connected(serving_ap_info)
            }
            (None, Some(ssid)) => fidl_sme::ClientStatusResponse::Connecting(ssid.to_vec()),
            (None, None) => fidl_sme::ClientStatusResponse::Idle(fidl_sme::Empty {}),
        };

        rsp.send(&mut response).expect("Failed to send StatusResponse.");
    }

    #[test]
    fn scan_success_returns_empty_results() {
        let scan_results_for_response = Vec::new();
        let scan_results = test_scan(scan_results_for_response);

        assert_eq!(scan_results, Vec::new());
    }

    #[test]
    fn scan_success_returns_results() {
        let mut scan_results_for_response = Vec::new();
        // due to restrictions for cloning fidl objects, forced to make a copy of the vector here
        let entry1 = create_scan_result(
            [0, 1, 2, 3, 4, 5],
            Ssid::try_from("foo").unwrap(),
            -30,
            20,
            Channel::new(1, Cbw::Cbw20),
            Protection::Wpa2Personal,
            Some(fidl_sme::Compatibility {
                mutual_security_protocols: vec![fidl_security::Protocol::Wpa2Personal],
            }),
        );
        let entry1_copy = entry1.clone();
        let entry2 = create_scan_result(
            [1, 2, 3, 4, 5, 6],
            Ssid::try_from("hello").unwrap(),
            -60,
            10,
            Channel::new(2, Cbw::Cbw20),
            Protection::Wpa2Personal,
            None,
        );
        let entry2_copy = entry2.clone();
        scan_results_for_response.push(entry1);
        scan_results_for_response.push(entry2);
        let mut expected_response = Vec::new();
        expected_response.push(entry1_copy);
        expected_response.push(entry2_copy);

        let scan_results = test_scan(scan_results_for_response);

        assert_eq!(scan_results, expected_response);
    }

    #[test]
    fn scan_error_correctly_handled() {
        // need to expect an error
        assert!(test_scan_error().is_err())
    }

    fn test_scan(scan_results: Vec<fidl_sme::ScanResult>) -> Vec<fidl_sme::ScanResult> {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut client_sme_req = server.into_future();

        let fut = passive_scan(&client_sme);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_scan_result_response(&mut exec, &mut client_sme_req, scan_results);

        let complete = exec.run_until_stalled(&mut fut);
        let request_result = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected a scan request result"),
        };
        let returned_scan_results = request_result.expect("failed to get scan results");

        returned_scan_results
    }

    fn send_scan_result_response(
        exec: &mut TestExecutor,
        server: &mut StreamFuture<fidl_sme::ClientSmeRequestStream>,
        scan_results: Vec<fidl_sme::ScanResult>,
    ) {
        match poll_client_sme_request(exec, server) {
            Poll::Ready(fidl_sme::ClientSmeRequest::Scan { responder, .. }) => {
                responder.send(&mut Ok(scan_results)).expect("failed to send scan results")
            }
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a scan request"),
        }
    }

    fn test_scan_error() -> Result<(), Error> {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut client_sme_req = server.into_future();

        let fut = passive_scan(&client_sme);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_scan_error_response(&mut exec, &mut client_sme_req);
        let _ = exec.run_until_stalled(&mut fut)?;
        Ok(())
    }

    fn send_scan_error_response(
        exec: &mut TestExecutor,
        server: &mut StreamFuture<fidl_sme::ClientSmeRequestStream>,
    ) {
        match poll_client_sme_request(exec, server) {
            Poll::Ready(fidl_sme::ClientSmeRequest::Scan { responder, .. }) => responder
                .send(&mut Err(fidl_sme::ScanErrorCode::InternalError))
                .expect("failed to send ScanError"),
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a scan request"),
        };
    }

    fn create_scan_result(
        bssid: [u8; 6],
        ssid: Ssid,
        rssi_dbm: i8,
        snr_db: i8,
        channel: Channel,
        protection: Protection,
        compatibility: Option<fidl_sme::Compatibility>,
    ) -> fidl_sme::ScanResult {
        fidl_sme::ScanResult {
            compatibility: compatibility.map(Box::new),
            timestamp_nanos: zx::Time::get_monotonic().into_nanos(),
            bss_description: fake_fidl_bss_description!(
                protection => protection,
                bssid: bssid,
                ssid: ssid,
                rssi_dbm: rssi_dbm,
                snr_db: snr_db,
                channel: channel,
            ),
        }
    }

    fn send_destroy_iface_response(
        exec: &mut TestExecutor,
        server: &mut StreamFuture<wlan_service::DeviceMonitorRequestStream>,
        status: zx::Status,
    ) {
        let responder = match poll_device_monitor_req(exec, server) {
            Poll::Ready(DeviceMonitorRequest::DestroyIface { responder, .. }) => responder,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a destroy iface request"),
        };

        // now send the response back
        let _result = responder.send(status.into_raw());
    }

    #[test]
    fn test_destroy_single_iface_ok() {
        let mut exec = TestExecutor::new().expect("failed to create an executor");
        let (monitor_service, server) = create_wlan_monitor_util();
        let mut next_device_service_req = server.into_future();

        let fut = destroy_iface(&monitor_service, 0);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_destroy_iface_response(&mut exec, &mut next_device_service_req, zx::Status::OK);

        match exec.run_until_stalled(&mut fut) {
            Poll::Ready(Ok(_)) => (),
            _ => panic!("Expected a status response"),
        };
    }
}
