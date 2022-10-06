// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        access_point::{state_machine as ap_fsm, types as ap_types},
        client::types as client_types,
        mode_management::{iface_manager_types::*, Defect},
        regulatory_manager::REGION_CODE_LEN,
    },
    anyhow::Error,
    async_trait::async_trait,
    fidl_fuchsia_wlan_sme as fidl_sme,
    futures::channel::{mpsc, oneshot},
};

#[async_trait]
pub trait IfaceManagerApi {
    /// Finds the client iface with the given network configuration, disconnects from the network,
    /// and removes the client's network configuration information.
    async fn disconnect(
        &mut self,
        network_id: ap_types::NetworkIdentifier,
        reason: client_types::DisconnectReason,
    ) -> Result<(), Error>;

    /// Selects a client iface, ensures that a ClientSmeProxy and client connectivity state machine
    /// exists for the iface, and then issues a connect request to the client connectivity state
    /// machine.
    async fn connect(&mut self, connect_req: client_types::ConnectRequest) -> Result<(), Error>;

    /// Marks an existing client interface as unconfigured.
    async fn record_idle_client(&mut self, iface_id: u16) -> Result<(), Error>;

    /// Returns an indication of whether or not any client interfaces are unconfigured.
    async fn has_idle_client(&mut self) -> Result<bool, Error>;

    /// Queries the properties of the provided interface ID and internally accounts for the newly
    /// added client or AP.
    async fn handle_added_iface(&mut self, iface_id: u16) -> Result<(), Error>;

    /// Removes all internal references of the provided interface ID.
    async fn handle_removed_iface(&mut self, iface_id: u16) -> Result<(), Error>;

    /// Selects a client iface and return it for use with a scan
    async fn get_sme_proxy_for_scan(&mut self) -> Result<fidl_sme::ClientSmeProxy, Error>;

    /// Disconnects all configured clients and disposes of all client ifaces before instructing
    /// the PhyManager to stop client connections.
    async fn stop_client_connections(
        &mut self,
        reason: client_types::DisconnectReason,
    ) -> Result<(), Error>;

    /// Passes the call to start client connections through to the PhyManager.
    async fn start_client_connections(&mut self) -> Result<(), Error>;

    /// Starts an AP interface with the provided configuration.
    async fn start_ap(&mut self, config: ap_fsm::ApConfig) -> Result<oneshot::Receiver<()>, Error>;

    /// Stops the AP interface corresponding to the provided configuration and destroys it.
    async fn stop_ap(&mut self, ssid: Ssid, password: Vec<u8>) -> Result<(), Error>;

    /// Stops all AP interfaces and destroys them.
    async fn stop_all_aps(&mut self) -> Result<(), Error>;

    /// Returns whether or not there is an iface that can support a WPA3 connection.
    async fn has_wpa3_capable_client(&mut self) -> Result<bool, Error>;

    /// Sets the country code for WLAN PHYs.
    async fn set_country(
        &mut self,
        country_code: Option<[u8; REGION_CODE_LEN]>,
    ) -> Result<(), Error>;

    /// Logs a defect encountered while attempting to control an interface.
    async fn report_defect(&mut self, defect: Defect) -> Result<(), Error>;
}

#[derive(Clone)]
pub struct IfaceManager {
    pub sender: mpsc::Sender<IfaceManagerRequest>,
}

#[async_trait]
impl IfaceManagerApi for IfaceManager {
    async fn disconnect(
        &mut self,
        network_id: ap_types::NetworkIdentifier,
        reason: client_types::DisconnectReason,
    ) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = DisconnectRequest { network_id, responder, reason };
        self.sender.try_send(IfaceManagerRequest::Disconnect(req))?;

