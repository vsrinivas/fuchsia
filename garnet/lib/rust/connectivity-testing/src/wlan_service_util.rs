// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, format_err, Error, ResultExt};
use fidl::endpoints;
use fidl_fuchsia_wlan_common as fidl_common;
use fidl_fuchsia_wlan_device_service::DeviceServiceProxy;
use fidl_fuchsia_wlan_sme as fidl_sme;
use fuchsia_syslog::fx_log_err;
use fuchsia_zircon as zx;
use futures::stream::TryStreamExt;

type WlanService = DeviceServiceProxy;

const SCAN_TIMEOUT_SECONDS: u8 = 20;

// Helper methods for calling wlan_service fidl methods
pub async fn get_iface_list(wlan_svc: &DeviceServiceProxy) -> Result<Vec<u16>, Error> {
    let response = wlan_svc.list_ifaces().await.context("Error getting iface list")?;
    let mut wlan_iface_ids = Vec::new();
    for iface in response.ifaces {
        wlan_iface_ids.push(iface.iface_id);
    }
    Ok(wlan_iface_ids)
}

pub async fn get_iface_sme_proxy(
    wlan_svc: &WlanService,
    iface_id: u16,
) -> Result<fidl_sme::ClientSmeProxy, Error> {
    let (sme_proxy, sme_remote) = endpoints::create_proxy()?;
    let status = wlan_svc
        .get_client_sme(iface_id, sme_remote)
        .await
        .context("error sending GetClientSme request")?;
    if status == zx::sys::ZX_OK {
        Ok(sme_proxy)
    } else {
        Err(format_err!("Invalid interface id {}", iface_id))
    }
}

pub async fn connect_to_network(
    iface_sme_proxy: &fidl_sme::ClientSmeProxy,
    target_ssid: Vec<u8>,
    target_pwd: Vec<u8>,
) -> Result<bool, Error> {
    let (connection_proxy, connection_remote) = endpoints::create_proxy()?;
    let target_ssid_clone = target_ssid.clone();

    // create ConnectRequest holding network info
    let credential = credential_from_password(target_pwd);
    let mut req = fidl_sme::ConnectRequest {
        ssid: target_ssid,
        credential,
        radio_cfg: fidl_sme::RadioConfig {
            override_phy: false,
            phy: fidl_common::Phy::Ht,
            override_cbw: false,
            cbw: fidl_common::Cbw::Cbw20,
            override_primary_chan: false,
            primary_chan: 0,
        },
        scan_type: fidl_common::ScanType::Passive,
    };

    let _result = iface_sme_proxy.connect(&mut req, Some(connection_remote))?;

    let connection_code = handle_connect_transaction(connection_proxy).await?;

    #[allow(unreachable_patterns)]
    let mut connected = match connection_code {
        fidl_sme::ConnectResultCode::Success => true,
        fidl_sme::ConnectResultCode::Canceled => {
            fx_log_err!("Connecting was canceled or superseded by another command");
            false
        }
        fidl_sme::ConnectResultCode::Failed => {
            fx_log_err!("Failed to connect to network");
            false
        }
        fidl_sme::ConnectResultCode::BadCredentials => {
            fx_log_err!("Failed to connect to network; bad credentials");
            false
        }
        e => {
            // also need to handle new result codes, generically return false here
            fx_log_err!("Failed to connect: {:?}", e);
            false
        }
    };

    if connected == true {
        let rsp =
            iface_sme_proxy.status().await.context("failed to check status from sme_proxy")?;

        connected = connected
            && match rsp.connected_to {
                Some(ref bss) if bss.ssid.as_slice().to_vec() == target_ssid_clone => true,
                Some(ref bss) => {
                    fx_log_err!(
                        "Connected to wrong network: {:?}. Expected: {:?}.",
                        bss.ssid.as_slice(),
                        target_ssid_clone
                    );
                    false
                }
                _ => false,
            };
    }

    Ok(connected)
}

