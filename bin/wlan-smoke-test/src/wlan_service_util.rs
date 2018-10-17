// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{bail, format_err, Error, ResultExt};
use fidl::endpoints;
use fidl_fuchsia_wlan_device_service::DeviceServiceProxy;
use fidl_fuchsia_wlan_sme as fidl_sme;
use fuchsia_syslog::{fx_log, fx_log_err};
use fuchsia_zircon as zx;
use futures::stream::TryStreamExt;
use std::fmt;

type WlanService = DeviceServiceProxy;

// Helper object to formate BSSIDs
#[allow(dead_code)]
pub struct Bssid(pub [u8; 6]);

impl fmt::Display for Bssid {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        write!(
            f,
            "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
            self.0[0], self.0[1], self.0[2], self.0[3], self.0[4], self.0[5]
        )
    }
}

// Helper methods for calling wlan_service fidl methods
pub async fn get_iface_list(wlan_svc: &DeviceServiceProxy) -> Result<Vec<u16>, Error> {
    let response = await!(wlan_svc.list_ifaces()).context("Error getting iface list")?;
    let mut wlan_iface_ids = Vec::new();
    for iface in response.ifaces {
        wlan_iface_ids.push(iface.iface_id);
    }
    Ok(wlan_iface_ids)
}

pub async fn get_iface_sme_proxy(
    wlan_svc: &WlanService, iface_id: u16,
) -> Result<fidl_sme::ClientSmeProxy, Error> {
    let (sme_proxy, sme_remote) = endpoints::create_proxy()?;
    let status = await!(wlan_svc.get_client_sme(iface_id, sme_remote))
        .context("error sending GetClientSme request")?;
    if status == zx::sys::ZX_OK {
        Ok(sme_proxy)
    } else {
        Err(format_err!("Invalid interface id {}", iface_id))
    }
}

