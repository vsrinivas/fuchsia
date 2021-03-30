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
    futures::stream::{FusedStream, Stream, StreamExt},
    log::warn,
    std::{
        collections::HashMap,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// A phone number.
#[derive(Debug, Clone, PartialEq, Hash, Default, Eq)]
pub struct Number(String);

/// A request was made using an unknown number.
#[derive(Debug, PartialEq)]
pub struct UnknownNumberError(Number);

/// A stream of updates to the state of calls. Each update contains the `Number` and the
/// `CallState`. When the channel for a given call is closed, an epitaph is returned with the
/// `Number`.
/// The `Number` uniquely identifies a call. TODO (fxbug.dev/64558): Handle multi-party calls.
type CallStateUpdates =
    StreamMap<Number, StreamWithEpitaph<Tagged<Number, HangingGetStream<CallState>>, Number>>;

/// Manages the state of all ongoing calls reported by the call manager. Provides updates on the
/// states and relays requests made by the Hands Free to act on calls.
pub(crate) struct Calls {
    new_calls: Option<HangingGetStream<(ClientEnd<CallMarker>, String, CallState)>>,
    current_calls: HashMap<Number, (CallProxy, CallState)>,
    call_updates: CallStateUpdates,
    terminated: bool,
}

impl Calls {
    pub fn new(proxy: Option<PeerHandlerProxy>) -> Self {
        let new_calls =
            proxy.map(|proxy| HangingGetStream::new(Box::new(move || Some(proxy.wait_for_call()))));
        Self {
            new_calls,
            current_calls: HashMap::new(),
            call_updates: CallStateUpdates::empty(),
            terminated: false,
        }
    }

    /// Insert a new call
    pub fn handle_new_call(
        &mut self,
        call: ClientEnd<CallMarker>,
        number: Number,
        state: CallState,
    ) {
        let call = call.into_proxy().unwrap();
        self.current_calls.insert(number.clone(), (call.clone(), state.clone()));
        let call_state = HangingGetStream::new(Box::new(move || Some(call.watch_state())));
        self.call_updates
            .insert(number.clone(), call_state.tagged(number.clone()).with_epitaph(number));
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
        number: &Number,
        f: impl FnOnce(&CallProxy) -> Result<(), fidl::Error>,
    ) -> Result<(), UnknownNumberError> {
        let result =
            f(&self.current_calls.get(number).ok_or(UnknownNumberError(number.clone()))?.0);
        if let Err(e) = result {
            if !e.is_closed() {
                warn!("Error making request on Call channel for {:?}: {}", number, e);
            }
            self.current_calls.remove(number);
        }
        Ok(())
    }

    /// Send a request to the call manager to place the call to `number` on hold.
    pub fn _request_hold(&mut self, number: &Number) -> Result<(), UnknownNumberError> {
        self._send_call_request(number, |proxy| proxy.request_hold())
    }

    /// Send a request to the call manager to make the call to `number` active.
    pub fn _request_active(&mut self, number: &Number) -> Result<(), UnknownNumberError> {
        self._send_call_request(number, |proxy| proxy.request_active())
    }

    /// Send a request to the call manager to terminate the call to `number`.
    pub fn _request_terminate(&mut self, number: &Number) -> Result<(), UnknownNumberError> {
        self._send_call_request(number, |proxy| proxy.request_terminate())
    }

    /// Send a request to the call manager to transfer the audio of the call to `number` from the
    /// headset to the fuchsia device.
    pub fn _request_transfer_audio(&mut self, number: &Number) -> Result<(), UnknownNumberError> {
        self._send_call_request(number, |proxy| proxy.request_transfer_audio())
    }

    /// Send a dtmf code to the call manager for active call,
    pub async fn send_dtmf_code(&mut self, _code: DtmfCode) {
        unimplemented!(
            "Sending a dtmf code can fail where other call requests cannot. \
            It must be handled separately"
        );
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
                    self.handle_new_call(call, number.clone(), state.clone());
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
                StreamItem::Item((number, Ok(state))) => return Some((number, state)),
                StreamItem::Item((number, Err(_error))) => {
                    self.call_updates.remove(&number);
                }
                StreamItem::Epitaph(number) => {
                    self.call_updates.remove(&number);
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

    /// The most common test setup includes an initialized Calls instance and an ongoing call.
    /// This helper function sets up a `Calls` instance in that state and returns the associated
    /// endpoints.
    fn setup_ongoing_call() -> (Calls, PeerHandlerRequestStream, CallRequestStream, Number) {
        let (proxy, peer_stream) =
            fidl::endpoints::create_proxy_and_stream::<PeerHandlerMarker>().unwrap();
        let mut calls = Calls::new(Some(proxy));
        let (client_end, call_stream) = fidl::endpoints::create_request_stream().unwrap();
        let num = Number("1".into());
        calls.handle_new_call(client_end, num.clone(), CallState::IncomingRinging);
        (calls, peer_stream, call_stream, num)
    }

    #[fasync::run_until_stalled(test)]
    async fn call_requests_send_requests_to_server() {
        let (mut calls, _peer_handler, mut call_stream, num) = setup_ongoing_call();

        calls._request_hold(&num).expect("valid number");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestHold { .. })));

        calls._request_active(&num).expect("valid number");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestActive { .. })));

        calls._request_terminate(&num).expect("valid number");
        assert_matches!(call_stream.next().await, Some(Ok(CallRequest::RequestTerminate { .. })));

        calls._request_transfer_audio(&num).expect("valid number");
        assert_matches!(
            call_stream.next().await,
            Some(Ok(CallRequest::RequestTransferAudio { .. }))
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn call_requests_invalid_number_return_error() {
        let (mut calls, _peer_handler, _call_stream, _num) = setup_ongoing_call();

        let invalid = Number("2".into());
        let result = calls._request_hold(&invalid);
        assert_eq!(result, Err(UnknownNumberError(invalid.clone())));

        let invalid = Number("2".into());
        let result = calls._request_active(&invalid);
        assert_eq!(result, Err(UnknownNumberError(invalid.clone())));

        let invalid = Number("2".into());
        let result = calls._request_terminate(&invalid);
        assert_eq!(result, Err(UnknownNumberError(invalid.clone())));

        let invalid = Number("2".into());
        let result = calls._request_transfer_audio(&invalid);
        assert_eq!(result, Err(UnknownNumberError(invalid.clone())));
    }

    #[fasync::run_until_stalled(test)]
    async fn call_requests_manager_closed_clears_call() {
        let (mut calls, _peer_handler, call_stream, num) = setup_ongoing_call();
        drop(call_stream);

        assert!(calls.current_calls.contains_key(&num));

        // A request made to a Call channel that is closed will remove the call entry.
        let result = calls._request_hold(&num);
        assert_eq!(result, Ok(()));

        assert!(!calls.current_calls.contains_key(&num));
    }

    #[test]
    fn calls_stream_lifecycle() {
        let mut exec = fasync::Executor::new().unwrap();

        let (mut calls, mut handler_stream, mut call_1, num_1) = setup_ongoing_call();

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
        let num_2 = match result {
            Poll::Ready(Some((number, CallState::IncomingRinging))) => number,
            result => panic!("Unexpected result: {:?}", result),
        };
        assert_eq!(num_2, Number("2".into()));

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
        let num = match result {
            Poll::Ready(Some((number, CallState::OngoingActive))) => number,
            result => panic!("Unexpected result: {:?}", result),
        };
        assert_eq!(num, num_1);

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
        let num = match result {
            Poll::Ready(Some((number, CallState::OngoingHeld))) => number,
            result => panic!("Unexpected result: {:?}", result),
        };
        assert_eq!(num, num_2);

        drop(call_2);

        // Stream doesn't have an item ready.
        let result = exec.run_until_stalled(&mut calls.next());
        assert_matches!(result, Poll::Ready(None));
        assert!(calls.is_terminated());
    }
}
