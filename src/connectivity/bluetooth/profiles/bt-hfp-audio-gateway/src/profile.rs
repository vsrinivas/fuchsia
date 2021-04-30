// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context as _,
    core::{
        convert::TryInto,
        pin::Pin,
        task::{Context, Poll},
    },
    fidl::{client::QueryResponseFut, encoding::Decodable, endpoints::create_request_stream},
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::{
        error::Error as BtError,
        profile::{Attribute, ProtocolDescriptor},
        types::{Channel, PeerId},
    },
    futures::{
        future::{FusedFuture, FutureExt},
        stream::{FusedStream, Stream, StreamExt},
    },
    log::trace,
};

use crate::{
    config::AudioGatewayFeatureSupport,
    error::{AdvertisementTerminated, Error},
    service_definitions,
};

/// Possible items produced by `Profile` when used as a `Stream`.
#[derive(Debug)]
pub enum ProfileEvent {
    /// A Bluetooth peer has requested a new HFP connection with the specified protocol
    /// descriptor information.
    ConnectionRequest { id: PeerId, protocol: Vec<ProtocolDescriptor>, channel: Channel },
    /// A Bluetooth peer has SDP records that meet the Profile's search for HFP Handsfree devices.
    SearchResult {
        id: PeerId,
        protocol: Option<Vec<ProtocolDescriptor>>,
        attributes: Vec<Attribute>,
    },
}

impl ProfileEvent {
    /// Return the PeerId associated with this event.
    pub fn peer_id(&self) -> PeerId {
        match self {
            Self::ConnectionRequest { id, .. } => *id,
            Self::SearchResult { id, .. } => *id,
        }
    }
}

/// Represents a BR/EDR based Bluetooth HFP profile which is advertising as a specific role and
/// searching for the complimentary role.
///
/// `Profile` implements the `Stream` trait and yields `ProfileEvent` items. Errors are yielded
/// when `Profile` fails to register a search request, fails to register an advertisement, when the
/// advertisement is terminated, or any channel to the bredr service returns an error.
pub struct Profile {
    proxy: bredr::ProfileProxy,
    advertisement: QueryResponseFut<bredr::ProfileAdvertiseResult>,
    connect_requests: bredr::ConnectionReceiverRequestStream,
    search_results: bredr::SearchResultsRequestStream,
    terminated: bool,
}

impl Profile {
    /// Register as an Audio Gateway using the provided config. Constructing and returning
    /// a `Profile` on success.
    pub fn register_audio_gateway(features: AudioGatewayFeatureSupport) -> anyhow::Result<Self> {
        let proxy = fuchsia_component::client::connect_to_protocol::<bredr::ProfileMarker>()
            .context("Failed to connect to Bluetooth Profile service")?;
        Self::register(
            proxy,
            service_definitions::audio_gateway(features),
            bredr::ServiceClassProfileIdentifier::Handsfree,
        )
    }

    /// Return a clone of the associated `ProfileProxy`.
    pub fn proxy(&self) -> bredr::ProfileProxy {
        self.proxy.clone()
    }

    /// Advertise `service` and search for `search_id` using `proxy`.
    fn register(
        proxy: bredr::ProfileProxy,
        service: bredr::ServiceDefinition,
        search_id: bredr::ServiceClassProfileIdentifier,
    ) -> anyhow::Result<Self> {
        let (results_client, search_results) =
            create_request_stream().context("SearchResults creation")?;
        proxy.search(search_id, &[], results_client)?;

        let (connect_client, connect_requests) =
            create_request_stream().context("ConnectionReceiver creation")?;
        let advertisement = proxy
            .advertise(&mut vec![service].into_iter(), Decodable::new_empty(), connect_client)
            .check()
            .context("Advertise request")?;

        Ok(Self { proxy, advertisement, connect_requests, search_results, terminated: false })
    }

    fn handle_connection_request(
        request: bredr::ConnectionReceiverRequest,
    ) -> Result<ProfileEvent, Error> {
        let bredr::ConnectionReceiverRequest::Connected { peer_id, channel, protocol, .. } =
            request;
        let id: PeerId = peer_id.into();
        let channel: Channel = channel.try_into().map_err(Error::profile_connection_receiver)?;
        let protocol: Vec<ProtocolDescriptor> =
            protocol.iter().map(ProtocolDescriptor::from).collect();
        trace!("Connection Request from {:?} - protocol {:#?}", peer_id, protocol);
        // TODO (fxbug.dev/66592): Validate protocol
        Ok(ProfileEvent::ConnectionRequest { id, channel, protocol })
    }

