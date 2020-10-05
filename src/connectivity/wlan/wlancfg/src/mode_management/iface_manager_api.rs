// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        access_point::{state_machine as ap_fsm, types as ap_types},
        client::state_machine as client_fsm,
        mode_management::iface_manager_types::*,
    },
    anyhow::Error,
    async_trait::async_trait,
    fidl_fuchsia_wlan_sme as fidl_sme,
    futures::channel::{mpsc, oneshot},
};

#[async_trait]
pub(crate) trait IfaceManagerApi {
    /// Finds the client iface with the given network configuration, disconnects from the network,
    /// and removes the client's network configuration information.
    async fn disconnect(&mut self, network_id: ap_types::NetworkIdentifier) -> Result<(), Error>;

    /// Selects a client iface, ensures that a ClientSmeProxy and client connectivity state machine
    /// exists for the iface, and then issues a connect request to the client connectivity state
    /// machine.
    async fn connect(
        &mut self,
        connect_req: client_fsm::ConnectRequest,
    ) -> Result<oneshot::Receiver<()>, Error>;

    /// Marks an existing client interface as unconfigured.
    async fn record_idle_client(&mut self, iface_id: u16) -> Result<(), Error>;

    /// Returns an indication of whether or not any client interfaces are unconfigured.
    async fn has_idle_client(&mut self) -> Result<bool, Error>;

    /// Queries the properties of the provided interface ID and internally accounts for the newly
    /// added client or AP.
    async fn handle_added_iface(&mut self, iface_id: u16) -> Result<(), Error>;

    /// Removes all internal references of the provided interface ID.
    async fn handle_removed_iface(&mut self, iface_id: u16) -> Result<(), Error>;

    /// Selects a client iface and issues a scan request.  On success, the `ScanTransactionProxy`
    /// is returned to the caller so that the scan results can be monitored.
    async fn scan(
        &mut self,
        scan_request: fidl_sme::ScanRequest,
    ) -> Result<fidl_sme::ScanTransactionProxy, Error>;

    /// Disconnects all configured clients and disposes of all client ifaces before instructing
    /// the PhyManager to stop client connections.
    async fn stop_client_connections(&mut self) -> Result<(), Error>;

    /// Passes the call to start client connections through to the PhyManager.
    async fn start_client_connections(&mut self) -> Result<(), Error>;

    /// Starts an AP interface with the provided configuration.
    async fn start_ap(
        &mut self,
        config: ap_fsm::ApConfig,
    ) -> Result<oneshot::Receiver<fidl_sme::StartApResultCode>, Error>;

    /// Stops the AP interface corresponding to the provided configuration and destroys it.
    async fn stop_ap(&mut self, ssid: Vec<u8>, password: Vec<u8>) -> Result<(), Error>;

    /// Stops all AP interfaces and destroys them.
    async fn stop_all_aps(&mut self) -> Result<(), Error>;
}

#[derive(Clone)]
pub(crate) struct IfaceManager {
    pub sender: mpsc::Sender<IfaceManagerRequest>,
}

#[async_trait]
impl IfaceManagerApi for IfaceManager {
    async fn disconnect(&mut self, network_id: ap_types::NetworkIdentifier) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = DisconnectRequest { network_id, responder };
        self.sender.try_send(IfaceManagerRequest::Disconnect(req))?;

