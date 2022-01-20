// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_utils::stream::{FlattenUnordered, FlattenUnorderedExt};
use core::{
    pin::Pin,
    task::{Context, Poll},
};
use fidl::client::QueryResponseFut;
use fidl_fuchsia_bluetooth_bredr as bredr;
use fidl_fuchsia_bluetooth_deviceid as di;
use fuchsia_zircon as zx;
use futures::{channel::mpsc, select, stream::FuturesUnordered, Future, FutureExt, StreamExt};
use tracing::{info, warn};

use crate::device_id::service_record::DeviceIdentificationService;
use crate::device_id::token::DeviceIdRequestToken;
use crate::error::Error;

/// Represents a classic advertisement with the upstream BR/EDR server.
pub struct BrEdrAdvertisement {
    /// The advertisement request future.
    pub adv_fut: QueryResponseFut<bredr::ProfileAdvertiseResult>,
    // Although we don't expect any connection requests, this channel must be kept alive for the
    // duration of the advertisement.
    pub connect_server: bredr::ConnectionReceiverRequestStream,
}

impl Future for BrEdrAdvertisement {
    type Output = Result<bredr::ProfileAdvertiseResult, fidl::Error>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.adv_fut.poll_unpin(cx)
    }
}

/// The server that manages the current set of Device Identification advertisements.
pub struct DeviceIdServer {
    /// The maximum number of concurrent DI advertisements this server supports.
    max_advertisements: usize,
    /// The connection to the upstream BR/EDR Profile server.
    profile: bredr::ProfileProxy,
    /// `DeviceIdentification` protocol requests from connected clients.
    device_id_requests: FlattenUnordered<mpsc::Receiver<di::DeviceIdentificationRequestStream>>,
    /// The current set of DI advertisements.
    device_id_advertisements: FuturesUnordered<DeviceIdRequestToken>,
}

impl DeviceIdServer {
    pub fn new(
        max_advertisements: usize,
        profile: bredr::ProfileProxy,
        device_id_clients: mpsc::Receiver<di::DeviceIdentificationRequestStream>,
    ) -> Self {
        Self {
            max_advertisements,
            profile,
            device_id_requests: device_id_clients.flatten_unordered(),
            device_id_advertisements: FuturesUnordered::new(),
        }
    }

    /// Returns the current number of DI records advertised.
    fn advertisement_size(&self) -> usize {
        self.device_id_advertisements.iter().map(|adv| adv.size()).sum()
    }

    fn contains_primary(&self) -> bool {
        self.device_id_advertisements.iter().filter(|adv| adv.contains_primary()).count() != 0
    }

    /// Returns true if the server has enough space to accommodate the `requested_space`.
    fn has_space(&self, requested_space: usize) -> bool {
        self.advertisement_size() + requested_space <= self.max_advertisements
    }

    pub async fn run(mut self) -> Result<(), Error> {
        loop {
            select! {
                device_id_request = self.device_id_requests.select_next_some() => {
                    match device_id_request {
                        Ok(req) => {
                            match self.handle_device_id_request(req) {
                                Ok(token) => self.device_id_advertisements.push(token),
                                Err(e) => info!("Couldn't handle DI request: {:?}", e),
                            }
                        }
                        Err(e) => warn!("Error receiving DI request: {:?}", e),
                    }
                }
                _finished = self.device_id_advertisements.select_next_some() => {}
                complete => {
                    break;
                }
            }
        }
        Ok(())
    }

    /// Advertises the provided service `defs` with the BR/EDR `profile` service. Returns a Future
    /// representing the advertisement on success, Error otherwise.
    fn advertise(
        profile: &bredr::ProfileProxy,
        defs: Vec<bredr::ServiceDefinition>,
    ) -> Result<BrEdrAdvertisement, Error> {
        let (connect_client, connect_server) =
            fidl::endpoints::create_request_stream::<bredr::ConnectionReceiverMarker>()?;
        let adv_fut = profile.advertise(
            &mut defs.into_iter(),
            bredr::ChannelParameters::EMPTY,
            connect_client,
        );
        Ok(BrEdrAdvertisement { adv_fut, connect_server })
    }