async fn handle_connect_transaction(
    connect_transaction: fidl_sme::ConnectTransactionProxy,
) -> Result<fidl_sme::ConnectResultCode, Error> {
    let mut event_stream = connect_transaction.take_event_stream();

    let mut result_code = fidl_sme::ConnectResultCode::Failed;

    while let Some(evt) = event_stream
        .try_next()
        .await
        .context("failed to receive connect result before the channel was closed")?
    {
        match evt {
            fidl_sme::ConnectTransactionEvent::OnFinished { code } => {
                result_code = code;
                break;
            }
        }
    }

    Ok(result_code)
}

pub async fn disconnect_from_network(
    iface_sme_proxy: &fidl_sme::ClientSmeProxy,
) -> Result<(), Error> {
    iface_sme_proxy.disconnect().await.context("failed to trigger disconnect")?;

    // check the status and ensure we are not connected to or connecting to anything
    let rsp = iface_sme_proxy.status().await.context("failed to check status from sme_proxy")?;
    if rsp.connected_to.is_some() || !rsp.connecting_to_ssid.is_empty() {
        bail!(
            "Disconnect confirmation failed: connected_to[{:?}] connecting_to_ssid:[{:?}]",
            rsp.connected_to,
            rsp.connecting_to_ssid
        );
    }
    Ok(())
}

pub async fn perform_scan(
    iface_sme_proxy: &fidl_sme::ClientSmeProxy,
) -> Result<Vec<fidl_sme::BssInfo>, Error> {
    let scan_transaction = start_scan_transaction(&iface_sme_proxy)?;

    get_scan_results(scan_transaction).await.map_err(Into::into)
}

fn start_scan_transaction(
    iface_sme_proxy: &fidl_sme::ClientSmeProxy,
) -> Result<fidl_sme::ScanTransactionProxy, Error> {
    let (scan_txn, remote) = endpoints::create_proxy()?;
    let mut req = fidl_sme::ScanRequest {
        timeout: SCAN_TIMEOUT_SECONDS,
        scan_type: fidl_common::ScanType::Passive,
    };
    iface_sme_proxy.scan(&mut req, remote)?;
    Ok(scan_txn)
}

async fn get_scan_results(
    scan_txn: fidl_sme::ScanTransactionProxy,
) -> Result<Vec<fidl_sme::BssInfo>, Error> {
    let mut stream = scan_txn.take_event_stream();
    let mut scan_results = vec![];

    while let Some(event) = stream
        .try_next()
        .await
        .context("failed to receive scan result before the channel was closed")?
    {
        match event {
            fidl_sme::ScanTransactionEvent::OnResult { aps } => scan_results.extend(aps),
            fidl_sme::ScanTransactionEvent::OnFinished {} => return Ok(scan_results),
            fidl_sme::ScanTransactionEvent::OnError { error } => {
                // error while waiting for scan results
                bail!("error when retrieving scan results {:?}", error);
            }
        }
    }

    bail!("ScanTransaction channel closed before scan finished");
}

fn credential_from_password(pwd: Vec<u8>) -> fidl_sme::Credential {
    if pwd.is_empty() {
        fidl_sme::Credential::None(fidl_sme::Empty)
    } else {
        fidl_sme::Credential::Password(pwd)
    }
}