        receiver.await?
    }

    async fn connect(&mut self, connect_req: client_types::ConnectRequest) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = ConnectRequest { request: connect_req, responder };
        self.sender.try_send(IfaceManagerRequest::Connect(req))?;

        receiver.await?
    }

    async fn record_idle_client(&mut self, iface_id: u16) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = RecordIdleIfaceRequest { iface_id, responder };
        self.sender.try_send(IfaceManagerRequest::RecordIdleIface(req))?;
        receiver.await?;
        Ok(())
    }

    async fn has_idle_client(&mut self) -> Result<bool, Error> {
        let (responder, receiver) = oneshot::channel();
        let req = HasIdleIfaceRequest { responder };
        self.sender.try_send(IfaceManagerRequest::HasIdleIface(req))?;
        receiver.await.map_err(|e| e.into())
    }

    async fn handle_added_iface(&mut self, iface_id: u16) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = AddIfaceRequest { iface_id, responder };
        self.sender.try_send(IfaceManagerRequest::AddIface(req))?;
        receiver.await?;
        Ok(())
    }

    async fn handle_removed_iface(&mut self, iface_id: u16) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = RemoveIfaceRequest { iface_id, responder };
        self.sender.try_send(IfaceManagerRequest::RemoveIface(req))?;
        receiver.await?;
        Ok(())
    }

    async fn get_sme_proxy_for_scan(
        &mut self,
    ) -> Result<fidl_fuchsia_wlan_sme::ClientSmeProxy, Error> {
        let (responder, receiver) = oneshot::channel();
        let req = ScanProxyRequest { responder };
        self.sender.try_send(IfaceManagerRequest::GetScanProxy(req))?;
        receiver.await?
    }

    async fn stop_client_connections(
        &mut self,
        reason: client_types::DisconnectReason,
    ) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = StopClientConnectionsRequest { responder, reason };
        self.sender.try_send(IfaceManagerRequest::StopClientConnections(req))?;
        receiver.await?
    }

    async fn start_client_connections(&mut self) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = StartClientConnectionsRequest { responder };
        self.sender.try_send(IfaceManagerRequest::StartClientConnections(req))?;
        receiver.await?
    }

    async fn start_ap(&mut self, config: ap_fsm::ApConfig) -> Result<oneshot::Receiver<()>, Error> {
        let (responder, receiver) = oneshot::channel();
        let req = StartApRequest { config, responder };
        self.sender.try_send(IfaceManagerRequest::StartAp(req))?;
        receiver.await?
    }

    async fn stop_ap(&mut self, ssid: Ssid, password: Vec<u8>) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = StopApRequest { ssid, password, responder };
        self.sender.try_send(IfaceManagerRequest::StopAp(req))?;
        receiver.await?
    }

    async fn stop_all_aps(&mut self) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = StopAllApsRequest { responder };
        self.sender.try_send(IfaceManagerRequest::StopAllAps(req))?;
        receiver.await?
    }

    async fn has_wpa3_capable_client(&mut self) -> Result<bool, Error> {
        let (responder, receiver) = oneshot::channel();
        let req = HasWpa3IfaceRequest { responder };
        self.sender.try_send(IfaceManagerRequest::HasWpa3Iface(req))?;
        Ok(receiver.await?)
    }

    async fn set_country(
        &mut self,
        country_code: Option<[u8; REGION_CODE_LEN]>,
    ) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = SetCountryRequest { country_code, responder };
        self.sender.try_send(IfaceManagerRequest::SetCountry(req))?;
        receiver.await?
    }

    async fn report_defect(&mut self, defect: Defect) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = ReportDefectRequest { defect, responder };
        self.sender.try_send(IfaceManagerRequest::ReportDefect(req))?;
        Ok(receiver.await?)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            access_point::types, config_management::network_config::Credential,
            mode_management::PhyFailure,
        },
        anyhow::format_err,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_wlan_common as fidl_common, fuchsia_async as fasync,
        futures::{future::BoxFuture, task::Poll, StreamExt},
        pin_utils::pin_mut,
        std::convert::TryFrom,
        test_case::test_case,
        wlan_common::{assert_variant, channel::Cbw, RadioConfig},
    };

    struct TestValues {
        exec: fasync::TestExecutor,
        iface_manager: IfaceManager,
        receiver: mpsc::Receiver<IfaceManagerRequest>,
    }

    fn test_setup() -> TestValues {
        let exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (sender, receiver) = mpsc::channel(1);
        TestValues { exec, iface_manager: IfaceManager { sender }, receiver }
    }

    #[derive(Clone)]
    enum NegativeTestFailureMode {
        RequestFailure,
        OperationFailure,
        ServiceFailure,
    }

    fn handle_negative_test_result_responder<T: std::fmt::Debug>(
        responder: oneshot::Sender<Result<T, Error>>,
        failure_mode: NegativeTestFailureMode,
    ) {
        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {
                panic!("Test bug: this request should have been handled previously")
            }
            NegativeTestFailureMode::OperationFailure => {
                responder
                    .send(Err(format_err!("operation failed")))
                    .expect("failed to send response");
            }
            NegativeTestFailureMode::ServiceFailure => {
                // Just drop the responder so that the client side sees a failure.
                drop(responder);
            }
        }
    }

    fn handle_negative_test_responder<T: std::fmt::Debug>(
        responder: oneshot::Sender<T>,
        failure_mode: NegativeTestFailureMode,
    ) {
        match failure_mode {
            NegativeTestFailureMode::RequestFailure | NegativeTestFailureMode::OperationFailure => {
                panic!("Test bug: invalid operation")
            }
            NegativeTestFailureMode::ServiceFailure => {
                // Just drop the responder so that the client side sees a failure.
                drop(responder);
            }
        }
    }

    fn iface_manager_api_negative_test(
        mut receiver: mpsc::Receiver<IfaceManagerRequest>,
        failure_mode: NegativeTestFailureMode,
    ) -> BoxFuture<'static, ()> {
        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {
                // Drop the receiver so that no requests can be made.
                drop(receiver);
                let fut = async move {};
                return Box::pin(fut);
            }
            _ => {}
        }

        let fut = async move {
            let req = match receiver.next().await {
                Some(req) => req,
                None => panic!("no request available."),
            };

            match req {
                // Result<(), Err> responder values
                IfaceManagerRequest::Disconnect(DisconnectRequest { responder, .. })
                | IfaceManagerRequest::StopClientConnections(StopClientConnectionsRequest {
                    responder,
                    ..
                })
                | IfaceManagerRequest::StartClientConnections(StartClientConnectionsRequest {
                    responder,
                })
                | IfaceManagerRequest::StopAp(StopApRequest { responder, .. })
                | IfaceManagerRequest::StopAllAps(StopAllApsRequest { responder, .. })
                | IfaceManagerRequest::SetCountry(SetCountryRequest { responder, .. })
                | IfaceManagerRequest::Connect(ConnectRequest { responder, .. }) => {
                    handle_negative_test_result_responder(responder, failure_mode);
                }
                // Result<ClientSmeProxy, Err>
                IfaceManagerRequest::GetScanProxy(ScanProxyRequest { responder }) => {
                    handle_negative_test_result_responder(responder, failure_mode);
                }
                // Result<oneshot::Receiver<()>, Err>
                IfaceManagerRequest::StartAp(StartApRequest { responder, .. }) => {
                    handle_negative_test_result_responder(responder, failure_mode);
                }
                // Unit responder values
                IfaceManagerRequest::RecordIdleIface(RecordIdleIfaceRequest {
                    responder, ..
                })
                | IfaceManagerRequest::AddIface(AddIfaceRequest { responder, .. })
                | IfaceManagerRequest::RemoveIface(RemoveIfaceRequest { responder, .. })
                | IfaceManagerRequest::ReportDefect(ReportDefectRequest { responder, .. }) => {
                    handle_negative_test_responder(responder, failure_mode);
                }
                // Boolean responder values
                IfaceManagerRequest::HasIdleIface(HasIdleIfaceRequest { responder })
                | IfaceManagerRequest::HasWpa3Iface(HasWpa3IfaceRequest { responder }) => {
                    handle_negative_test_responder(responder, failure_mode);
                }
            }
        };
        Box::pin(fut)
    }

    #[fuchsia::test]
    fn test_disconnect_succeeds() {
        let mut test_values = test_setup();

        // Issue a disconnect command and wait for the command to be sent.
        let req = ap_types::NetworkIdentifier {
            ssid: Ssid::try_from("foo").unwrap(),
            security_type: ap_types::SecurityType::None,
        };
        let req_reason = client_types::DisconnectReason::NetworkUnsaved;
        let disconnect_fut = test_values.iface_manager.disconnect(req.clone(), req_reason);
        pin_mut!(disconnect_fut);

        assert_variant!(test_values.exec.run_until_stalled(&mut disconnect_fut), Poll::Pending);

        // Verify that the receiver sees the command and send back a response.
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::Disconnect(DisconnectRequest {
                network_id, responder, reason
            }))) => {
                assert_eq!(network_id, req);
                assert_eq!(reason, req_reason);
                responder.send(Ok(())).expect("failed to send disconnect response");
            }
        );

        // Verify that the disconnect requestr receives the response.
        assert_variant!(
            test_values.exec.run_until_stalled(&mut disconnect_fut),
            Poll::Ready(Ok(()))
        );
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn disconnect_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Issue a disconnect command and wait for the command to be sent.
        let req = ap_types::NetworkIdentifier {
            ssid: Ssid::try_from("foo").unwrap(),
            security_type: ap_types::SecurityType::None,
        };
        let disconnect_fut = test_values
            .iface_manager
            .disconnect(req.clone(), client_types::DisconnectReason::NetworkUnsaved);
        pin_mut!(disconnect_fut);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut disconnect_fut),
                    Poll::Pending
                );
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the disconnect requestr receives the response.
        assert_variant!(
            test_values.exec.run_until_stalled(&mut disconnect_fut),
            Poll::Ready(Err(_))
        );
    }

    #[fuchsia::test]
    fn test_connect_succeeds() {
        let mut test_values = test_setup();

        // Issue a connect command and wait for the command to be sent.
        let req = client_types::ConnectRequest {
            target: client_types::ConnectionCandidate {
                network: client_types::NetworkIdentifier {
                    ssid: Ssid::try_from("foo").unwrap(),
                    security_type: client_types::SecurityType::None,
                },
                credential: Credential::None,
                scanned: None,
            },
            reason: client_types::ConnectReason::FidlConnectRequest,
        };
        let connect_fut = test_values.iface_manager.connect(req.clone());
        pin_mut!(connect_fut);

        assert_variant!(test_values.exec.run_until_stalled(&mut connect_fut), Poll::Pending);

        // Verify that the receiver sees the command and send back a response.
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::Connect(ConnectRequest {
                request, responder
            }))) => {
                assert_eq!(request, req);
                responder.send(Ok(())).expect("failed to send connect response");
            }
        );

        // Verify that the connect requestr receives the response.
        assert_variant!(test_values.exec.run_until_stalled(&mut connect_fut), Poll::Ready(Ok(_)));
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn connect_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Issue a connect command and wait for the command to be sent.
        let req = client_types::ConnectRequest {
            target: client_types::ConnectionCandidate {
                network: client_types::NetworkIdentifier {
                    ssid: Ssid::try_from("foo").unwrap(),
                    security_type: client_types::SecurityType::None,
                },
                credential: Credential::None,
                scanned: None,
            },
            reason: client_types::ConnectReason::FidlConnectRequest,
        };
        let connect_fut = test_values.iface_manager.connect(req.clone());
        pin_mut!(connect_fut);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut connect_fut),
                    Poll::Pending
                );
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the request completes in error.
        assert_variant!(test_values.exec.run_until_stalled(&mut connect_fut), Poll::Ready(Err(_)));
    }

    #[fuchsia::test]
    fn test_record_idle_client_succeeds() {
        let mut test_values = test_setup();

        // Request that an idle client be recorded.
        let iface_id = 123;
        let idle_client_fut = test_values.iface_manager.record_idle_client(iface_id);
        pin_mut!(idle_client_fut);

        assert_variant!(test_values.exec.run_until_stalled(&mut idle_client_fut), Poll::Pending);

        // Verify that the receiver sees the request.
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(
                Some(IfaceManagerRequest::RecordIdleIface(RecordIdleIfaceRequest{ iface_id: 123, responder}))
            ) => {
                responder.send(()).expect("failed to send idle iface response");
            }
        );

        // Verify that the client sees the response.
        assert_variant!(
            test_values.exec.run_until_stalled(&mut idle_client_fut),
            Poll::Ready(Ok(()))
        );
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn test_record_idle_client_service_failure(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Request that an idle client be recorded.
        let iface_id = 123;
        let idle_client_fut = test_values.iface_manager.record_idle_client(iface_id);
        pin_mut!(idle_client_fut);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut idle_client_fut),
                    Poll::Pending
                );
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the client side finishes
        assert_variant!(
            test_values.exec.run_until_stalled(&mut idle_client_fut),
            Poll::Ready(Err(_))
        );
    }

    #[fuchsia::test]
    fn test_has_idle_client_success() {
        let mut test_values = test_setup();

        // Query whether there is an idle client
        let idle_client_fut = test_values.iface_manager.has_idle_client();
        pin_mut!(idle_client_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut idle_client_fut), Poll::Pending);

        // Verify that the service sees the query
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(
                Some(IfaceManagerRequest::HasIdleIface(HasIdleIfaceRequest{ responder}))
            ) => responder.send(true).expect("failed to reply to idle client query")
        );

        // Verify that the client side finishes
        assert_variant!(
            test_values.exec.run_until_stalled(&mut idle_client_fut),
            Poll::Ready(Ok(true))
        );
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn idle_client_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Query whether there is an idle client
        let idle_client_fut = test_values.iface_manager.has_idle_client();
        pin_mut!(idle_client_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut idle_client_fut), Poll::Pending);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut idle_client_fut),
                    Poll::Pending
                );
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the request completes in error.
        assert_variant!(
            test_values.exec.run_until_stalled(&mut idle_client_fut),
            Poll::Ready(Err(_))
        );
    }

    #[fuchsia::test]
    fn test_add_iface_success() {
        let mut test_values = test_setup();

        // Add an interface
        let added_iface_fut = test_values.iface_manager.handle_added_iface(123);
        pin_mut!(added_iface_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut added_iface_fut), Poll::Pending);

        // Verify that the service sees the query
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(
                Some(IfaceManagerRequest::AddIface(AddIfaceRequest{ iface_id: 123, responder }))
            ) => {
                responder.send(()).expect("failed to respond while adding iface");
            }
        );

        // Verify that the client side finishes
        assert_variant!(
            test_values.exec.run_until_stalled(&mut added_iface_fut),
            Poll::Ready(Ok(()))
        );
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn add_iface_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Add an interface
        let added_iface_fut = test_values.iface_manager.handle_added_iface(123);
        pin_mut!(added_iface_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut added_iface_fut), Poll::Pending);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut added_iface_fut),
                    Poll::Pending
                );
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the request completes in error.
        assert_variant!(
            test_values.exec.run_until_stalled(&mut added_iface_fut),
            Poll::Ready(Err(_))
        );
    }

    #[fuchsia::test]
    fn test_remove_iface_success() {
        let mut test_values = test_setup();

        // Report the removal of an interface.
        let removed_iface_fut = test_values.iface_manager.handle_removed_iface(123);
        pin_mut!(removed_iface_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut removed_iface_fut), Poll::Pending);

        // Verify that the service sees the query
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(
                Some(IfaceManagerRequest::RemoveIface(RemoveIfaceRequest{ iface_id: 123, responder }))
            ) => {
                responder.send(()).expect("failed to respond while adding iface");
            }
        );

        // Verify that the client side finishes
        assert_variant!(
            test_values.exec.run_until_stalled(&mut removed_iface_fut),
            Poll::Ready(Ok(()))
        );
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn remove_iface_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Report the removal of an interface.
        let removed_iface_fut = test_values.iface_manager.handle_removed_iface(123);
        pin_mut!(removed_iface_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut removed_iface_fut), Poll::Pending);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut removed_iface_fut),
                    Poll::Pending
                );
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the client side finishes
        assert_variant!(
            test_values.exec.run_until_stalled(&mut removed_iface_fut),
            Poll::Ready(Err(_))
        );
    }

    #[fuchsia::test]
    fn test_get_scan_proxy_success() {
        let mut test_values = test_setup();

        // Request a scan
        let scan_proxy_fut = test_values.iface_manager.get_sme_proxy_for_scan();
        pin_mut!(scan_proxy_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut scan_proxy_fut), Poll::Pending);

        // Verify that the service sees the request.
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::GetScanProxy(ScanProxyRequest{
                responder
            }))) => {
                let (proxy, _) = create_proxy::<fidl_sme::ClientSmeMarker>()
                    .expect("failed to create scan sme proxy");
                responder.send(Ok(proxy)).expect("failed to send scan sme proxy");
            }
        );

        // Verify that the client side gets the scan proxy
        assert_variant!(
            test_values.exec.run_until_stalled(&mut scan_proxy_fut),
            Poll::Ready(Ok(_))
        );
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn scan_proxy_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Request a scan
        let scan_proxy_fut = test_values.iface_manager.get_sme_proxy_for_scan();
        pin_mut!(scan_proxy_fut);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut scan_proxy_fut),
                    Poll::Pending
                );
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that an error is returned.
        assert_variant!(
            test_values.exec.run_until_stalled(&mut scan_proxy_fut),
            Poll::Ready(Err(_))
        );
    }

    #[fuchsia::test]
    fn test_stop_client_connections_succeeds() {
        let mut test_values = test_setup();

        // Request a scan
        let stop_fut = test_values.iface_manager.stop_client_connections(
            client_types::DisconnectReason::FidlStopClientConnectionsRequest,
        );
        pin_mut!(stop_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Pending);

        // Verify that the service sees the request.
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::StopClientConnections(StopClientConnectionsRequest{
                responder, reason
            }))) => {
                assert_eq!(reason, client_types::DisconnectReason::FidlStopClientConnectionsRequest);
                responder.send(Ok(())).expect("failed sending stop client connections response");
            }
        );

        // Verify that the client side gets the response.
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(())));
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn stop_client_connections_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Request a scan
        let stop_fut = test_values.iface_manager.stop_client_connections(
            client_types::DisconnectReason::FidlStopClientConnectionsRequest,
        );
        pin_mut!(stop_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Pending);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Pending);
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the client side gets the response.
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Ready(Err(_)));
    }

    #[fuchsia::test]
    fn test_start_client_connections_succeeds() {
        let mut test_values = test_setup();

        // Start client connections
        let start_fut = test_values.iface_manager.start_client_connections();
        pin_mut!(start_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut start_fut), Poll::Pending);

        // Verify that the service sees the request.
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::StartClientConnections(StartClientConnectionsRequest{
                responder
            }))) => {
                responder.send(Ok(())).expect("failed sending stop client connections response");
            }
        );

        // Verify that the client side gets the response.
        assert_variant!(test_values.exec.run_until_stalled(&mut start_fut), Poll::Ready(Ok(())));
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn start_client_connections_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Start client connections
        let start_fut = test_values.iface_manager.start_client_connections();
        pin_mut!(start_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut start_fut), Poll::Pending);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(test_values.exec.run_until_stalled(&mut start_fut), Poll::Pending);
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the client side gets the response.
        assert_variant!(test_values.exec.run_until_stalled(&mut start_fut), Poll::Ready(Err(_)));
    }

    fn create_ap_config() -> ap_fsm::ApConfig {
        ap_fsm::ApConfig {
            id: types::NetworkIdentifier {
                ssid: Ssid::try_from("foo").unwrap(),
                security_type: types::SecurityType::None,
            },
            credential: vec![],
            radio_config: RadioConfig::new(fidl_common::WlanPhyType::Ht, Cbw::Cbw20, 6),
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        }
    }

    #[fuchsia::test]
    fn test_start_ap_succeeds() {
        let mut test_values = test_setup();

        // Start an AP
        let start_fut = test_values.iface_manager.start_ap(create_ap_config());
        pin_mut!(start_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut start_fut), Poll::Pending);

        // Verify the service sees the request
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::StartAp(StartApRequest{
                config, responder
            }))) => {
                assert_eq!(config, create_ap_config());

                let (_, receiver) = oneshot::channel();
                responder.send(Ok(receiver)).expect("failed to send start AP response");
            }
        );

        // Verify that the client gets the response
        assert_variant!(test_values.exec.run_until_stalled(&mut start_fut), Poll::Ready(Ok(_)));
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn start_ap_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Start an AP
        let start_fut = test_values.iface_manager.start_ap(create_ap_config());
        pin_mut!(start_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut start_fut), Poll::Pending);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(test_values.exec.run_until_stalled(&mut start_fut), Poll::Pending);
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the client gets the response
        assert_variant!(test_values.exec.run_until_stalled(&mut start_fut), Poll::Ready(Err(_)));
    }

    #[fuchsia::test]
    fn test_stop_ap_succeeds() {
        let mut test_values = test_setup();

        // Stop an AP
        let stop_fut = test_values
            .iface_manager
            .stop_ap(Ssid::try_from("foo").unwrap(), "bar".as_bytes().to_vec());
        pin_mut!(stop_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Pending);

        // Verify the service sees the request
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::StopAp(StopApRequest{
                ssid, password, responder
            }))) => {
                assert_eq!(ssid, Ssid::try_from("foo").unwrap());
                assert_eq!(password, "bar".as_bytes().to_vec());

                responder.send(Ok(())).expect("failed to send stop AP response");
            }
        );

        // Verify that the client gets the response
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn stop_ap_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Stop an AP
        let stop_fut = test_values
            .iface_manager
            .stop_ap(Ssid::try_from("foo").unwrap(), "bar".as_bytes().to_vec());
        pin_mut!(stop_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Pending);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Pending);
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the client gets the response
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Ready(Err(_)));
    }

    #[fuchsia::test]
    fn test_stop_all_aps_succeeds() {
        let mut test_values = test_setup();

        // Stop an AP
        let stop_fut = test_values.iface_manager.stop_all_aps();
        pin_mut!(stop_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Pending);

        // Verify the service sees the request
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);
        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::StopAllAps(StopAllApsRequest{
                responder
            }))) => {
                responder.send(Ok(())).expect("failed to send stop AP response");
            }
        );

        // Verify that the client gets the response
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(_)));
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn stop_all_aps_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Stop an AP
        let stop_fut = test_values.iface_manager.stop_all_aps();
        pin_mut!(stop_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Pending);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Pending);
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the client gets the response
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Ready(Err(_)));
    }

    #[fuchsia::test]
    fn test_has_wpa3_capable_client_success() {
        let mut test_values = test_setup();

        // Query whether there is an iface that can do WPA3.
        let has_wpa3_fut = test_values.iface_manager.has_wpa3_capable_client();
        pin_mut!(has_wpa3_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut has_wpa3_fut), Poll::Pending);

        // Verify that the service sees the query
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(
                Some(IfaceManagerRequest::HasWpa3Iface(HasWpa3IfaceRequest{ responder}))
            ) => responder.send(true).expect("failed to reply to wpa3 iface query")
        );

        // Verify that the client side finishes
        assert_variant!(
            test_values.exec.run_until_stalled(&mut has_wpa3_fut),
            Poll::Ready(Ok(true))
        );
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn has_wpa3_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Query whether there is an iface with WPA3 support
        let has_wpa3_fut = test_values.iface_manager.has_wpa3_capable_client();
        pin_mut!(has_wpa3_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut has_wpa3_fut), Poll::Pending);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut has_wpa3_fut),
                    Poll::Pending
                );
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the request completes in error.
        assert_variant!(test_values.exec.run_until_stalled(&mut has_wpa3_fut), Poll::Ready(Err(_)));
    }

    #[fuchsia::test]
    fn test_set_country_succeeds() {
        let mut test_values = test_setup();

        // Set country code
        let set_country_fut = test_values.iface_manager.set_country(None);
        pin_mut!(set_country_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut set_country_fut), Poll::Pending);

        // Verify the service sees the request
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::SetCountry(SetCountryRequest{
                country_code: None,
                responder
            }))) => {
                responder.send(Ok(())).expect("failed to send stop AP response");
            }
        );

        // Verify that the client gets the response
        assert_variant!(
            test_values.exec.run_until_stalled(&mut set_country_fut),
            Poll::Ready(Ok(_))
        );
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn set_country_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Set country code
        let set_country_fut = test_values.iface_manager.set_country(None);
        pin_mut!(set_country_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut set_country_fut), Poll::Pending);
        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut set_country_fut),
                    Poll::Pending
                );
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the client gets the response
        assert_variant!(
            test_values.exec.run_until_stalled(&mut set_country_fut),
            Poll::Ready(Err(_))
        );
    }

    #[fuchsia::test]
    fn test_report_defect_succeeds() {
        let mut test_values = test_setup();
        let defect = Defect::Phy(PhyFailure::IfaceCreationFailure { phy_id: 2 });
        let report_fut = test_values.iface_manager.report_defect(defect);
        pin_mut!(report_fut);

        assert_variant!(test_values.exec.run_until_stalled(&mut report_fut), Poll::Pending);

        // Verify that the request has come through.
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::ReportDefect(ReportDefectRequest {
                defect, responder
            }))) => {
                assert_eq!(defect, Defect::Phy(PhyFailure::IfaceCreationFailure {phy_id: 2}));
                responder.send(()).expect("failed to send defect response");
            }
        );

        // Verify that the defect reporter receives the response.
        assert_variant!(test_values.exec.run_until_stalled(&mut report_fut), Poll::Ready(Ok(())));
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    #[fuchsia::test(add_test_attr = false)]
    fn report_defect_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        let defect = Defect::Phy(PhyFailure::IfaceCreationFailure { phy_id: 2 });
        let report_fut = test_values.iface_manager.report_defect(defect);
        pin_mut!(report_fut);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(test_values.exec.run_until_stalled(&mut report_fut), Poll::Pending);
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that the client gets the response
        assert_variant!(test_values.exec.run_until_stalled(&mut report_fut), Poll::Ready(Err(_)));
    }
}
