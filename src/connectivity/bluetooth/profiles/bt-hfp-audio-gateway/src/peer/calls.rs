// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::calls::{
        call_list::CallList,
        types::{CallIndicators, CallIndicatorsUpdates, FidlNumber},
    },
    crate::{
        error::CallError,
        peer::procedure::{dtmf::DtmfCode, hold::CallHoldAction},
    },
    async_utils::{
        hanging_get::client::HangingGetStream,
        stream::{StreamItem, StreamMap, StreamWithEpitaph, Tagged, WithEpitaph, WithTag},
    },
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_bluetooth_hfp::{
        CallAction as FidlCallAction, CallMarker, CallProxy, PeerHandlerProxy, RedialLast,
        TransferActive,
    },
    fuchsia_async as fasync,
    futures::stream::{FusedStream, Stream, StreamExt},
    log::{debug, info, warn},
    std::{
        pin::Pin,
        task::{Context, Poll},
    },
};

pub use {fidl_fuchsia_bluetooth_hfp::CallState, number::Number, types::Direction};

/// Defines the types associated with a phone number.
mod number;

/// Defines the collection used to identify and store calls.
mod call_list;

/// Defines commonly used types when interacting with a call.
pub mod types;

/// The index associated with a call, that is guaranteed to be unique for the lifetime of the call,
/// but will be recycled after the call is released.
pub type CallIdx = usize;

/// Internal state and resources associated with a single call.
struct CallEntry {
    /// Proxy associated with this call.
    // TODO (fxb/64550): Remove when call requests are initiated
    #[allow(unused)]
    proxy: CallProxy,
    /// The remote party's number.
    number: Number,
    /// Current state.
    state: CallState,
    /// Time of the last update to the call's `state`.
    state_updated_at: fasync::Time,
    /// Direction of the call. If the Direction cannot be determined from the Call's CallState, it
    /// is set to `Unknown`.
    direction: Direction,
}

impl CallEntry {
    pub fn new(proxy: CallProxy, number: Number, state: CallState) -> Self {
        let state_updated_at = fasync::Time::now();
        Self { proxy, number, state, state_updated_at, direction: state.into() }
    }

    /// Update the state. Also update the direction if it is Unknown.
    /// `state_updated_at` is changed only if self.state != state.
    pub fn set_state(&mut self, state: CallState) {
        if self.direction == Direction::Unknown {
            self.direction = state.into();
        }
        if self.state != state {
            self.state_updated_at = fasync::Time::now();
            self.state = state;
        }
    }

    // TODO (fxb/64550): Remove when call requests are initiated
    #[allow(unused)]
    pub fn is_active(&self) -> bool {
        self.state == CallState::OngoingActive
    }
}

/// The current state of a call.
#[derive(Debug, Clone, PartialEq)]
pub struct Call {
    /// Unique identifier associated with a call for the lifetime of the call.
    /// Once the state is `Terminated` or `TransferredToAg` the index may be reused by another
    /// call.
    pub index: CallIdx,
    /// Remote party's number.
    pub number: Number,
    /// Current state.
    pub state: CallState,
    /// Direction of the call.
    pub direction: Direction,
}

impl Call {
    pub fn new(index: CallIdx, number: Number, state: CallState, direction: Direction) -> Self {
        Self { index, number, state, direction }
    }
}

/// A stream of updates to the state of calls. Each update contains the `Number` and the
/// `CallState`. When the channel for a given call is closed, an epitaph is returned with the
/// `Number`.
/// The `Number` uniquely identifies a call. TODO (fxbug.dev/64558): Handle multi-party calls.
type CallStateUpdates =
    StreamMap<CallIdx, StreamWithEpitaph<Tagged<CallIdx, HangingGetStream<CallState>>, CallIdx>>;

