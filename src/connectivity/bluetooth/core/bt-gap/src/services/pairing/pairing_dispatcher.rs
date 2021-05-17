// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module defines a `PairingDispatcher`, a struct for delegating pairing requests from
//! multiple downstream hosts to a single upstream `PairingDelegate`. The dispatcher registers as
//! the PairingDelegate to the hosts by using the fuchsia.bluetooth.host.Host.SetPairingDelegate()
//! method.
//!
//! Each PairingDispatcher corresponds to precisely one upstream delegate - if a new upstream is
//! to be used, a new PairingDispatcher must be created and registered with the relevant hosts.
//! This ensures the lifetime of the Dispatcher matches that of the upstream, and transitively -
//! that any requests handled by the current upstream do not outlive the upstream delegate. By
//! tying pending requests to a given dispatcher, we clearly enforce the correct lifecycle so that
//! each request cannot receive responses from more than one upstream.
//!
//! A PairingDispatcher can be created via `PairingDispatcher::new()`, which returns both a
//! `PairingDispatcher` and a `PairingDispatcherHandle`.
//!
//! `PairingDispatcher::run()` consumes the Dispatcher itself and returns a Future which must be
//! polled to execute the routing behavior; it will forward requests to the upstream and responses
//! will be dispatched back to the originating hosts.
//!
//! The PairingDispatcherHandle provides access to the corresponding PairingDispatcher to add new
//! hosts, which will be processed by the Future returned from `PairingDispatcher::run()`. When the
//! handle is dropped, the PairingDispatcher will be closed.

