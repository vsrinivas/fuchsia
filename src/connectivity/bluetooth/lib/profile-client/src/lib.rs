// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! An interface for interacting with the `fuchsia.bluetooth.bredr.Profile` protocol.
//! This interface provides convenience methods to register service searches and advertisements
//! using the `Profile` protocol and includes a Stream implementation which can be polled to
//! receive Profile API updates.
//!
//! ### Example Usage:
//!
//! // Connect to the `f.b.bredr.Profile` protocol.
//! let profile_svc = fuchsia_component::client::connect_to_protocol::<ProfileMarker>()?;
//!
//! // Create a new `ProfileClient` by registering an advertisement. Register searches.
//! let svc_defs = vec![..];
//! let channel_params = ChannelParameters { .. };
//! let mut profile_client = ProfileClient::advertise(profile_svc, &svc_defs, channel_params)?;
//! profile_client.add_search(..)?;
//! profile_client.add_search(..)?;
//!
//! // Listen for events from the ProfileClient stream implementation.
//! while let Some(event) = profile_client.next().await? {
//!     match event {
//!         ProfileEvent::PeerConnected { .. } => {} // Do something
//!         ProfileEvent::SearchResult { .. } => {} // Do something
//!     }
//! }
//!

use {
    fidl::{client::QueryResponseFut, endpoints::create_request_stream},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::types::{Channel, PeerId},
    futures::{
        stream::{FusedStream, Stream, StreamExt},
        task::{Context, Poll, Waker},
        FutureExt,
    },
    std::{
        convert::{TryFrom, TryInto},
        pin::Pin,
    },
    tracing::trace,
};

/// Error type used by this library.
mod error;

pub use crate::error::Error;

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Debug)]
pub enum ProfileEvent {
    /// A peer has connected.
    PeerConnected { id: PeerId, protocol: Vec<bredr::ProtocolDescriptor>, channel: Channel },
    /// A peer matched one of the search results that was started.
    SearchResult {
        id: PeerId,
        protocol: Option<Vec<bredr::ProtocolDescriptor>>,
        attributes: Vec<bredr::Attribute>,
    },
}

impl ProfileEvent {
    pub fn peer_id(&self) -> PeerId {
        match self {
            Self::PeerConnected { id, .. } => *id,
            Self::SearchResult { id, .. } => *id,
        }
    }
}

impl TryFrom<bredr::SearchResultsRequest> for ProfileEvent {
    type Error = Error;
    fn try_from(value: bredr::SearchResultsRequest) -> Result<Self> {
        let bredr::SearchResultsRequest::ServiceFound { peer_id, protocol, attributes, responder } =
            value;
        let id: PeerId = peer_id.into();
        responder.send()?;
        trace!(
            "Profile Search Result: {:?} - protocol {:?}, attributes {:?}",
            id,
            protocol,
            attributes
        );
        Ok(ProfileEvent::SearchResult { id, protocol, attributes })
    }
}

impl TryFrom<bredr::ConnectionReceiverRequest> for ProfileEvent {
    type Error = Error;
    fn try_from(value: bredr::ConnectionReceiverRequest) -> Result<Self> {
        let bredr::ConnectionReceiverRequest::Connected { peer_id, channel, protocol, .. } = value;
        let id = peer_id.into();
        let channel = channel.try_into().map_err(Error::connection_receiver)?;
        trace!("Connection from {:?} - protocol {:?}", id, protocol);
        Ok(ProfileEvent::PeerConnected { id, channel, protocol })
    }
}

/// Provides an interface to interact with the `fuchsia.bluetooth.bredr.Profile` protocol.
///
/// Currently, this implementation supports a single advertisement and multiple searches.
/// Search result events can be returned for any of the registered services. In the case of
/// multiple registered searches, consider using the `fuchsia_bluetooth::profile::find_service_class`
/// function in the `fuchsia_bluetooth` crate to identify the Service Class of the returned event.
///
/// The `ProfileClient` is typically used as a stream of ConnectionReceiver connection requests
/// and SearchResults events. The stream is considered terminated if the advertisement (if set)
/// has terminated, the ConnectionReceiver stream associated with the advertisement has terminated,
/// or if _any_ of the registered searches have terminated.
///
/// For information about the Profile API, see the [FIDL Docs](//sdk/fidl/fuchsia.bluetooth.bredr/profile.fidl).
pub struct ProfileClient {
    /// The proxy that is used to start new searches and advertise.
    proxy: bredr::ProfileProxy,
    /// The result for the advertisement. Terminates when the advertisement has completed.
    advertisement: Option<QueryResponseFut<bredr::ProfileAdvertiseResult>>,
    connection_receiver: Option<bredr::ConnectionReceiverRequestStream>,
    /// The registered results from the search streams. Polled in order.
    searches: Vec<bredr::SearchResultsRequestStream>,
    /// This waker will be woken if a new search is added.
    stream_waker: Option<Waker>,
    /// True once any of the searches, or the advertisement, have completed.
    terminated: bool,
}

