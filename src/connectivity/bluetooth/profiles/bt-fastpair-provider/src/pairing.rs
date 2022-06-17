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
    PairingDelegateOnPairingRequestResponder as PairingResponder, PairingDelegateProxy,
    PairingDelegateRequest, PairingDelegateRequestStream, PairingMethod, PairingProxy,
};
use fuchsia_bluetooth::types::{Peer, PeerId};
use futures::stream::{FusedStream, FuturesUnordered, Stream, StreamExt};
use futures::{future::BoxFuture, FutureExt};
use std::{collections::HashMap, convert::TryFrom};
use tracing::{debug, trace, warn};

use crate::types::{AccountKey, Error};

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

/// State of a Fast Pair pairing procedure.
enum ProcedureState {
    /// The procedure has been initialized - e.g the peer has sent a key-based pairing GATT request.
    Started,
    /// Classic/LE pairing is in progress and will be completed upon confirmation of the Seeker's
    /// passkey.
    Pairing { passkey: u32, responder: PairingResponder },
    /// Passkey verification is complete and the procedure is effectively finished.
    PasskeyChecked,
}

/// An active Fast Pair Pairing procedure.
struct Procedure {
    /// Shared secret used to encode/decode messages sent/received in the procedure.
    key: AccountKey,
    /// Current status of the procedure.
    state: ProcedureState,
}