/// Manages the state of all ongoing calls reported by the call manager.
///
/// ### Fetching Call information.
///
/// `Calls` provides methods for interacting with specific calls or listing all calls. These methods
/// can be used to query information about a call such as the associated `Number` or the
/// `CallState`.
///
///
/// ### Requesting Action on Calls
///
/// `Calls` can be used to request that a new call be made or for a call's state to be changed. Any
/// request to change the state of a call cannot be handled directly by `Calls`. Instead the request
/// will be forwarded to the Call Manager service for handling.
///
/// The state of a call is not updated to reflect a request until the Call Manager notifies `Calls`
/// that a change has taken effect. A client of `Calls` cannot assume that a request succeeded
/// until the status has been updated in `Calls`.
///
///
/// ### Calls as Stream of Call Status Updates.
///
/// Clients that are interested in changes to Call Status Updates should poll `Calls`' Stream
/// implementation. A Vec of `Indicator`s will be produced as a stream item whenever the status
/// of at least one call related `Indicator` has changed since the last stream item was produced.
///
/// The returned Vec has both Ordering and Uniqueness guarantees.
///
/// It is guaranteed to be ordered such that Indicator::Call values come before Indicator::CallSetup
/// values and Indicator::CallSetup values come before Indicator::CallHeld values.
///
/// This Vec is also is guaranteed to have no more than one value for each `Indicator` variant
/// which represents the current state of the calls as reported by the Call Manager.
pub(crate) struct Calls {
    /// A Stream of new calls.
    new_calls: Option<HangingGetStream<(ClientEnd<CallMarker>, FidlNumber, CallState)>>,
    /// Store the current state and associated resources of every Call.
    current_calls: CallList<CallEntry>,
    /// A Stream of all updates to the state of ongoing calls.
    call_updates: CallStateUpdates,
    /// The last set of indicator values returned from Polling Calls as a Stream.
    reported_indicators: CallIndicators,
    /// The Calls Stream terminated state.
    terminated: bool,
}

impl Calls {
    pub fn new(proxy: Option<PeerHandlerProxy>) -> Self {
        let new_calls = proxy
            .map(|proxy| HangingGetStream::new(Box::new(move || Some(proxy.watch_next_call()))));
        Self {
            new_calls,
            current_calls: CallList::default(),
            call_updates: CallStateUpdates::empty(),
            reported_indicators: CallIndicators::default(),
            terminated: false,
        }
    }

    /// Insert a new call.
    /// Returns the index of the call inserted.
    fn handle_new_call(
        &mut self,
        call: ClientEnd<CallMarker>,
        number: Number,
        state: CallState,
    ) -> CallIdx {
        let proxy = call.into_proxy().unwrap();
        let call = CallEntry::new(proxy.clone(), number.clone(), state);
        let index = self.current_calls.insert(call);
        let call_state = HangingGetStream::new(Box::new(move || Some(proxy.watch_state())));
        self.call_updates.insert(index, call_state.tagged(index).with_epitaph(index));
        self.terminated = false;
        index
    }

    /// Check and mark this stream "terminated" if the appropriate conditions are met.
    /// Specifically, Calls is terminated if both `new_calls` and `call_updates` cannot produce any
    /// new values.
    fn check_termination_condition(&mut self) {
        self.terminated = self.new_calls.is_none() && self.call_updates.inner().is_empty();
    }

    /// Run `f`, passing the CallProxy associated with the given `number` into `f` and removing
    /// the CallProxy from the map if an error is returned by running `f`.
    fn send_call_request(
        &mut self,
        index: CallIdx,
        f: impl FnOnce(&CallProxy) -> Result<(), fidl::Error>,
    ) -> Result<(), CallError> {
        let call = self.current_calls.get(index).ok_or(CallError::UnknownIndexError(index))?;
        let result = f(&call.proxy);
        if let Err(e) = result {
            if !e.is_closed() {
                warn!("Error making request on Call channel for call {:?}: {}", index, e);
            }
            self.remove_call(index);
        }
        Ok(())
    }

    /// Send a request to the call manager to place the call on hold.
    #[cfg(test)]
    fn request_hold(&mut self, index: CallIdx) -> Result<(), CallError> {
        self.send_call_request(index, CallProxy::request_hold)
    }

