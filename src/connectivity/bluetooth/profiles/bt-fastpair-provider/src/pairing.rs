// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_helpers::maybe_stream::MaybeStream;
use core::{
    pin::Pin,
    task::{Context, Poll},
};
use fidl::endpoints::{ControlHandle, Proxy, RequestStream};
use fidl_fuchsia_bluetooth_sys::{
    InputCapability, OutputCapability, PairingDelegateMarker,
    PairingDelegateOnPairingRequestResponder, PairingDelegateProxy, PairingDelegateRequest,
    PairingDelegateRequestStream, PairingMethod, PairingProxy,
};
use fuchsia_bluetooth::types::{Peer, PeerId};
use futures::stream::{FusedStream, FuturesUnordered, Stream, StreamExt};
use futures::{future::BoxFuture, FutureExt};
use std::convert::TryFrom;
use tracing::{debug, trace, warn};

use crate::types::Error;

pub struct PairingArgs {
    input: InputCapability,
    output: OutputCapability,
    delegate: PairingDelegateProxy,
}

/// The current owner of the Pairing Delegate.
#[derive(Debug, PartialEq)]
enum PairingDelegateOwner {
    /// The Fast Pair service owns the Pairing Delegate.
    FastPair,
    /// The upstream client owns the Pairing Delegate.
    Upstream,
}

impl PairingDelegateOwner {
    /// Returns true if Fast Pair currently owns the delegate.
    fn is_fast_pair(&self) -> bool {
        *self == Self::FastPair
    }
}

/// The `PairingManager` is responsible for servicing `sys.Pairing` requests.
///
/// Because pairing can't occur unless a Pairing Delegate is set, the `PairingManager` is
/// constructed with the upstream client's `sys.Pairing` connection. Therefore, the lifetime of the
/// `PairingManager` is tied to that of the upstream client and the downstream connection to the
/// Pairing Delegate.
///
/// The `PairingManager` handles pairing events related to Fast Pair and forwards unrelated events
/// directly to the upstream `sys.Pairing` client. Only one Pairing Delegate can be active at a
/// time. Because Fast Pair and the upstream client may require different I/O capabilities, the
/// `PairingManager` unregisters and re-registers the delegate any time there is a change in
/// ownership.
///
/// Note: Due to a limitation of the `sys.Pairing` API, any non-Fast Pair pairing request made while
/// Fast Pair owns the Pairing Delegate will incorrectly use Fast Pair I/O capabilities. Ideally,
/// we'd like the granularity of I/O capabilities to be at the peer level. To avoid confusion at
/// the upstream level, the `PairingManager` will reject such pairing requests while Fast Pair is
/// active. See fxbug.dev/101721 for more details.
///
/// PairingManager implements `Stream`. Clients should poll this stream to receive updates about
/// Fast Pair pairing.
pub struct PairingManager {
    /// Connection to the `sys.Pairing` protocol.
    pairing: PairingProxy,
    /// Upstream client that has ownership of the Pairing Delegate.
    upstream_client: PairingArgs,
    /// This future resolves when the upstream client closes its Pairing Delegate.
    upstream_connection: BoxFuture<'static, ()>,
    /// The current owner of the downstream connection to the Pairing Delegate.
    owner: PairingDelegateOwner,
    /// The downstream connection to the Pairing Delegate.
    /// This is an optional stream because the PairingManager resets the delegate any time there is
    /// a change in the delegate `owner`.
    pairing_requests: MaybeStream<PairingDelegateRequestStream>,
    /// Active tasks relaying a request from the downstream Pairing Delegate to the upstream client.
    relay_tasks: FuturesUnordered<BoxFuture<'static, ()>>,
    /// If the PairingManager is finished - i.e either the upstream or downstream delegate
    /// connection has terminated.
    terminated: bool,
}

impl PairingManager {
    #[allow(unused)]
    // TODO(fxbug.dev/96222): Remove when used by Provider server.
    pub fn new(pairing: PairingProxy, upstream_client: PairingArgs) -> Result<Self, Error> {
        let delegate = upstream_client.delegate.clone();
        let upstream_connection = async move {
            let _ = delegate.on_closed().await;
        }
        .boxed();
        let mut this = Self {
            pairing,
            upstream_client,
            upstream_connection,
            pairing_requests: MaybeStream::default(),
            owner: PairingDelegateOwner::Upstream,
            relay_tasks: FuturesUnordered::new(),
            terminated: false,
        };
        this.set_pairing_delegate(PairingDelegateOwner::Upstream)?;
        Ok(this)
    }