pub async fn get_wlan_mac_addr(
    wlan_svc: &DeviceServiceProxy,
    iface_id: u16,
) -> Result<[u8; 6], Error> {
    let (_status, resp) = wlan_svc.query_iface(iface_id).await?;
    Ok(resp.ok_or(format_err!("No valid iface response"))?.mac_addr)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::RequestStream,
        fidl_fuchsia_wlan_device_service::{
            self as wlan_service, DeviceServiceMarker, DeviceServiceProxy, DeviceServiceRequest,
            DeviceServiceRequestStream, IfaceListItem, ListIfacesResponse, QueryIfaceResponse,
        },
        fidl_fuchsia_wlan_sme::{
            BssInfo, ClientSmeMarker, ClientSmeRequest, ClientSmeRequestStream, ConnectResultCode,
            Protection,
        },
        fuchsia_async::Executor,
        futures::stream::{StreamExt, StreamFuture},
        futures::task::Poll,
        pin_utils::pin_mut,
        wlan_common::assert_variant,
    };

    #[test]
    fn list_ifaces_returns_iface_id_vector() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let (wlan_service, server) = create_wlan_service_util();
        let mut next_device_service_req = server.into_future();

        // create the data to use in the response
        let iface_id_list: Vec<u16> = vec![0, 1, 35, 36];
        let mut iface_list_vec = vec![];
        for id in &iface_id_list {
            iface_list_vec.push(IfaceListItem { iface_id: *id });
        }

        let fut = get_iface_list(&wlan_service);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_iface_list_response(&mut exec, &mut next_device_service_req, iface_list_vec);

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
        assert_eq!(response, iface_id_list);
    }

    #[test]
    fn list_ifaces_properly_handles_zero_ifaces() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let (wlan_service, server) = create_wlan_service_util();
        let mut next_device_service_req = server.into_future();

        // create the data to use in the response
        let iface_id_list: Vec<u16> = vec![];
        let iface_list_vec = vec![];

        let fut = get_iface_list(&wlan_service);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_iface_list_response(&mut exec, &mut next_device_service_req, iface_list_vec);

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
        assert_eq!(response, iface_id_list);
    }

    fn poll_device_service_req(
        exec: &mut Executor,
        next_device_service_req: &mut StreamFuture<DeviceServiceRequestStream>,
    ) -> Poll<DeviceServiceRequest> {
        exec.run_until_stalled(next_device_service_req).map(|(req, stream)| {
            *next_device_service_req = stream.into_future();
            req.expect("did not expect the DeviceServiceRequestStream to end")
                .expect("error polling device service request stream")
        })
    }

    fn send_iface_list_response(
        exec: &mut Executor,
        server: &mut StreamFuture<wlan_service::DeviceServiceRequestStream>,
        iface_list_vec: Vec<IfaceListItem>,
    ) {
        let responder = match poll_device_service_req(exec, server) {
            Poll::Ready(DeviceServiceRequest::ListIfaces { responder }) => responder,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a ListIfaces request"),
        };

        // now send the response back
        let _result = responder.send(&mut ListIfacesResponse { ifaces: iface_list_vec });
    }

    #[test]
    fn get_client_sme_valid_iface() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let (wlan_service, server) = create_wlan_service_util();
        let mut next_device_service_req = server.into_future();

        let fut = get_iface_sme_proxy(&wlan_service, 1);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // pass in that we expect this to succeed
        send_sme_proxy_response(&mut exec, &mut next_device_service_req, zx::Status::OK);

        match exec.run_until_stalled(&mut fut) {
            Poll::Ready(Ok(_)) => (),
            _ => panic!("Expected a status response"),
        };
    }

    fn send_sme_proxy_response(
        exec: &mut Executor,
        server: &mut StreamFuture<wlan_service::DeviceServiceRequestStream>,
        status: zx::Status,
    ) {
        let responder = match poll_device_service_req(exec, server) {
            Poll::Ready(DeviceServiceRequest::GetClientSme { responder, .. }) => responder,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a GetClientSme request"),
        };

        // now send the response back
        let _result = responder.send(status.into_raw());
    }

    #[test]
    fn get_client_sme_invalid_iface() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let (wlan_service, server) = create_wlan_service_util();
        let mut next_device_service_req = server.into_future();

        let fut = get_iface_sme_proxy(&wlan_service, 1);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // pass in that we expect this to fail with zx::Status::NOT_FOUND
        send_sme_proxy_response(&mut exec, &mut next_device_service_req, zx::Status::NOT_FOUND);

        let complete = exec.run_until_stalled(&mut fut);

        match complete {
            Poll::Ready(Err(_)) => (),
            _ => panic!("Expected a status response"),
        };
    }

    #[test]
    fn connect_to_network_success_returns_true() {
        let connect_result = test_connect("TestAp", "", "TestAp", ConnectResultCode::Success);
        assert!(connect_result);
    }

    #[test]
    fn connect_to_network_failed_returns_false() {
        let connect_result = test_connect("TestAp", "", "", ConnectResultCode::Failed);
        assert!(!connect_result);
    }

    #[test]
    fn connect_to_network_canceled_returns_false() {
        let connect_result = test_connect("TestAp", "", "", ConnectResultCode::Canceled);
        assert!(!connect_result);
    }

    #[test]
    fn connect_to_network_bad_credentials_returns_false() {
        let connect_result = test_connect("TestAp", "", "", ConnectResultCode::BadCredentials);
        assert!(!connect_result);
    }

    #[test]
    fn connect_to_network_different_ssid_returns_false() {
        let connect_result = test_connect("TestAp1", "", "TestAp2", ConnectResultCode::Success);
        assert!(!connect_result);
    }

    fn test_connect(
        ssid: &str,
        password: &str,
        connected_to: &str,
        result_code: ConnectResultCode,
    ) -> bool {
        let mut exec = Executor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut next_client_sme_req = server.into_future();

        let target_ssid = ssid.as_bytes();
        let target_password = password.as_bytes();

        let fut = connect_to_network(&client_sme, target_ssid.to_vec(), target_password.to_vec());
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // have the request, need to send a response
        send_connect_request_response(
            &mut exec,
            &mut next_client_sme_req,
            target_ssid,
            credential_from_password(target_password.to_vec()),
            result_code,
        );

        // if connection is successful, status is requested to extract ssid
        if result_code == ConnectResultCode::Success {
            assert!(exec.run_until_stalled(&mut fut).is_pending());
            send_status_response(
                &mut exec,
                &mut next_client_sme_req,
                connected_to.as_bytes().to_vec(),
                target_ssid.to_vec(),
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
    fn connect_to_network_properly_passes_network_info_with_password() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut next_client_sme_req = server.into_future();

        let target_ssid = "TestAp".as_bytes();
        let target_password = "password".as_bytes();

        let fut = connect_to_network(&client_sme, target_ssid.to_vec(), target_password.to_vec());
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // verify the connect request info
        verify_connect_request_info(
            &mut exec,
            &mut next_client_sme_req,
            target_ssid,
            credential_from_password(target_password.to_vec()),
        );
    }

    #[test]
    fn connect_to_network_properly_passes_network_info_open() {
        let mut exec = Executor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut next_client_sme_req = server.into_future();

        let target_ssid = "TestAp".as_bytes();
        let target_password = "".as_bytes();

        let fut = connect_to_network(&client_sme, target_ssid.to_vec(), target_password.to_vec());
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // verify the connect request info
        verify_connect_request_info(
            &mut exec,
            &mut next_client_sme_req,
            target_ssid,
            credential_from_password(vec![]),
        );
    }

    fn verify_connect_request_info(
        exec: &mut Executor,
        server: &mut StreamFuture<ClientSmeRequestStream>,
        expected_ssid: &[u8],
        expected_credential: fidl_sme::Credential,
    ) {
        match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Connect { req, .. }) => {
                assert_eq!(expected_ssid, &req.ssid[..]);
                assert_eq_credentials(&req.credential, &expected_credential);
            }
            _ => panic!("expected a Connect request"),
        }
    }

    fn send_connect_request_response(
        exec: &mut Executor,
        server: &mut StreamFuture<ClientSmeRequestStream>,
        expected_ssid: &[u8],
        expected_credential: fidl_sme::Credential,
        connect_result: ConnectResultCode,
    ) {
        let responder = match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Connect { req, txn, .. }) => {
                assert_eq!(expected_ssid, &req.ssid[..]);
                assert_eq_credentials(&req.credential, &expected_credential);
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
            .send_on_finished(connect_result)
            .expect("failed to send OnFinished to ConnectTransaction");
    }

    fn poll_client_sme_request(
        exec: &mut Executor,
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

    fn create_wlan_service_util() -> (DeviceServiceProxy, DeviceServiceRequestStream) {
        let (proxy, server) = endpoints::create_proxy::<DeviceServiceMarker>()
            .expect("failed to create a wlan_service channel for tests");
        let server = server.into_stream().expect("failed to create a wlan_service response stream");
        (proxy, server)
    }

    enum StatusResponse {
        Empty,
        Connected,
        Connecting,
    }

    #[test]
    fn disconnect_with_empty_status_response() {
        if let Poll::Ready(result) = test_disconnect(StatusResponse::Empty) {
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
        let mut exec = Executor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut client_sme_req = server.into_future();

        let fut = disconnect_from_network(&client_sme);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_disconnect_request_response(&mut exec, &mut client_sme_req);

        assert!(exec.run_until_stalled(&mut fut).is_pending());

        match status {
            StatusResponse::Empty => {
                send_status_response(&mut exec, &mut client_sme_req, vec![], vec![])
            }
            StatusResponse::Connected => {
                send_status_response(&mut exec, &mut client_sme_req, vec![1, 2, 3, 4], vec![])
            }
            StatusResponse::Connecting => {
                send_status_response(&mut exec, &mut client_sme_req, vec![], vec![1, 2, 3, 4])
            }
        }

        exec.run_until_stalled(&mut fut)
    }

    fn send_disconnect_request_response(
        exec: &mut Executor,
        server: &mut StreamFuture<ClientSmeRequestStream>,
    ) {
        let rsp = match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Disconnect { responder }) => responder,
            Poll::Pending => panic!("Expected a DisconnectRequest"),
            _ => panic!("Expected a DisconnectRequest"),
        };
        rsp.send().expect("Failed to send DisconnectResponse.");
    }

    fn create_bssinfo_using_ssid(ssid: Vec<u8>) -> Option<Box<BssInfo>> {
        match ssid.is_empty() {
            true => None,
            _ => {
                let bss_info: fidl_sme::BssInfo = fidl_sme::BssInfo {
                    bssid: [0, 1, 2, 3, 4, 5],
                    ssid: ssid,
                    rx_dbm: -30,
                    channel: 1,
                    protection: Protection::Wpa2Personal,
                    compatible: true,
                };
                Some(Box::new(bss_info))
            }
        }
    }

    fn send_status_response(
        exec: &mut Executor,
        server: &mut StreamFuture<ClientSmeRequestStream>,
        connected_to: Vec<u8>,
        connecting_to_ssid: Vec<u8>,
    ) {
        let rsp = match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Status { responder }) => responder,
            Poll::Pending => panic!("Expected a StatusRequest"),
            _ => panic!("Expected a StatusRequest"),
        };

        let connected_to_bss_info = create_bssinfo_using_ssid(connected_to);

        let mut response = fidl_sme::ClientStatusResponse {
            connected_to: connected_to_bss_info,
            connecting_to_ssid: connecting_to_ssid,
        };

        rsp.send(&mut response).expect("Failed to send StatusResponse.");
    }

    #[test]
    fn scan_success_returns_empty_results() {
        let scan_results_for_response = Vec::new();
        let scan_results = test_perform_scan(scan_results_for_response);

        assert_eq!(scan_results, Vec::new());
    }

    #[test]
    fn scan_success_returns_results() {
        let mut scan_results_for_response = Vec::new();
        // due to restrictions for cloning fidl objects, forced to make a copy of the vector here
        let entry1 = create_bss_info(
            [0, 1, 2, 3, 4, 5],
            b"foo".to_vec(),
            -30,
            1,
            Protection::Wpa2Personal,
            true,
        );
        let entry1_copy = fidl_sme::BssInfo { ssid: entry1.ssid.clone(), ..entry1 };
        let entry2 = create_bss_info(
            [1, 2, 3, 4, 5, 6],
            b"hello".to_vec(),
            -60,
            2,
            Protection::Wpa2Personal,
            false,
        );
        let entry2_copy = fidl_sme::BssInfo { ssid: entry2.ssid.clone(), ..entry2 };
        scan_results_for_response.push(entry1);
        scan_results_for_response.push(entry2);
        let mut expected_response = Vec::new();
        expected_response.push(entry1_copy);
        expected_response.push(entry2_copy);

        let scan_results = test_perform_scan(scan_results_for_response);

        assert_eq!(scan_results, expected_response);
    }

    #[test]
    fn scan_error_correctly_handled() {
        // need to expect an error
        assert!(test_perform_scan_error().is_err())
    }

    fn test_perform_scan(mut scan_results: Vec<fidl_sme::BssInfo>) -> Vec<fidl_sme::BssInfo> {
        let mut exec = Executor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut client_sme_req = server.into_future();

        let fut = perform_scan(&client_sme);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_scan_result_response(&mut exec, &mut client_sme_req, &mut scan_results);

        let complete = exec.run_until_stalled(&mut fut);
        let request_result = match complete {
            Poll::Ready(result) => result,
            _ => panic!("Expected a scan request result"),
        };
        let returned_scan_results = request_result.expect("failed to get scan results");

        returned_scan_results
    }

    fn send_scan_result_response(
        exec: &mut Executor,
        server: &mut StreamFuture<fidl_sme::ClientSmeRequestStream>,
        scan_results: &mut Vec<fidl_sme::BssInfo>,
    ) {
        let transaction = match poll_client_sme_request(exec, server) {
            Poll::Ready(fidl_sme::ClientSmeRequest::Scan { txn, .. }) => txn,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a scan request"),
        };

        // now send the response back
        let transaction = transaction
            .into_stream()
            .expect("failed to create a scan transaction stream")
            .control_handle();
        transaction
            .send_on_result(&mut scan_results.into_iter())
            .expect("failed to send scan results");
        transaction.send_on_finished().expect("failed to send OnFinished to ScanTransaction");
    }

    fn test_perform_scan_error() -> Result<(), Error> {
        let mut exec = Executor::new().expect("failed to create an executor");
        let (client_sme, server) = create_client_sme_proxy();
        let mut client_sme_req = server.into_future();

        let fut = perform_scan(&client_sme);
        pin_mut!(fut);
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        send_scan_error_response(&mut exec, &mut client_sme_req);
        let _ = exec.run_until_stalled(&mut fut)?;
        Ok(())
    }

    fn send_scan_error_response(
        exec: &mut Executor,
        server: &mut StreamFuture<fidl_sme::ClientSmeRequestStream>,
    ) {
        let transaction = match poll_client_sme_request(exec, server) {
            Poll::Ready(fidl_sme::ClientSmeRequest::Scan { txn, .. }) => txn,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a scan request"),
        };

        // create error to send back
        let mut scan_error = fidl_sme::ScanError {
            code: fidl_sme::ScanErrorCode::InternalError,
            message: "Scan error".to_string(),
        };

        // now send the response back
        let transaction = transaction
            .into_stream()
            .expect("failed to create a scan transaction stream")
            .control_handle();
        transaction.send_on_error(&mut scan_error).expect("failed to send ScanError");
    }

    fn create_bss_info(
        bssid: [u8; 6],
        ssid: Vec<u8>,
        rx_dbm: i8,
        channel: u8,
        protection: Protection,
        compatible: bool,
    ) -> fidl_sme::BssInfo {
        fidl_sme::BssInfo { bssid, ssid, rx_dbm, channel, protection, compatible }
    }

    fn assert_eq_credentials(
        actual_credential: &fidl_sme::Credential,
        expected_credential: &fidl_sme::Credential,
    ) {
        match actual_credential {
            fidl_sme::Credential::Password(password) => match expected_credential {
                fidl_sme::Credential::Password(expected_password) => {
                    assert_eq!(&expected_password[..], &password[..]);
                }
                expected => panic!("got password, expected: {:?}", expected),
            },
            fidl_sme::Credential::None(_) => match expected_credential {
                fidl_sme::Credential::None(_) => (),
                expected => panic!("got no password, expected: {:?}", expected),
            },
            unsupported => panic!("unsupported credential type: {:?}", unsupported),
        }
    }

    fn send_fake_query_iface_response(
        exec: &mut Executor,
        req_stream: &mut DeviceServiceRequestStream,
        fake_mac_addr: Option<[u8; 6]>,
    ) {
        use fuchsia_zircon::sys::{ZX_ERR_NOT_FOUND, ZX_OK};

        let req = exec.run_until_stalled(&mut req_stream.next());
        let responder = assert_variant !(
            req,
            Poll::Ready(Some(Ok(DeviceServiceRequest::QueryIface{iface_id : _, responder})))
            => responder);
        if let Some(mac) = fake_mac_addr {
            let mut response = fake_iface_query_response(mac);
            responder
                .send(ZX_OK, Some(fidl::encoding::OutOfLine(&mut response)))
                .expect("sending fake response with mac address");
        } else {
            responder.send(ZX_ERR_NOT_FOUND, None).expect("sending fake response with none")
        }
    }

    fn fake_iface_query_response(mac_addr: [u8; 6]) -> QueryIfaceResponse {
        QueryIfaceResponse {
            role: fidl_fuchsia_wlan_device::MacRole::Client,
            id: 0,
            phy_id: 0,
            phy_assigned_id: 0,
            mac_addr,
        }
    }

    #[test]
    fn test_get_wlan_mac_addr_ok() {
        let (mut exec, proxy, mut req_stream) = crate::setup_fake_service::<DeviceServiceMarker>();
        let mac_addr_fut = get_wlan_mac_addr(&proxy, 0);
        pin_mut!(mac_addr_fut);

        assert_variant!(exec.run_until_stalled(&mut mac_addr_fut), Poll::Pending);

        send_fake_query_iface_response(&mut exec, &mut req_stream, Some([1, 2, 3, 4, 5, 6]));

        let mac_addr = assert_variant!(exec.run_until_stalled(&mut mac_addr_fut),
                                       Poll::Ready(Ok(addr)) => addr);
        assert_eq!(mac_addr, [1, 2, 3, 4, 5, 6]);
    }

    #[test]
    fn test_get_wlan_mac_addr_not_found() {
        let (mut exec, proxy, mut req_stream) = crate::setup_fake_service::<DeviceServiceMarker>();
        let mac_addr_fut = get_wlan_mac_addr(&proxy, 0);
        pin_mut!(mac_addr_fut);

        assert_variant!(exec.run_until_stalled(&mut mac_addr_fut), Poll::Pending);

        send_fake_query_iface_response(&mut exec, &mut req_stream, None);

        let err_str = assert_variant!(exec.run_until_stalled(&mut mac_addr_fut),
                                      Poll::Ready(Err(e)) => format !("{}", e));
        assert_eq!("No valid iface response", err_str);
    }

    #[test]
    fn test_get_wlan_mac_addr_service_interrupted() {
        let (mut exec, proxy, req_stream) = crate::setup_fake_service::<DeviceServiceMarker>();
        let mac_addr_fut = get_wlan_mac_addr(&proxy, 0);
        pin_mut!(mac_addr_fut);

        assert_variant!(exec.run_until_stalled(&mut mac_addr_fut), Poll::Pending);

        // Simulate service not being available by closing the channel
        std::mem::drop(req_stream);

        let err_str = assert_variant!(exec.run_until_stalled(&mut mac_addr_fut),
                                      Poll::Ready(Err(e)) => format !("{}", e));
        assert!(err_str.contains("PEER_CLOSED"));
    }
}