    /// Send a request to the call manager to make the call active.
    /// Only one call can be active at a time. Before requesting the call at `index` be made
    /// active, all active calls are either terminated or placed on hold depending on
    /// `terminate_others`.
    pub fn request_active(
        &mut self,
        index: CallIdx,
        terminate_others: bool,
    ) -> Result<(), CallError> {
        let action =
            if terminate_others { CallProxy::request_terminate } else { CallProxy::request_hold };

        // Collect active_calls into a Vec to avoid double borrowing self.
        let active_calls: Vec<_> = self
            .current_calls
            .calls()
            .filter_map(|(i, call)| (i != index && call.is_active()).then(|| i))
            .collect();

        for i in active_calls {
            // Failures are ignored.
            let _ = self.send_call_request(i, action);
        }
        self.send_call_request(index, CallProxy::request_active)
    }

    /// Send a request to the call manager to terminate the call.
    pub fn request_terminate(&mut self, index: CallIdx) -> Result<(), CallError> {
        self.send_call_request(index, CallProxy::request_terminate)
    }

    /// Send a request to the call manager to transfer the audio of the call from the
    /// headset to the fuchsia device.
    #[cfg(test)]
    fn request_transfer_audio(&mut self, index: CallIdx) -> Result<(), CallError> {
        self.send_call_request(index, CallProxy::request_transfer_audio)
    }

    /// Send a dtmf code to the call manager for active call,
    pub async fn send_dtmf_code(&mut self, _code: DtmfCode) {
        unimplemented!(
            "Sending a dtmf code can fail where other call requests cannot. \
            It must be handled separately"
        );
    }

    /// Return a Vec of the current call state for every call that `Calls` is tracking.
    pub fn current_calls(&self) -> Vec<Call> {
        self.current_calls
            .calls()
            .map(|(index, call)| Call::new(index, call.number.clone(), call.state, call.direction))
            .collect()
    }

    /// Return true if at least one call is in the Active state.
    #[cfg(test)]
    fn is_call_active(&self) -> bool {
        self.calls().any(|c| c.1.state == CallState::OngoingActive)
    }

    /// Remove all references to the call assigned to `index`.
    fn remove_call(&mut self, index: CallIdx) {
        self.call_updates.remove(&index);
        self.current_calls.remove(index);
    }

    /// Iterator over all calls in `state`.
    fn all_by_state(
        &self,
        state: CallState,
    ) -> impl Iterator<Item = (CallIdx, &CallEntry)> + Clone {
        self.calls().filter(move |c| c.1.state == state)
    }

    /// Operations that act on calls in a particular state should act on the call that was put into
    /// that state first. `oldest_by_state` allows querying for a call that meets that criteria.
    fn oldest_by_state(&self, state: CallState) -> Option<(CallIdx, &CallEntry)> {
        self.all_by_state(state).min_by_key(|c| c.1.state_updated_at)
    }

    /// Return the Call that has been in the IncomingRinging call state the longest if at least one
    /// exists.
    pub fn ringing(&self) -> Option<Call> {
        self.oldest_by_state(CallState::IncomingRinging)
            .map(|(idx, call)| Call::new(idx, call.number.clone(), call.state, call.direction))
    }

    /// Get the Call that has been waiting the longest. Returns None if there are no waiting calls.
    pub fn waiting(&self) -> Option<Call> {
        self.oldest_by_state(CallState::IncomingWaiting)
            .map(|(idx, call)| Call::new(idx, call.number.clone(), call.state, call.direction))
    }

    /// Answer a single call based on the following policy:
    ///
    ///   * The oldest Incoming Ringing call is answered.
    ///   * If there are no Incoming Ringing calls, the oldest Incoming Waiting call is answered.
    ///   * If there are no calls in any of the previously specified states, return an Error.
    // Incoming Waiting is prioritized last because implementations should be using the AT+CHLD
    // command to manage waiting calls instead of the ATA command.
    pub fn answer(&mut self) -> Result<(), CallError> {
        use CallState::*;
        let states = vec![IncomingRinging, IncomingWaiting];
        let idx = self
            .oldest_by_state(states[0])
            .or_else(|| self.oldest_by_state(states[1]))
            .ok_or(CallError::None(states))?
            .0;
        self.request_active(idx, true)
    }