    /// Attempts to set the downstream Pairing Delegate.
    ///
    /// Note: This is an internal method. To avoid unnecessary churn, it should only be used when
    /// there is a change in ownership of the delegate.
    ///
    /// Returns Error if the delegate couldn't be set.
    fn set_pairing_delegate(&mut self, owner: PairingDelegateOwner) -> Result<(), Error> {
        self.close_delegate();

        let (input, output) = match owner {
            PairingDelegateOwner::FastPair => {
                (InputCapability::Confirmation, OutputCapability::Display)
            }
            PairingDelegateOwner::Upstream => {
                (self.upstream_client.input, self.upstream_client.output)
            }
        };

        let (c, s) = fidl::endpoints::create_request_stream::<PairingDelegateMarker>()?;
        self.pairing.set_pairing_delegate(input, output, c)?;
        self.pairing_requests.set(s);
        self.owner = owner;
        Ok(())
    }

    fn close_delegate(&mut self) {
        if let Some(stream) = MaybeStream::take(&mut self.pairing_requests) {
            stream.control_handle().shutdown();
        }
    }

    /// Attempts to claim the Pairing Delegate for Fast Pair use.
    /// No-op if the delegate is already owned by Fast Pair.
    /// Returns Error if the delegate was unable to be set.
    #[allow(unused)]
    // TODO(fxbug.dev/96222): Remove when used by Provider server.
    pub fn claim_delegate(&mut self) -> Result<(), Error> {
        // While unusual, this typically signifies another ongoing Fast Pair procedure - probably
        // with a different peer.
        if self.owner.is_fast_pair() {
            return Ok(());
        }

        // Give pairing capabilities to Fast Pair.
        self.set_pairing_delegate(PairingDelegateOwner::FastPair)
    }

    /// Releases the Pairing Delegate currently owned by Fast Pair back to the upstream client.
    /// No-op if the delegate is not currently owned by Fast Pair.
    /// Returns Error if the operation couldn't be completed.
    #[allow(unused)]
    // TODO(fxbug.dev/96222): Remove when used by Provider server.
    pub fn release_delegate(&mut self) -> Result<(), Error> {
        // Release the downstream connection to the Pairing Delegate if Fast Pair currently owns it.
        if self.owner.is_fast_pair() {
            // Give pairing capabilities back to the upstream client.
            self.set_pairing_delegate(PairingDelegateOwner::Upstream)?;
        }

        Ok(())
    }

    async fn proxy_pairing_request(request: PairingDelegateRequest, proxy: PairingDelegateProxy) {
        match request {
            PairingDelegateRequest::OnPairingRequest {
                peer,
                method,
                displayed_passkey,
                responder,
            } => {
                let (accept, entered_passkey) =
                    match proxy.on_pairing_request(peer, method, displayed_passkey).await {
                        Ok((a, p)) => (a, p),
                        Err(e) => {
                            warn!("FIDL error when handling OnPairingRequest: {:?}", e);
                            (false, 0)
                        }
                    };
                let _ = responder.send(accept, entered_passkey);
            }
            PairingDelegateRequest::OnPairingComplete { mut id, success, .. } => {
                let _ = proxy.on_pairing_complete(&mut id, success);
            }
            PairingDelegateRequest::OnRemoteKeypress { mut id, keypress, .. } => {
                let _ = proxy.on_remote_keypress(&mut id, keypress);
            }
        }
    }

    fn handle_pairing_request(
        &mut self,
        request: PairingDelegateRequest,
    ) -> Result<Option<PairingEvent>, Error> {
        if !self.owner.is_fast_pair() {
            debug!("Relaying request to upstream: {:?}", request);
            let relay_task =
                Self::proxy_pairing_request(request, self.upstream_client.delegate.clone()).boxed();
            self.relay_tasks.push(relay_task);
            return Ok(None);
        }

        match request {
            PairingDelegateRequest::OnPairingRequest {
                peer,
                method,
                displayed_passkey,
                responder,
            } => {
                let peer = Peer::try_from(peer)?;
                if method != PairingMethod::PasskeyComparison {
                    warn!("Received unsupported Fast Pair pairing method {:?}", method);
                    let _ = responder.send(false, 0u32);
                    return Ok(None);
                }
                Ok(Some(PairingEvent::Request {
                    id: peer.id,
                    passkey: displayed_passkey,
                    responder,
                }))
            }
            PairingDelegateRequest::OnPairingComplete { id, success, .. } => {
                debug!("OnPairingComplete for peer {:?}: success = {}", id, success);
                Ok(Some(PairingEvent::Complete { id: id.into() }))
            }
            PairingDelegateRequest::OnRemoteKeypress { .. } => Ok(None),
        }
    }
}

