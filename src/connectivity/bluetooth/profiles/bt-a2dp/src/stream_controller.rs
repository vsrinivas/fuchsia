// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    bt_a2dp::permits::{Permit, Permits},
    core::pin::Pin,
    core::task::{Context, Poll},
    fidl::endpoints::RequestStream,
    fidl_fuchsia_bluetooth_internal_a2dp::{
        ControllerRequest, ControllerRequestStream, ControllerSuspendResponder,
        StreamSuspenderRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    futures::select,
    futures::stream::{SelectAll, Stream},
    futures::StreamExt,
    log::{info, trace, warn},
    std::cell::RefCell,
    std::sync::{Arc, Weak},
};

/// An interface for managing the state of streaming connections with remote peers.
pub trait StreamController {
    /// Manages the lifetime of a stream suspend request. When the `Token` is dropped, the streaming
    /// suspension may be lifted.
    type Token: Send + Unpin;

    /// Suspend the stream associated with `id` until the Token is dropped. If `id` is None,
    /// all streams will be suspended.
    fn suspend(&self, id: Option<PeerId>) -> Result<Self::Token, Error>;
}

/// Owns the set of streaming Permits used to suspend & release A2DP streams.
#[derive(Clone)]
pub struct PermitsManager {
    permits: Permits,
    held: RefCell<Weak<Vec<Permit>>>,
}

impl From<Permits> for PermitsManager {
    fn from(permits: Permits) -> Self {
        Self { permits, held: RefCell::new(Weak::new()) }
    }
}

impl StreamController for PermitsManager {
    type Token = Arc<Vec<Permit>>;
    // TOOD(fxbug.dev/77016): Use id to actually grab only the permits owned by PeerId
    fn suspend(&self, _id: Option<PeerId>) -> Result<Self::Token, Error> {
        if let Some(token) = self.held.borrow().upgrade() {
            return Ok(token);
        }
        let token = Arc::new(self.permits.seize());
        self.held.replace(Arc::downgrade(&token));
        Ok(token)
    }
}

/// A Token representing a suspension on an A2DP media stream.
struct SuspendToken<S: Stream, SC: StreamController> {
    /// The identifier associated with the stream. An `id` of None represents a global suspension
    /// on all streams.
    id: Option<PeerId>,
    /// The stream representing the lifetime of the FIDL client's suspend request.
    stream: S,
    /// The responder used to notify the FIDL client that the suspend request has terminated.
    ///
    /// This will be set as long as the `stream` is active and will be consumed on stream
    /// termination.
    responder: Option<ControllerSuspendResponder>,
    /// The underlying `StreamController` token used to manage the lifetime of the A2DP streaming
    /// hold.
    _token: <SC as StreamController>::Token,
}

impl<S, SC> Stream for SuspendToken<S, SC>
where
    S: Stream + Unpin,
    SC: StreamController,
{
    type Item = S::Item;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.stream.poll_next_unpin(cx)
    }
}

impl<S: Stream, SC: StreamController> Drop for SuspendToken<S, SC> {
    fn drop(&mut self) {
        // The FIDL client no longer needs stream suspension. Discard the `token` representing the
        // suspension and notify the client that it has been processed.
        info!("a2dp.Controller client dropped suspend request for {:?}", self.id);
        let _ = self.responder.take().expect("responder exists").send();
    }
}