    fn handle_search_result(request: bredr::SearchResultsRequest) -> Result<ProfileEvent, Error> {
        let bredr::SearchResultsRequest::ServiceFound { peer_id, protocol, attributes, responder } =
            request;
        let id: PeerId = peer_id.into();
        responder.send().map_err(Error::profile_search_results)?;
        let protocol = protocol.map(|p| p.iter().map(ProtocolDescriptor::from).collect());
        let attributes = attributes.iter().map(Attribute::from).collect();
        trace!("Discovered {:?} - protocol {:#?}, attributes {:#?}", peer_id, protocol, attributes);
        Ok(ProfileEvent::SearchResult { id, protocol, attributes })
    }

    /// Check whether the stream should be terminated, marking it terminated if necessary and
    /// returning the termination state.
    fn check_for_stream_termination(&mut self) -> bool {
        if self.connect_requests.is_terminated()
            || self.search_results.is_terminated()
            || self.advertisement.is_terminated()
        {
            self.terminated = true;
        }
        self.terminated
    }
}

impl Stream for Profile {
    type Item = Result<ProfileEvent, Error>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        // A stream must never be polled after it has been terminated.
        if self.terminated {
            panic!("Polling Profile after it has terminated");
        }

        // Check if any of the underlying streams or futures were marked terminated by the previous
        // call to Profile::poll_next.
        if self.check_for_stream_termination() {
            return Poll::Ready(None);
        }

        // Check to see if the advertisement has been removed. The stream returns an error and sets
        // itself terminated in this situation. It should be up to the caller decide what to do in
        // this case.
        if let Poll::Ready(advertise_result) = self.advertisement.poll_unpin(cx) {
            let error = match advertise_result {
                Ok(Ok(())) => Error::profile_advertise(AdvertisementTerminated),
                Ok(Err(e)) => Error::profile_advertise(BtError::from(e)),
                Err(e) => Error::profile_advertise(e),
            };
            return Poll::Ready(Some(Err(error)));
        }

        // Check to see if there are connection requests available via the BR/EDR
        // ConnectionRequestStream.
        if let Poll::Ready(r) = self.connect_requests.poll_next_unpin(cx) {
            return Poll::Ready(r.map(|r| {
                r.map_err(Error::profile_connection_receiver)
                    .and_then(Self::handle_connection_request)
            }));
        }

        // Check to see if there are search results available via the BR/EDR
        // SearchResultsRequestStream.
        if let Poll::Ready(r) = self.search_results.poll_next_unpin(cx) {
            return Poll::Ready(r.map(|r| {
                r.map_err(Error::profile_search_results).and_then(Self::handle_search_result)
            }));
        }

        Poll::Pending
    }
}

impl FusedStream for Profile {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

#[cfg(test)]
pub(crate) mod test_server {
    use {super::*, fidl_fuchsia_bluetooth_bredr as bredr};

    /// Register a new Profile object, and create an associated test server.
    pub(crate) fn setup_profile_and_test_server() -> (Profile, LocalProfileTestServer) {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Create new profile connection");

        let profile = Profile::register(
            proxy,
            service_definitions::audio_gateway(Default::default()),
            bredr::ServiceClassProfileIdentifier::Handsfree,
        )
        .expect("register profile");
        (profile, stream.into())
    }

    /// Holds all the server side resources associated with a `Profile`'s connection to
    /// fuchsia.bluetooth.bredr.Profile. Provides helper methods for common test related tasks.
    /// Some fields are optional because they are not populated until the Profile has completed
    /// registration.
    pub(crate) struct LocalProfileTestServer {
        pub stream: bredr::ProfileRequestStream,
        pub responder: Option<bredr::ProfileAdvertiseResponder>,
        pub receiver: Option<bredr::ConnectionReceiverProxy>,
        pub results: Option<bredr::SearchResultsProxy>,
    }

    impl From<bredr::ProfileRequestStream> for LocalProfileTestServer {
        fn from(stream: bredr::ProfileRequestStream) -> Self {
            Self { stream, responder: None, receiver: None, results: None }
        }
    }

    impl LocalProfileTestServer {
        pub fn receiver(&self) -> &bredr::ConnectionReceiverProxy {
            self.receiver.as_ref().expect("receiver to be present")
        }

        pub fn results(&self) -> &bredr::SearchResultsProxy {
            self.results.as_ref().expect("results to be present")
        }

        /// Returns true if the `Profile` has registered an `Advertise` and `Search` request.
        fn is_registration_complete(&self) -> bool {
            self.responder.is_some() && self.receiver.is_some() && self.results.is_some()
        }