    /// Terminate a call.
    ///
    /// The AT+CHUP command does not specify a particular call to be terminated. When there are
    /// multiple calls that could be terminated, the Audio Gateway must make a determination as to
    /// which call to terminate. This prioritization is based on the perceived urgency of a call in
    /// a given state.
    ///
    /// Terminate a single call based on the following policy:
    ///
    ///   * The oldest Incoming Ringing call is terminated.
    ///   * If there are no Incoming Ringing calls, the oldest Ongoing Active call is terminated.
    ///   * If there are no Ongoing Active calls, the oldest Ongoing Held call is terminated.
    ///   * If there are no Ongoing Held calls, the oldest Incoming Waiting call is terminated.
    ///   * If there are no calls in any of the previously specified states, return an Error.
    // If it becomes desirable for this policy to be configurable, it should be pulled out into the
    // component's configuration data.
    //
    // Incoming Waiting is prioritized last because implementations should be using the AT+CHLD
    // command to manage waiting calls instead of the AT+CHUP command.
    pub fn hang_up(&mut self) -> Result<(), CallError> {
        use CallState::*;
        let states = vec![IncomingRinging, OngoingActive, OngoingHeld, IncomingWaiting];
        let idx = self
            .oldest_by_state(states[0])
            .or_else(|| self.oldest_by_state(states[1]))
            .or_else(|| self.oldest_by_state(states[2]))
            .or_else(|| self.oldest_by_state(states[3]))
            .ok_or(CallError::None(states))?
            .0;
        self.request_terminate(idx)
    }

    /// Perform the supplementary call service related to held or waiting calls specified by
    /// `action`. See HFP v1.8, Section 4.22 for more information.
    ///
    /// Waiting calls are prioritized over held calls when an action operates on a specific call.
    /// Per 4.34.2 AT+CHLD: "Where both a held and a waiting call exist, the above procedures shall
    /// apply to the waiting call (i.e., not to the held call) in conflicting situation."
    ///
    /// Returns an error if the CallIdx associated with a specific action is invalid.
    pub fn hold(&mut self, action: CallHoldAction) -> Result<(), CallError> {
        use {CallHoldAction::*, CallState::*};
        match action {
            ReleaseAllHeld => {
                let waiting_calls: Vec<_> =
                    self.all_by_state(IncomingWaiting).map(|c| c.0).collect();
                let held_calls: Vec<_> = self.all_by_state(OngoingHeld).map(|c| c.0).collect();

                // Terminated state on incoming waiting calls is prioritized over held calls.
                let to_release = if !waiting_calls.is_empty() { waiting_calls } else { held_calls };

                for idx in to_release {
                    let _ = self.request_terminate(idx);
                }
            }
            ReleaseAllActive => {
                // Active state on incoming waiting calls is prioritized over held calls.
                let idx = self
                    .oldest_by_state(IncomingWaiting)
                    .or_else(|| self.oldest_by_state(OngoingHeld))
                    .ok_or(CallError::None(vec![IncomingWaiting, OngoingHeld]))?
                    .0;

                self.request_active(idx, true)?;
            }
            ReleaseSpecified(idx) => self.request_terminate(idx)?,
            HoldActiveAndAccept => {
                // Active state on incoming waiting calls is prioritized over held calls.
                let idx = self
                    .oldest_by_state(IncomingWaiting)
                    .or_else(|| self.oldest_by_state(OngoingHeld))
                    .ok_or(CallError::None(vec![IncomingWaiting, OngoingHeld]))?
                    .0;

                self.request_active(idx, false)?;
            }
            HoldAllExceptSpecified(idx) => self.request_active(idx, false)?,
        }
        Ok(())
    }

