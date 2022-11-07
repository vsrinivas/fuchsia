// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_helpers::maybe_stream::MaybeStream;
use async_utils::stream::FutureMap;
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
use fuchsia_async::{self as fasync, DurationExt};
use fuchsia_bluetooth::types::{Peer, PeerId};
use fuchsia_zircon as zx;
use futures::stream::{FusedStream, FuturesUnordered, Stream, StreamExt};
use futures::{future::BoxFuture, Future, FutureExt};
use std::convert::TryFrom;
use tracing::{debug, info, trace, warn};

use crate::types::{Error, SharedSecret};

pub struct PairingArgs {
    pub input: InputCapability,
    pub output: OutputCapability,
    pub delegate: PairingDelegateProxy,
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
    /// Passkey verification is complete - this doesn't necessarily mean the Provider and Seeker
    /// passkeys were equal, only that the comparison took place.
    PasskeyChecked,
    /// Classic/LE pairing was successfully completed.
    PairingComplete,
    /// The Account Key has been written. The peer is expected to write this key after pairing is
    /// complete (e.g. In the `PairingComplete` state).
    AccountKeyWritten,
}

impl std::fmt::Debug for ProcedureState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Started => write!(f, "Started"),
            Self::Pairing { .. } => write!(f, "Pairing"),
            Self::PasskeyChecked => write!(f, "PasskeyChecked"),
            Self::PairingComplete => write!(f, "PairingComplete"),
            Self::AccountKeyWritten => write!(f, "AccountKeyWritten"),
        }
    }
}

/// An active Fast Pair Pairing procedure.
struct Procedure {
    /// PeerId of the remote peer that we are currently pairing with. The PeerId is associated with
    /// the LE-version of the peer because the it always initiates Fast Pair over LE.
    le_id: PeerId,
    /// PeerId of the remote peer that we are currently pairing with. This is set during pairing if
    /// the peer initiates pairing over BR/EDR.
    bredr_id: Option<PeerId>,
    /// Shared secret used to encode/decode messages sent/received in the procedure.
    key: SharedSecret,
    /// Current status of the procedure.
    state: ProcedureState,
    /// Tracks the timeout of the pairing procedure.
    timer: Option<fasync::Timer>,
}

impl Procedure {
    /// Default timeout duration for a pairing procedure. Per the GFPS specification, if progress
    /// is not made within this amount of time, the procedure should be terminated.
    /// See https://developers.google.com/nearby/fast-pair/specifications/service/gatt#procedure
    const DEFAULT_PROCEDURE_TIMEOUT_DURATION: zx::Duration = zx::Duration::from_seconds(10);

    fn new(id: PeerId, key: SharedSecret) -> Self {
        let timer = fasync::Timer::new(Self::DEFAULT_PROCEDURE_TIMEOUT_DURATION.after_now());
        Self { le_id: id, bredr_id: None, key, state: ProcedureState::Started, timer: Some(timer) }
    }

    /// Moves the procedure to the new pairing `state` and resets the deadline for the procedure.
    fn transition(&mut self, state: ProcedureState) -> ProcedureState {
        let old_state = std::mem::replace(&mut self.state, state);
        self.timer = Some(fasync::Timer::new(Self::DEFAULT_PROCEDURE_TIMEOUT_DURATION.after_now()));
        old_state
    }

    fn set_bredr_id(&mut self, id: PeerId) {
        self.bredr_id = Some(id);
    }

    fn is_started(&self) -> bool {
        matches!(self.state, ProcedureState::Started)
    }

    fn is_passkey_checked(&self) -> bool {
        matches!(self.state, ProcedureState::PasskeyChecked)
    }

    fn is_complete(&self) -> bool {
        // The procedure can be completed if either 1) the peer has written the Account Key or 2)
        // the peer is only doing a personalized name write, in which case only procedure
        // initialization occurs.
        matches!(self.state, ProcedureState::AccountKeyWritten) || self.is_started()
    }
}