    /// Ensures that the `service` can be registered. Returns true if the service needs to be
    /// marked as primary, false otherwise.
    fn validate_service(&self, service: &DeviceIdentificationService) -> Result<bool, zx::Status> {
        // Ensure that the server has space for the requested records.
        if !self.has_space(service.size()) {
            return Err(zx::Status::NO_RESOURCES);
        }

        // Ensure that only one service is requesting primary.
        if self.contains_primary() && service.contains_primary() {
            return Err(zx::Status::ALREADY_EXISTS);
        }

        // If this is the first advertisement, it may need to be marked as primary
        let needs_primary = self.advertisement_size() == 0;
        Ok(needs_primary)
    }

    fn handle_device_id_request(
        &self,
        request: di::DeviceIdentificationRequest,
    ) -> Result<DeviceIdRequestToken, Error> {
        let di::DeviceIdentificationRequest::SetDeviceIdentification { records, token, responder } =
            request;
        info!("Received SetDeviceIdentification request: {:?}", records);

        let mut service = match DeviceIdentificationService::from_di_records(&records) {
            Err(e) => {
                let _ = responder.send(&mut Err(zx::Status::INVALID_ARGS.into_raw()));
                return Err(e);
            }
            Ok(defs) => defs,
        };

        match self.validate_service(&mut service) {
            Ok(true) => {
                let _ = service.maybe_update_primary();
            }
            Ok(false) => {}
            Err(e) => {
                let _ = responder.send(&mut Err(e.into_raw()));
                return Err(e.into());
            }
        }

        let client_request = match token.into_stream() {
            Err(e) => {
                // If the `token` is already closed, then the FIDL client no longer wants to
                // advertise. This is OK. However, return error so that this can be logged by the
                // server as the FIDL client request was not handled.
                if e.is_closed() {
                    let _ = responder.send(&mut Ok(()));
                } else {
                    let _ = responder.send(&mut Err(zx::Status::CANCELED.into_raw()));
                }
                return Err(e.into());
            }
            Ok(s) => s,
        };

        let bredr_advertisement = match Self::advertise(&self.profile, (&service).into()) {
            Err(e) => {
                let _ = responder.send(&mut Err(zx::Status::CANCELED.into_raw()));
                return Err(e);
            }
            Ok(fut) => fut,
        };

        // The lifetime of the request is tied to the FIDL client and the upstream BR/EDR
        // service advertisement.
        Ok(DeviceIdRequestToken::new(service, bredr_advertisement, client_request, responder))
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use async_test_helpers::run_while;
    use async_utils::PollExt;
    use fidl::client::QueryResponseFut;
    use fidl_fuchsia_bluetooth::ErrorCode;
    use fuchsia_async as fasync;
    use futures::{pin_mut, SinkExt};

    use crate::device_id::service_record::tests::minimal_record;
    use crate::DEFAULT_MAX_DEVICE_ID_ADVERTISEMENTS;

    fn setup_server(
        max: Option<usize>,
    ) -> (
        fasync::TestExecutor,
        DeviceIdServer,
        bredr::ProfileRequestStream,
        mpsc::Sender<di::DeviceIdentificationRequestStream>,
    ) {
        let exec = fasync::TestExecutor::new().unwrap();

        let (sender, receiver) = mpsc::channel(0);
        let (profile, profile_server) =
            fidl::endpoints::create_proxy_and_stream::<bredr::ProfileMarker>()
                .expect("valid endpoints");
        let max = max.unwrap_or(DEFAULT_MAX_DEVICE_ID_ADVERTISEMENTS);
        let server = DeviceIdServer::new(max, profile, receiver);

        (exec, server, profile_server, sender)
    }

    fn connect_fidl_client<Fut>(
        exec: &mut fasync::TestExecutor,
        server_fut: Fut,
        mut sender: mpsc::Sender<di::DeviceIdentificationRequestStream>,
    ) -> (Fut, di::DeviceIdentificationProxy)
    where
        Fut: Future + Unpin,
    {
        let (c, s) = fidl::endpoints::create_proxy_and_stream::<di::DeviceIdentificationMarker>()
            .expect("valid endpoints");

        let send_fut = sender.send(s);
        let (send_result, server_fut) = run_while(exec, server_fut, send_fut);
        assert_matches!(send_result, Ok(_));

        (server_fut, c)
    }

    /// Makes a SetDeviceIdentification request to the server.
    /// `primary` specifies if the requested DI record is the primary record.
    fn make_request(
        client: &di::DeviceIdentificationProxy,
        primary: bool,
    ) -> (di::DeviceIdentificationHandleProxy, QueryResponseFut<Result<(), i32>>) {
        let records = vec![minimal_record(primary)];
        let (token_client, token_server) =
            fidl::endpoints::create_proxy::<di::DeviceIdentificationHandleMarker>()
                .expect("valid endpoints");
        let request_fut = client
            .set_device_identification(&mut records.into_iter(), token_server)
            .check()
            .expect("valid fidl request");
        (token_client, request_fut)
    }

    fn expect_advertise_request(
        exec: &mut fasync::TestExecutor,
        profile_server: &mut bredr::ProfileRequestStream,
    ) -> bredr::ProfileRequest {
        let mut profile_requests = Box::pin(profile_server.next());
        match exec.run_until_stalled(&mut profile_requests).expect("Should have request") {
            Some(Ok(req @ bredr::ProfileRequest::Advertise { .. })) => req,
            x => panic!("Expected Advertise request, got: {:?}", x),
        }
    }

    #[fuchsia::test]
    fn fidl_client_terminates_advertisement() {
        let (mut exec, server, mut profile_server, sender) = setup_server(None);
        let server_fut = server.run();
        pin_mut!(server_fut);

        // Connect a new FIDL client and make a SetDeviceId request.
        let (mut server_fut, client) = connect_fidl_client(&mut exec, server_fut, sender.clone());
        let (token, fidl_client_fut) = make_request(&client, false);
        // Run the server task to process the request.
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("server still active");
        pin_mut!(fidl_client_fut);

        // Should expect the server to attempt to advertise over BR/EDR.
        let _adv_request = expect_advertise_request(&mut exec, &mut profile_server)
            .into_advertise()
            .expect("Advertise request");

        // FIDL client request should still be alive because the server is still advertising.
        let _ = exec.run_until_stalled(&mut fidl_client_fut).expect_pending("still active");
        // Client no longer wants to advertise - request should terminate.
        drop(token);
        let (fidl_client_result, _server_fut) = run_while(&mut exec, server_fut, fidl_client_fut);
        assert_matches!(fidl_client_result, Ok(Ok(_)));
    }

    #[fuchsia::test]
    fn fidl_client_notified_when_upstream_advertisement_terminated() {
        let (mut exec, server, mut profile_server, sender) = setup_server(None);
        let server_fut = server.run();
        pin_mut!(server_fut);

        // Connect a new FIDL client and make a SetDeviceId request.
        let (mut server_fut, client) = connect_fidl_client(&mut exec, server_fut, sender.clone());
        let (_token, fidl_client_fut) = make_request(&client, false);
        // Run the server task to process the request.
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("server still active");
        pin_mut!(fidl_client_fut);

        // Should expect the server to attempt to advertise over BR/EDR.
        let (_, _, _connect_proxy, responder) =
            expect_advertise_request(&mut exec, &mut profile_server)
                .into_advertise()
                .expect("Advertise request");

        // FIDL client request should still be alive because the server is still advertising.
        let _ = exec.run_until_stalled(&mut fidl_client_fut).expect_pending("still active");

        // Upstream BR/EDR Profile server terminates advertisement for some reason.
        let _ = responder.send(&mut Err(ErrorCode::TimedOut));
        let (fidl_client_result, _server_fut) = run_while(&mut exec, server_fut, fidl_client_fut);
        assert_matches!(fidl_client_result, Ok(Ok(_)));
    }

    #[fuchsia::test]
    fn multiple_clients_advertise_success() {
        let (mut exec, server, mut profile_server, sender) = setup_server(None);
        let server_fut = server.run();
        pin_mut!(server_fut);

        // Connect two FIDL clients and make two SetDeviceId requests.
        let (server_fut, client1) = connect_fidl_client(&mut exec, server_fut, sender.clone());
        let (mut server_fut, client2) = connect_fidl_client(&mut exec, server_fut, sender.clone());
        let (_token1, fidl_client_fut1) = make_request(&client1, false);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("server still active");
        let (_token2, fidl_client_fut2) = make_request(&client2, false);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("server still active");
        pin_mut!(fidl_client_fut1);
        pin_mut!(fidl_client_fut2);

        // Both FIDL client advertise requests shouldn't resolve yet as the advertisement is OK.
        let _ = exec.run_until_stalled(&mut fidl_client_fut1).expect_pending("still active");
        let _ = exec.run_until_stalled(&mut fidl_client_fut2).expect_pending("still active");

        // Upstream BR/EDR server should receive both requests.
        let _adv_request1 = expect_advertise_request(&mut exec, &mut profile_server);
        let _adv_request2 = expect_advertise_request(&mut exec, &mut profile_server);

        let _ = exec.run_until_stalled(&mut fidl_client_fut1).expect_pending("still active");
        let _ = exec.run_until_stalled(&mut fidl_client_fut2).expect_pending("still active");
    }

    #[fuchsia::test]
    fn client_advertisement_rejected_when_server_full() {
        // Make a server with no capacity.
        let (mut exec, server, _profile_server, sender) = setup_server(Some(0));
        let server_fut = server.run();
        pin_mut!(server_fut);

        let (mut server_fut, client) = connect_fidl_client(&mut exec, server_fut, sender.clone());
        let (_token, fidl_client_fut) = make_request(&client, false);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("server still active");

        // The FIDL client request should immediately resolve as the server cannot register it.
        let (fidl_client_result, _server_fut) = run_while(&mut exec, server_fut, fidl_client_fut);
        let expected_err = zx::Status::NO_RESOURCES.into_raw();
        assert_eq!(fidl_client_result.expect("fidl result is ok"), Err(expected_err));
    }

    #[fuchsia::test]
    fn second_client_rejected_when_requesting_primary() {
        let (mut exec, server, _profile_server, sender) = setup_server(None);
        let server_fut = server.run();
        pin_mut!(server_fut);

        // First FIDL client makes a request with a primary record.
        let (mut server_fut, client1) = connect_fidl_client(&mut exec, server_fut, sender.clone());
        let (_token1, fidl_client_fut1) = make_request(&client1, true);
        pin_mut!(fidl_client_fut1);
        let _ = exec.run_until_stalled(&mut server_fut).expect_pending("server still active");
        let _ = exec
            .run_until_stalled(&mut fidl_client_fut1)
            .expect_pending("advertisement still active");

        // Second FIDL client tries to make a request with another primary record - should be rejected.
        let (server_fut, client2) = connect_fidl_client(&mut exec, server_fut, sender.clone());
        let (_token2, fidl_client_fut2) = make_request(&client2, true);
        pin_mut!(fidl_client_fut2);
        // The FIDL client request should immediately resolve as the server cannot register it.
        let (fidl_client_result, _server_fut) = run_while(&mut exec, server_fut, fidl_client_fut2);
        let expected_err = zx::Status::ALREADY_EXISTS.into_raw();
        assert_eq!(fidl_client_result.expect("fidl result is ok"), Err(expected_err));
    }
}