    /// Returns true if the state of any calls requires ringing.
    pub fn should_ring(&self) -> bool {
        self.calls().any(|c| c.1.state == CallState::IncomingRinging)
    }

    /// Get the current `CallIndicators` based on the state of all calls.
    pub fn indicators(&mut self) -> CallIndicators {
        let mut calls = self.calls().collect::<Vec<_>>();
        calls.sort_by_key(|c| c.1.state_updated_at);
        CallIndicators::find(calls.into_iter().rev().map(|c| c.1.state))
    }

    /// Return an iterator of the calls and associated call indices.
    fn calls(&self) -> impl Iterator<Item = (CallIdx, &CallEntry)> + Clone {
        self.current_calls.calls()
    }

    /// Helper function to poll the new_calls hanging get for any available items.
    /// Polls until there are no more items ready. Updates internal state with the
    /// available items.
    fn poll_and_consume_new_calls(&mut self, cx: &mut Context<'_>) {
        // Loop until pending or self.new_calls is set to None.
        while let Some(new_calls) = &mut self.new_calls {
            match new_calls.poll_next_unpin(cx) {
                Poll::Ready(Some(Ok((call, fidl_number, state)))) => {
                    self.handle_new_call(call, fidl_number.into(), state);
                }
                Poll::Ready(Some(Err(_e))) => self.new_calls = None,
                Poll::Ready(None) => self.new_calls = None,
                Poll::Pending => break,
            }
        }
    }

    /// Helper function to poll the call_updates collection for call state changes.
    /// Polls until there are no more updates ready. Updates internal state with
    /// available items.
    fn poll_and_consume_call_updates(&mut self, cx: &mut Context<'_>) {
        // Loop until self.call_updates is terminated or the stream is exhausted.
        while !self.call_updates.is_terminated() {
            match self.call_updates.poll_next_unpin(cx) {
                Poll::Ready(Some(item)) => match item {
                    StreamItem::Item((index, Ok(state))) => {
                        if let Some(call) = self.current_calls.get_mut(index) {
                            call.set_state(state);
                        } else {
                            self.remove_call(index);
                        }
                    }
                    StreamItem::Item((index, Err(e))) => {
                        info!("Call {} channel closed with error: {}", index, e);
                        self.remove_call(index);
                    }
                    StreamItem::Epitaph(index) => {
                        debug!("Call {} channel closed", index);
                        self.remove_call(index);
                    }
                },
                Poll::Ready(None) | Poll::Pending => break,
            }
        }
    }
}

impl Unpin for Calls {}

impl Stream for Calls {
    type Item = CallIndicatorsUpdates;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.check_termination_condition();

        if self.terminated {
            return Poll::Ready(None);
        }

        // Update the state of all new and ongoing calls.
        self.poll_and_consume_new_calls(cx);
        self.poll_and_consume_call_updates(cx);

        let previous = self.reported_indicators;
        self.reported_indicators = self.indicators();

        // Return a list of all the indicators that have changed as a result of the
        // new call state.
        let changes = self.reported_indicators.difference(previous);

        if !changes.is_empty() {
            Poll::Ready(Some(changes))
        } else {
            Poll::Pending
        }
    }
}

