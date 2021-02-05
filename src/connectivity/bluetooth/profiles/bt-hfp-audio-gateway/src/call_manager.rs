// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    core::{
        pin::Pin,
        task::{Context, Poll, Waker},
    },
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_bluetooth_hfp::{
        CallManagerRequest, CallManagerRequestStream, CallManagerWatchForPeerResponder,
        PeerHandlerMarker,
    },
    fuchsia_bluetooth::types::PeerId,
    fuchsia_zircon as zx,
    futures::{lock::Mutex, ready, stream::FusedStream, FutureExt, Stream, StreamExt},
    log::info,
    std::{
        collections::VecDeque,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        },
    },
};

/// Item type returned by `<CallManager as Stream>::poll_next`.
#[cfg_attr(test, derive(Debug))]
pub enum CallManagerEvent {
    /// A new Call Manager has been registered.
    ManagerRegistered,
}

/// Abstracts over the Call Manager FIDL interface.
///
/// Because actions can be initiated by either side of the interface,
/// async methods are provided to allow HFP to initiate actions and a Stream interface is provided
/// to signal that the Call Manager is initiating actions.
///
/// `CallManager` implements `Clone` and all clones derived from a single instance of `CallManager`
/// reference the same internal state. Only one of these clones (or the original) should treat the
/// `CallManager` as a Stream. If more than one clone calls `Stream` related methods, it is
/// possible for stream items to be missed by one instance or another.
pub struct CallManager {
    /// Shared state wrapped in an async-aware mutex.
    inner: Arc<Mutex<CallManagerInner>>,
    /// Flag used for the FusedStream implementation of `CallManager`.
    /// This is an AtomicBool so that it can be accessed from non-async contexts.
    inactive: Arc<AtomicBool>,
    terminated: bool,
    // The logic necessary to determine if a stream is inactive is simplified by making
    // `service_provider` an Option that is taken from the `CallManager`. It allows the
    // CallManagerServiceProvider to immediately Drop and set the the inactive flag when all
    // instances that are not stored in the CallManager are dropped.
    //
    // Weak references could be used here but the author of this component has not been presented
    // with a use-case for multiple calls to `CallManager::service_provider`.
    service_provider: Option<CallManagerServiceProvider>,
}

impl CallManager {
    pub fn new() -> Self {
        let inner = Arc::new(Mutex::new(CallManagerInner::new()));
        let inactive = Arc::new(AtomicBool::new(false));
        Self {
            service_provider: Some(CallManagerServiceProvider::new(
                inner.clone(),
                inactive.clone(),
            )),
            inner,
            inactive,
            terminated: false,
        }
    }

    /// Return the `CallManagerServiceProvider` that will route incoming requests to this
    /// `CallManager`. The provider is only returned the first time this method is called.
    /// Repeated calls will return `None`. CallManagerServiceProvider implements `Clone`. The
    /// correct way to create multiple instances are to make clones.
    pub fn service_provider(&mut self) -> Option<CallManagerServiceProvider> {
        self.service_provider.take()
    }

    /// A new Bluetooth peer that supports the HFP HF role has been connected.
    pub async fn peer_added(&self, id: PeerId, handle: ServerEnd<PeerHandlerMarker>) {
        let mut inner = self.inner.lock().await;
        inner.added_peers.push_back((id, handle));
        if let Err(e) = inner.respond_to_peer_watcher() {
            info!("call manager connection closed: {}", e);
            *inner = CallManagerInner::new();
        }
    }
}

impl Stream for CallManager {
    type Item = CallManagerEvent;
    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let this = &mut *self;

        if this.terminated {
            panic!("Cannot poll a terminated stream");
        }

        let mut inner = ready!(this.inner.lock().poll_unpin(cx));

        // Update internal state before returning an item.
        if let Err(reason) = inner.poll_inner_stream(cx) {
            info!("call manager connection closed: {}", reason);
            *inner = CallManagerInner::new();
        }

        // When the CallManager has no current fidl connection and it has been marked inactive, it
        // can never produce new values or register a new call manager FIDL connection.
        // At this point it should be marked terminated and return a `None` stream output value.
        if inner.stream.is_none() && this.inactive.load(Ordering::SeqCst) {
            this.terminated = true;
            return Poll::Ready(None);
        }