impl Future for Procedure {
    type Output = PeerId;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        match &mut self.timer {
            None => panic!("Future polled after completion (deadline reached)"),
            Some(timer) => match timer.poll_unpin(cx) {
                Poll::Ready(()) => {
                    trace!("Pairing procedure deadline reached: {:?}", self);
                    self.timer = None;
                    Poll::Ready(self.le_id)
                }
                Poll::Pending => Poll::Pending,
            },
        }
    }
}

impl Drop for Procedure {
    fn drop(&mut self) {
        // Replaced state is irrelevant as object will be dropped.
        match std::mem::replace(&mut self.state, ProcedureState::PairingComplete) {
            ProcedureState::Pairing { responder, .. } => {
                // Reject the pairing request as dropping a Procedure likely signals Error or it is
                // no longer needed.
                let _ = responder.send(false, 0);
            }
            _ => {}
        }
    }
}

impl std::fmt::Debug for Procedure {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("Procedure")
            .field("le_id", &self.le_id)
            .field("bredr_id", &self.bredr_id)
            .field("state", &self.state)
            .field("timer", &self.timer)
            .finish()
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
    /// Active pairing procedures. Each pairing procedure is identified by the peer's LE-discovered
    /// PeerId.
    procedures: FutureMap<PeerId, Procedure>,
    /// If the PairingManager is finished - i.e either the upstream or downstream delegate
    /// connection has terminated.
    terminated: bool,
}

impl PairingManager {
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
            procedures: FutureMap::new(),
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
        if !self.owner.is_fast_pair() || !self.procedures.inner().is_empty() {
            return Ok(());
        }