impl Procedure {
    fn new(key: AccountKey) -> Self {
        Self { key, state: ProcedureState::Started }
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
/// The `PairingManager` supports the handling of Fast Pair pairing procedures with multiple peers.
/// However, there can only be one active procedure per remote peer. When the pairing procedure
/// completes, (e.g ProcedureState::PasskeyChecked), it will remain in the set of active
/// `procedures` until explicitly removed via `PairingManager::complete_pairing_procedure`.
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
    /// Active pairing procedures.
    // TODO(fxbug.dev/99757): Active procedures should be cleared if no progress has been made
    // within 10 seconds or when complete (e.g an Account Key has been written).
    procedures: HashMap<PeerId, Procedure>,
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
            procedures: HashMap::new(),
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
    fn claim_delegate(&mut self) -> Result<(), Error> {
        // While unusual, this typically signifies another ongoing Fast Pair procedure - probably
        // with a different peer.
        if self.owner.is_fast_pair() {
            return Ok(());
        }

        // Give pairing capabilities to Fast Pair.
        self.set_pairing_delegate(PairingDelegateOwner::FastPair)
    }

    /// Releases the Pairing Delegate back to the upstream client.
    /// No-op if the delegate is not currently owned by Fast Pair or if there are other Fast Pair
    /// procedures in progress.
    /// Returns Error if the operation couldn't be completed.
    fn release_delegate(&mut self) -> Result<(), Error> {
        if !self.owner.is_fast_pair() || !self.procedures.is_empty() {
            return Ok(());
        }

        // Give pairing capabilities back to the upstream client.
        self.set_pairing_delegate(PairingDelegateOwner::Upstream)
    }

    /// Returns the shared secret AccountKey for the active procedure with the remote peer.
    pub fn key_for_procedure(&self, id: &PeerId) -> Option<&AccountKey> {
        self.procedures.get(id).map(|procedure| &procedure.key)
    }

    /// Attempts to initiate a new Fast Pair pairing procedure with the provided peer.
    pub fn new_pairing_procedure(&mut self, id: PeerId, key: AccountKey) -> Result<(), Error> {
        if self.procedures.contains_key(&id) {
            return Err(Error::internal(&format!("Pairing with {:?} already in progress", id)));
        }

        self.claim_delegate()?;
        let _ = self.procedures.insert(id, Procedure::new(key));
        Ok(())
    }

    /// Attempts to compare the provided `gatt_passkey` with the passkey saved in the ongoing
    /// pairing procedure with the peer.
    /// If the comparison is made, responds to the in-progress pairing request with the result of
    /// the comparison.
    /// Note: There is an implicit assumption that the peer has already made the pairing request
    /// before `compare_passkey` is called with the GATT passkey. While the GFPS does specify this
    /// ordering, this may not always be the case in practice. fxbug.dev/102963 tracks the changes
    /// needed to be resilient to the ordering of messages.
    ///
    /// Returns Error if the comparison was unable to be made (e.g the pairing procedure didn't
    /// exist or wasn't in the expected state).
    /// Returns the passkey on success (e.g the comparison took place, not that `gatt_passkey`
    /// necessarily matched the expected passkey).
    pub fn compare_passkey(&mut self, id: PeerId, gatt_passkey: u32) -> Result<u32, Error> {
        debug!("Comparing passkey for {:?} (gatt passkey: {})", id, gatt_passkey);
        let procedure = self
            .procedures
            .get_mut(&id)
            .filter(|p| matches!(p.state, ProcedureState::Pairing { .. }))
            .ok_or(Error::internal(&format!("Unexpected passkey response for {:?}", id)))?;

        match std::mem::replace(&mut procedure.state, ProcedureState::PasskeyChecked) {
            ProcedureState::Pairing { passkey, responder } => {
                let accept = passkey == gatt_passkey;
                let _ = responder.send(accept, gatt_passkey);
                Ok(passkey)
            }
            _ => unreachable!("Checked `procedure.state`"),
        }
    }

    /// Attempts to complete the pairing procedure by handing pairing capabilities back to the
    /// upstream client.
    /// Returns Error if there is no such finished procedure or if the pairing handoff failed.
    #[allow(unused)]
    // TODO(fxbug.dev/96222): Remove when used by Provider server.
    pub fn complete_pairing_procedure(&mut self, id: PeerId) -> Result<(), Error> {
        if !self
            .procedures
            .get(&id)
            .map_or(false, |p| matches!(p.state, ProcedureState::PasskeyChecked))
        {
            return Err(Error::internal(&format!("Can't complete procedure with {:?}", id)));
        }

        // Procedure is in the correct (finished) state and we can clean up and try to give the
        // delegate back to the upstream client.
        let _ = self.procedures.remove(&id).expect("procedure finished");
        self.release_delegate()
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
        peer: Peer,
        method: PairingMethod,
        passkey: u32,
        responder: PairingResponder,
    ) -> Result<(), Error> {
        if let Some(procedure) =
            self.procedures.get_mut(&peer.id).filter(|p| matches!(p.state, ProcedureState::Started))
        {
            if method == PairingMethod::PasskeyComparison {
                procedure.state = ProcedureState::Pairing { passkey, responder };
                return Ok(());
            }

            warn!("Received unsupported Fast Pair pairing method {:?}", method);
            // The current pairing procedure is no longer valid.
            let _ = self.procedures.remove(&peer.id).expect("procedure is active");
            self.release_delegate()?;
        } else {
            warn!("Unexpected pairing request for {}. Ignoring..", peer.id);
            // TODO(fxbug.dev/101721): Consider relaying upstream if I/O capabilities are defined
            // per peer.
        }

        // Reject the pairing attempt since it is either unexpected or invalid.
        let _ = responder.send(false, 0u32);
        Ok(())
    }

    fn handle_pairing_delegate_request(
        &mut self,
        request: PairingDelegateRequest,
    ) -> Result<Option<PeerId>, Error> {
        debug!("Received PairingDelegate request: {:?}", request);
        if !self.owner.is_fast_pair() {
            debug!("Relaying PairingDelegate request to upstream");
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
                self.handle_pairing_request(peer, method, displayed_passkey, responder)?;
            }
            PairingDelegateRequest::OnPairingComplete { id, success, .. } => {
                let id = id.into();
                debug!("OnPairingComplete for {} (success = {})", id, success);
                if self
                    .procedures
                    .get(&id)
                    .map_or(false, |p| matches!(p.state, ProcedureState::PasskeyChecked))
                {
                    if success {
                        return Ok(Some(id));
                    }

                    // Otherwise, pairing failed and the pairing procedure is terminated.
                    let _ = self.procedures.remove(&id).expect("procedure finished");
                    let _ = self.release_delegate();
                }
            }
            PairingDelegateRequest::OnRemoteKeypress { .. } => {}
        }
        Ok(None)
    }
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
/// The Stream produces an item anytime Fast Pair pairing successfully completes with a remote peer.
impl Stream for PairingManager {
    type Item = PeerId;

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
                Some(Ok(request)) => match self.handle_pairing_delegate_request(request) {
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
pub(crate) mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use fidl::client::QueryResponseFut;
    use fidl_fuchsia_bluetooth_sys::{PairingKeypress, PairingMarker, PairingRequestStream};
    use fuchsia_bluetooth::types::Address;
    use futures::{future::Either, pin_mut};

    use crate::types::keys;

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
        pub async fn new_with_manager() -> (PairingManager, Self) {
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
            self.make_pairing_request_internal(id, PairingMethod::PasskeyComparison, passkey)
        }

        fn make_pairing_request_internal(
            &self,
            id: PeerId,
            method: PairingMethod,
            passkey: u32,
        ) -> QueryResponseFut<(bool, u32)> {
            let delegate = self.downstream_delegate_client.clone();
            delegate.on_pairing_request(example_peer(id), method, passkey)
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

    pub(crate) fn example_peer(id: PeerId) -> fidl_fuchsia_bluetooth_sys::Peer {
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

        // A new pairing procedure is started between us and the peer.
        let id = PeerId(123);
        assert_matches!(manager.new_pairing_procedure(id, keys::tests::example_aes_key()), Ok(_));
        mock.expect_set_pairing_delegate().await;
        assert_matches!(manager.key_for_procedure(&id), Some(_));

        // Peer tries to pair - no stream item since pairing isn't complete.
        let request_fut = mock.make_pairing_request(id, 555666);
        assert_matches!(manager.select_next_some().now_or_never(), None);

        // Remote peer wants to compare passkeys - no stream item since pairing isn't complete.
        let passkey = manager.compare_passkey(id, 555666).expect("successful comparison");
        assert_eq!(passkey, 555666);
        assert_matches!(manager.select_next_some().now_or_never(), None);
        // Expect the pairing request to complete as passkeys have been verified.
        let result = request_fut.await.expect("fidl response");
        assert_eq!(result, (true, 555666));

        // Downstream server signals pairing completion.
        let _ = mock
            .downstream_delegate_client
            .on_pairing_complete(&mut id.into(), true)
            .expect("valid fidl request");
        // Expect a Pairing Manager stream item indicating completion of pairing with the peer.
        let result = manager.select_next_some().await;
        assert_eq!(result, id);
        // Shared secret for the procedure is still available.
        assert_matches!(manager.key_for_procedure(&id), Some(_));

        // PairingManager owner will complete pairing once the peer makes a GATT Write to the
        // Account Key characteristic.
        assert_matches!(manager.complete_pairing_procedure(id), Ok(_));
        // All pairing related work is complete, so we expect PairingManager to hand pairing
        // capabilities back to the upstream. The shared secret should no longer be saved.
        mock.expect_set_pairing_delegate().await;
        assert_matches!(manager.key_for_procedure(&id), None);
    }

    #[fuchsia::test]
    async fn invalid_fast_pair_method_is_rejected() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        let methods = vec![
            PairingMethod::Consent,
            PairingMethod::PasskeyDisplay,
            PairingMethod::PasskeyEntry,
        ];
        for unsupported_pairing_method in methods {
            // A new pairing procedure is started between us and the peer.
            let id = PeerId(123);
            assert_matches!(
                manager.new_pairing_procedure(id, keys::tests::example_aes_key()),
                Ok(_)
            );
            mock.expect_set_pairing_delegate().await;
            assert_matches!(manager.key_for_procedure(&id), Some(_));

            let request_fut =
                mock.make_pairing_request_internal(id, unsupported_pairing_method, 555666);
            let manager_fut = manager.select_next_some();
            pin_mut!(request_fut, manager_fut);

            match futures::future::select(manager_fut, request_fut).await {
                Either::Left(_) => panic!("Unexpected Pairing Manager stream item"),
                Either::Right((Ok((false, 0)), manager_fut)) => {
                    // Should be handled gracefully, and rejected. There are no other active pairing
                    // procedures so pairing is handed back to upstream.
                    let () = mock.expect_set_pairing_delegate().await;
                    // No pairing event.
                    assert_matches!(manager_fut.now_or_never(), None);
                }
                Either::Right((r, _mgr_fut)) => panic!("Unexpected result: {:?}", r),
            }
        }
    }

    #[fuchsia::test]
    async fn no_stream_item_when_pairing_is_unsuccessful() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        // A new pairing procedure is started between us and the peer.
        let id = PeerId(123);
        assert_matches!(manager.new_pairing_procedure(id, keys::tests::example_aes_key()), Ok(_));
        mock.expect_set_pairing_delegate().await;
        assert_matches!(manager.key_for_procedure(&id), Some(_));

        // Peer tries to pair - no stream item since pairing isn't complete.
        let request_fut = mock.make_pairing_request(id, 555666);
        assert_matches!(manager.select_next_some().now_or_never(), None);

        // Remote peer requests to compare passkeys.
        let passkey = manager.compare_passkey(id, 555666).expect("successful comparison");
        assert_eq!(passkey, 555666);
        assert_matches!(manager.select_next_some().now_or_never(), None);
        // Expect the pairing request to complete as passkeys have been verified.
        let result = request_fut.await.expect("fidl response");
        assert_eq!(result, (true, 555666));

        // Peer rejects pairing via the downstream `sys.Pairing` server.
        let _ = mock
            .downstream_delegate_client
            .on_pairing_complete(&mut id.into(), false)
            .expect("valid fidl request");
        let manager_fut = manager.select_next_some();
        // There are no active pairing procedures so pairing is handed back to upstream.
        let expect_fut = mock.expect_set_pairing_delegate();
        pin_mut!(manager_fut, expect_fut);
        match futures::future::select(manager_fut, expect_fut).await {
            Either::Left(_) => panic!("Unexpected Pairing Manager stream item"),
            Either::Right(((), _manager_fut)) => {}
        }
        // Per GFPS Pairing Procedure Step 16, if pairing fails, then the shared secret must be
        // discarded.
        assert_matches!(manager.key_for_procedure(&id), None);
    }