#[derive(Debug)]
pub enum PairingEvent {
    Request { id: PeerId, passkey: u32, responder: PairingDelegateOnPairingRequestResponder },
    Complete { id: PeerId },
}

/// The Stream implementation for `PairingManager` does 3 things:
///   (1) Poll whether the connection to the upstream `sys.Pairing` client is open. Because pairing
///       functionality is determined by the client's existence, the `PairingManager` stream will
///       terminate if the client disconnects.
///   (2) Drive pairing relay futures to completion. This is applicable to non-Fast Pair requests.
///       Relaying the request to the upstream `sys.Pairing` client is async. Each request is
///       inserted into a collection and is progressed anytime the stream is polled.
///   (3) Poll the connection to the downstream `sys.Pairing` delegate. Received pairing requests
///       will be handled by the `PairingManager`.
impl Stream for PairingManager {
    type Item = PairingEvent;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.is_terminated() {
            panic!("Cannot poll a terminated stream");
        }

        // Check if upstream client is still there.
        if let Poll::Ready(_) = self.upstream_connection.poll_unpin(cx) {
            trace!("Upstream client disconnected. PairingManager stream is terminated.");
            self.terminated = true;
            return Poll::Ready(None);
        }

        // Drive any active relay tasks to completion.
        if !self.relay_tasks.is_terminated() {
            let _ = Pin::new(&mut self.relay_tasks).poll_next(cx);
        }

        // Check for any new pairing requests from the downstream Pairing Delegate.
        while let Poll::Ready(result) = self.pairing_requests.poll_next_unpin(cx) {
            match result {
                Some(Ok(request)) => match self.handle_pairing_request(request) {
                    Ok(None) => continue,
                    Ok(event) => return Poll::Ready(event),
                    Err(e) => {
                        warn!("Error handling PairingDelegate FIDL request: {:?}", e);
                    }
                },
                Some(Err(e)) => {
                    warn!("Error in PairingDelegate FIDL channel: {}", e);
                }
                None => (),
            }

            // Either the `PairingDelegate` stream is exhausted, or there is an error in the
            // channel.
            trace!("Downstream channel closed. PairingManager stream is terminated.");
            self.terminated = true;
            return Poll::Ready(None);
        }

        // Drive any relay tasks that may have been added after processing `self.pairing_requests`.
        if !self.relay_tasks.is_terminated() {
            let _ = Pin::new(&mut self.relay_tasks).poll_next(cx);
        }
        Poll::Pending
    }
}

impl FusedStream for PairingManager {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use fidl::client::QueryResponseFut;
    use fidl_fuchsia_bluetooth_sys::{PairingKeypress, PairingMarker, PairingRequestStream};
    use fuchsia_bluetooth::types::Address;
    use futures::{future::Either, pin_mut};

    /// Holds onto the necessary FIDL connections to facilitate pairing. Provides convenience
    /// methods to drive pairing procedures.
    pub struct MockPairing {
        pub downstream_pairing_server: PairingRequestStream,
        pub downstream_delegate_client: PairingDelegateProxy,
        pub upstream_delegate_server: PairingDelegateRequestStream,
    }

    impl MockPairing {
        /// Builds and returns a PairingManager and MockPairing object which can be used to simulate
        /// upstream & downstream pairing actions.
        async fn new_with_manager() -> (PairingManager, Self) {
            let (c, mut downstream_pairing_server) =
                fidl::endpoints::create_proxy_and_stream::<PairingMarker>().unwrap();
            let (delegate, upstream_delegate_server) =
                fidl::endpoints::create_proxy_and_stream::<PairingDelegateMarker>().unwrap();
            let client = PairingArgs {
                input: InputCapability::None,
                output: OutputCapability::None,
                delegate,
            };
            let manager = PairingManager::new(c, client).expect("can create pairing manager");
            // Expect the manager to register a delegate on behalf of the upstream.
            let (_, _, delegate, _) = downstream_pairing_server
                .select_next_some()
                .await
                .expect("fidl request")
                .into_set_pairing_delegate()
                .unwrap();
            let downstream_delegate_client = delegate.into_proxy().unwrap();

            let mock = MockPairing {
                downstream_pairing_server,
                downstream_delegate_client,
                upstream_delegate_server,
            };
            (manager, mock)
        }