impl ProfileClient {
    /// Create a new Profile that doesn't advertise any services.
    pub fn new(proxy: bredr::ProfileProxy) -> Self {
        Self {
            proxy,
            advertisement: None,
            connection_receiver: None,
            searches: Vec::new(),
            stream_waker: None,
            terminated: false,
        }
    }

    /// Create a new Profile that advertises the services in `services`.
    /// Incoming connections will request the `channel mode` provided.
    pub fn advertise(
        proxy: bredr::ProfileProxy,
        services: &[bredr::ServiceDefinition],
        channel_params: bredr::ChannelParameters,
    ) -> Result<Self> {
        if services.is_empty() {
            return Ok(Self::new(proxy));
        }
        let (connect_client, connection_receiver) = create_request_stream()?;
        let advertisement = proxy
            .advertise(&mut services.into_iter().cloned(), channel_params, connect_client)
            .check()?;
        Ok(Self {
            advertisement: Some(advertisement),
            connection_receiver: Some(connection_receiver),
            ..Self::new(proxy)
        })
    }

    pub fn add_search(
        &mut self,
        service_uuid: bredr::ServiceClassProfileIdentifier,
        attributes: &[u16],
    ) -> Result<()> {
        if self.terminated {
            return Err(Error::AlreadyTerminated);
        }

        let (results_client, results_stream) = create_request_stream()?;
        self.proxy.search(service_uuid, attributes, results_client)?;
        self.searches.push(results_stream);

        if let Some(waker) = self.stream_waker.take() {
            waker.wake();
        }
        Ok(())
    }
}

impl FusedStream for ProfileClient {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

impl Stream for ProfileClient {
    type Item = Result<ProfileEvent>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.terminated {
            panic!("Profile polled after terminated");
        }

        if let Some(advertisement) = self.advertisement.as_mut() {
            if let Poll::Ready(result) = advertisement.poll_unpin(cx) {
                self.terminated = true;
                let error = match result {
                    Ok(result) => Error::Advertisement { result },
                    Err(fidl_error) => fidl_error.into(),
                };
                return Poll::Ready(Some(Err(error)));
            };
        }

        if let Some(receiver) = self.connection_receiver.as_mut() {
            if let Poll::Ready(item) = receiver.poll_next_unpin(cx) {
                match item {
                    Some(Ok(request)) => return Poll::Ready(Some(request.try_into())),
                    Some(Err(e)) => return Poll::Ready(Some(Err(Error::connection_receiver(e)))),
                    None => {
                        self.terminated = true;
                        return Poll::Ready(None);
                    }
                };
            };
        }

        for search in &mut self.searches {
            if let Poll::Ready(item) = search.poll_next_unpin(cx) {
                match item {
                    Some(Ok(request)) => return Poll::Ready(Some(request.try_into())),
                    Some(Err(e)) => return Poll::Ready(Some(Err(Error::search_result(e)))),
                    None => {
                        self.terminated = true;
                        return Poll::Ready(None);
                    }
                }
            }
        }

        // Return pending, store the waker to wake if a new poll target is added.
        self.stream_waker = Some(cx.waker().clone());
        Poll::Pending
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::Uuid;
    use futures::{pin_mut, Future};
    use futures_test::task::new_count_waker;

    fn make_profile_service_definition(service_uuid: Uuid) -> bredr::ServiceDefinition {
        bredr::ServiceDefinition {
            service_class_uuids: Some(vec![service_uuid.into()]),
            protocol_descriptor_list: Some(vec![
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::L2Cap,
                    params: vec![bredr::DataElement::Uint16(bredr::PSM_AVDTP)],
                },
                bredr::ProtocolDescriptor {
                    protocol: bredr::ProtocolIdentifier::Avdtp,
                    params: vec![bredr::DataElement::Uint16(0x0103)], // Indicate v1.3
                },
            ]),
            profile_descriptors: Some(vec![bredr::ProfileDescriptor {
                profile_id: bredr::ServiceClassProfileIdentifier::AdvancedAudioDistribution,
                major_version: 1,
                minor_version: 2,
            }]),
            ..bredr::ServiceDefinition::EMPTY
        }
    }