        // After all internal state has been updated, return a ready item or return pending and
        // store a waker.
        if let Some(item) = inner.outstanding_events.pop_front() {
            Poll::Ready(Some(item))
        } else {
            inner.waker = Some(cx.waker().clone());
            Poll::Pending
        }
    }
}

impl FusedStream for CallManager {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

#[derive(Clone)]
pub struct CallManagerServiceProvider {
    inner: Arc<Mutex<CallManagerInner>>,
    /// A reference counted wrapper around the inactive flag that will be set once all Call manager
    /// service providers cloned from the same instance have been dropped.
    inactive_flag: Arc<SetInactive>,
}

impl CallManagerServiceProvider {
    /// Make a new CallManagerServiceProvider. `inner` and `inactive` should be cloned from the
    /// respectively named values in the associated `CallManager`.
    fn new(inner: Arc<Mutex<CallManagerInner>>, inactive: Arc<AtomicBool>) -> Self {
        Self { inner, inactive_flag: Arc::new(SetInactive(inactive)) }
    }

    /// Set the CallManager FIDL channel if one is not set. If there is already an active call
    /// manager channel, the provided proxy is dropped.
    ///
    /// This will indirectly signal the `CallManager` stream to wake up if it has been polled and
    /// is in a pending state.
    pub async fn register(&self, stream: CallManagerRequestStream) {
        let mut inner = self.inner.lock().await;
        if inner.stream.is_none() {
            inner.waker.take().map(Waker::wake);

            // Ensure there is no old state hanging around.
            *inner = CallManagerInner::new();

            info!("setting call manager");
            inner.stream = Some(stream);
            inner.outstanding_events.push_back(CallManagerEvent::ManagerRegistered);
        } else {
            info!("call manager already set. Dropping new connection");
            stream.control_handle().shutdown_with_epitaph(zx::Status::UNAVAILABLE);
        }
    }
}

/// Represents the state needed by the `CallManager`.
struct CallManagerInner {
    /// Optional stream of requests from an CallManager client.
    /// There can be zero or one connection to the CallManager protocol which is why an Option is
    /// used.
    stream: Option<CallManagerRequestStream>,
    /// Hanging Get responder that should be used to convey the next set of updates
    peer_watcher: Option<CallManagerWatchForPeerResponder>,
    /// Peers that have been added since the last time a WatchPeers hanging get request was made.
    added_peers: VecDeque<(PeerId, ServerEnd<PeerHandlerMarker>)>,
    /// When a new manager comes along, it should be made aware of all
    /// peers that the CallManager struct knows about. This will need to generate
    /// a ManagePeer event for each peer in the system.
    /// `outstanding_events` holds all events that are ready to be returned immediately by polling
    /// the stream.
    /// It is possible for events to be generated by actions unrelated to Stream::poll_next. Those
    /// events need to be stored somewhere until the stream is polled. This is that somewhere.
    outstanding_events: VecDeque<CallManagerEvent>,
    waker: Option<Waker>,
}

impl CallManagerInner {
    /// Create a new empty `CallManagerInner`.
    fn new() -> Self {
        Self {
            stream: None,
            peer_watcher: None,
            added_peers: VecDeque::new(),
            outstanding_events: VecDeque::new(),
            waker: None,
        }
    }

    /// If there is a responder present and there are changes to respond with, form a response and
    /// send it, consuming the responder in the process.
    ///
    /// Returns an error if sending the peer handle using the responder failed.
    fn respond_to_peer_watcher(&mut self) -> Result<(), fidl::Error> {
        // Handle all outstanding updates when there is a responder present.
        if let Some(responder) = self.peer_watcher.take() {
            if let Some((id, handle)) = self.added_peers.pop_front() {
                responder.send(&mut id.into(), handle)?;
            } else {
                // Put the watcher back since there are no peers to respond with.
                self.peer_watcher = Some(responder);
            }
        }
        Ok(())
    }