impl FusedStream for Calls {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

/// Call actions that can be requested of the AG.
#[derive(Debug)]
pub enum CallAction {
    InitiateByNumber(String),
    InitiateByMemoryLocation(String),
    InitiateByRedialLast,
    TransferActive,
}

impl From<CallAction> for FidlCallAction {
    fn from(call_action: CallAction) -> Self {
        match call_action {
            CallAction::InitiateByNumber(number) => Self::DialFromNumber(number),
            CallAction::InitiateByMemoryLocation(location) => Self::DialFromLocation(location),
            CallAction::InitiateByRedialLast => Self::RedialLast(RedialLast),
            CallAction::TransferActive => Self::TransferActive(TransferActive),
        }
    }
}

impl From<FidlCallAction> for CallAction {
    fn from(fidl_call_action: FidlCallAction) -> Self {
        match fidl_call_action {
            FidlCallAction::DialFromNumber(number) => Self::InitiateByNumber(number),
            FidlCallAction::DialFromLocation(location) => Self::InitiateByMemoryLocation(location),
            FidlCallAction::RedialLast(RedialLast) => Self::InitiateByRedialLast,
            FidlCallAction::TransferActive(TransferActive) => Self::TransferActive,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_bluetooth_hfp::{
            CallRequest, CallRequestStream, PeerHandlerMarker, PeerHandlerRequest,
            PeerHandlerRequestStream,
        },
        fuchsia_async as fasync,
        matches::assert_matches,
    };

    #[test]
    fn call_is_active() {
        // executor must be created before fidl endpoints can be created
        let _exec = fasync::TestExecutor::new().unwrap();
        let (proxy, _) = fidl::endpoints::create_proxy::<CallMarker>().unwrap();

        let mut call = CallEntry::new(proxy, "1".into(), CallState::IncomingRinging);

        assert!(!call.is_active());
        call.set_state(CallState::OngoingActive);
        assert!(call.is_active());
        call.set_state(CallState::Terminated);
        assert!(!call.is_active());
    }

    /// The most common test setup includes an initialized Calls instance and an ongoing call.
    /// This helper function sets up a `Calls` instance in that state and returns the associated
    /// endpoints.
    fn setup_ongoing_call() -> (Calls, PeerHandlerRequestStream, CallRequestStream, CallIdx, Number)
    {
        let (proxy, peer_stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();
        let mut calls = Calls::new(Some(proxy));
        let (client_end, call_stream) = fidl::endpoints::create_request_stream().unwrap();
        let num = Number::from("1");
        calls.handle_new_call(client_end, num.clone(), CallState::IncomingRinging);
        let expected = CallIndicators {
            call: types::Call::None,
            callsetup: types::CallSetup::Incoming,
            callwaiting: false,
            callheld: types::CallHeld::None,
        };
        assert_eq!(calls.indicators(), expected);
        (calls, peer_stream, call_stream, 1, num)
    }

    #[test]
    fn calls_should_ring_succeeds() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (mut calls, _peer_handler, mut call_stream, _idx, _num) = setup_ongoing_call();
        assert!(calls.should_ring());

        assert_calls_indicators(&mut exec, &mut calls);

        update_call(&mut exec, &mut call_stream, CallState::OngoingActive);
        assert_calls_indicators(&mut exec, &mut calls);

        // Call is no longer ringing after call state has changed
        assert!(!calls.should_ring());
    }

    #[fasync::run_until_stalled(test)]
    async fn call_requests_send_requests_to_server() {
        let (mut calls, _peer_handler, mut call_stream, idx, _num) = setup_ongoing_call();

        calls.request_hold(idx).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestHold { .. })));

        // Make a second call that is active
        let (client_end, mut call_stream_2) = fidl::endpoints::create_request_stream().unwrap();
        let num = "2".into();
        let _ = calls.handle_new_call(client_end, num, CallState::OngoingActive);