    #[test]
    fn service_advertisement_and_ends_when_advertisement_ends() {
        let mut exec = fasync::TestExecutor::new().expect("executor should build");
        let (proxy, mut profile_stream) = create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Profile proxy should be created");

        let source_uuid = Uuid::new16(bredr::ServiceClassProfileIdentifier::AudioSource as u16);
        let defs = vec![make_profile_service_definition(source_uuid)];
        let channel_params = bredr::ChannelParameters {
            channel_mode: Some(bredr::ChannelMode::Basic),
            ..bredr::ChannelParameters::EMPTY
        };

        let mut profile = ProfileClient::advertise(proxy, &defs, channel_params.clone())
            .expect("Advertise succeeds");

        let (_connect_proxy, adv_responder) = expect_advertisement_registration(
            &mut exec,
            &mut profile_stream,
            defs,
            channel_params.into(),
        );

        {
            let event_fut = profile.next();
            pin_mut!(event_fut);
            assert!(exec.run_until_stalled(&mut event_fut).is_pending());

            adv_responder.send(&mut Ok(())).expect("able to respond");

            match exec.run_until_stalled(&mut event_fut) {
                Poll::Ready(Some(Err(Error::Advertisement { result: Ok(()) }))) => {}
                x => panic!("Expected an error from the advertisement, got {:?}", x),
            };
        }

        assert!(profile.is_terminated());
    }

    #[test]
    fn connection_request_relayed_to_stream() {
        let mut exec = fasync::TestExecutor::new().expect("executor should build");
        let (proxy, mut profile_stream) = create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Profile proxy should be created");

        let source_uuid = Uuid::new16(bredr::ServiceClassProfileIdentifier::AudioSource as u16);
        let defs = vec![make_profile_service_definition(source_uuid)];
        let channel_params = bredr::ChannelParameters {
            channel_mode: Some(bredr::ChannelMode::Basic),
            ..bredr::ChannelParameters::EMPTY
        };

        let mut profile = ProfileClient::advertise(proxy, &defs, channel_params.clone())
            .expect("Advertise succeeds");

        let (connect_proxy, _adv_responder) = expect_advertisement_registration(
            &mut exec,
            &mut profile_stream,
            defs,
            channel_params.into(),
        );

        let remote_peer = PeerId(12343);
        {
            let event_fut = profile.next();
            pin_mut!(event_fut);
            assert!(exec.run_until_stalled(&mut event_fut).is_pending());

            let (_local, remote) = Channel::create();
            connect_proxy
                .connected(
                    &mut remote_peer.into(),
                    bredr::Channel::try_from(remote).unwrap(),
                    &mut vec![].iter_mut(),
                )
                .expect("connection should work");

            match exec.run_until_stalled(&mut event_fut) {
                Poll::Ready(Some(Ok(ProfileEvent::PeerConnected { id, .. }))) => {
                    assert_eq!(id, remote_peer);
                }
                x => panic!("Expected an error from the advertisement, got {:?}", x),
            };
        }

        // Stream should error and terminate when the advertisement is disconnected.
        drop(connect_proxy);

        match exec.run_until_stalled(&mut profile.next()) {
            Poll::Ready(None) => {}
            x => panic!("Expected profile to end on advertisement drop, got {:?}", x),
        };

        assert!(profile.is_terminated());
    }