        // Give pairing capabilities back to the upstream client.
        self.set_pairing_delegate(PairingDelegateOwner::Upstream)
    }

    /// Returns the SharedSecret for the active procedure with the remote peer.
    pub fn key_for_procedure(&mut self, le_id: &PeerId) -> Option<&SharedSecret> {
        self.procedures.inner().get(le_id).map(|procedure| &procedure.key)
    }

    /// Attempts to initiate a new Fast Pair pairing procedure with the provided peer.
    pub fn new_pairing_procedure(&mut self, le_id: PeerId, key: SharedSecret) -> Result<(), Error> {
        if self.procedures.contains_key(&le_id) {
            return Err(Error::internal(&format!("Pairing with {le_id:?} already in progress")));
        }

        self.claim_delegate()?;
        let _ = self.procedures.insert(le_id, Procedure::new(le_id, key));
        Ok(())
    }

    /// Attempts to compare the provided `gatt_passkey` with the passkey saved in the ongoing
    /// pairing procedure with the peer.
    /// If the comparison is made, responds to the in-progress pairing request with the result of
    /// the comparison.
    ///
    /// Returns Error if the comparison was unable to be made (e.g the pairing procedure didn't
    /// exist or wasn't in the expected state).
    /// Returns the passkey on success (e.g the comparison took place, not that `gatt_passkey`
    /// necessarily matched the expected passkey).
    // TODO(fxbug.dev/102963): There is an implicit assumption that the peer has already made the
    // pairing request before `compare_passkey` is called with the GATT passkey. While the GFPS
    // does specify this ordering, this may not always be the case in practice.
    pub fn compare_passkey(&mut self, le_id: PeerId, gatt_passkey: u32) -> Result<u32, Error> {
        debug!(?le_id, %gatt_passkey, "Comparing passkey");
        let procedure = self
            .procedures
            .inner()
            .get_mut(&le_id)
            .filter(|p| matches!(p.state, ProcedureState::Pairing { .. }))
            .ok_or(Error::internal(&format!("Unexpected passkey response for {le_id:?}")))?;

        match procedure.transition(ProcedureState::PasskeyChecked) {
            ProcedureState::Pairing { passkey, responder } => {
                let accept = passkey == gatt_passkey;
                let _ = responder.send(accept, gatt_passkey);
                Ok(passkey)
            }
            _ => unreachable!("Checked `procedure.state`"),
        }
    }

    /// Progresses the procedure after an Account Key has been written.
    /// Returns Error if there is no active procedure in the correct state.
    // TODO(fxbug.dev/102963): There is an implicit assumption that the peer has already
    // successfully completed the Classic/LE pairing request (e.g OnPairingComplete
    // { success = true }) before `account_key_write` is called. While the GFPS does specify this
    // ordering, this may not always be the case in practice.
    pub fn account_key_write(&mut self, le_id: PeerId) -> Result<(), Error> {
        debug!(?le_id, "Processing account key write");
        let procedure = self
            .procedures
            .inner()
            .get_mut(&le_id)
            .filter(|p| matches!(p.state, ProcedureState::PairingComplete))
            .ok_or(Error::internal(&format!(
                "Procedure with {le_id:?} is not in the correct state"
            )))?;
        let _ = procedure.transition(ProcedureState::AccountKeyWritten);
        Ok(())
    }

    /// Attempts to complete the pairing procedure by handing pairing capabilities back to the
    /// upstream client.
    /// Returns Error if there is no such finished procedure or if the pairing handoff failed.
    pub fn complete_pairing_procedure(&mut self, le_id: PeerId) -> Result<(), Error> {
        if !self.procedures.inner().get(&le_id).map_or(false, |p| p.is_complete()) {
            return Err(Error::internal(&format!(
                "Procedure with {le_id:?} is not in the correct state"
            )));
        }

        // Procedure is in the correct (finished) state and we can clean up and try to give the
        // delegate back to the upstream client.
        self.cancel_pairing_procedure(&le_id)
    }

    /// Cancels the Fast Pair pairing procedure with the peer.
    /// Returns Ok if the PairingDelegate was successfully released, or Error otherwise.
    pub fn cancel_pairing_procedure(&mut self, id: &PeerId) -> Result<(), Error> {
        let _ = self.procedures.remove(id);
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
        le_or_bredr_id: PeerId,
        method: PairingMethod,
        passkey: u32,
        responder: PairingResponder,
    ) -> Result<(), Error> {
        // Unsupported pairing methods will always be rejected.
        if method != PairingMethod::PasskeyComparison {
            warn!(?le_or_bredr_id, ?method, "Received unsupported pairing method");
            let _ = responder.send(false, 0u32);
            self.cancel_pairing_procedure(&le_or_bredr_id)?;
            return Ok(());
        }

        let procedure = match self.procedures.inner().get_mut(&le_or_bredr_id) {
            // Most common case - this occurs because the peer initiates Fast Pair pairing via
            // LE but is making this pairing request over BR/EDR. Therefore, the system will
            // have assigned two unique PeerIds for the same peer. This will get coalesced once
            // the peer successfully bonds. For now, we try to find the first valid pairing
            // procedure that is in the correct state.
            // TODO(fxbug.dev/107780): Consider limiting the PairingManager to one active procedure
            // at a time. Then we won't have to worry about this case.
            None => {
                let procedure =
                    self.procedures.inner().iter_mut().find(|(_le_id, p)| p.is_started());
                if procedure.is_none() {
                    warn!(
                        ?le_or_bredr_id,
                        "Couldn't match pairing request with inflight Fast Pair procedure"
                    );
                    // TODO(fxbug.dev/101721): Consider relaying upstream if I/O capabilities are
                    // defined per peer.
                    let _ = responder.send(false, 0u32);
                    return Ok(());
                }
                procedure.unwrap().1
            }
            // The pairing request is matched to a known procedure that is in the right state.
            Some(p) if p.is_started() => p,
            // There is an active Fast Pair procedure with the peer but it is not in the right
            // state. Error.
            Some(p) => {
                warn!(
                    ?le_or_bredr_id, ?p.state,
                    "Received unexpected pairing request",
                );
                // The current pairing procedure is no longer valid.
                let _ = responder.send(false, 0u32);
                self.cancel_pairing_procedure(&le_or_bredr_id)?;
                return Ok(());
            }
        };

        // We've successfully matched the pairing request to an ongoing Procedure. The `responder`
        // will be used when passkey verification occurs.
        let _ = procedure.transition(ProcedureState::Pairing { passkey, responder });
        procedure.set_bredr_id(le_or_bredr_id);
        Ok(())
    }

    fn handle_pairing_complete(
        &mut self,
        le_or_bredr_id: PeerId,
        success: bool,
    ) -> Result<Option<PeerId>, Error> {
        debug!(?le_or_bredr_id, success, "pairing complete");
        // Try to match the request to the peer's LE or BR/EDR PeerId.
        let procedure = self
            .procedures
            .inner()
            .iter_mut()
            .find(|(id, p)| **id == le_or_bredr_id || p.bredr_id == Some(le_or_bredr_id));
        if procedure.is_none() {
            debug!(?le_or_bredr_id, "Pairing complete for non-Fast Pair peer. Ignoring..");
            // TODO(fxbug.dev/101721): Consider relaying upstream if I/O capabilities are defined
            // per peer.
            return Ok(None);
        }
        let (le_id, procedure) = procedure.unwrap();

        if success && procedure.is_passkey_checked() {
            let _ = procedure.transition(ProcedureState::PairingComplete);
            // Regardless of which PeerId matched, we want to notify the component that the LE
            // variant has completed - the rest of the component only cares about LE.
            return Ok(Some(*le_id));
        }

        if success {
            warn!(?le_id, ?procedure.state, "Unexpected pairing success for Fast Pair procedure");
            // TODO(fxbug.dev/103204): This indicates Fast Pair pairing was completed in an
            // non spec-compliant manner. We should remove the bond via sys.Access/Forget.
        } else {
            info!(?le_id, "Pairing failure");
        }

        let id = *le_id;
        self.cancel_pairing_procedure(&id)?;
        Ok(None)
    }

    fn handle_pairing_delegate_request(
        &mut self,
        request: PairingDelegateRequest,
    ) -> Result<Option<PeerId>, Error> {
        debug!(?request, "Received PairingDelegate request");
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
                self.handle_pairing_request(peer.id, method, displayed_passkey, responder)?;
            }
            PairingDelegateRequest::OnPairingComplete { id, success, .. } => {
                return self.handle_pairing_complete(id.into(), success);
            }
            PairingDelegateRequest::OnRemoteKeypress { .. } => {}
        }
        Ok(None)
    }

    fn poll_procedure_deadlines(&mut self, cx: &mut Context<'_>) {
        if self.procedures.is_terminated() {
            return;
        }

        if let Poll::Ready(Some(id)) = self.procedures.poll_next_unpin(cx) {
            info!(?id, "Deadline reached for Fast Pair procedure. Canceling");
            // Deadline was reached (e.g no updates within the expected time). Procedure is no
            // longer valid.
            let _ = self.cancel_pairing_procedure(&id);
        }
    }
}

