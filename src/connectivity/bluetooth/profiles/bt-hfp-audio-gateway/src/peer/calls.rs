// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        procedure::dtmf::DtmfCode,
        protocol::indicators::{CallIndicators, CallIndicatorsUpdates},
    },
    async_utils::{
        hanging_get::client::HangingGetStream,
        stream::{StreamItem, StreamMap, StreamWithEpitaph, Tagged, WithEpitaph, WithTag},
    },
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_bluetooth_hfp::{CallMarker, CallProxy, CallState, PeerHandlerProxy},
    fuchsia_async as fasync,
    futures::stream::{FusedStream, Stream, StreamExt},
    log::{debug, info, warn},
    std::{
        pin::Pin,
        task::{Context, Poll},
    },
};

pub use number::Number;

// Enclose `Number` in a submodule to keep the private items from leaking into the `calls` module.
mod number {
    use super::FidlNumber;

    /// A phone number.
    #[derive(Debug, Clone, PartialEq, Hash, Default, Eq)]
    pub struct Number(String);

    impl Number {
        /// Format value indicating no changes on the number presentation are required.
        /// See HFP v1.8, Section 4.34.2.
        const NUMBER_FORMAT: i64 = 129;

        /// Returns the numeric representation of the Number's format as specified in HFP v1.8,
        /// Section 4.34.2.
        pub fn type_(&self) -> i64 {
            Number::NUMBER_FORMAT
        }
    }

    impl From<Number> for String {
        fn from(x: Number) -> Self {
            x.0
        }
    }

    impl From<&str> for Number {
        fn from(n: &str) -> Self {
            // Phone numbers must be enclosed in double quotes
            let inner = if n.starts_with("\"") && n.ends_with("\"") {
                n.to_string()
            } else {
                format!("\"{}\"", n)
            };
            Self(inner)
        }
    }

    impl From<FidlNumber> for Number {
        fn from(n: FidlNumber) -> Self {
            n.as_str().into()
        }
    }
}

/// The index associated with a call, that is guaranteed to be unique for the lifetime of the call,
/// but will be recycled after the call is released.
pub type CallIdx = usize;

/// A request was made using an unknown call index.
#[derive(Debug, PartialEq)]
pub struct UnknownIndexError(CallIdx);

/// The direction of call initiation.
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum Direction {
    /// Call Direction is not known at this time.
    Unknown,
    /// Call originated on this device. This is also known as an Outgoing call.
    MobileOriginated,
    /// Call is terminated on this device. This is also known as an Incoming call.
    MobileTerminated,
}

impl From<CallState> for Direction {
    fn from(x: CallState) -> Self {
        match x {
            CallState::IncomingRinging | CallState::IncomingWaiting => Self::MobileTerminated,
            CallState::OutgoingDialing | CallState::OutgoingAlerting => Self::MobileOriginated,
            _ => Self::Unknown,
        }
    }
}

impl From<Direction> for i64 {
    fn from(x: Direction) -> Self {
        match x {
            // When we do not know the direction, arbitrarily choose Mobile Originated.
            // TODO (fxbug.dev/73326): Update the FIDL API so that the direction is always provided.
            Direction::Unknown => 0,
            Direction::MobileOriginated => 0,
            Direction::MobileTerminated => 1,
        }
    }
}

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

