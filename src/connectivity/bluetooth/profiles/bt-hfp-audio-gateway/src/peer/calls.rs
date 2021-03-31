// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::procedure::dtmf::DtmfCode,
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

/// A phone number.
#[derive(Debug, Clone, PartialEq, Hash, Default, Eq)]
pub struct Number(String);

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

/// Internal state and resources associated with a single call.
#[allow(unused)] // TODO: Remove in fxrev.dev/497389
struct CallEntry {
    /// Proxy associated with this call.
    proxy: CallProxy,
    /// The remote party's number.
    number: Number,
    /// Current state.
    state: CallState,
    /// Time of the last update to the call's `state`.
    state_updated_at: fasync::Time,
    /// Direction of the call.
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

/// Manages the state of all ongoing calls reported by the call manager. Provides updates on the
/// states and relays requests made by the Hands Free to act on calls.
pub(crate) struct Calls {
    new_calls: Option<HangingGetStream<(ClientEnd<CallMarker>, String, CallState)>>,
    current_calls: CallList<CallEntry>,
    call_updates: CallStateUpdates,
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
    pub fn _request_active(&mut self, index: CallIdx) -> Result<(), UnknownIndexError> {
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
    #[allow(unused)] // TODO: Remove in fxrev.dev/497389
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

    /// Helper function to poll the new_calls hanging get for the next available new call.
    /// Sets up a call_state hanging get when a new call is available.
    ///
    /// Returns an Item if one is ready.
    fn poll_next_new_calls(&mut self, cx: &mut Context<'_>) -> Option<<Self as Stream>::Item> {
        // First check for new calls from the call service.
        // Returns early if there is an item ready for output.
        if let Some(new_calls) = &mut self.new_calls {
            match new_calls.poll_next_unpin(cx) {
                Poll::Ready(Some(Ok((call, number_str, state)))) => {
                    let number = Number(number_str);
                    self.handle_new_call(call, number.clone(), state);
                    return Some((number, state));
                }
                Poll::Ready(Some(Err(_e))) => self.new_calls = None,
                Poll::Ready(None) => self.new_calls = None,
                Poll::Pending => (),
            }
        }
        None
    }

    /// Helper function to poll the call_updates collection for call state changes.
    fn poll_next_call_updates(&mut self, cx: &mut Context<'_>) -> Option<<Self as Stream>::Item> {
        match self.call_updates.poll_next_unpin(cx) {
            Poll::Ready(Some(item)) => match item {
                StreamItem::Item((index, Ok(state))) => {
                    if let Some(call) = self.current_calls.get_mut(index) {
                        call.set_state(state);
                        return Some((call.number.clone(), state));
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
            Poll::Ready(None) | Poll::Pending => (),
        }
        None
    }
}

impl Unpin for Calls {}

impl Stream for Calls {
    type Item = (Number, CallState);

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.terminated {
            panic!("cannot poll terminated");
        }

        if let Some(item) = self.poll_next_new_calls(cx) {
            return Poll::Ready(Some(item));
        }

        let result = self.poll_next_call_updates(cx);

        self.check_termination_condition();

        if self.terminated {
            return Poll::Ready(None);
        } else {
            result.map(|item| Poll::Ready(Some(item))).unwrap_or(Poll::Pending)
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
    #[allow(unused)] // TODO: Remove in fxrev.dev/497389
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
    #[allow(unused)] // TODO: Remove in fxrev.dev/497389
    fn calls(&self) -> impl Iterator<Item = (CallIdx, &T)> {
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
        fidl_fuchsia_bluetooth_hfp::{
            CallRequest, CallRequestStream, PeerHandlerMarker, PeerHandlerRequest,
            PeerHandlerRequestStream,
        },
        fuchsia_async as fasync,
        matches::assert_matches,
    };

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
        let num = Number("1".into());
        calls.handle_new_call(client_end, num.clone(), CallState::IncomingRinging);
        (calls, peer_stream, call_stream, 1, num)
    }

    #[fasync::run_until_stalled(test)]
    async fn call_requests_send_requests_to_server() {
        let (mut calls, _peer_handler, mut call_stream, idx, _num) = setup_ongoing_call();

        calls._request_hold(idx).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestHold { .. })));

        calls._request_active(idx).expect("valid index");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestActive { .. })));

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