        // Sending a RequestActive for the first call will send a RequestTerminate for the second
        // call when `true` is passed to request_active.
        calls.request_active(idx, true).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestActive { .. })));
        assert_matches!(call_stream_2.next().await, Some(Ok(CallRequest::RequestTerminate { .. })));

        // Place the first call back on hold, so that we can activate it again.
        calls.request_hold(idx).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestHold { .. })));

        // Make a third call that is active
        let (client_end, mut call_stream_3) = fidl::endpoints::create_request_stream().unwrap();
        let num = "3".into();
        let _ = calls.handle_new_call(client_end, num, CallState::OngoingActive);

        // Sending a RequestActive for the first call will send a RequestHold for the third
        // call when `false` is passed to request_active.
        calls.request_active(idx, false).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestActive { .. })));
        assert_matches!(call_stream_3.next().await, Some(Ok(CallRequest::RequestHold { .. })));

        calls.request_terminate(idx).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestTerminate { .. })));

        calls.request_transfer_audio(idx).expect("valid index");
        assert_matches!(
            call_stream.next().await,
            Some(Ok(CallRequest::RequestTransferAudio { .. }))
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn call_requests_invalid_index_return_error() {
        let (mut calls, _peer_handler, _call_stream, _idx, _num) = setup_ongoing_call();

        let invalid = 2;
        let result = calls.request_hold(invalid);
        assert_eq!(result, Err(CallError::UnknownIndexError(invalid.clone())));

        let result = calls.request_active(invalid, false);
        assert_eq!(result, Err(CallError::UnknownIndexError(invalid.clone())));

        let result = calls.request_terminate(invalid);
        assert_eq!(result, Err(CallError::UnknownIndexError(invalid.clone())));

        let result = calls.request_transfer_audio(invalid);
        assert_eq!(result, Err(CallError::UnknownIndexError(invalid.clone())));
    }

    #[fasync::run_until_stalled(test)]
    async fn call_requests_manager_closed_clears_call() {
        let (mut calls, _peer_handler, call_stream, idx, num) = setup_ongoing_call();
        drop(call_stream);

        let call =
            calls.current_calls().into_iter().find(|info| info.index == idx && info.number == num);
        assert!(call.is_some(), "Call must exist in list of calls");

        // A request made to a Call channel that is closed will remove the call entry.
        let result = calls.request_hold(idx);
        assert_eq!(result, Ok(()));

        let call =
            calls.current_calls().into_iter().find(|info| info.index == idx && info.number == num);
        assert!(call.is_none(), "Call must not exist in list of calls");
    }

    /// Make a new call, manually driving async execution.
    #[track_caller]
    fn new_call(
        exec: &mut fasync::TestExecutor,
        stream: &mut PeerHandlerRequestStream,
        num: &str,
        state: CallState,
    ) -> CallRequestStream {
        // Get WatchNextCall request.
        let responder = match exec.run_until_stalled(&mut stream.next()) {
            Poll::Ready(Some(Ok(PeerHandlerRequest::WatchNextCall { responder, .. }))) => responder,
            result => panic!("Unexpected result: {:?}", result),
        };
        // Respond with a call.
        let (client, call) = fidl::endpoints::create_request_stream::<CallMarker>().unwrap();
        responder.send(client, num, state).expect("response to succeed");
        call
    }

    /// Update call state, manually driving async execution.
    /// Expects a watchstate call to be the next pending item on `stream`.
    #[track_caller]
    fn update_call(
        exec: &mut fasync::TestExecutor,
        stream: &mut CallRequestStream,
        state: CallState,
    ) {
        // Get WatchState request for call
        let responder = match exec.run_until_stalled(&mut stream.next()) {
            Poll::Ready(Some(Ok(CallRequest::WatchState { responder, .. }))) => responder,
            result => panic!("Unexpected result: {:?}", result),
        };
        // Respond with a call state.
        responder.send(state).expect("response to succeed");
    }

    /// Assert the Calls stream is pending, manually driving async execution.
    #[track_caller]
    fn assert_calls_pending(exec: &mut fasync::TestExecutor, calls: &mut Calls) {
        let result = exec.run_until_stalled(&mut calls.next());
        assert!(result.is_pending());
    }

    /// Return the next item from the Calls stream, manually driving async execution.
    /// Panics if the stream does not produce some item.
    #[track_caller]
    fn assert_calls_indicators(
        exec: &mut fasync::TestExecutor,
        calls: &mut Calls,
    ) -> CallIndicatorsUpdates {
        let result = exec.run_until_stalled(&mut calls.next());
        match result {
            Poll::Ready(Some(ind)) => ind,
            result => panic!("Unexpected result: {:?}", result),
        }
    }

    /// Poll the calls  driving async execution, until there are no more updates and and we receive
    /// Pending.  Returns the most recent indicators if any were sent.
    #[track_caller]
    fn poll_calls_until_pending(
        exec: &mut fasync::TestExecutor,
        calls: &mut Calls,
    ) -> Option<CallIndicatorsUpdates> {
        let mut indicators = None;
        loop {
            match exec.run_until_stalled(&mut calls.next()) {
                Poll::Ready(Some(ind)) => indicators = Some(ind),
                Poll::Pending => return indicators,
                Poll::Ready(None) => panic!("Calls terminated before expected!"),
            }
        }
    }

    #[test]
    fn calls_is_call_active() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (proxy, mut peer_stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();
        let mut calls = Calls::new(Some(proxy));

        // No active call when there are no calls.
        assert!(!calls.is_call_active());

        poll_calls_until_pending(&mut exec, &mut calls);
        let mut call_stream =
            new_call(&mut exec, &mut peer_stream, "1", CallState::IncomingRinging);
        poll_calls_until_pending(&mut exec, &mut calls);

        // When we have an ringing call, we are still not active yet.

        assert!(!calls.is_call_active());

        poll_calls_until_pending(&mut exec, &mut calls);
        update_call(&mut exec, &mut call_stream, CallState::OngoingActive);
        poll_calls_until_pending(&mut exec, &mut calls);

        assert!(calls.is_call_active());

        poll_calls_until_pending(&mut exec, &mut calls);
        update_call(&mut exec, &mut call_stream, CallState::OngoingHeld);
        poll_calls_until_pending(&mut exec, &mut calls);

        assert!(!calls.is_call_active());
    }

    #[test]
    fn calls_stream_lifecycle() {
        // Test the Stream for items when a single call is tracked, then a second call is added,
        // when the states of those calls are modified, and finally, when both calls have been
        // removed from the stream.

        let mut exec = fasync::TestExecutor::new().unwrap();

        let (mut calls, mut handler_stream, mut call_1, _idx_1, _num_1) = setup_ongoing_call();

        let item = assert_calls_indicators(&mut exec, &mut calls);
        let expected = CallIndicatorsUpdates {
            callsetup: Some(types::CallSetup::Incoming),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(item, expected);

        // Stream doesn't have an item ready.
        assert_calls_pending(&mut exec, &mut calls);

        let mut call_2 = new_call(&mut exec, &mut handler_stream, "2", CallState::OutgoingAlerting);

        // There are no new calls in this test so close handler stream.
        drop(handler_stream);

        // Stream has an item ready.
        let item = assert_calls_indicators(&mut exec, &mut calls);
        // The ready item is OutgoingAlerting even though there is also an IncomingRinging call.
        // This is because the OutgoingAlerting call state was reported last.
        let expected = CallIndicatorsUpdates {
            callsetup: Some(types::CallSetup::OutgoingAlerting),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(item, expected);

        // Stream doesn't have an item ready.
        assert_calls_pending(&mut exec, &mut calls);

        update_call(&mut exec, &mut call_1, CallState::OngoingActive);

        // Stream has an item ready.
        let item = assert_calls_indicators(&mut exec, &mut calls);
        // Only indicators that have changed are returned.
        let expected = CallIndicatorsUpdates {
            call: Some(types::Call::Some),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(item, expected);

        drop(call_1);

        update_call(&mut exec, &mut call_2, CallState::OngoingHeld);

        // Stream has an item ready.
        let item = assert_calls_indicators(&mut exec, &mut calls);
        let expected = CallIndicatorsUpdates {
            callsetup: Some(types::CallSetup::None),
            callheld: Some(types::CallHeld::Held),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(item, expected);

        drop(call_2);

        // Stream has an item ready.
        let item = assert_calls_indicators(&mut exec, &mut calls);
        let expected = CallIndicatorsUpdates {
            call: Some(types::Call::None),
            callheld: Some(types::CallHeld::None),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(item, expected);

        assert!(!calls.is_terminated());

        // Stream is complete.
        let result = exec.run_until_stalled(&mut calls.next());
        assert_matches!(result, Poll::Ready(None));
        assert!(calls.is_terminated());
    }
}