/// Handles an A2DP stream suspend `request`.
///
/// Uses the provided `controller` to suspend the media stream.
///
/// On success, returns a `SuspendToken` representing the suspension, or None if failure.
fn handle_suspend_request<SC: StreamController>(
    request: ControllerRequest,
    controller: &SC,
) -> Option<SuspendToken<StreamSuspenderRequestStream, SC>> {
    let ControllerRequest::Suspend { peer_id, token, responder } = request;
    info!("Received a2dp.Controller FIDL request to suspend for peer {:?}", peer_id);

    let id = peer_id.map(|id| (*id).into());
    let stream = match token.into_stream() {
        Ok(st) => st,
        Err(e) => {
            if !e.is_closed() {
                warn!("StreamSuspender channel closed with unexpected error: {:?}", e);
            }
            let _ = responder.send();
            return None;
        }
    };

    // Request to suspend the stream via the `controller`. The lifetime of the returned Token
    // will be tied to that of the FIDL client's suspend `stream`.
    let token = match controller.suspend(id) {
        Ok(token) => token,
        Err(e) => {
            // If the suspend request couldn't be processed, close the `StreamSuspender` channel and
            // notify the FIDL client.
            trace!("Couldn't suspend stream for {:?}: {:?}", id, e);
            let _ = stream.control_handle().shutdown_with_epitaph(fuchsia_zircon::Status::INTERNAL);
            let _ = responder.send();
            return None;
        }
    };

    // Notify FIDL client that suspend was successful.
    if let Err(e) = stream.control_handle().send_on_suspended() {
        trace!("Couldn't notify client of stream suspension: {:?}", e);
    }

    Some(SuspendToken { id, stream, responder: Some(responder), _token: token })
}

/// Processes FIDL requests from the `stream`.
///
/// The lifetime of this task is tied to the provided `stream`. Each Suspend request
/// uses the `controller` to modify the A2DP streaming state with a given peer.
async fn handle_stream_controller_connection<SC: StreamController>(
    mut stream: ControllerRequestStream,
    controller: SC,
) {
    info!("a2dp.Controller client connected");
    let mut suspend_tokens: SelectAll<SuspendToken<StreamSuspenderRequestStream, SC>> =
        SelectAll::new();
    loop {
        select! {
            suspend_request = stream.select_next_some() => {
                let request = match suspend_request {
                    Err(e) => {
                        info!("a2dp.Controller request error: {}. Exiting", e);
                        break;
                    }
                    Ok(r) => r,
                };

                if let Some(token) = handle_suspend_request(request, &controller) {
                    suspend_tokens.push(token);
                }
            }
            // The `StreamSuspender` protocol contains no methods.
            _ = suspend_tokens.select_next_some() => {},
            complete => break,
        }
    }
    info!("a2dp.Controller connection finished");
}