/// The Stream implementation for `PairingManager` does 4 things:
///   (1) Poll the connection to the upstream `sys.Pairing` client to check if it is open. Because
///       pairing functionality is determined by the client's existence, the `PairingManager` stream
///       will terminate if the client disconnects.
///   (2) Drive pairing relay futures to completion. This is applicable to non-Fast Pair requests.
///       Relaying the request to the upstream `sys.Pairing` client is async. Each request is
///       inserted into a collection and is progressed anytime the stream is polled.
///   (3) Poll the connection to the downstream `sys.Pairing` delegate. Received pairing requests
///       will be handled by the `PairingManager`.
///   (4) Poll the set of active Fast Pair pairing procedures. Per the GFPS, if a procedure doesn't
///       progress within `Procedure::DEFAULT_PROCEDURE_TIMEOUT_DURATION`, the shared secret `key` is no
///       longer valid.
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

        // Check if any procedures have missed deadlines.
        self.poll_procedure_deadlines(cx);
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
    use async_utils::PollExt;
    use fidl::client::QueryResponseFut;
    use fidl_fuchsia_bluetooth_sys::{PairingKeypress, PairingMarker, PairingRequestStream};
    use fuchsia_bluetooth::types::Address;
    use futures::{future::Either, pin_mut};

    use crate::types::keys;

    /// Holds onto the necessary FIDL connections to facilitate pairing. Provides convenience
    /// methods to drive pairing procedures.
    pub struct MockPairing {
        pub pairing_svc: PairingProxy,
        pub downstream_pairing_server: PairingRequestStream,
        pub downstream_delegate_client: PairingDelegateProxy,
        pub upstream_delegate_server: PairingDelegateRequestStream,
    }

    impl MockPairing {
        /// Builds and returns a PairingManager and MockPairing object which can be used to simulate
        /// upstream & downstream pairing actions.
        pub async fn new_with_manager() -> (PairingManager, Self) {
            let (pairing_svc, mut downstream_pairing_server) =
                fidl::endpoints::create_proxy_and_stream::<PairingMarker>().unwrap();
            let (delegate, upstream_delegate_server) =
                fidl::endpoints::create_proxy_and_stream::<PairingDelegateMarker>().unwrap();
            let client = PairingArgs {
                input: InputCapability::None,
                output: OutputCapability::None,
                delegate,
            };
            let manager = PairingManager::new(pairing_svc.clone(), client)
                .expect("can create pairing manager");
            // Expect the manager to register a delegate on behalf of the upstream.
            let (_, _, delegate, _) = downstream_pairing_server
                .select_next_some()
                .await
                .expect("fidl request")
                .into_set_pairing_delegate()
                .unwrap();
            let downstream_delegate_client = delegate.into_proxy().unwrap();

            let mock = MockPairing {
                pairing_svc,
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

        // A new pairing procedure is started between us and the peer. Expect the delegate to be
        // claimed.
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

        // The peer will make a GATT Write to the Account Key characteristic. The shared secret will
        // still be available as the peer can optionally set a personalized name via GATT.
        assert_matches!(manager.account_key_write(id), Ok(_));
        assert_matches!(manager.key_for_procedure(&id), Some(_));

        // The peer can set the personalized name. This is the last step in the procedure.
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
        // Can't complete the procedure since pairing failed.
        assert_matches!(manager.complete_pairing_procedure(id), Err(Error::InternalError(_)));
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
        // Put the procedure in the `Pairing` state.
        let request_fut = mock.make_pairing_request(id, 555666);
        assert_matches!(manager.select_next_some().now_or_never(), None);
        assert_matches!(manager.compare_passkey(id, 555666), Ok(_));
        let result = request_fut.await.expect("fidl response");
        assert_eq!(result, (true, 555666));
        // Trying to compare passkeys again is an error.
        assert_matches!(manager.compare_passkey(id, 555666), Err(Error::InternalError(_)));
        // Can't complete the procedure when pairing is in progress.
        assert_matches!(manager.complete_pairing_procedure(id), Err(Error::InternalError(_)));

        // Put the procedure in the `PairingComplete` state.
        let _ = mock
            .downstream_delegate_client
            .on_pairing_complete(&mut id.into(), true)
            .expect("valid fidl request");
        let result = manager.select_next_some().await;
        assert_eq!(result, id);

        // Trying to compare passkeys or complete the procedure is an error.
        assert_matches!(manager.compare_passkey(id, 555666), Err(Error::InternalError(_)));
        assert_matches!(manager.complete_pairing_procedure(id), Err(Error::InternalError(_)));

        // Put the procedure in the terminal `AccountKeyWritten` state. Procedure can be completed.
        assert_matches!(manager.account_key_write(id), Ok(_));
        // Trying to compare passkey or signal an account key write again is an Error.
        assert_matches!(manager.compare_passkey(id, 555666), Err(Error::InternalError(_)));
        assert_matches!(manager.account_key_write(id), Err(_));
        assert_matches!(manager.complete_pairing_procedure(id), Ok(_));
    }

    // TODO(fxbug.dev/102963): This test will be obsolete if the PairingManager is resilient to
    // ordering of events.
    #[fuchsia::test]
    async fn complete_pairing_before_on_pairing_complete_is_error() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        let id = PeerId(123);
        assert_matches!(manager.new_pairing_procedure(id, keys::tests::example_aes_key()), Ok(_));
        mock.expect_set_pairing_delegate().await;

        // Peer tries to pair - no stream item since pairing isn't complete.
        let request_fut = mock.make_pairing_request(id, 555666);
        assert_matches!(manager.select_next_some().now_or_never(), None);

        // No stream item after comparing passkeys since pairing isn't complete.
        let passkey = manager.compare_passkey(id, 555666).expect("successful comparison");
        assert_eq!(passkey, 555666);
        assert_matches!(manager.select_next_some().now_or_never(), None);
        // Expect the pairing request to resolve as passkeys have been verified.
        let result = request_fut.await.expect("fidl response");
        assert_eq!(result, (true, 555666));

        // Attempting to complete pairing before OnPairingComplete is an Error.
        assert_matches!(manager.complete_pairing_procedure(id), Err(_));
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

        // Downstream server signals pairing completion.
        let _ = mock
            .downstream_delegate_client
            .on_pairing_complete(&mut id1.into(), true)
            .expect("valid fidl request");
        // Expect a Pairing Manager stream item indicating completion of pairing with the peer.
        let result = manager.select_next_some().await;
        assert_eq!(result, id1);

        // Peer makes a GATT write to the Account Key characteristic.
        assert_matches!(manager.account_key_write(id1), Ok(_));

        // PairingManager owner will complete pairing once the peer optionally sets a personalized
        // name. If no name is set, then the procedure will eventually time out after
        // `Procedure::DEFAULT_PROCEDURE_TIMEOUT_DURATION`.
        assert_matches!(manager.complete_pairing_procedure(id1), Ok(_));
        // Because a pairing procedure for `id2` is still active, the Pairing Delegate
        // should not be reset.
        assert_matches!(mock.downstream_pairing_server.next().now_or_never(), None);
        assert_matches!(manager.key_for_procedure(&id1), None);
        assert_matches!(manager.key_for_procedure(&id2), Some(_));
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
    fn procedure_terminates_after_timer_completes() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        exec.set_fake_time(fasync::Time::from_nanos(1_000_000_000));

        let id = PeerId(123);
        let procedure = Procedure::new(id, keys::tests::example_aes_key());
        pin_mut!(procedure);
        let _ = exec.run_until_stalled(&mut procedure).expect_pending("deadline not reached");

        // Advancing time by less than the deadline means the Procedure Future isn't done.
        let half_dur = Procedure::DEFAULT_PROCEDURE_TIMEOUT_DURATION / 2;
        exec.set_fake_time(fasync::Time::after(half_dur));
        assert!(!exec.wake_expired_timers());
        let _ = exec.run_until_stalled(&mut procedure).expect_pending("deadline not reached");

        // Transitioning the procedure should reset the timer.
        let _ = procedure.transition(ProcedureState::PasskeyChecked);
        let _ = exec.run_until_stalled(&mut procedure).expect_pending("deadline not reached");

        // Even though the original deadline was met, the procedure should still be active since it
        // has a new deadline.
        exec.set_fake_time(fasync::Time::after(half_dur));
        assert!(!exec.wake_expired_timers());
        let _ = exec.run_until_stalled(&mut procedure).expect_pending("deadline not reached");

        // Deadline reached - procedure should resolve.
        exec.set_fake_time(fasync::Time::after(half_dur));
        assert!(exec.wake_expired_timers());
        let finished_id = exec.run_until_stalled(&mut procedure).expect("deadline reached");
        assert_eq!(finished_id, id);
    }

    #[fuchsia::test]
    fn procedure_evicted_after_deadline() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        exec.set_fake_time(fasync::Time::from_nanos(1_000_000_000));

        let setup_fut = MockPairing::new_with_manager();
        pin_mut!(setup_fut);
        let (mut manager, mut mock) =
            exec.run_until_stalled(&mut setup_fut).expect("can create pairing manager");

        // New procedure is started - the shared secret should be available.
        let id = PeerId(123);
        assert_matches!(manager.new_pairing_procedure(id, keys::tests::example_aes_key()), Ok(_));
        let _ = exec.run_until_stalled(&mut manager.next()).expect_pending("no stream item");
        {
            let expect_fut = mock.expect_set_pairing_delegate();
            pin_mut!(expect_fut);
            let () = exec.run_until_stalled(&mut expect_fut).expect("pairing delegate request");
        }
        assert_matches!(manager.key_for_procedure(&id), Some(_));

        // Deadline not reached yet - procedure is still in progress.
        let half_dur = Procedure::DEFAULT_PROCEDURE_TIMEOUT_DURATION / 2;
        exec.set_fake_time(fasync::Time::after(half_dur));
        assert!(!exec.wake_expired_timers());
        let _ = exec.run_until_stalled(&mut manager.next()).expect_pending("no stream item");
        assert_matches!(manager.key_for_procedure(&id), Some(_));

        // Peer makes a pairing request - should transition to the next state in the procedure.
        let request_fut = mock.make_pairing_request(id, 555666);
        pin_mut!(request_fut);
        let _ = exec.run_until_stalled(&mut request_fut).expect_pending("waiting for response");
        let _ = exec.run_until_stalled(&mut manager.next()).expect_pending("no stream item");
        assert_matches!(manager.key_for_procedure(&id), Some(_));

        // Advancing time to the deadline should evict the procedure - shared secret no longer
        // available.
        exec.set_fake_time(fasync::Time::after(Procedure::DEFAULT_PROCEDURE_TIMEOUT_DURATION));
        assert!(exec.wake_expired_timers());
        let _ = exec.run_until_stalled(&mut manager.next()).expect_pending("no stream item");
        assert_matches!(manager.key_for_procedure(&id), None);
        // Peer's pairing request should be rejected as the procedure was canceled.
        let (accepted, _) = exec
            .run_until_stalled(&mut request_fut)
            .expect("pairing response")
            .expect("result is OK");
        assert!(!accepted);
        // Because there are no other active procedures, the PairingDelegate should be handed back.
        let expect_fut = mock.expect_set_pairing_delegate();
        pin_mut!(expect_fut);
        let () = exec.run_until_stalled(&mut expect_fut).expect("pairing delegate request");
    }

    #[fuchsia::test]
    async fn pairing_procedure_with_differing_ids() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        // Define a remote peer that has been assigned an LE and BR/EDR PeerId.
        let le_id = PeerId(123);
        let bredr_id = PeerId(789);

        // Pairing procedure is always initiated by the LE peer.
        assert_matches!(
            manager.new_pairing_procedure(le_id, keys::tests::example_aes_key()),
            Ok(_)
        );
        mock.expect_set_pairing_delegate().await;
        // Peer tries to pair over BR/EDR - no stream item since pairing isn't complete.
        let request_fut = mock.make_pairing_request(bredr_id, 123456);
        assert_matches!(manager.select_next_some().now_or_never(), None);
        // Procedure should still be active.
        assert_matches!(manager.key_for_procedure(&le_id), Some(_));

        // Remote peer wants to compare passkeys - no stream item since pairing isn't complete.
        let passkey = manager.compare_passkey(le_id, 123456).expect("successful comparison");
        assert_eq!(passkey, 123456);
        assert_matches!(manager.select_next_some().now_or_never(), None);
        // Expect the pairing request to complete as passkeys have been verified.
        let result = request_fut.await.expect("fidl response");
        assert_eq!(result, (true, 123456));

        // Downstream server signals pairing completion.
        let _ = mock
            .downstream_delegate_client
            .on_pairing_complete(&mut bredr_id.into(), true)
            .expect("valid fidl request");
        // Expect a Pairing Manager stream item indicating completion of pairing with the peer. The
        // LE PeerId associated with this peer is used.
        let result = manager.select_next_some().await;
        assert_eq!(result, le_id);
        // Shared secret for the procedure is still available.
        assert_matches!(manager.key_for_procedure(&le_id), Some(_));

        // Expect an Account Key write.
        assert_matches!(manager.account_key_write(le_id), Ok(_));

        // PairingManager owner will complete pairing once the peer sets a personalized name.
        assert_matches!(manager.complete_pairing_procedure(le_id), Ok(_));
        // All pairing related work is complete, so we expect PairingManager to hand pairing
        // capabilities back to the upstream. The shared secret should no longer be saved.
        mock.expect_set_pairing_delegate().await;
        assert_matches!(manager.key_for_procedure(&le_id), None);
    }

    /// This test exercises the eviction flow in the event the peer does not set a personalized name
    /// We expect the procedure to be active for `Procedure::DEFAULT_PROCEDURE_TIMEOUT_DURATION`
    /// after the Account Key write.
    #[fuchsia::test]
    fn procedure_without_name_write_evicted_after_deadline() {
        let mut exec = fasync::TestExecutor::new_with_fake_time().unwrap();
        exec.set_fake_time(fasync::Time::from_nanos(1_000_000_000));

        let setup_fut = MockPairing::new_with_manager();
        pin_mut!(setup_fut);
        let (mut manager, mut mock) =
            exec.run_until_stalled(&mut setup_fut).expect("can create pairing manager");

        // New procedure is started.
        let id = PeerId(123);
        assert_matches!(manager.new_pairing_procedure(id, keys::tests::example_aes_key()), Ok(_));
        let _ = exec.run_until_stalled(&mut manager.next()).expect_pending("no stream item");
        {
            let expect_fut = mock.expect_set_pairing_delegate();
            pin_mut!(expect_fut);
            let () = exec.run_until_stalled(&mut expect_fut).expect("pairing delegate request");
        }

        // Deadline not reached yet - procedure is still in progress.
        let half_dur = Procedure::DEFAULT_PROCEDURE_TIMEOUT_DURATION / 2;
        exec.set_fake_time(fasync::Time::after(half_dur));
        assert!(!exec.wake_expired_timers());
        let _ = exec.run_until_stalled(&mut manager.next()).expect_pending("no stream item");
        assert_matches!(manager.key_for_procedure(&id), Some(_));

        // Peer tries to pair.
        let request_fut = mock.make_pairing_request(id, 555666);
        pin_mut!(request_fut);
        let _ = exec.run_until_stalled(&mut request_fut).expect_pending("waiting for response");
        let _ = exec.run_until_stalled(&mut manager.next()).expect_pending("no stream item");
        assert_matches!(manager.key_for_procedure(&id), Some(_));

        // Passkey comparison - pairing request should resolve.
        let passkey = manager.compare_passkey(id, 555666).expect("successful comparison");
        assert_eq!(passkey, 555666);
        let _ = exec.run_until_stalled(&mut manager.next()).expect_pending("no stream item");
        let _ = exec.run_until_stalled(&mut request_fut).expect("pairing response");

        // Downstream server signals pairing completion - expect the stream item with `id`.
        let _ = mock
            .downstream_delegate_client
            .on_pairing_complete(&mut id.into(), true)
            .expect("valid fidl request");
        let finished_id = exec.run_until_stalled(&mut manager.next()).expect("stream item");
        assert_eq!(finished_id, Some(id));

        // Peer makes an Account Key write. This is the last mandatory step of the procedure.
        let _ = manager.account_key_write(id).expect("correct state");
        let _ = exec.run_until_stalled(&mut manager.next()).expect_pending("no stream item");

        // The peer doesn't optionally set the device name. After the deadline, the procedure should
        // be completed and cleaned up.
        exec.set_fake_time(fasync::Time::after(Procedure::DEFAULT_PROCEDURE_TIMEOUT_DURATION));
        assert!(exec.wake_expired_timers());
        let _ = exec.run_until_stalled(&mut manager.next()).expect_pending("no stream item");
        assert_matches!(manager.key_for_procedure(&id), None);
        // Because there are no other active procedures, the PairingDelegate should be handed back.
        let expect_fut = mock.expect_set_pairing_delegate();
        pin_mut!(expect_fut);
        let () = exec.run_until_stalled(&mut expect_fut).expect("pairing delegate request");
    }

    #[fuchsia::test]
    async fn complete_procedure_after_initialization() {
        let (mut manager, mut mock) = MockPairing::new_with_manager().await;

        // Initialize a new procedure.
        let id = PeerId(123);
        manager.new_pairing_procedure(id, keys::tests::example_aes_key()).expect("can initialize");
        mock.expect_set_pairing_delegate().await;
        assert_matches!(manager.key_for_procedure(&id), Some(_));

        // The procedure can be immediately completed if the peer is simply writing a personalized
        // name.
        assert_matches!(manager.complete_pairing_procedure(id), Ok(_));
        assert_matches!(manager.key_for_procedure(&id), None);
    }
}