    /// Poll `stream` if it is Some, updating internal state based on the output of
    /// polling the stream.
    ///
    /// Returns an error if the call manager has closed the stream or has used the API
    /// incorrectly. The error value is a String specifying the reason for failure.
    /// This string value can be logged for diagnostic purposes.
    fn poll_inner_stream(&mut self, cx: &mut Context<'_>) -> Result<(), String> {
        match self.stream.as_mut().map(|s| s.poll_next_unpin(cx)) {
            None | Some(Poll::Pending) => Ok(()),
            Some(Poll::Ready(None)) => Err("Peer closed".to_string()),
            Some(Poll::Ready(Some(Err(reason)))) => Err(reason.to_string()),
            Some(Poll::Ready(Some(Ok(CallManagerRequest::WatchForPeer { responder, .. })))) => {
                if self.peer_watcher.is_some() {
                    if let Some(stream) = &self.stream {
                        stream.control_handle().shutdown_with_epitaph(zx::Status::BAD_STATE);
                    }
                    Err("client sent multiple overlapping WatchPeers requests".to_string())
                } else {
                    self.peer_watcher = Some(responder);
                    self.respond_to_peer_watcher().map_err(|e| e.to_string())
                }
            }
        }
    }
}

/// A wrapper around an inactive flag that will set the flag on drop.
struct SetInactive(Arc<AtomicBool>);

impl Drop for SetInactive {
    fn drop(&mut self) {
        self.0.store(true, Ordering::SeqCst);
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        async_utils::traits::PollExt,
        fidl::endpoints::{self, Proxy},
        fidl_fuchsia_bluetooth as bt, fidl_fuchsia_bluetooth_bredr as bredr,
        fidl_fuchsia_bluetooth_hfp::{CallManagerMarker, CallManagerProxy},
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::pin_mut,
        matches::assert_matches,
    };

    async fn setup() -> (CallManager, CallManagerProxy, CallManagerServiceProvider) {
        let mut manager = CallManager::new();
        let service = manager.service_provider().unwrap();

        let (proxy, stream) = endpoints::create_proxy_and_stream::<CallManagerMarker>().unwrap();
        service.register(stream).await;
        (manager, proxy, service)
    }

    #[fasync::run_until_stalled(test)]
    async fn register_assigns_a_new_call_manager_and_produces_event() {
        let (mut manager, _proxy, _service) = setup().await;
        let item = manager.next().await;
        assert_matches!(item, Some(CallManagerEvent::ManagerRegistered));
    }

    #[fasync::run_until_stalled(test)]
    async fn register_second_time_closes_stream_with_epitaph() {
        let (_, _proxy, service) = setup().await;

        // Attempting to register a second stream will result in that stream being dropped.
        let (proxy2, stream2) = endpoints::create_proxy_and_stream::<CallManagerMarker>().unwrap();
        service.register(stream2).await;
        let result = proxy2.watch_for_peer().check();
        assert_matches!(
            result,
            Err(fidl::Error::ClientChannelClosed { status: zx::Status::UNAVAILABLE, .. })
        );
    }

    #[test]
    fn poll_next_is_expected_pending() {
        let mut exec = fasync::Executor::new().unwrap();
        let (mut manager, _proxy, _) = exec.run_singlethreaded(setup());

        // The first item after a client has connected is a registered event
        let response = exec.run_until_stalled(&mut manager.next());
        assert_matches!(response, Poll::Ready(Some(_)));

        // After that, the stream returns pending since there no items ready.
        let response = exec.run_until_stalled(&mut manager.next());
        assert_matches!(response, Poll::Pending);
    }

    #[fasync::run_until_stalled(test)]
    async fn stream_terminates_on_call_manager_disconnect() {
        let (mut manager, proxy, _) = setup().await;
        drop(proxy);

        let item = manager.next().await;
        assert!(item.is_none());
        assert!(manager.is_terminated());
    }