        pub async fn expect_set_pairing_delegate(&mut self) {
            let (_, _, delegate, _) = self
                .downstream_pairing_server
                .select_next_some()
                .await
                .expect("fidl request")
                .into_set_pairing_delegate()
                .unwrap();
            self.downstream_delegate_client = delegate.into_proxy().unwrap();
        }

        pub fn make_pairing_request(
            &self,
            id: PeerId,
            passkey: u32,
        ) -> QueryResponseFut<(bool, u32)> {
            let delegate = self.downstream_delegate_client.clone();
            delegate.on_pairing_request(example_peer(id), PairingMethod::PasskeyComparison, passkey)
        }
    }

    #[fuchsia::test]
    async fn lifetime_tied_to_upstream() {
        let (mut manager, mock) = MockPairing::new_with_manager().await;
        assert!(!manager.is_terminated());

        // Upstream delegate server goes away - expect the manager to terminate.
        drop(mock.upstream_delegate_server);
        assert_matches!(manager.next().await, None);
        assert!(manager.is_terminated());

        // Discarding the manager should signal termination to the downstream.
        drop(manager);
        let _ = mock.downstream_delegate_client.on_closed().await.expect("gracefully closed");
    }

    #[fuchsia::test]
    async fn downstream_termination_closes_manager() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;
        assert!(!manager.is_terminated());

        // Downstream goes away - manager should finish.
        drop(mock.downstream_delegate_client);
        assert_matches!(manager.next().await, None);
        assert!(manager.is_terminated());