        receiver.await?
    }

    async fn connect(
        &mut self,
        connect_req: client_fsm::ConnectRequest,
    ) -> Result<oneshot::Receiver<()>, Error> {
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

    async fn scan(
        &mut self,
        scan_request: fidl_sme::ScanRequest,
    ) -> Result<fidl_sme::ScanTransactionProxy, Error> {
        let (responder, receiver) = oneshot::channel();
        let req = ScanRequest { scan_request, responder };
        self.sender.try_send(IfaceManagerRequest::Scan(req))?;
        receiver.await?
    }

    async fn stop_client_connections(&mut self) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = StopClientConnectionsRequest { responder };
        self.sender.try_send(IfaceManagerRequest::StopClientConnections(req))?;
        receiver.await?
    }

    async fn start_client_connections(&mut self) -> Result<(), Error> {
        let (responder, receiver) = oneshot::channel();
        let req = StartClientConnectionsRequest { responder };
        self.sender.try_send(IfaceManagerRequest::StartClientConnections(req))?;
        receiver.await?
    }

    async fn start_ap(
        &mut self,
        config: ap_fsm::ApConfig,
    ) -> Result<oneshot::Receiver<fidl_sme::StartApResultCode>, Error> {
        let (responder, receiver) = oneshot::channel();
        let req = StartApRequest { config, responder };
        self.sender.try_send(IfaceManagerRequest::StartAp(req))?;
        receiver.await?
    }

    async fn stop_ap(&mut self, ssid: Vec<u8>, password: Vec<u8>) -> Result<(), Error> {
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
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            access_point::types, config_management::network_config::Credential,
            util::logger::set_logger_for_test,
        },
        anyhow::format_err,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_wlan_policy as fidl_policy, fuchsia_async as fasync,
        futures::{future::BoxFuture, task::Poll, StreamExt},
        pin_utils::pin_mut,
        test_case::test_case,
        wlan_common::{
            assert_variant,
            channel::{Cbw, Phy},
            RadioConfig,
        },
    };

    struct TestValues {
        exec: fasync::Executor,
        iface_manager: IfaceManager,
        receiver: mpsc::Receiver<IfaceManagerRequest>,
    }

    fn test_setup() -> TestValues {
        set_logger_for_test();
        let exec = fasync::Executor::new().expect("failed to create an executor");
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
                IfaceManagerRequest::Disconnect(DisconnectRequest { responder, .. })
                | IfaceManagerRequest::StopClientConnections(StopClientConnectionsRequest {
                    responder,
                })
                | IfaceManagerRequest::StartClientConnections(StartClientConnectionsRequest {
                    responder,
                })
                | IfaceManagerRequest::StopAp(StopApRequest { responder, .. })
                | IfaceManagerRequest::StopAllAps(StopAllApsRequest { responder, .. }) => {
                    handle_negative_test_result_responder(responder, failure_mode);
                }
                IfaceManagerRequest::Connect(ConnectRequest { responder, .. }) => {
                    handle_negative_test_result_responder(responder, failure_mode);
                }
                IfaceManagerRequest::Scan(ScanRequest { responder, .. }) => {
                    handle_negative_test_result_responder(responder, failure_mode);
                }
                IfaceManagerRequest::StartAp(StartApRequest { responder, .. }) => {
                    handle_negative_test_result_responder(responder, failure_mode);
                }
                IfaceManagerRequest::RecordIdleIface(RecordIdleIfaceRequest {
                    responder, ..
                })
                | IfaceManagerRequest::AddIface(AddIfaceRequest { responder, .. })
                | IfaceManagerRequest::RemoveIface(RemoveIfaceRequest { responder, .. }) => {
                    handle_negative_test_responder(responder, failure_mode);
                }
                IfaceManagerRequest::HasIdleIface(HasIdleIfaceRequest { responder }) => {
                    handle_negative_test_responder(responder, failure_mode);
                }
            }
        };
        Box::pin(fut)
    }

    #[test]
    fn test_disconnect_succeeds() {
        let mut test_values = test_setup();

        // Issue a disconnect command and wait for the command to be sent.
        let req = ap_types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: fidl_policy::SecurityType::None,
        };
        let disconnect_fut = test_values.iface_manager.disconnect(req.clone());
        pin_mut!(disconnect_fut);

        assert_variant!(test_values.exec.run_until_stalled(&mut disconnect_fut), Poll::Pending);

        // Verify that the receiver sees the command and send back a response.
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::Disconnect(DisconnectRequest {
                network_id, responder
            }))) => {
                assert_eq!(network_id, req);
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
    fn disconnect_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Issue a disconnect command and wait for the command to be sent.
        let req = ap_types::NetworkIdentifier {
            ssid: "foo".as_bytes().to_vec(),
            type_: fidl_policy::SecurityType::None,
        };
        let disconnect_fut = test_values.iface_manager.disconnect(req.clone());
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

    #[test]
    fn test_connect_succeeds() {
        let mut test_values = test_setup();

        // Issue a connect command and wait for the command to be sent.
        let req = client_fsm::ConnectRequest {
            network: fidl_policy::NetworkIdentifier {
                ssid: "foo".as_bytes().to_vec(),
                type_: fidl_policy::SecurityType::None,
            },
            credential: Credential::None,
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
                let (_, receiver) = oneshot::channel();
                responder.send(Ok(receiver)).expect("failed to send connect response");
            }
        );

        // Verify that the connect requestr receives the response.
        assert_variant!(test_values.exec.run_until_stalled(&mut connect_fut), Poll::Ready(Ok(_)));
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    fn connect_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Issue a connect command and wait for the command to be sent.
        let req = client_fsm::ConnectRequest {
            network: fidl_policy::NetworkIdentifier {
                ssid: "foo".as_bytes().to_vec(),
                type_: fidl_policy::SecurityType::None,
            },
            credential: Credential::None,
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

    #[test]
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

    #[test]
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

    #[test]
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

    #[test]
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

    #[test]
    fn test_scan_success() {
        let mut test_values = test_setup();

        // Request a scan
        let scan_fut = test_values
            .iface_manager
            .scan(fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {}));
        pin_mut!(scan_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut scan_fut), Poll::Pending);

        // Verify that the service sees the request.
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::Scan(ScanRequest{
                scan_request: _,
                responder
            }))) => {
                let (proxy, _) = create_proxy::<fidl_sme::ScanTransactionMarker>()
                    .expect("failed to create scan proxy");
                responder.send(Ok(proxy)).expect("failed to send scan proxy");
            }
        );

        // Verify that the client side gets the scan proxy
        assert_variant!(test_values.exec.run_until_stalled(&mut scan_fut), Poll::Ready(Ok(_)));
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    fn scan_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Request a scan
        let scan_fut = test_values
            .iface_manager
            .scan(fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {}));
        pin_mut!(scan_fut);

        let service_fut =
            iface_manager_api_negative_test(test_values.receiver, failure_mode.clone());
        pin_mut!(service_fut);

        match failure_mode {
            NegativeTestFailureMode::RequestFailure => {}
            _ => {
                // Run the request and the servicing of the request
                assert_variant!(test_values.exec.run_until_stalled(&mut scan_fut), Poll::Pending);
                assert_variant!(
                    test_values.exec.run_until_stalled(&mut service_fut),
                    Poll::Ready(())
                );
            }
        }

        // Verify that an error is returned.
        assert_variant!(test_values.exec.run_until_stalled(&mut scan_fut), Poll::Ready(Err(_)));
    }

    #[test]
    fn test_stop_client_connections_succeeds() {
        let mut test_values = test_setup();

        // Request a scan
        let stop_fut = test_values.iface_manager.stop_client_connections();
        pin_mut!(stop_fut);
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Pending);

        // Verify that the service sees the request.
        let next_message = test_values.receiver.next();
        pin_mut!(next_message);

        assert_variant!(
            test_values.exec.run_until_stalled(&mut next_message),
            Poll::Ready(Some(IfaceManagerRequest::StopClientConnections(StopClientConnectionsRequest{
                responder
            }))) => {
                responder.send(Ok(())).expect("failed sending stop client connections response");
            }
        );

        // Verify that the client side gets the response.
        assert_variant!(test_values.exec.run_until_stalled(&mut stop_fut), Poll::Ready(Ok(())));
    }

    #[test_case(NegativeTestFailureMode::RequestFailure; "request failure")]
    #[test_case(NegativeTestFailureMode::OperationFailure; "operation failure")]
    #[test_case(NegativeTestFailureMode::ServiceFailure; "service failure")]
    fn stop_client_connections_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Request a scan
        let stop_fut = test_values.iface_manager.stop_client_connections();
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

    #[test]
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
                ssid: "foo".as_bytes().to_vec(),
                type_: fidl_policy::SecurityType::None,
            },
            credential: vec![],
            radio_config: RadioConfig::new(Phy::Ht, Cbw::Cbw20, 6),
            mode: types::ConnectivityMode::Unrestricted,
            band: types::OperatingBand::Any,
        }
    }

    #[test]
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

    #[test]
    fn test_stop_ap_succeeds() {
        let mut test_values = test_setup();

        // Stop an AP
        let stop_fut =
            test_values.iface_manager.stop_ap("foo".as_bytes().to_vec(), "bar".as_bytes().to_vec());
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
                assert_eq!(ssid, "foo".as_bytes().to_vec());
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
    fn stop_ap_negative_test(failure_mode: NegativeTestFailureMode) {
        let mut test_values = test_setup();

        // Stop an AP
        let stop_fut =
            test_values.iface_manager.stop_ap("foo".as_bytes().to_vec(), "bar".as_bytes().to_vec());
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

    #[test]
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
}