    #[fuchsia::test]
    async fn unequal_passkey_comparison_is_ok() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        // A new pairing procedure is started between us and the peer.
        let id = PeerId(123);
        assert_matches!(manager.new_pairing_procedure(id, keys::tests::example_aes_key()), Ok(_));
        mock.expect_set_pairing_delegate().await;
        assert_matches!(manager.key_for_procedure(&id), Some(_));

        // Peer tries to pair - no stream item since pairing isn't complete.
        let provider_passkey = 123456;
        let request_fut = mock.make_pairing_request(id, provider_passkey);
        assert_matches!(manager.select_next_some().now_or_never(), None);

        // Remote peer requests to compare passkeys (Seeker passkey is sent over GATT). Comparison
        // should succeed even though passkeys are different.
        let seeker_passkey = 987654;
        let passkey = manager.compare_passkey(id, seeker_passkey).expect("successful comparison");
        assert_eq!(passkey, provider_passkey);
        assert_matches!(manager.select_next_some().now_or_never(), None);
        // Expect the pairing request to complete with failure as passkeys were not the same.
        let (success, _) = request_fut.await.expect("fidl response");
        assert!(!success);
        // Even though passkeys were mismatched, the pairing procedure can still continue as the
        // GFPS notes that the Seeker can still accept pairing after doing its own passkey check.
        assert_matches!(manager.key_for_procedure(&id), Some(_));
    }