/// The fuchsia.bluetooth.hfp library representation of a Number.
type FidlNumber = String;

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
        let new_calls =
            proxy.map(|proxy| HangingGetStream::new(Box::new(move || Some(proxy.wait_for_call()))));
        Self {
            new_calls,
            current_calls: CallList::default(),
            call_updates: CallStateUpdates::empty(),
            reported_indicators: CallIndicators::default(),
            terminated: false,
        }
    }

    /// Insert a new call
    fn handle_new_call(&mut self, call: ClientEnd<CallMarker>, number: Number, state: CallState) {
        let proxy = call.into_proxy().unwrap();
        let call = CallEntry::new(proxy.clone(), number.clone(), state);
        let index = self.current_calls.insert(call);
        let call_state = HangingGetStream::new(Box::new(move || Some(proxy.watch_state())));
        self.call_updates.insert(index, call_state.tagged(index).with_epitaph(index));
        self.terminated = false;
    }

    /// Check and mark this stream "terminated" if the appropriate conditions are met.
    /// Specifically, Calls is terminated if both `new_calls` and `call_updates` cannot produce any
    /// new values.
    fn check_termination_condition(&mut self) {
        self.terminated = self.new_calls.is_none() && self.call_updates.inner().is_empty();
    }

    /// Run `f`, passing the CallProxy associated with the given `number` into `f` and removing
    /// the CallProxy from the map if an error is returned by running `f`.
    fn _send_call_request(
        &mut self,
        index: CallIdx,
        f: impl FnOnce(&CallProxy) -> Result<(), fidl::Error>,
    ) -> Result<(), UnknownIndexError> {
        let result = f(&self.current_calls.get(index).ok_or(UnknownIndexError(index))?.proxy);
        if let Err(e) = result {
            if !e.is_closed() {
                warn!("Error making request on Call channel for call {:?}: {}", index, e);
            }
            self.remove_call(index);
        }
        Ok(())
    }

    /// Send a request to the call manager to place the call on hold.
    pub fn _request_hold(&mut self, index: CallIdx) -> Result<(), UnknownIndexError> {
        self._send_call_request(index, |proxy| proxy.request_hold())
    }

    /// Send a request to the call manager to make the call active.
    /// Only one call can be active at a time. Before requesting the call at `index` be made
    /// active, all active calls are either terminated or placed on hold depending on
    /// `terminate_others`.
    pub fn _request_active(
        &mut self,
        index: CallIdx,
        terminate_others: bool,
    ) -> Result<(), UnknownIndexError> {
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
            let _ = self._send_call_request(i, action);
        }
        self._send_call_request(index, |proxy| proxy.request_active())
    }

    /// Send a request to the call manager to terminate the call.
    pub fn _request_terminate(&mut self, index: CallIdx) -> Result<(), UnknownIndexError> {
        self._send_call_request(index, |proxy| proxy.request_terminate())
    }

    /// Send a request to the call manager to transfer the audio of the call from the
    /// headset to the fuchsia device.
    pub fn _request_transfer_audio(&mut self, index: CallIdx) -> Result<(), UnknownIndexError> {
        self._send_call_request(index, |proxy| proxy.request_transfer_audio())
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

    /// Remove all references to the call assigned to `index`.
    fn remove_call(&mut self, index: CallIdx) {
        self.call_updates.remove(&index);
        self.current_calls.remove(index);
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

/// A collection designed for the specific requirements of storing Calls with an associated index.
///
/// The requirements found in HFP v1.8, Section 4.34.2, "+CLCC":
///
///   * Each call is assigned a number starting at 1.
///   * Calls hold their number until they are released.
///   * New calls take the lowest available number.
///
/// Note: "Insert" is a O(n) operation in order to simplify the implementation.
/// This data structure is best suited towards small n for this reason.
struct CallList<T> {
    inner: Vec<Option<T>>,
}

impl<T> Default for CallList<T> {
    fn default() -> Self {
        Self { inner: Vec::default() }
    }
}

impl<T> CallList<T> {
    /// Insert a new value into the list, returning an index that is guaranteed to be unique until
    /// the value is removed from the list.
    fn insert(&mut self, value: T) -> CallIdx {
        let index = if let Some(index) = self.inner.iter_mut().position(|v| v.is_none()) {
            self.inner[index] = Some(value);
            index
        } else {
            self.inner.push(Some(value));
            self.inner.len() - 1
        };

        Self::to_call_index(index)
    }

    /// Retrieve a value by index. Returns `None` if the index does not point to a value.
    // TODO (fxb/64550): Remove when call requests are initiated
    #[allow(unused)]
    fn get(&self, index: CallIdx) -> Option<&T> {
        match Self::to_internal_index(index) {
            Some(index) => self.inner.get(index).map(|v| v.as_ref()).unwrap_or(None),
            None => None,
        }
    }

    /// Retrieve a mutable reference to a value by index. Returns `None` if the index does not point
    /// to a value.
    fn get_mut(&mut self, index: CallIdx) -> Option<&mut T> {
        match Self::to_internal_index(index) {
            Some(index) => self.inner.get_mut(index).map(|v| v.as_mut()).unwrap_or(None),
            None => None,
        }
    }

    /// Remove a value by index. Returns `None` if the value did not point to a value.
    fn remove(&mut self, index: CallIdx) -> Option<T> {
        match Self::to_internal_index(index) {
            Some(index) => self.inner.get_mut(index).map(|v| v.take()).unwrap_or(None),
            None => None,
        }
    }

    /// Return an iterator of the calls and associated call indices.
    fn calls(&self) -> impl Iterator<Item = (CallIdx, &T)> + Clone {
        self.inner
            .iter()
            .enumerate()
            .flat_map(|(i, entry)| entry.as_ref().map(|v| (Self::to_call_index(i), v)))
    }

    /// Convert a `CallIdx` to the internal index used to locate a call.
    ///
    /// The CallIdx for a call starts at 1 instead of 0, so the internal index must be decremented
    /// after being received by the user.
    ///
    /// Returns `None` if `index` is 0 because 0 is an invalid index.
    fn to_internal_index(index: CallIdx) -> Option<usize> {
        (index != 0).then(|| index - 1)
    }

    /// Convert the internal index for a call to the external `CallIdx`.
    /// The CallIdx for a call starts at 1 instead of 0, so the internal index must be incremented
    /// before being returned to the user.
    fn to_call_index(internal: usize) -> CallIdx {
        internal + 1
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::protocol::indicators,
        fidl_fuchsia_bluetooth_hfp::{
            CallRequest, CallRequestStream, PeerHandlerMarker, PeerHandlerRequest,
            PeerHandlerRequestStream,
        },
        fuchsia_async as fasync,
        matches::assert_matches,
    };

    #[test]
    fn number_type_in_valid_range() {
        let number = Number::from("1234567");
        // type values must be in range 128-175.
        assert!(number.type_() >= 128);
        assert!(number.type_() <= 175);
    }

    #[test]
    fn number_str_roundtrip() {
        let number = Number::from("1234567");
        assert_eq!(number.clone(), Number::from(&*String::from(number)));
    }

    #[test]
    fn call_is_active() {
        // executor must be created before fidl endpoints can be created
        let _exec = fasync::Executor::new().unwrap();
        let (proxy, _) = fidl::endpoints::create_proxy::<CallMarker>().unwrap();

        let mut call = CallEntry::new(proxy, "1".into(), CallState::IncomingRinging);

        assert!(!call.is_active());
        call.set_state(CallState::OngoingActive);
        assert!(call.is_active());
        call.set_state(CallState::Terminated);
        assert!(!call.is_active());
    }

    #[test]
    fn call_list_insert() {
        let mut list = CallList::default();
        let i1 = list.insert(1);
        assert_eq!(i1, 1, "The first value must be assigned the number 1");
        let i2 = list.insert(2);
        assert_eq!(i2, 2, "The second value is assigned the next available number");
    }

    #[test]
    fn call_list_get() {
        let mut list = CallList::default();
        let i1 = list.insert(1);
        let i2 = list.insert(2);
        assert_eq!(list.get(0), None);
        assert_eq!(list.get(i1), Some(&1));
        assert_eq!(list.get(i2), Some(&2));
        assert_eq!(list.get(3), None);
    }

    #[test]
    fn call_list_get_mut() {
        let mut list = CallList::default();
        let i1 = list.insert(1);
        let i2 = list.insert(2);
        assert_eq!(list.get_mut(i1), Some(&mut 1));
        assert_eq!(list.get_mut(i2), Some(&mut 2));
        assert_eq!(list.get_mut(3), None);
    }

    #[test]
    fn call_list_remove() {
        let mut list = CallList::default();
        let i1 = list.insert(1);
        let i2 = list.insert(2);
        let removed = list.remove(i1);
        assert!(removed.is_some());
        assert_eq!(list.get(i1), None, "The value at i1 is removed");
        assert_eq!(list.get(i2), Some(&2), "The value at i2 is untouched");
        let invalid_idx = 0;
        assert!(list.remove(invalid_idx).is_none());
    }

    #[test]
    fn call_list_remove_and_insert_behaves() {
        let mut list = CallList::default();
        let i1 = list.insert(1);
        let i2 = list.insert(2);
        let i3 = list.insert(3);
        let i4 = list.insert(4);
        list.remove(i2);
        list.remove(i1);
        list.remove(i3);
        let i5 = list.insert(5);
        assert_eq!(i5, i1, "i5 is the lowest possible index (i1) even though i1 was not the first or last index removed");
        assert_eq!(list.get(i5), Some(&5), "The value at i5 is correct");
        let i6 = list.insert(6);
        let i7 = list.insert(7);
        assert_eq!(i6, i2, "i6 takes the next available index (i2)");
        assert_eq!(i7, i3, "i7 takes the next available index (i3)");
        let i8_ = list.insert(8);
        assert_ne!(i8_, i4, "i4 is reserved, so i8_ must take a new index");
        assert_eq!(
            i8_, 5,
            "i8_ takes an index of 5 since it is the last of the 5 values to be inserted"
        );
    }

    #[test]
    fn call_list_iter_returns_all_valid_values() {
        let mut list = CallList::default();
        let i1 = list.insert(1);
        let i2 = list.insert(2);
        let i3 = list.insert(3);
        let i4 = list.insert(4);
        list.remove(i2);
        let actual: Vec<_> = list.calls().collect();
        let expected = vec![(i1, &1), (i3, &3), (i4, &4)];
        assert_eq!(actual, expected);
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
            call: indicators::Call::None,
            callsetup: indicators::CallSetup::Incoming,
            callheld: indicators::CallHeld::None,
        };
        assert_eq!(calls.indicators(), expected);
        (calls, peer_stream, call_stream, 1, num)
    }

    #[test]
    fn calls_should_ring_succeeds() {
        let mut exec = fasync::Executor::new().unwrap();

        let (mut calls, _peer_handler, mut call_stream, _idx, _num) = setup_ongoing_call();
        assert!(calls.should_ring());

        poll_next_item(&mut exec, &mut calls);

        update_call(&mut exec, &mut call_stream, CallState::OngoingActive);
        poll_next_item(&mut exec, &mut calls);

        // Call is no longer ringing after call state has changed
        assert!(!calls.should_ring());
    }

    #[fasync::run_until_stalled(test)]
    async fn call_requests_send_requests_to_server() {
        let (mut calls, _peer_handler, mut call_stream, idx, _num) = setup_ongoing_call();

        calls._request_hold(idx).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestHold { .. })));

        // Make a second call that is active
        let (client_end, mut call_stream_2) = fidl::endpoints::create_request_stream().unwrap();
        let num = "2".into();
        let _ = calls.handle_new_call(client_end, num, CallState::OngoingActive);

        // Sending a RequestActive for the first call will send a RequestTerminate for the second
        // call when `true` is passed to request_active.
        calls._request_active(idx, true).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestActive { .. })));
        assert_matches!(call_stream_2.next().await, Some(Ok(CallRequest::RequestTerminate { .. })));

        // Place the first call back on hold, so that we can activate it again.
        calls._request_hold(idx).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestHold { .. })));

        // Make a third call that is active
        let (client_end, mut call_stream_3) = fidl::endpoints::create_request_stream().unwrap();
        let num = "3".into();
        let _ = calls.handle_new_call(client_end, num, CallState::OngoingActive);

        // Sending a RequestActive for the first call will send a RequestHold for the third
        // call when `false` is passed to request_active.
        calls._request_active(idx, false).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestActive { .. })));
        assert_matches!(call_stream_3.next().await, Some(Ok(CallRequest::RequestHold { .. })));

        calls._request_terminate(idx).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestTerminate { .. })));

        calls._request_transfer_audio(idx).expect("valid index");
        assert_matches!(
            call_stream.next().await,
            Some(Ok(CallRequest::RequestTransferAudio { .. }))
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn call_requests_invalid_index_return_error() {
        let (mut calls, _peer_handler, _call_stream, _idx, _num) = setup_ongoing_call();

        let invalid = 2;
        let result = calls._request_hold(invalid);
        assert_eq!(result, Err(UnknownIndexError(invalid.clone())));

        let result = calls._request_active(invalid, false);
        assert_eq!(result, Err(UnknownIndexError(invalid.clone())));

        let result = calls._request_terminate(invalid);
        assert_eq!(result, Err(UnknownIndexError(invalid.clone())));

        let result = calls._request_transfer_audio(invalid);
        assert_eq!(result, Err(UnknownIndexError(invalid.clone())));
    }

    #[fasync::run_until_stalled(test)]
    async fn call_requests_manager_closed_clears_call() {
        let (mut calls, _peer_handler, call_stream, idx, num) = setup_ongoing_call();
        drop(call_stream);

        let call =
            calls.current_calls().into_iter().find(|info| info.index == idx && info.number == num);
        assert!(call.is_some(), "Call must exist in list of calls");

        // A request made to a Call channel that is closed will remove the call entry.
        let result = calls._request_hold(idx);
        assert_eq!(result, Ok(()));

        let call =
            calls.current_calls().into_iter().find(|info| info.index == idx && info.number == num);
        assert!(call.is_none(), "Call must not exist in list of calls");
    }

    /// Make a new call, manually driving async execution.
    #[track_caller]
    fn new_call(
        exec: &mut fasync::Executor,
        stream: &mut PeerHandlerRequestStream,
        num: &str,
        state: CallState,
    ) -> CallRequestStream {
        // Get WaitForCall request.
        let responder = match exec.run_until_stalled(&mut stream.next()) {
            Poll::Ready(Some(Ok(PeerHandlerRequest::WaitForCall { responder, .. }))) => responder,
            result => panic!("Unexpected result: {:?}", result),
        };
        // Respond with a call.
        let (client, call) = fidl::endpoints::create_request_stream::<CallMarker>().unwrap();
        responder.send(client, num, state).expect("response to succeed");
        call
    }

    /// Update call state, manually driving async execution.
    #[track_caller]
    fn update_call(exec: &mut fasync::Executor, stream: &mut CallRequestStream, state: CallState) {
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
    fn assert_poll_pending(exec: &mut fasync::Executor, calls: &mut Calls) {
        let result = exec.run_until_stalled(&mut calls.next());
        assert!(result.is_pending());
    }

    /// Return the next item from the Calls stream, manually driving async execution.
    /// Panics if the stream does not produce some item.
    #[track_caller]
    fn poll_next_item(exec: &mut fasync::Executor, calls: &mut Calls) -> CallIndicatorsUpdates {
        let result = exec.run_until_stalled(&mut calls.next());
        match result {
            Poll::Ready(Some(ind)) => ind,
            result => panic!("Unexpected result: {:?}", result),
        }
    }

    #[test]
    fn calls_stream_lifecycle() {
        // Test the Stream for items when a single call is tracked, then a second call is added,
        // when the states of those calls are modified, and finally, when both calls have been
        // removed from the stream.

        let mut exec = fasync::Executor::new().unwrap();

        let (mut calls, mut handler_stream, mut call_1, _idx_1, _num_1) = setup_ongoing_call();

        let item = poll_next_item(&mut exec, &mut calls);
        let expected = CallIndicatorsUpdates {
            callsetup: Some(indicators::CallSetup::Incoming),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(item, expected);

        // Stream doesn't have an item ready.
        assert_poll_pending(&mut exec, &mut calls);

        let mut call_2 = new_call(&mut exec, &mut handler_stream, "2", CallState::OutgoingAlerting);

        // There are no new calls in this test so close handler stream.
        drop(handler_stream);

        // Stream has an item ready.
        let item = poll_next_item(&mut exec, &mut calls);
        // The ready item is OutgoingAlerting even though there is also an IncomingRinging call.
        // This is because the OutgoingAlerting call state was reported last.
        let expected = CallIndicatorsUpdates {
            callsetup: Some(indicators::CallSetup::OutgoingAlerting),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(item, expected);

        // Stream doesn't have an item ready.
        assert_poll_pending(&mut exec, &mut calls);

        update_call(&mut exec, &mut call_1, CallState::OngoingActive);

        // Stream has an item ready.
        let item = poll_next_item(&mut exec, &mut calls);
        // Only indicators that have changed are returned.
        let expected = CallIndicatorsUpdates {
            call: Some(indicators::Call::Some),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(item, expected);

        drop(call_1);

        update_call(&mut exec, &mut call_2, CallState::OngoingHeld);

        // Stream has an item ready.
        let item = poll_next_item(&mut exec, &mut calls);
        let expected = CallIndicatorsUpdates {
            callsetup: Some(indicators::CallSetup::None),
            callheld: Some(indicators::CallHeld::Held),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(item, expected);

        drop(call_2);

        // Stream has an item ready.
        let item = poll_next_item(&mut exec, &mut calls);
        let expected = CallIndicatorsUpdates {
            call: Some(indicators::Call::None),
            callheld: Some(indicators::CallHeld::None),
            ..CallIndicatorsUpdates::default()
        };
        assert_eq!(item, expected);

        assert!(!calls.is_terminated());

        // Stream doesn't have an item ready.
        let result = exec.run_until_stalled(&mut calls.next());
        assert_matches!(result, Poll::Ready(None));
        assert!(calls.is_terminated());
    }
}