        /// Run through the registration process of a new `Profile`.
        pub async fn complete_registration(&mut self) {
            while let Some(request) = self.stream.next().await {
                match request {
                    Ok(bredr::ProfileRequest::Advertise { receiver, responder, .. }) => {
                        if self.is_registration_complete() {
                            panic!("unexpected second advertise request");
                        }
                        self.responder = Some(responder);
                        self.receiver = Some(receiver.into_proxy().unwrap());
                        if self.is_registration_complete() {
                            break;
                        }
                    }
                    Ok(bredr::ProfileRequest::Search { results, .. }) => {
                        if self.is_registration_complete() {
                            panic!("unexpected second search request");
                        }
                        self.results = Some(results.into_proxy().unwrap());
                        if self.is_registration_complete() {
                            break;
                        }
                    }
                    _ => panic!("unexpected result on profile request stream: {:?}", request),
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::error::{AdvertisementTerminated, ProfileResource};
    use {
        fidl_fuchsia_bluetooth as bt, fuchsia_async as fasync, futures::StreamExt,
        matches::assert_matches,
    };

    #[fasync::run_singlethreaded(test)]
    async fn registration_causes_advertisement_and_search_request() {
        let (_profile, mut server) = test_server::setup_profile_and_test_server();

        let server = fasync::Task::local(async move {
            server.complete_registration().await;
            assert!(server.responder.is_some());
            assert!(server.receiver.is_some());
            assert!(server.results.is_some());
        });

        server.await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn stream_returns_events() {
        let (mut profile, mut server) = test_server::setup_profile_and_test_server();

        let _server = fasync::Task::local(async move {
            // First, register the profile.
            server.complete_registration().await;

            // Second, send a search results event.
            server
                .results()
                .service_found(&mut bt::PeerId { value: 1 }, None, &mut vec![].iter_mut())
                .await
                .expect("successful request to search results receiver");

            // Third, send a connection request event.
            let (_left, right) = fuchsia_bluetooth::types::Channel::create();
            server
                .receiver()
                .connected(
                    &mut bt::PeerId { value: 1 },
                    right.try_into().expect("valid channel"),
                    &mut vec![].iter_mut(),
                )
                .expect("successful request to connection receiver");

            // This will be pending forever since the server does not expect any further messages in
            // this test.
            server.stream.next().await;
        });

        // The profile server task first sends a search result.
        let event = profile.next().await;
        assert_matches!(event, Some(Ok(ProfileEvent::SearchResult { .. })));

        // The profile server task then sends a connection request.
        let event = profile.next().await;
        assert_matches!(event, Some(Ok(ProfileEvent::ConnectionRequest { .. })));

        // Profile is waiting on futher events.
        let event = profile.next().now_or_never();
        assert_matches!(event, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn stream_end_of_advertise_returns_error() {
        let (mut profile, mut server) = test_server::setup_profile_and_test_server();

        let _server = fasync::Task::local(async move {
            server.complete_registration().await;
            server.responder.take().expect("responder to be present").send(&mut Ok(())).unwrap();
        });

        // The profile server task then sends a connection request.
        let event = profile.next().await;
        if let Some(Err(Error::ProfileResourceError {
            resource: ProfileResource::Advertise,
            source,
        })) = event
        {
            assert!(
                source.is::<AdvertisementTerminated>(),
                "unexpected error polling Profile Stream"
            );
        } else {
            panic!("unexpected result from Profile stream: {:?}", event);
        }

        // After an error, the stream will return None and terminate.
        let event = profile.next().await;
        assert_matches!(event, None);

        assert!(profile.is_terminated());
    }

    #[fasync::run_singlethreaded(test)]
    async fn stream_advertise_error_returns_error() {
        let (mut profile, mut server) = test_server::setup_profile_and_test_server();

        let _server = fasync::Task::local(async move {
            server.complete_registration().await;
            server
                .responder
                .take()
                .expect("responder to be present")
                .send(&mut Err(bt::ErrorCode::Already))
                .unwrap();
        });

        // The profile server task then sends a connection request.
        let event = profile.next().await;
        if let Some(Err(Error::ProfileResourceError {
            resource: ProfileResource::Advertise,
            source,
        })) = event
        {
            assert!(source.is::<BtError>(), "unexpected error polling Profile Stream");
        } else {
            panic!("unexpected result from Profile stream: {:?}", event);
        }

        // After an error, the stream will return None and terminate.
        let event = profile.next().await;
        assert_matches!(event, None);

        assert!(profile.is_terminated());
    }

    #[fasync::run_singlethreaded(test)]
    async fn stream_terminates_on_server_disconnect() {
        let (mut profile, mut server) = test_server::setup_profile_and_test_server();

        let server = fasync::Task::local(async move {
            server.complete_registration().await;

            // Send an event to check that the profile is receiving events at this point
            server
                .results()
                .service_found(&mut bt::PeerId { value: 1 }, None, &mut vec![].iter_mut())
                .await
                .expect("successful request to search results receiver");
        });

        // The profile server task first sends a search result.
        let event = profile.next().await;
        assert_matches!(event, Some(Ok(ProfileEvent::SearchResult { .. })));

        assert!(!profile.is_terminated());

        drop(server);

        // The profile server task has been dropped so the event stream is closed and Profile
        // return an Error followed by None.
        let event = profile.next().await;
        assert_matches!(event, Some(Err(_)));

        assert!(!profile.is_terminated());

        let event = profile.next().await;
        assert_matches!(event, None);

        assert!(profile.is_terminated());
    }
}
