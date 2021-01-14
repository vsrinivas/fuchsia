// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::{client::QueryResponseFut, endpoints::create_request_stream},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::types::{Channel, PeerId},
    futures::{
        stream::{FusedStream, Stream, StreamExt},
        task::{Context, Poll},
        FutureExt,
    },
    log::trace,
    std::{
        convert::{TryFrom, TryInto},
        pin::Pin,
    },
    thiserror::Error,
};

#[derive(Error, Debug)]
pub enum Error {
    /// A search result stream produced an error.
    /// `result` is Ok(()) if a result finished early.
    #[error("Search Result stream ended in error: {:?}", source)]
    SearchResult { source: Box<dyn std::error::Error + Send + Sync> },
    /// A connection receiver finished with an error.
    #[error("Connection Receiver stream ended in error: {:?}", source)]
    ConnectionReceiver { source: Box<dyn std::error::Error + Send + Sync> },
    /// The services advertised were completed unexpectedly. `result` contains the response from
    /// the profile advertise call.
    #[error("Advertisement ended prematurely: {:?}", result)]
    Advertisement { result: bredr::ProfileAdvertiseResult },
    /// Another FIDL error has occurred.
    #[error("FIDL Error occurred: {:?}", source)]
    Fidl { source: fidl::Error },
}

impl Error {
    fn connection_receiver<E>(e: E) -> Self
    where
        E: Into<Box<dyn std::error::Error + Send + Sync>>,
    {
        Self::ConnectionReceiver { source: e.into() }
    }

    fn search_result<E>(e: E) -> Self
    where
        E: Into<Box<dyn std::error::Error + Send + Sync>>,
    {
        Self::SearchResult { source: e.into() }
    }
}

impl From<fidl::Error> for Error {
    fn from(source: fidl::Error) -> Self {
        Error::Fidl { source }
    }
}

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
    pub fn peer_id(&self) -> &PeerId {
        match self {
            Self::PeerConnected { id, .. } => id,
            Self::SearchResult { id, .. } => id,
        }
    }
}

impl TryFrom<bredr::SearchResultsRequest> for ProfileEvent {
    type Error = Error;
    fn try_from(value: bredr::SearchResultsRequest) -> Result<Self, Self::Error> {
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
    fn try_from(value: bredr::ConnectionReceiverRequest) -> Result<Self, Self::Error> {
        let bredr::ConnectionReceiverRequest::Connected { peer_id, channel, protocol, .. } = value;
        let id = peer_id.into();
        let channel = channel.try_into().map_err(Error::connection_receiver)?;
        trace!("Connection from {:?} - protocol {:?}", id, protocol);
        Ok(ProfileEvent::PeerConnected { id, channel, protocol })
    }
}

/// Interacts with the fuchsia.bluetooth.bredr.Profile service by advertising a set of service
/// definitions, and registers a set of service searches.
pub struct ProfileClient {
    /// The proxy that is used to start new searches and advertise.
    proxy: bredr::ProfileProxy,
    /// The result for the advertisement. Terminates when the advertisement has completed.
    advertisement: Option<QueryResponseFut<bredr::ProfileAdvertiseResult>>,
    connection_receiver: Option<bredr::ConnectionReceiverRequestStream>,
    /// The registered results from the search streams. Polled in order.
    searches: Vec<bredr::SearchResultsRequestStream>,
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
            terminated: false,
        }
    }

    /// Create a new Profile that advertises the services in `services`.
    /// Incoming connections will request the `channel mode` provided.
    pub fn advertise(
        proxy: bredr::ProfileProxy,
        services: &[bredr::ServiceDefinition],
        channel_mode: bredr::ChannelMode,
    ) -> Result<Self, Error> {
        if services.is_empty() {
            return Ok(Self::new(proxy));
        }
        let (connect_client, connection_receiver) = create_request_stream()?;
        let advertisement = proxy
            .advertise(
                &mut services.into_iter().cloned(),
                bredr::ChannelParameters {
                    channel_mode: Some(channel_mode),
                    ..bredr::ChannelParameters::EMPTY
                },
                connect_client,
            )
            .check()?;
        Ok(Self {
            proxy,
            advertisement: Some(advertisement),
            connection_receiver: Some(connection_receiver),
            searches: Vec::new(),
            terminated: false,
        })
    }

    pub fn add_search(
        &mut self,
        service_uuid: bredr::ServiceClassProfileIdentifier,
        attributes: &[u16],
    ) -> Result<(), Error> {
        let (results_client, results_stream) = create_request_stream()?;
        self.proxy.search(service_uuid, attributes, results_client)?;
        self.searches.push(results_stream);
        Ok(())
    }
}

impl FusedStream for ProfileClient {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

impl Stream for ProfileClient {
    type Item = Result<ProfileEvent, Error>;

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
        Poll::Pending
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::create_proxy_and_stream;
    use fuchsia_async as fasync;
    use fuchsia_bluetooth::types::Uuid;
    use futures::pin_mut;

    use crate::make_profile_service_definition;

    #[test]
    fn test_advertises_and_ends_when_advertisement_ends() {
        let mut exec = fasync::Executor::new().expect("executor should build");
        let (proxy, mut profile_stream) = create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Profile proxy should be created");

        let source_uuid = Uuid::new16(bredr::ServiceClassProfileIdentifier::AudioSource as u16);
        let defs = &[make_profile_service_definition(source_uuid)];

        let mut profile = ProfileClient::advertise(proxy, defs, bredr::ChannelMode::Basic)
            .expect("Advertise succeeds");

        let (_connect_proxy, adv_responder) =
            match exec.run_until_stalled(&mut profile_stream.next()) {
                Poll::Ready(Some(Ok(bredr::ProfileRequest::Advertise {
                    services,
                    parameters,
                    receiver,
                    responder,
                }))) => {
                    assert_eq!(&services[..], defs);
                    assert_eq!(Some(bredr::ChannelMode::Basic), parameters.channel_mode);
                    (receiver.into_proxy().expect("proxy for connection receiver"), responder)
                }
                x => panic!("Expected ready advertisement request, got {:?}", x),
            };

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

    fn expect_search_registration(
        exec: &mut fasync::Executor,
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
    fn test_responds_to_search_results() {
        let mut exec = fasync::Executor::new().expect("executor should build");
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
    }
}