        let result = calls._request_active(invalid);
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

    #[test]
    fn calls_stream_lifecycle() {
        let mut exec = fasync::Executor::new().unwrap();

        let (mut calls, mut handler_stream, mut call_1, idx_1, num_1) = setup_ongoing_call();
        let num_2 = Number("2".into());

        // Stream doesn't have an item ready.
        let result = exec.run_until_stalled(&mut calls.next());
        assert!(result.is_pending());

        // Get WaitForCall request.
        let responder = match exec.run_until_stalled(&mut handler_stream.next()) {
            Poll::Ready(Some(Ok(PeerHandlerRequest::WaitForCall { responder, .. }))) => responder,
            result => panic!("Unexpected result: {:?}", result),
        };
        // Respond with a call.
        let (client, mut call_2) = fidl::endpoints::create_request_stream::<CallMarker>().unwrap();
        responder.send(client, "2", CallState::IncomingRinging).expect("response to succeed");

        // There are no new calls in this test so close handler stream.
        drop(handler_stream);

        // Stream has an item ready.
        let result = exec.run_until_stalled(&mut calls.next());
        let info = match result {
            Poll::Ready(Some(info)) => info,
            result => panic!("Unexpected result: {:?}", result),
        };
        let expected =
            Call::new(2, num_2.clone(), CallState::IncomingRinging, Direction::MobileTerminated);
        assert_eq!(info.0, expected.number);
        assert_eq!(info.1, expected.state);

        assert!(!calls.is_terminated());

        // Stream doesn't have an item ready.
        let result = exec.run_until_stalled(&mut calls.next());
        assert!(result.is_pending());

        // Get WatchState request for call 1.
        let responder = match exec.run_until_stalled(&mut call_1.next()) {
            Poll::Ready(Some(Ok(CallRequest::WatchState { responder, .. }))) => responder,
            result => panic!("Unexpected result: {:?}", result),
        };
        // Respond with a call state.
        responder.send(CallState::OngoingActive).expect("response to succeed");

        // Stream has an item ready.
        let result = exec.run_until_stalled(&mut calls.next());
        let info = match result {
            Poll::Ready(Some(info)) => info,
            result => panic!("Unexpected result: {:?}", result),
        };

        let expected =
            Call::new(idx_1, num_1, CallState::OngoingActive, Direction::MobileTerminated);
        assert_eq!(info.0, expected.number);
        assert_eq!(info.1, expected.state);

        drop(call_1);

        // Stream doesn't have an item ready.
        let result = exec.run_until_stalled(&mut calls.next());
        assert!(result.is_pending());
        assert!(!calls.is_terminated());

        // Get WatchState request for call 2.
        let responder = match exec.run_until_stalled(&mut call_2.next()) {
            Poll::Ready(Some(Ok(CallRequest::WatchState { responder, .. }))) => responder,
            result => panic!("Unexpected result: {:?}", result),
        };
        // Respond with a call state.
        responder.send(CallState::OngoingHeld).expect("response to succeed");

        // Stream has an item ready.
        let result = exec.run_until_stalled(&mut calls.next());
        let info = match result {
            Poll::Ready(Some(info)) => info,
            result => panic!("Unexpected result: {:?}", result),
        };

        let expected =
            Call::new(2, num_2.clone(), CallState::OngoingHeld, Direction::MobileTerminated);
        assert_eq!(info.0, expected.number);
        assert_eq!(info.1, expected.state);

        drop(call_2);

        // Stream doesn't have an item ready.
        let result = exec.run_until_stalled(&mut calls.next());
        assert_matches!(result, Poll::Ready(None));
        assert!(calls.is_terminated());
    }
}
