// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use core::{
    pin::Pin,
    task::{Context, Poll},
};
use fidl_fuchsia_bluetooth_deviceid as di;
use futures::{future::FusedFuture, stream::StreamFuture, Future, FutureExt, StreamExt};
use tracing::{debug, info, warn};

use crate::device_id::server::BrEdrAdvertisement;
use crate::device_id::service_record::DeviceIdentificationService;

/// A token representing a FIDL client's Device Identification advertisement request.
pub struct DeviceIdRequestToken {
    /// The DI service that was requested by the client to be advertised.
    service: DeviceIdentificationService,
    /// The upstream BR/EDR advertisement request.
    advertisement: Option<BrEdrAdvertisement>,
    /// The channel representing the FIDL client's request.
    client_request: Option<StreamFuture<di::DeviceIdentificationHandleRequestStream>>,
    /// The responder used to notify the FIDL client that the request has terminated.
    ///
    /// This will be set as long as the `fut` is active and will be consumed when the token is
    /// dropped.
    responder: Option<di::DeviceIdentificationSetDeviceIdentificationResponder>,
}

impl DeviceIdRequestToken {
    pub fn new(
        service: DeviceIdentificationService,
        advertisement: BrEdrAdvertisement,
        client_request: di::DeviceIdentificationHandleRequestStream,
        responder: di::DeviceIdentificationSetDeviceIdentificationResponder,
    ) -> Self {
        Self {
            service,
            advertisement: Some(advertisement),
            client_request: Some(client_request.into_future()),
            responder: Some(responder),
        }
    }

    pub fn size(&self) -> usize {
        self.service.size()
    }

    pub fn contains_primary(&self) -> bool {
        self.service.contains_primary()
    }

    // Notifies the `responder` when the request has been closed.
    fn notify_responder(responder: di::DeviceIdentificationSetDeviceIdentificationResponder) {
        let _ = responder.send(&mut Ok(()));
        info!("DeviceIdRequestToken successfully closed");
    }
}

impl Future for DeviceIdRequestToken {
    type Output = ();

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        let mut advertisement = self.advertisement.take().expect("can't poll future twice");
        let mut client_request = self.client_request.take().expect("can't poll future twice");
        if let Poll::Ready(_x) = advertisement.poll_unpin(cx) {
            debug!("Upstream BR/EDR server terminated DI advertisement: {:?}", _x);
            Self::notify_responder(self.responder.take().expect("responder exists"));
            return Poll::Ready(());
        }

        match client_request.poll_unpin(cx) {
            Poll::Ready((Some(_req), _s)) => {
                panic!("Unexpected request in DI Token stream: {:?}", _req)
            }
            Poll::Ready((None, _s)) => {
                debug!("DI FIDL client closed token channel");
                Self::notify_responder(self.responder.take().expect("responder exists"));
                Poll::Ready(())
            }
            Poll::Pending => {
                self.advertisement = Some(advertisement);
                self.client_request = Some(client_request);
                Poll::Pending
            }
        }
    }
}

impl FusedFuture for DeviceIdRequestToken {
    fn is_terminated(&self) -> bool {
        self.advertisement.is_none() || self.client_request.is_none()
    }
}