    #[fuchsia::test]
    async fn duplicate_pairing_procedure_is_error() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        let id = PeerId(123);
        assert_matches!(manager.new_pairing_procedure(id, keys::tests::example_aes_key()), Ok(_));
        mock.expect_set_pairing_delegate().await;

        // Trying to start a new pairing procedure for the same peer is an Error.
        assert_matches!(
            manager.new_pairing_procedure(id, keys::tests::example_aes_key()),
            Err(Error::InternalError(_))
        );
        // Don't expect a new delegate to be claimed.
        assert_matches!(mock.downstream_pairing_server.next().now_or_never(), None);
    }

    #[fuchsia::test]
    async fn unexpected_procedure_updates_is_error() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        let id = PeerId(123);
        // Can't compare passkey for a procedure that doesn't exist.
        assert_matches!(manager.compare_passkey(id, 555666), Err(Error::InternalError(_)));
        // Can't complete a procedure that doesn't exist.
        assert_matches!(manager.complete_pairing_procedure(id), Err(Error::InternalError(_)));

        // Start a new procedure.
        assert_matches!(manager.new_pairing_procedure(id, keys::tests::example_aes_key()), Ok(_));
        mock.expect_set_pairing_delegate().await;

        // Comparing passkeys before pairing begins is an error.
        assert_matches!(manager.compare_passkey(id, 555666), Err(Error::InternalError(_)));
        // Can't complete a procedure that hasn't verified passkeys.
        assert_matches!(manager.complete_pairing_procedure(id), Err(Error::InternalError(_)));
    }

    #[fuchsia::test]
    async fn concurrent_pairing_procedures() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        let id1 = PeerId(123);
        let id2 = PeerId(987);

        // We can start two pairing procedures - we only expect one SetPairingDelegate since Fast
        // Pair will request it after the first call.
        assert_matches!(manager.new_pairing_procedure(id1, keys::tests::example_aes_key()), Ok(_));
        mock.expect_set_pairing_delegate().await;
        assert_matches!(manager.new_pairing_procedure(id2, keys::tests::example_aes_key()), Ok(_));
        assert_matches!(mock.downstream_pairing_server.next().now_or_never(), None);

        // First peer tries to pair - no stream item since pairing isn't complete.
        let request_fut = mock.make_pairing_request(id1, 555666);
        assert_matches!(manager.select_next_some().now_or_never(), None);

        // No stream item after comparing passkeys since pairing isn't complete.
        let passkey = manager.compare_passkey(id1, 555666).expect("successful comparison");
        assert_eq!(passkey, 555666);
        assert_matches!(manager.select_next_some().now_or_never(), None);
        // Expect the pairing request to resolve as passkeys have been verified.
        let result = request_fut.await.expect("fidl response");
        assert_eq!(result, (true, 555666));

        // PairingManager owner will complete pairing once the peer makes a GATT Write to the
        // Account Key characteristic.
        assert_matches!(manager.complete_pairing_procedure(id1), Ok(_));
        // However, because a pairing procedure for `id2` is still active, the Pairing Delegate
        // should not be reset.
        assert_matches!(mock.downstream_pairing_server.next().now_or_never(), None);
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
}