    fn expect_advertisement_registration(
        exec: &mut fasync::TestExecutor,
        profile_stream: &mut bredr::ProfileRequestStream,
        expected_defs: Vec<bredr::ServiceDefinition>,
        expected_params: bredr::ChannelParameters,
    ) -> (bredr::ConnectionReceiverProxy, bredr::ProfileAdvertiseResponder) {
        match exec.run_until_stalled(&mut profile_stream.next()) {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Advertise {
                services,
                parameters,
                receiver,
                responder,
            }))) => {
                assert_eq!(&services[..], expected_defs);
                assert_eq!(parameters, expected_params);
                (receiver.into_proxy().expect("proxy for connection receiver"), responder)
            }
            x => panic!("Expected ready advertisement request, got {:?}", x),
        }
    }

    fn expect_search_registration(
        exec: &mut fasync::TestExecutor,
        profile_stream: &mut bredr::ProfileRequestStream,
        search_uuid: bredr::ServiceClassProfileIdentifier,
        search_attrs: &[u16],
    ) -> bredr::SearchResultsProxy {
        match exec.run_until_stalled(&mut profile_stream.next()) {
            Poll::Ready(Some(Ok(bredr::ProfileRequest::Search {
                service_uuid,
                attr_ids,
                results,
                ..
            }))) => {
                assert_eq!(&attr_ids[..], search_attrs);
                assert_eq!(service_uuid, search_uuid);
                results.into_proxy().expect("proxy from client end")
            }
            x => panic!("Expected ready request for a search, got: {:?}", x),
        }
    }

    #[test]
    fn responds_to_search_results() {
        let mut exec = fasync::TestExecutor::new().expect("executor should build");
        let (proxy, mut profile_stream) = create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Profile proxy should be created");

        let mut profile = ProfileClient::new(proxy);

        let search_attrs = &[bredr::ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST];

        let source_uuid = bredr::ServiceClassProfileIdentifier::AudioSource;
        profile.add_search(source_uuid, search_attrs).expect("adding search succeeds");

        let sink_uuid = bredr::ServiceClassProfileIdentifier::AudioSink;
        profile.add_search(sink_uuid, search_attrs).expect("adding search succeeds");

        // Get the search clients out
        let source_results_proxy =
            expect_search_registration(&mut exec, &mut profile_stream, source_uuid, search_attrs);
        let sink_results_proxy =
            expect_search_registration(&mut exec, &mut profile_stream, sink_uuid, search_attrs);

        // Send a search request, process the request (by polling event stream) and confirm it responds.

        // Report a search result, which should be replied to.
        let mut attributes = vec![];
        let found_peer_id = PeerId(1);
        let results_fut = source_results_proxy.service_found(
            &mut found_peer_id.into(),
            None,
            &mut attributes.iter_mut(),
        );
        pin_mut!(results_fut);

        match exec.run_until_stalled(&mut profile.next()) {
            Poll::Ready(Some(Ok(ProfileEvent::SearchResult { id, .. }))) => {
                assert_eq!(found_peer_id, id);
            }
            x => panic!("Expected search result to be ready: {:?}", x),
        }

        match exec.run_until_stalled(&mut results_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Expected a response from the source result, got {:?}", x),
        };

        let results_fut = sink_results_proxy.service_found(
            &mut found_peer_id.into(),
            None,
            &mut attributes.iter_mut(),
        );
        pin_mut!(results_fut);

        match exec.run_until_stalled(&mut profile.next()) {
            Poll::Ready(Some(Ok(ProfileEvent::SearchResult { id, .. }))) => {
                assert_eq!(found_peer_id, id);
            }
            x => panic!("Expected search result to be ready: {:?}", x),
        }

        match exec.run_until_stalled(&mut results_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Expected a response from the sink result, got {:?}", x),
        };

        // Stream should error and terminate when one of the result streams is disconnected.
        drop(source_results_proxy);

        match exec.run_until_stalled(&mut profile.next()) {
            Poll::Ready(None) => {}
            x => panic!("Expected profile to end on search result drop, got {:?}", x),
        };

        assert!(profile.is_terminated());

        // Adding a search after termination should fail.
        assert!(profile.add_search(sink_uuid, &[]).is_err());
    }

    #[test]
    fn waker_gets_awoken_when_search_added() {
        let mut exec = fasync::TestExecutor::new().expect("executor should build");
        let (proxy, mut profile_stream) = create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Profile proxy should be created");

        let mut profile = ProfileClient::new(proxy);

        // Polling the ProfileClient stream before any poll targets have been added should save
        // a waker to be awoken when a new search is added.
        let profile_fut = profile.next();

        let (waker, profile_fut_wake_count) = new_count_waker();
        let mut counting_ctx = Context::from_waker(&waker);

        pin_mut!(profile_fut);
        assert!(profile_fut.poll(&mut counting_ctx).is_pending());

        // Since there are no poll targets, save the initial count. We expect this count
        // to change when a new poll target is added.
        let initial_count = profile_fut_wake_count.get();

        // Adding a search should be OK. We expect to get the search request and the
        // waker should be awoken.
        let source_uuid = bredr::ServiceClassProfileIdentifier::AudioSource;
        profile.add_search(source_uuid, &[]).expect("adding search succeeds");
        let search_proxy =
            expect_search_registration(&mut exec, &mut profile_stream, source_uuid, &[]);

        // Since we've added a search, we expect the wake count to increase by one.
        let after_search_count = profile_fut_wake_count.get();
        assert_eq!(after_search_count, initial_count + 1);

        // Reporting a search result should work as intended. The stream should produce an event.
        let mut attributes = vec![];
        let found_peer_id = PeerId(123);
        let results_fut =
            search_proxy.service_found(&mut found_peer_id.into(), None, &mut attributes.iter_mut());
        pin_mut!(results_fut);

        match exec.run_until_stalled(&mut profile.next()) {
            Poll::Ready(Some(Ok(ProfileEvent::SearchResult { id, .. }))) => {
                assert_eq!(found_peer_id, id);
            }
            x => panic!("Expected search result to be ready: {:?}", x),
        }

        match exec.run_until_stalled(&mut results_fut) {
            Poll::Ready(Ok(())) => {}
            x => panic!("Expected a response from the source result, got {:?}", x),
        };
    }
}