    #[fasync::run_until_stalled(test)]
    async fn registered_stream_dropped_on_call_manager_malformed_request() {
        let mut manager = CallManager::new();
        let service = manager.service_provider().unwrap();

        // Create a stream of an unexpected protocol type.
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<bredr::ProfileMarker>().unwrap();
        // Pretend that stream is the expected protocol type.
        let stream = stream.cast_stream::<CallManagerRequestStream>();
        // Send a request using the wrong protocol to the server end.
        let _ = proxy
            .connect(
                &mut fidl_fuchsia_bluetooth::PeerId { value: 1 },
                &mut bredr::ConnectParameters::L2cap(bredr::L2capParameters::EMPTY),
            )
            .check()
            .expect("request to be sent");

        service.register(stream).await;

        let poll_result = manager.next().now_or_never();
        // Even though a stream was registered, it should not produce a
        // CallManagerEvent::ManagerRegistered output on the stream since there was a malformed
        // request detected immediately when polling the stream.
        assert!(poll_result.is_none());

        // Malformed request should cause the proxy to close.
        let _ = proxy.on_closed().await;

        // Check that the CallManager is still in a good state to accept a new
        // client and return valid items.
        let (proxy, stream) = endpoints::create_proxy_and_stream::<CallManagerMarker>().unwrap();
        service.register(stream).await;
        let item = manager.next().await;
        assert_matches!(item, Some(CallManagerEvent::ManagerRegistered));

        let (_, handle) = endpoints::create_proxy().unwrap();
        manager.peer_added(PeerId(1), handle).await;
        // poll the manager stream in the background while making a watch request
        // in this task.
        let _manager = fasync::Task::local(async move {
            loop {
                manager.next().await;
            }
        });
        let response = proxy.watch_for_peer().await;
        assert_matches!(response, Ok((bt::PeerId { value: 1 }, _handle)));
    }

    #[fasync::run_until_stalled(test)]
    async fn peer_added_updates_call_manager_client() {
        let (mut manager, proxy, _) = setup().await;

        // Add 2 peers to make sure multiple peers can be queued
        let (_, handle) = endpoints::create_proxy().unwrap();
        manager.peer_added(PeerId(1), handle).await;

        let (_, handle) = endpoints::create_proxy().unwrap();
        manager.peer_added(PeerId(2), handle).await;

        let _stream = fasync::Task::local(async move {
            while let Some(_) = manager.next().await {}
            panic!("manager stream was not expected to end");
        });

        let response = proxy.watch_for_peer().await;
        assert_matches!(response, Ok((bt::PeerId { value: 1 }, _handle)));

        let response = proxy.watch_for_peer().await;
        assert_matches!(response, Ok((bt::PeerId { value: 2 }, _handle)));
    }

    #[test]
    fn peer_added_terminates_stream_on_error() {
        let mut exec = fasync::Executor::new().unwrap();
        let (mut manager, proxy, _) = exec.run_singlethreaded(setup());

        // Pump watch for peer hanging get request.
        let mut watch_for_peers_fut = proxy.watch_for_peer();
        let _ = exec.run_until_stalled(&mut watch_for_peers_fut);

        // Pump manager stream
        let _ = exec.run_until_stalled(&mut manager.next());

        // Drop all references to the client end
        drop(proxy);
        drop(watch_for_peers_fut);

        assert!(!manager.is_terminated());

        // Run the peer handler in a block to force the borrow on manager to end.
        {
            // Adding a peer would trigger a response to the watch for peers hanging get. That will
            // cause the manager to notice that the client end is closed and set the stream to
            // terminated.
            let (_, handle) = endpoints::create_proxy().unwrap();
            let peer_added_fut = manager.peer_added(PeerId(1), handle);
            pin_mut!(peer_added_fut);
            exec.run_until_stalled(&mut peer_added_fut)
                .expect("peer_added is expected to complete");
        }

        // Pump the manager stream
        let result = exec.run_until_stalled(&mut manager.next());
        assert_matches!(result, Poll::Ready(None));

        assert!(manager.is_terminated());
    }

    #[fasync::run_until_stalled(test)]
    async fn error_on_multiple_outstanding_watch_for_peer_requests() {
        let (mut manager, proxy, _) = setup().await;

        let _stream = fasync::Task::local(async move {
            while let Some(_) = manager.next().await {}
            assert!(manager.is_terminated());
        });

        let fut = proxy.watch_for_peer();
        let response = proxy.watch_for_peer().await;
        assert_matches!(
            response,
            Err(fidl::Error::ClientChannelClosed { status: zx::Status::BAD_STATE, .. })
        );
        let response = fut.await;
        assert_matches!(
            response,
            Err(fidl::Error::ClientChannelClosed { status: zx::Status::BAD_STATE, .. })
        );
    }
}