        // Discarding the manager should signal termination to the upstream.
        drop(manager);
        let upstream_event = mock.upstream_delegate_server.next().await;
        assert_matches!(upstream_event, None);
    }

    #[fuchsia::test]
    async fn pairing_relay() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        // Manager stream should constantly be polled. We don't expect any events since it is in
        // relay mode.
        let _manager_task = fuchsia_async::Task::local(async move {
            let event = manager.next().await;
            panic!("Unexpected pairing manager event {:?}", event);
        });

        // OnPairingRequest propagated to upstream.
        let sent_passkey = 123456;
        let downstream_fut = mock.make_pairing_request(PeerId(123), sent_passkey);
        let upstream_fut = mock.upstream_delegate_server.select_next_some();
        pin_mut!(downstream_fut, upstream_fut);
        match futures::future::select(downstream_fut, upstream_fut).await {
            Either::Left(_) => panic!("Pairing Request fut resolved before receiving response"),
            Either::Right((Ok(upstream_result), downstream_fut)) => {
                let (_peer, method, displayed_passkey, responder) =
                    upstream_result.into_on_pairing_request().unwrap();
                assert_eq!(method, PairingMethod::PasskeyComparison); // Used in `MockPairing`.
                assert_eq!(displayed_passkey, sent_passkey);
                // Respond positively.
                let _ = responder.send(true, sent_passkey);
                // Downstream request should resolve.
                let downstream_result = downstream_fut.await.expect("should resolve ok");
                assert_eq!(downstream_result, (true, sent_passkey));
            }
            Either::Right((r, _downstream_fut)) => panic!("Unexpected result: {:?}", r),
        }

        // OnPairingComplete propagated to upstream.
        mock.downstream_delegate_client
            .on_pairing_complete(&mut PeerId(123).into(), true)
            .expect("fidl request");
        let _event = mock
            .upstream_delegate_server
            .select_next_some()
            .await
            .expect("fidl request")
            .into_on_pairing_complete()
            .unwrap();

        // OnRemoteKeypress propagated to upstream.
        mock.downstream_delegate_client
            .on_remote_keypress(&mut PeerId(123).into(), PairingKeypress::DigitEntered)
            .expect("fidl request");
        let _event = mock
            .upstream_delegate_server
            .select_next_some()
            .await
            .expect("fidl request")
            .into_on_remote_keypress()
            .unwrap();
    }

    #[fuchsia::test]
    async fn remote_keypress_is_no_op() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        // Fast Pair mode - requesting this should result in a new downstream request.
        assert_matches!(manager.claim_delegate(), Ok(_));
        mock.expect_set_pairing_delegate().await;

        // Keypress event is irrelevant in Fast Pair mode. No stream item.
        mock.downstream_delegate_client
            .on_remote_keypress(&mut PeerId(123).into(), PairingKeypress::DigitEntered)
            .expect("fidl request");
        assert_matches!(manager.next().now_or_never(), None);
    }

    fn example_peer(id: PeerId) -> fidl_fuchsia_bluetooth_sys::Peer {
        let peer = Peer {
            id,
            address: Address::Public([1, 2, 3, 4, 5, 6]),
            technology: fidl_fuchsia_bluetooth_sys::TechnologyType::DualMode,
            connected: false,
            bonded: false,
            name: None,
            appearance: None,
            device_class: None,
            rssi: None,
            tx_power: None,
            le_services: vec![],
            bredr_services: vec![],
        };
        (&peer).into()
    }

    #[fuchsia::test]
    async fn claim_delegate_with_pairing_events() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        assert_matches!(manager.claim_delegate(), Ok(_));
        mock.expect_set_pairing_delegate().await;

        // The manager should receive the pairing request and produce a stream item.
        let id = PeerId(2);
        let request_fut = mock.make_pairing_request(id, 555666);
        let manager_fut = manager.select_next_some();
        pin_mut!(request_fut, manager_fut);

        match futures::future::select(manager_fut, request_fut).await {
            Either::Right(_) => panic!("Request fut finished before manager processed it"),
            Either::Left((manager_event, request_fut)) => {
                let (passkey, responder) =
                    if let PairingEvent::Request { passkey, responder, .. } = manager_event {
                        (passkey, responder)
                    } else {
                        panic!("Expected Pairing Request");
                    };
                assert_eq!(passkey, 555666);
                let _ = responder.send(true, 555666);

                let _ = request_fut.await.expect("FIDL response");
            }
        }

        // The manager should receive the pairing complete event and produce a stream item.
        mock.downstream_delegate_client
            .on_pairing_complete(&mut id.into(), true)
            .expect("valid fidl request");
        let result = manager.select_next_some().await;
        assert_matches!(result, PairingEvent::Complete { .. });
    }

    #[fuchsia::test]
    async fn claim_and_release_delegate() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        assert_matches!(manager.claim_delegate(), Ok(_));
        mock.expect_set_pairing_delegate().await;
        // Claiming the delegate again is a no-op.
        assert_matches!(manager.claim_delegate(), Ok(_));
        assert_matches!(mock.downstream_pairing_server.next().now_or_never(), None);

        // Releasing delegate should result in a new PairingDelegate being set.
        assert_matches!(manager.release_delegate(), Ok(_));
        mock.expect_set_pairing_delegate().await;
        // Releasing the delegate again is a no-op.
        assert_matches!(manager.release_delegate(), Ok(_));
        assert_matches!(mock.downstream_pairing_server.next().now_or_never(), None);

        // Pairing Requests are routed upstream.
        mock.downstream_delegate_client
            .on_pairing_complete(&mut PeerId(123).into(), true)
            .expect("fidl request");
        assert_matches!(manager.next().now_or_never(), None);
        let _event = mock
            .upstream_delegate_server
            .select_next_some()
            .await
            .expect("fidl request")
            .into_on_pairing_complete()
            .unwrap();
    }

    #[fuchsia::test]
    async fn pairing_request_unsupported_pairing_method() {
        // Create a new Pairing Manager and give ownership to Fast Pair.
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;
        assert_matches!(manager.claim_delegate(), Ok(_));
        mock.expect_set_pairing_delegate().await;

        let methods = vec![
            PairingMethod::Consent,
            PairingMethod::PasskeyDisplay,
            PairingMethod::PasskeyEntry,
        ];
        for unsupported_pairing_method in methods {
            let request_fut = mock.downstream_delegate_client.on_pairing_request(
                example_peer(PeerId(2)),
                unsupported_pairing_method,
                555666,
            );
            let manager_fut = manager.select_next_some();
            pin_mut!(request_fut, manager_fut);

            match futures::future::select(manager_fut, request_fut).await {
                Either::Left(_) => panic!("Unexpected Pairing Manager stream item"),
                Either::Right((Ok((false, 0)), manager_fut)) => {
                    // The Pairing Request should be rejected and no Pairing Manager stream items.
                    assert_matches!(manager_fut.now_or_never(), None);
                }
                Either::Right((r, _mgr_fut)) => panic!("Unexpected result: {:?}", r),
            }
        }
    }
}