pub async fn connect_to_network(
    iface_sme_proxy: &fidl_sme::ClientSmeProxy, target_ssid: Vec<u8>, target_pwd: Vec<u8>,
) -> Result<bool, Error> {
    let (connection_proxy, connection_remote) = endpoints::create_proxy()?;
    let target_ssid_clone = target_ssid.clone();

    // create ConnectRequest holding network info
    let mut req = fidl_sme::ConnectRequest {
        ssid: target_ssid,
        password: target_pwd,
        params: fidl_sme::ConnectPhyParams {
            override_phy: false,
            phy: fidl_sme::Phy::Ht,
            override_cbw: false,
            cbw: fidl_sme::Cbw::Cbw20,
        },
    };

    let _result = iface_sme_proxy.connect(&mut req, Some(connection_remote))?;

    let connection_code = await!(handle_connect_transaction(connection_proxy))?;

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
            await!(iface_sme_proxy.status()).context("failed to check status from sme_proxy")?;

        connected = connected && match rsp.connected_to {
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

    while let Some(evt) = await!(event_stream.try_next())
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
    await!(iface_sme_proxy.disconnect()).context("failed to trigger disconnect")?;

    // check the status and ensure we are not connected to or connecting to anything
    let rsp = await!(iface_sme_proxy.status()).context("failed to check status from sme_proxy")?;
    if rsp.connected_to.is_some() || !rsp.connecting_to_ssid.is_empty() {
        bail!(
            "Disconnect confirmation failed: connected_to[{:?}] connecting_to_ssid:[{:?}]",
            rsp.connected_to,
            rsp.connecting_to_ssid
        );
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::RequestStream;
    use fidl_fuchsia_wlan_device_service as wlan_service;
    use fidl_fuchsia_wlan_device_service::{DeviceServiceMarker, DeviceServiceProxy};
    use fidl_fuchsia_wlan_device_service::{DeviceServiceRequest, DeviceServiceRequestStream};
    use fidl_fuchsia_wlan_device_service::{IfaceListItem, ListIfacesResponse};
    use fidl_fuchsia_wlan_sme::BssInfo;
    use fidl_fuchsia_wlan_sme::ClientSmeMarker;
    use fidl_fuchsia_wlan_sme::ConnectResultCode;
    use fidl_fuchsia_wlan_sme::{ClientSmeRequest, ClientSmeRequestStream};
    use fuchsia_async as fasync;
    use futures::stream::{StreamExt, StreamFuture};
    use futures::task::Poll;
    use pin_utils::pin_mut;

    #[test]
    fn list_ifaces_returns_iface_id_vector() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (wlan_service, server) = create_wlan_service_util();
        let mut next_device_service_req = server.into_future();

        // create the data to use in the response
        let iface_id_list: Vec<u16> = vec![0, 1, 35, 36];
        let mut iface_list_vec = vec![];
        for id in &iface_id_list {
            iface_list_vec.push(IfaceListItem {
                iface_id: *id,
                path: "/foo/bar/".to_string(),
            });
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
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
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
        exec: &mut fasync::Executor,
        next_device_service_req: &mut StreamFuture<DeviceServiceRequestStream>,
    ) -> Poll<DeviceServiceRequest> {
        exec.run_until_stalled(next_device_service_req)
            .map(|(req, stream)| {
                *next_device_service_req = stream.into_future();
                req.expect("did not expect the DeviceServiceRequestStream to end")
                    .expect("error polling device service request stream")
            })
    }

    fn send_iface_list_response(
        exec: &mut fasync::Executor,
        server: &mut StreamFuture<wlan_service::DeviceServiceRequestStream>,
        iface_list_vec: Vec<IfaceListItem>,
    ) {
        let responder = match poll_device_service_req(exec, server) {
            Poll::Ready(DeviceServiceRequest::ListIfaces { responder }) => responder,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a ListIfaces request"),
        };

        // now send the response back
        let _result = responder.send(&mut ListIfacesResponse {
            ifaces: iface_list_vec,
        });
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
        ssid: &str, password: &str, connected_to: &str, result_code: ConnectResultCode,
    ) -> bool {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
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
            target_password,
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
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
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
            target_password,
        );
    }

    #[test]
    fn connect_to_network_properly_passes_network_info_open() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
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
            target_password,
        );
    }

    fn verify_connect_request_info(
        exec: &mut fasync::Executor, server: &mut StreamFuture<ClientSmeRequestStream>,
        expected_ssid: &[u8], expected_password: &[u8],
    ) {
        match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Connect { req, .. }) => {
                assert_eq!(expected_ssid, &req.ssid[..]);
                assert_eq!(expected_password, &req.password[..]);
            }
            _ => panic!("expected a Connect request"),
        }
    }

    fn send_connect_request_response(
        exec: &mut fasync::Executor, server: &mut StreamFuture<ClientSmeRequestStream>,
        expected_ssid: &[u8], expected_password: &[u8], connect_result: ConnectResultCode,
    ) {
        let responder = match poll_client_sme_request(exec, server) {
            Poll::Ready(ClientSmeRequest::Connect { req, txn, .. }) => {
                assert_eq!(expected_ssid, &req.ssid[..]);
                assert_eq!(expected_password, &req.password[..]);
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
        exec: &mut fasync::Executor, next_client_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
    ) -> Poll<ClientSmeRequest> {
        exec.run_until_stalled(next_client_sme_req)
            .map(|(req, stream)| {
                *next_client_sme_req = stream.into_future();
                req.expect("did not expect the ClientSmeRequestStream to end")
                    .expect("error polling client sme request stream")
            })
    }

    fn create_client_sme_proxy() -> (fidl_sme::ClientSmeProxy, ClientSmeRequestStream) {
        let (proxy, server) = endpoints::create_proxy::<ClientSmeMarker>()
            .expect("failed to create sme client channel");
        let server = server
            .into_stream()
            .expect("failed to create a client sme response stream");
        (proxy, server)
    }

    fn create_wlan_service_util() -> (DeviceServiceProxy, DeviceServiceRequestStream) {
        let (proxy, server) = endpoints::create_proxy::<DeviceServiceMarker>()
            .expect("failed to create a wlan_service channel for tests");
        let server = server
            .into_stream()
            .expect("failed to create a wlan_service response stream");
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
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
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
        exec: &mut fasync::Executor, server: &mut StreamFuture<ClientSmeRequestStream>,
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
                    protected: true,
                    compatible: true,
                };
                Some(Box::new(bss_info))
            }
        }
    }

    fn send_status_response(
        exec: &mut fasync::Executor, server: &mut StreamFuture<ClientSmeRequestStream>,
        connected_to: Vec<u8>, connecting_to_ssid: Vec<u8>,
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

        rsp.send(&mut response)
            .expect("Failed to send StatusResponse.");
    }
}