impl Drop for DeviceIdRequestToken {
    fn drop(&mut self) {
        if let Some(responder) = self.responder.take() {
            warn!("DeviceIdRequestToken for service {:?} dropped unexpectedly", self.service);
            let _ = responder.send(&mut Err(fuchsia_zircon::Status::CANCELED.into_raw()));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use async_utils::PollExt;
    use fidl::client::QueryResponseFut;
    use fidl_fuchsia_bluetooth_bredr::{
        ChannelParameters, ConnectionReceiverMarker, ConnectionReceiverProxy, ProfileMarker,
    };
    use fuchsia_async as fasync;
    use futures::{future::MaybeDone, pin_mut, StreamExt};

    use crate::device_id::service_record::tests::minimal_record;

    fn make_set_device_id_request(
        exec: &mut fasync::TestExecutor,
    ) -> (
        di::DeviceIdentificationRequest,
        di::DeviceIdentificationHandleProxy,
        QueryResponseFut<Result<(), i32>>,
    ) {
        let (c, mut s) =
            fidl::endpoints::create_proxy_and_stream::<di::DeviceIdentificationMarker>()
                .expect("valid endpoints");

        let records = vec![minimal_record(false)];
        let (token_client, token_server) =
            fidl::endpoints::create_proxy::<di::DeviceIdentificationHandleMarker>()
                .expect("valid endpoints");
        let request_fut = c
            .set_device_identification(&mut records.into_iter(), token_server)
            .check()
            .expect("valid fidl request");

        let mut next = Box::pin(s.next());
        match exec.run_singlethreaded(&mut next).expect("fidl request") {
            Ok(request) => (request, token_client, request_fut),
            x => panic!("Expected SetDeviceIdentification request but got: {:?}", x),
        }
    }

    /// Makes a DeviceIdRequestToken from the provided `responder`. If `done` is true, then the
    /// upstream advertisement emulated by this function will immediately resolve.
    /// Returns the token, the proxy associated with the upstream advertisement, and a "junk" task
    /// that should be kept alive for the duration of the test.
    fn make_token(
        responder: di::DeviceIdentificationSetDeviceIdentificationResponder,
        request_server: di::DeviceIdentificationHandleRequestStream,
        done: bool,
    ) -> (DeviceIdRequestToken, ConnectionReceiverProxy, fasync::Task<()>) {
        let (adv_fut, junk_task) = if done {
            (QueryResponseFut(MaybeDone::Done(Ok(Ok(())))), fasync::Task::local(async {}))
        } else {
            // Otherwise, we need a future that will not resolve yet.
            let (c, _s) = fidl::endpoints::create_proxy_and_stream::<ProfileMarker>().unwrap();
            let (c2, _s2) =
                fidl::endpoints::create_request_stream::<ConnectionReceiverMarker>().unwrap();
            let fut = c.advertise(&mut vec![].into_iter(), ChannelParameters::EMPTY, c2);
            let task = fasync::Task::local(async {
                let (_s, _s2, _c) = (_s, _s2, c); // Keep everything alive.
                futures::future::pending::<()>().await;
            });
            (fut, task)
        };
        // Use a random service definition.
        let svc = DeviceIdentificationService::from_di_records(&vec![minimal_record(false)])
            .expect("should parse record");
        // A BR/EDR advertise request.
        let (connect_client, connect_server) =
            fidl::endpoints::create_proxy_and_stream::<ConnectionReceiverMarker>().unwrap();
        let advertisement = BrEdrAdvertisement { adv_fut, connect_server };
        let token = DeviceIdRequestToken::new(svc, advertisement, request_server, responder);

        (token, connect_client, junk_task)
    }

    #[fuchsia::test]
    fn responder_notified_with_error_on_token_drop() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (request, _request_client, client_fut) = make_set_device_id_request(&mut exec);
        let (_record, request_server, responder) =
            request.into_set_device_identification().expect("set device id request");
        pin_mut!(client_fut);

        let (token, _connect_client, _junk_task) =
            make_token(responder, request_server.into_stream().unwrap(), /* done= */ false);

        // The client request should still be alive since the `token` is alive.
        exec.run_until_stalled(&mut client_fut).expect_pending("Token is still alive");
        // Token unexpectedly dropped - FIDL client should be notified with Error.
        drop(token);
        let res = exec
            .run_until_stalled(&mut client_fut)
            .expect("Token dropped, client fut should resolve")
            .expect("fidl response");
        assert_eq!(res, Err(fuchsia_zircon::Status::CANCELED.into_raw()));
    }

    #[fuchsia::test]
    fn token_terminates_when_upstream_advertisement_terminates() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (request, _request_client, client_fut) = make_set_device_id_request(&mut exec);
        let (_record, request_server, responder) =
            request.into_set_device_identification().expect("set device id request");
        pin_mut!(client_fut);

        let (token, _connect_client, _junk_task) =
            make_token(responder, request_server.into_stream().unwrap(), /* done= */ true);
        pin_mut!(token);

        // The client request should still be alive since the `token` is alive.
        exec.run_until_stalled(&mut client_fut).expect_pending("Token is still alive");

        // Because the `advertisement` used in `make_token` is done, we expect the `token` to
        // resolve.
        let () = exec
            .run_until_stalled(&mut token)
            .expect("Upstream advertisement done, token should resolve");
        assert!(token.is_terminated());
        // The FIDL client should then receive the response that the closure has been processed.
        let res = exec
            .run_until_stalled(&mut client_fut)
            .expect("Token dropped, client fut should resolve")
            .expect("fidl response");
        assert_eq!(res, Ok(()));
    }

    #[fuchsia::test]
    fn token_terminates_when_fidl_client_closes_channel() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (request, _request_client, client_fut) = make_set_device_id_request(&mut exec);
        let (_record, request_server, responder) =
            request.into_set_device_identification().expect("set device id request");
        pin_mut!(client_fut);

        let (token, _connect_client, _junk_task) =
            make_token(responder, request_server.into_stream().unwrap(), /* done= */ false);
        pin_mut!(token);
        assert!(!token.is_terminated());

        // The client request should still be alive since the `token` is alive.
        exec.run_until_stalled(&mut client_fut)
            .expect_pending("Client request still waiting for response");
        exec.run_until_stalled(&mut token).expect_pending("Token still waiting for closure");

        // Because the FIDL client closed its end of the channel, we expect the `token` to resolve.
        drop(_request_client);
        let () = exec
            .run_until_stalled(&mut token)
            .expect("FIDL client request terminated, token should resolve");
        assert!(token.is_terminated());
        // The FIDL client should then receive the response that its close request is processed.
        let res = exec
            .run_until_stalled(&mut client_fut)
            .expect("Token terminated, client fut should resolve")
            .expect("fidl response");
        assert_eq!(res, Ok(()));
    }
}