/// Defines a FIDL service implementation for the `a2dp.Controller` capability and spawns a
/// task to process incoming requests. Protocol requests will be handled by the task and
/// use the provided `controller` to manipulate A2DP streaming state.
pub fn add_stream_controller_capability<SC: StreamController + Clone + Send + 'static>(
    fs: &mut ServiceFs<ServiceObj<'_, ()>>,
    controller: SC,
) {
    fs.dir("svc").add_fidl_service({
        move |s| {
            fasync::Task::spawn(handle_stream_controller_connection(s, controller.clone())).detach()
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        anyhow::format_err,
        fidl::client::QueryResponseFut,
        fidl::endpoints::{create_proxy, create_proxy_and_stream},
        fidl_fuchsia_bluetooth_internal_a2dp::{
            ControllerMarker, ControllerProxy, StreamSuspenderEvent, StreamSuspenderMarker,
            StreamSuspenderProxy,
        },
        futures::channel::mpsc,
        parking_lot::Mutex,
    };

    #[derive(Debug, PartialEq)]
    enum Event {
        // The `controller` has received a Suspend request.
        Suspend(Option<PeerId>),
        // The `StreamController::Token` associated with the suspend request has been dropped.
        TokenDropped,
    }

    // A token that notifies the Sender when it has been dropped.
    struct MockToken(mpsc::Sender<Event>);

    impl Drop for MockToken {
        fn drop(&mut self) {
            let _ = self.0.try_send(Event::TokenDropped).unwrap();
        }
    }

    /// A mock implementation of a `StreamController` interface implementation that echos events to
    /// the `sender`.
    #[derive(Clone)]
    struct MockStreamController {
        sender: mpsc::Sender<Event>,
        // Whether the `StreamController::suspend` request should error.
        should_error: bool,
    }

    impl MockStreamController {
        fn new(should_error: bool) -> (Self, mpsc::Receiver<Event>) {
            let (s, r) = mpsc::channel(0);
            (Self { sender: s, should_error }, r)
        }
    }

    impl StreamController for MockStreamController {
        type Token = MockToken;

        fn suspend(&self, id: Option<PeerId>) -> Result<Self::Token, Error> {
            let mut s = self.sender.clone();
            let _ = s.try_send(Event::Suspend(id.clone())).unwrap();
            if self.should_error {
                Err(format_err!("Suspend error"))
            } else {
                Ok(MockToken(s.clone()))
            }
        }
    }

    fn setup_server_and_mock_controller(
        should_error: bool,
    ) -> (fasync::Task<()>, ControllerProxy, mpsc::Receiver<Event>) {
        let (c, s) = create_proxy_and_stream::<ControllerMarker>().unwrap();
        let (controller, test_events) = MockStreamController::new(should_error);

        let _server_task =
            fasync::Task::local(handle_stream_controller_connection(s, controller.clone()));

        (_server_task, c, test_events)
    }

    #[track_caller]
    async fn expect_fidl_event_for_client(client: &StreamSuspenderProxy) {
        let mut event_stream = client.take_event_stream();
        match event_stream.next().await {
            Some(Ok(StreamSuspenderEvent::OnSuspended { .. })) => {}
            x => panic!("Expected ready with event but got: {:?}", x),
        }
    }

    #[track_caller]
    async fn expect_event(listener: &mut mpsc::Receiver<Event>, expected: Event) {
        match listener.next().await {
            Some(event) => assert_eq!(event, expected),
            x => panic!("Expected ready with event but got: {:?}", x),
        }
    }

    #[track_caller]
    async fn expect_pending(listener: &mut mpsc::Receiver<Event>) {
        assert!(futures::poll!(listener.next()).is_pending());
    }

    /// Makes a client suspend request. Returns the Future associated with the request,
    /// and the client end of the suspend token (to be held as the stream suspend request).
    fn make_suspend_request(
        controller_svc: &ControllerProxy,
        id: Option<PeerId>,
    ) -> (QueryResponseFut<()>, StreamSuspenderProxy) {
        let (c, s) = create_proxy::<StreamSuspenderMarker>().unwrap();
        let mut peer_id: Option<fidl_fuchsia_bluetooth::PeerId> = id.map(|id| id.into());
        let fidl_req = controller_svc
            .suspend(peer_id.as_mut(), s)
            .check()
            .expect("FIDL suspend request should succeed");

        (fidl_req, c)
    }

    #[fuchsia::test]
    async fn suspend_and_release_is_handled_by_server() {
        let (mut server, controller_svc, mut test_events) =
            setup_server_and_mock_controller(/* should_error */ false);

        // Initially, no events from server.
        expect_pending(&mut test_events).await;

        // Client wants to suspend stream for some peer.
        let remote_id = PeerId(9);
        let (fidl_request, client_token) = make_suspend_request(&controller_svc, Some(remote_id));

        // Expect it to be processed by the `server` and sent to the mock `controller`.
        expect_event(&mut test_events, Event::Suspend(Some(remote_id))).await;
        // Also expect a FIDL event notifying the client that the stream was suspended.
        expect_fidl_event_for_client(&client_token).await;

        // Client no longer needs suspension - the server should detect this and drop the
        // `StreamController::Token` it is holding for this client.
        drop(client_token);
        expect_event(&mut test_events, Event::TokenDropped).await;
        // Async suspend request should resolve.
        fidl_request.await.expect("FIDL suspend request should resolve");

        // Even though there are no more tokens, the server task should still be active since
        // the FIDL client still holds the `controller_svc` handle.
        assert!(futures::poll!(&mut server).is_pending());
    }

    #[fuchsia::test]
    async fn concurrent_suspend_requests_generate_two_suspend_events() {
        let (_server, controller_svc, mut test_events) =
            setup_server_and_mock_controller(/* should_error */ false);

        // Client wants to suspend stream for some peer.
        let remote_id = PeerId(13);
        let (fidl_request1, client_token1) = make_suspend_request(&controller_svc, Some(remote_id));
        // Expect mock `controller` to receive the request + the client notified via a FIDL event.
        expect_event(&mut test_events, Event::Suspend(Some(remote_id))).await;
        expect_fidl_event_for_client(&client_token1).await;

        // Another request to suspend the stream for the same peer.
        let (fidl_request2, client_token2) = make_suspend_request(&controller_svc, Some(remote_id));
        // Expect mock `controller` to receive the request + the client notified via a FIDL event.
        expect_event(&mut test_events, Event::Suspend(Some(remote_id))).await;
        expect_fidl_event_for_client(&client_token2).await;

        // Client #1 is done, doesn't need suspension - expect the server to drop the
        // `StreamController::Token` associated with the request.
        drop(client_token1);
        expect_event(&mut test_events, Event::TokenDropped).await;
        fidl_request1.await.expect("FIDL suspend request should resolve");
        // The other Token should be untouched - no other events.
        expect_pending(&mut test_events).await;

        // Client #2 is done - expect TokenDropped and suspend FIDL call to resolve.
        drop(client_token2);
        expect_event(&mut test_events, Event::TokenDropped).await;
        fidl_request2.await.expect("FIDL suspend request should resolve");
    }

    #[fuchsia::test]
    async fn sequential_suspend_requests_generate_two_suspend_events() {
        let (_server, controller_svc, mut test_events) =
            setup_server_and_mock_controller(/* should_error */ false);

        // Client wants to suspend stream for some peer.
        let remote_id = PeerId(16);
        let (fidl_request1, client_token1) = make_suspend_request(&controller_svc, Some(remote_id));
        // Expect mock `controller` to receive the request + the client notified via a FIDL event.
        expect_event(&mut test_events, Event::Suspend(Some(remote_id))).await;
        expect_fidl_event_for_client(&client_token1).await;

        // Request #1 is done - Token should be dropped.
        drop(client_token1);
        expect_event(&mut test_events, Event::TokenDropped).await;
        fidl_request1.await.expect("FIDL suspend request should resolve");

        // Another request to suspend the stream for the same peer is OK. Should generate events.
        let (fidl_request2, client_token2) = make_suspend_request(&controller_svc, Some(remote_id));
        expect_event(&mut test_events, Event::Suspend(Some(remote_id))).await;
        expect_fidl_event_for_client(&client_token2).await;

        // Request #2 is done - Token should be dropped.
        drop(client_token2);
        expect_event(&mut test_events, Event::TokenDropped).await;
        fidl_request2.await.expect("FIDL suspend request should resolve");
    }

    #[fuchsia::test]
    async fn events_generated_with_both_global_and_specific_stream_suspension() {
        let (_server, controller_svc, mut test_events) =
            setup_server_and_mock_controller(/* should_error */ false);

        // Client wants a global suspension.
        let (fidl_request1, client_token1) = make_suspend_request(&controller_svc, None);
        // Expect mock `controller` to receive the request + the client notified via a FIDL event.
        expect_event(&mut test_events, Event::Suspend(None)).await;
        expect_fidl_event_for_client(&client_token1).await;

        // Client wants a specific peer suspension.
        let remote_id = PeerId(567);
        let (fidl_request2, client_token2) = make_suspend_request(&controller_svc, Some(remote_id));
        // Expect mock `controller` to receive the request + the client notified via a FIDL event.
        expect_event(&mut test_events, Event::Suspend(Some(remote_id))).await;
        expect_fidl_event_for_client(&client_token2).await;

        // Client request #2 is done - expect the Token dropping event.
        drop(client_token2);
        expect_event(&mut test_events, Event::TokenDropped).await;
        fidl_request2.await.expect("FIDL suspend request should resolve");

        // Request #1 is done - expect the Token dropping event.
        drop(client_token1);
        expect_event(&mut test_events, Event::TokenDropped).await;
        fidl_request1.await.expect("FIDL suspend request should resolve");
    }

    #[track_caller]
    async fn expect_fidl_error_for_client(client: &StreamSuspenderProxy) {
        let mut event_stream = client.take_event_stream();
        match event_stream.next().await {
            Some(Err(fidl::Error::ClientChannelClosed {
                status: fuchsia_zircon::Status::INTERNAL,
                ..
            })) => {}
            x => panic!("Expected ready with INTERNAL error but got: {:?}", x),
        }
    }

    #[fuchsia::test]
    async fn stream_controller_suspend_error_closes_channel() {
        let (_server, controller_svc, mut test_events) =
            setup_server_and_mock_controller(/* should_error */ true);

        // Client wants to suspend stream for some peer.
        let remote_id = PeerId(55);
        let (fidl_request, client_token) = make_suspend_request(&controller_svc, Some(remote_id));

        // Expect mock `controller` to receive the suspend request, but it errors so the
        // `client_token`protocol should close with zx::Status::INTERNAL. The FIDL call should
        // also terminate.
        expect_event(&mut test_events, Event::Suspend(Some(remote_id))).await;
        expect_fidl_error_for_client(&client_token).await;
        fidl_request.await.expect("FIDL suspend request should resolve");
    }

    /// Verifies that a token that is already closed is processed by the task and immediately
    /// results in stream suspending + lifting.
    #[fuchsia::test]
    async fn closed_suspend_token_is_correctly_handled() {
        let (_server, controller_svc, mut test_events) =
            setup_server_and_mock_controller(/* should_error */ false);

        // Client wants to suspend stream for some peer but closes token before sending the request.
        let remote_id = PeerId(11);
        let (c, s) = create_proxy::<StreamSuspenderMarker>().unwrap();
        drop(c);
        let fidl_request = controller_svc
            .suspend(Some(remote_id.into()).as_mut(), s)
            .check()
            .expect("FIDL suspend request should succeed");

        // Expect the suspend to be processed by the `server` and sent to the mock `controller`.
        expect_event(&mut test_events, Event::Suspend(Some(remote_id))).await;
        // The server should immediately recognize that the token was dropped and should release.
        expect_event(&mut test_events, Event::TokenDropped).await;
        // Async suspend request should resolve.
        fidl_request.await.expect("FIDL suspend request should resolve");
    }

    #[fuchsia::test]
    async fn stream_controller_connection_handler_exits_when_exhausted() {
        let (mut server, controller_svc, mut test_events) =
            setup_server_and_mock_controller(/* should_error */ false);

        // Client suspends for some peer.
        let remote_id = PeerId(19);
        let (fidl_request, client_token) = make_suspend_request(&controller_svc, Some(remote_id));

        // Expect it to be processed by the `server` and sent to the mock `controller` and a FIDL
        // event generated.
        expect_event(&mut test_events, Event::Suspend(Some(remote_id))).await;
        expect_fidl_event_for_client(&client_token).await;

        // Client disconnects the Controller protocol - the server task should still be active
        // since there are outstanding tokens to be processed.
        drop(controller_svc);
        assert!(futures::poll!(&mut server).is_pending());

        // Client drops suspend token - the server should detect this and drop the
        // `StreamController::Token` it is holding for this client.
        drop(client_token);
        expect_event(&mut test_events, Event::TokenDropped).await;
        // Async suspend request should resolve.
        fidl_request.await.expect("FIDL suspend request should resolve");
        // The server task should then complete since both the `Controller` request stream and outstanding
        // tokens have been exhausted.
        server.await;
    }

    #[fuchsia::test]
    fn permit_manager_seizes_permits() {
        const TOTAL_PERMITS: usize = 2;
        let permits = Permits::new(TOTAL_PERMITS);

        let manager = PermitsManager::from(permits.clone());

        let permit_holder = Arc::new(Mutex::new(None));

        let revoke_from_holder_fn = {
            let holder = permit_holder.clone();
            move || holder.lock().take().expect("should be holding Permit")
        };

        let permit = permits.get_revokable(revoke_from_holder_fn.clone());
        permit_holder.lock().insert(permit.expect("permit"));

        let token = manager.suspend(None).expect("suspend permits");

        // Can't get a permit, token holds them all, and took the one we had.
        assert!(permits.get_revokable(revoke_from_holder_fn.clone()).is_none());
        assert!(permit_holder.lock().is_none(), "permit should have been revoked");

        let token2 = manager.suspend(None).expect("second suspension");
        drop(token);

        // Can't get a permit, because token2 is still holding a token suspending everything.
        assert!(permits.get_revokable(revoke_from_holder_fn.clone()).is_none());

        // Can get another permit after the second client drops.
        drop(token2);
        let permit = permits.get_revokable(revoke_from_holder_fn).expect("permit");
        permit_holder.lock().insert(permit);
        drop(permit_holder);
        drop(manager);
        drop(permits);
    }
}