use {
    anyhow::Error,
    async_utils::stream::{IndexedStreams, StreamItem, WithEpitaph, WithTag},
    fidl::endpoints::Proxy,
    fidl_fuchsia_bluetooth_host::HostProxy,
    fidl_fuchsia_bluetooth_sys::{
        InputCapability, OutputCapability, PairingDelegateEvent, PairingDelegateEventStream,
        PairingDelegateProxy,
        PairingDelegateRequest::{self, *},
        PairingDelegateRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::{HostId, Peer, PeerId},
    futures::{
        channel::mpsc,
        future::{BoxFuture, FutureExt},
        select,
        sink::SinkExt,
        stream::StreamExt,
    },
    log::{info, warn},
    std::convert::TryFrom,
};

use crate::services::pairing::pairing_requests::PairingRequests;

type Responder = fidl_fuchsia_bluetooth_sys::PairingDelegateOnPairingRequestResponder;

/// The return value from an OnPairingRequest call
type PairingResponse = Result<(bool, u32), fidl::Error>;

/// A dispatcher for routing pairing requests from hosts to an upstream delegate. See the module
/// level comment for more detail.
pub struct PairingDispatcher {
    /// Bluetooth IO capabilities that determine how Pairing should be completed.
    input: InputCapability,
    output: OutputCapability,
    /// The upstream delegate to dispatch requests to
    upstream: PairingDelegateProxy,
    // The event stream of the upstream delegate
    upstream_events: PairingDelegateEventStream,
    /// Host Drivers we are currently dispatching requests for
    hosts: IndexedStreams<HostId, PairingDelegateRequestStream>,
    /// Currently in-flight requests
    inflight_requests: PairingRequests<(PairingResponse, Responder)>,
    /// New hosts that we have been asked to begin processing
    hosts_added: mpsc::Receiver<(HostId, HostProxy)>,
}

impl Drop for PairingDispatcher {
    fn drop(&mut self) {
        // Drop hosts to stop processing the tasks of all host channels
        self.hosts = IndexedStreams::empty();
        for (_, reqs) in self.inflight_requests.take_all_requests().into_iter() {
            for peer_id in reqs.into_iter() {
                let _ignored = self.upstream.on_pairing_complete(&mut peer_id.into(), false);
            }
        }
    }
}

impl PairingDispatcher {
    /// Create a new Dispatcher and corresponding Handle
    pub fn new(
        upstream: PairingDelegateProxy,
        input: InputCapability,
        output: OutputCapability,
    ) -> (PairingDispatcher, PairingDispatcherHandle) {
        let (hosts_added, handle) = PairingDispatcherHandle::new(upstream.clone());

        let upstream_events = upstream.take_event_stream();
        let dispatcher = PairingDispatcher {
            input,
            output,
            upstream,
            upstream_events,
            hosts: IndexedStreams::empty(),
            inflight_requests: PairingRequests::empty(),
            hosts_added,
        };

        (dispatcher, handle)
    }

    /// Start relaying pairing requests from the host end of `host_proxy` to our upstream
    fn start_handling_host(&mut self, id: HostId, host_proxy: HostProxy) -> Result<(), Error> {
        let (client, requests) = fidl::endpoints::create_request_stream()?;
        host_proxy.set_pairing_delegate(self.input, self.output, client)?;
        // Historically we spawned a task to handle these requests
        // Instead, store a value that can be polled directly
        if self.hosts.insert(id, requests.tagged(id).with_epitaph(id)).is_some() {
            warn!("Replaced host {} in handling", id);
        }
        Ok(())
    }

    /// Run the PairingDispatcher, processing incoming host requests and responses to in-flight
    /// requests from the upstream delegate
    pub async fn run(mut self) {
        while !self.handle_next().await {}
    }

    /// Handle a single incoming event for the PairingDispatcher. Return true if the dispatcher
    /// should terminate, and false if it should continue
    async fn handle_next(&mut self) -> bool {
        select! {
            host_event = self.hosts.select_next_some() => {
                match host_event {
                    // A new request was received from a host
                    StreamItem::Item((host, req)) => {
                        match req {
                            Ok(req) => self.handle_host_request(req, host),
                            Err(_) => {
                                drop(self.hosts.remove(&host));
                                false
                            }
                        }
                    }
                    // A host channel has closed; notify upstream that all of its in-flight
                    // requests have aborted
                    StreamItem::Epitaph(host) => {
                        if let Some(reqs) = self.inflight_requests.remove_host_requests(host) {
                            for peer_id in reqs {
                                if let Err(e) = self.upstream.on_pairing_complete(&mut peer_id.into(), false) {
                                    // If we receive an error communicating with upstream,
                                    // terminate
                                    warn!("Error notifying upstream when downstream closed: {}", e);
                                    return true;
                                }
                            }
                        }
                        false
                    }
                }
            },
            pending_req = &mut self.inflight_requests.select_next_some() => {
                let (peer_id, (upstream_response, downstream)) = pending_req;
                match upstream_response {
                    Ok((accept, passkey)) => {
                        let accepted = if accept { "accepted" } else { "rejected" };
                        info!(
                            "PairingDelegate {} pairing request to PeerId {}. Passkey: {}",
                            accepted, peer_id, passkey
                        );
                        if let Err(e) = downstream.send(accept, passkey) {
                            warn!("Error replying to pairing request from bt-host: {}", e);
                            // TODO(fxbug.dev/45325) - when errors occur communicating with a downstream
                            // host, we should unregister and remove that host
                        }
                        false
                    }
                    Err(e) => {
                        // All fidl errors are considered fatal with respect to the upstream
                        // channel. If we receive any error, we consider the upstream closed and
                        // terminate the dispatcher. Terminating will result in the downstream
                        // request being canceled so we do not need to specifically notify.
                        warn!("Terminating Pairing Delegate: Error handling pairing request: {}", e);
                        true
                    }
                }
            },
            host_added = self.hosts_added.next().fuse() => {
                match host_added {
                    Some((id, proxy)) => {
                        if let Err(_) = self.start_handling_host(id.clone(), proxy) {
                            warn!("Failed to register pairing delegate channel for bt-host {}", id)
                        }
                        false
                    }
                    // If empty, all senders have been dropped and we should close
                    None => true,
                }
            },
            event = self.upstream_events.next().fuse() => {
                match event {
                     // TODO(fxbug.dev/76133): Handle OnLocalKeypress event.
                     Some(Ok(PairingDelegateEvent::OnLocalKeypress {id: _, keypress: _})) => {
                         warn!("Ignoring pairing delegate local keypress (unimplemented)");
                         false
                     },
                     // If the pairing delegate event stream closes or errors, terminate the dispatcher.
                     _ => true,

                }
            },
        }
    }

    /// Handle a single incoming request from a BT-Host. Return true if the dispatcher
    /// should terminate, and false if it should continue
    fn handle_host_request(&mut self, event: PairingDelegateRequest, host: HostId) -> bool {
        match event {
            OnPairingRequest { peer, method, displayed_passkey, responder } => {
                match Peer::try_from(peer) {
                    Ok(peer) => {
                        let id = peer.id;
                        let response: BoxFuture<'static, _> = self
                            .upstream
                            .on_pairing_request((&peer).into(), method, displayed_passkey)
                            .map(move |res| (res, responder))
                            .boxed();
                        info!(
                            "Making PairingDelegate Pairing Request to PeerId {} with method {:?} \
                            (passkey: {})",
                            id, method, displayed_passkey
                        );
                        self.inflight_requests.insert(host, id, response)
                    }
                    Err(e) => {
                        warn!("PairingRequest received with invalid Peer: {:?}", e);
                        if let Err(e) = responder.send(false, 0) {
                            warn!("Error communicating with downstream bt-host {}: {:?}", host, e);
                            // TODO(fxbug.dev/45325) - when errors occur communicating with a downstream
                            // host, we should unregister and remove that host
                        }
                    }
                }
                false
            }
            OnPairingComplete { mut id, success, control_handle: _ } => {
                if self.upstream.on_pairing_complete(&mut id, success).is_err() {
                    warn!(
                        "Failed to propagate OnPairingComplete for peer {}; upstream cancelled",
                        PeerId::from(id)
                    );
                    true
                } else {
                    false
                }
            }
            OnRemoteKeypress { mut id, keypress, control_handle: _ } => {
                if self.upstream.on_remote_keypress(&mut id, keypress).is_err() {
                    warn!(
                        "Failed to propagate OnRemoteKeypress for peer {}; upstream cancelled",
                        PeerId::from(id)
                    );
                    true
                } else {
                    false
                }
            }
        }
    }
}

/// A Handle to a `PairingDispatcher`. This can be used to interact with the dispatcher whilst it
/// is being executed elsewhere.
///
/// * Use `PairingDispatcherHandle::add_host()` to begin processing requests from a new Host.
/// * To terminate the PairingDispatcher, simply drop the Handle to signal to the dispatcher to
/// close. The Dispatcher termination is asynchronous - at the point the Handle is dropped, the
/// dispatcher is not guaranteed to have finished execution and must continue to be polled to
/// finalize.
pub struct PairingDispatcherHandle {
    /// Add a host to the PairingDispatcher
    add_hosts: mpsc::Sender<(HostId, HostProxy)>,
    /// Upstream handle, to determine when we've closed
    upstream: PairingDelegateProxy,
}

impl PairingDispatcherHandle {
    pub fn new(upstream: PairingDelegateProxy) -> (mpsc::Receiver<(HostId, HostProxy)>, Self) {
        let (add_hosts, hosts_receiver) = mpsc::channel(0);
        (hosts_receiver, Self { add_hosts, upstream })
    }

    /// Add a new Host identified by `id` to this PairingDispatcher, so the dispatcher will handle
    /// pairing requests from the host serving to `proxy`.
    pub fn add_host(&self, id: HostId, proxy: HostProxy) {
        let mut channel = self.add_hosts.clone();
        let host_id = id.clone();
        let sent = async move { channel.send((host_id, proxy)).await };
        fasync::Task::spawn(async move {
            if let Err(_) = sent.await {
                info!("Failed to send channel for Host {:?} to pairing delegate", id)
            }
        })
        .detach();
    }

    /// Is the upstream channel this dispatcher routes to closed?
    pub fn is_closed(&self) -> bool {
        self.upstream.is_closed()
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        anyhow::Error,
        fidl_fuchsia_bluetooth::Appearance,
        fidl_fuchsia_bluetooth_sys as sys, fuchsia_async as fasync,
        fuchsia_bluetooth::types::Address,
        futures::{
            future::{self, BoxFuture, Future, FutureExt},
            stream::TryStreamExt,
        },
        matches::assert_matches,
    };

    fn simple_test_peer(id: PeerId) -> Peer {
        Peer {
            id: id.into(),
            address: Address::Public([1, 2, 3, 4, 5, 6]),
            technology: sys::TechnologyType::LowEnergy,
            name: Some("Peer Name".into()),
            appearance: Some(Appearance::Watch),
            device_class: None,
            rssi: None,
            tx_power: None,
            connected: false,
            bonded: false,
            le_services: vec![],
            bredr_services: vec![],
        }
    }

    // Closed upstream delegate causes request to fail
    #[fasync::run_singlethreaded(test)]
    async fn test_no_delegate_rejects() -> Result<(), anyhow::Error> {
        let (proxy, requests) =
            fidl::endpoints::create_proxy_and_stream::<sys::PairingDelegateMarker>()?;

        let peer = simple_test_peer(PeerId(0));

        // Make the request
        let result_fut = proxy.on_pairing_request((&peer).into(), sys::PairingMethod::Consent, 0);

        // Create a dispatcher with a closed upstream
        let (upstream, upstream_server) =
            fidl::endpoints::create_proxy_and_stream::<sys::PairingDelegateMarker>()?;
        let (mut dispatcher, _handle) =
            PairingDispatcher::new(upstream, InputCapability::None, OutputCapability::None);
        // We directly insert the proxy to avoid the indirection of having to provide an
        // implementation of Host.SetPairingDelegate
        let _ =
            dispatcher.hosts.insert(HostId(0), requests.tagged(HostId(0)).with_epitaph(HostId(0)));

        // Handle the request message
        let terminate = dispatcher.handle_next().await;
        // We shouldn't terminate yet
        assert_eq!(terminate, false);

        // Deliberately drop (and hence close) the upstream
        std::mem::drop(upstream_server);

        // Run the dispatcher until termination (should terminate because the upstream is closed)
        dispatcher.run().await;

        let result = result_fut.await.map(|(success, _)| success);

        // We should be rejected by the upstream being closed
        assert_matches!(result, Err(e) if e.is_closed());
        Ok(())
    }

    // Upstream error causes dropped stream (i.e. handler returns err)
    #[fasync::run_singlethreaded(test)]
    async fn test_bad_delegate_drops_stream() -> Result<(), Error> {
        let (proxy, requests) =
            fidl::endpoints::create_proxy_and_stream::<sys::PairingDelegateMarker>()?;

        let peer = simple_test_peer(PeerId(0));

        let (mut dispatcher, handle, run_upstream) =
            dispatcher_from_handler(|_req| future::ok(()))?;
        // Forcibly insert the proxy
        let _ =
            dispatcher.hosts.insert(HostId(0), requests.tagged(HostId(0)).with_epitaph(HostId(0)));

        let make_request = async move {
            let result =
                proxy.on_pairing_request((&peer).into(), sys::PairingMethod::Consent, 0).await;
            // Our channel should have been closed as the responder was dropped
            assert_matches!(result, Err(fidl::Error::ClientChannelClosed { .. }));
            // Now close the dispatcher so the test will finish
            std::mem::drop(handle);
        };

        let run_upstream = async move {
            assert_eq!(run_upstream.await.map_err(|e| e.to_string()), Ok(()));
        };

        let run_handler = dispatcher.run();

        // Wait on all three tasks.
        // All three tasks should terminate, as the handler will terminate, and then the upstream
        // will close as the upstream channel is dropped
        future::join3(make_request, run_handler, run_upstream).await;
        Ok(())
    }

    // Create a pairing delegate from a given handler function
    fn dispatcher_from_handler<Fut>(
        handler: impl Fn(PairingDelegateRequest) -> Fut + Send + Sync + 'static,
    ) -> Result<
        (PairingDispatcher, PairingDispatcherHandle, BoxFuture<'static, Result<(), fidl::Error>>),
        Error,
    >
    where
        Fut: Future<Output = Result<(), fidl::Error>> + Send + Sync + 'static,
    {
        let (upstream, requests) =
            fidl::endpoints::create_proxy_and_stream::<sys::PairingDelegateMarker>()?;

        let (dispatcher, handle) =
            PairingDispatcher::new(upstream, InputCapability::None, OutputCapability::None);

        Ok((dispatcher, handle, requests.try_for_each(handler).boxed()))
    }

    // Successful response from Upstream reaches the downstream request
    #[fasync::run_singlethreaded(test)]
    async fn test_success_notifies_responder() -> Result<(), Error> {
        let (proxy, requests) =
            fidl::endpoints::create_proxy_and_stream::<sys::PairingDelegateMarker>()?;

        let peer = simple_test_peer(PeerId(0));
        let passkey = 42;

        let (mut dispatcher, handle, run_upstream) =
            dispatcher_from_handler(move |req| async move {
                if let OnPairingRequest { peer: _, method: _, displayed_passkey: _, responder } =
                    req
                {
                    assert!(responder.send(true, passkey).is_ok());
                }
                Ok(())
            })?;

        // Forcibly insert the proxy
        let _ =
            dispatcher.hosts.insert(HostId(0), requests.tagged(HostId(0)).with_epitaph(HostId(0)));

        let make_request = async move {
            let result =
                proxy.on_pairing_request((&peer).into(), sys::PairingMethod::Consent, 0).await;
            // Our channel should have been closed as the responder was dropped
            let _passkey = passkey;
            assert_matches!(result, Ok((true, _passkey)));
            // Now close the dispatcher so the test will finish
            std::mem::drop(handle);
        };

        let run_upstream = async move {
            assert_eq!(run_upstream.await.map_err(|e| e.to_string()), Ok(()));
        };

        let run_handler = dispatcher.run();

        // Wait on all three tasks.
        // All three tasks should terminate, as the handler will terminate, and then the upstream
        // will close as the upstream channel is dropped
        future::join3(make_request, run_handler, run_upstream).await;
        Ok(())
    }

    // Adding a host causes its requests to be processed
    #[fasync::run_singlethreaded(test)]
    async fn test_add_host() -> Result<(), Error> {
        let passkey = 42;
        let (dispatcher, handle, run_upstream) = dispatcher_from_handler(move |req| async move {
            if let OnPairingRequest { peer: _, method: _, displayed_passkey: _, responder } = req {
                assert!(responder.send(true, passkey).is_ok());
            }
            Ok(())
        })?;

        let make_request = async move {
            // Create a fake Host connection
            let (host_proxy, mut host_requests) = fidl::endpoints::create_proxy_and_stream::<
                fidl_fuchsia_bluetooth_host::HostMarker,
            >()
            .expect("Creating Host channel should not fail");
            // Tell the dispatcher to add our host
            handle.add_host(HostId(1), host_proxy);

            let req = host_requests.try_next().await.unwrap().unwrap();
            let _passkey = passkey;
            // When we get the set_pairing_delegate call, we can send the request
            let proxy = req
                .into_set_pairing_delegate()
                .expect("Should be SetPairingDelegate")
                .2
                .into_proxy()
                .expect("Host ClientEnd should become Proxy");
            let peer = simple_test_peer(PeerId(0));
            let result =
                proxy.on_pairing_request((&peer).into(), sys::PairingMethod::Consent, 0).await;
            // Our channel should have been closed as the responder was dropped
            assert_matches!(result, Ok((true, _passkey)));
            // Now close the dispatcher so the test will finish
            std::mem::drop(handle);
        };

        let run_upstream = async move {
            assert_eq!(run_upstream.await.map_err(|e| e.to_string()), Ok(()));
        };

        let run_handler = dispatcher.run();

        // Wait on all three tasks.
        // All three tasks should terminate, as the handler will terminate, and then the upstream
        // will close as the upstream channel is dropped
        future::join3(make_request, run_handler, run_upstream).await;
        Ok(())
    }
}
