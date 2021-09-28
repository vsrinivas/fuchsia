// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Context as _, fidl_fuchsia_bluetooth_bredr as bredr, profile_client::ProfileClient};

use crate::{config::AudioGatewayFeatureSupport, service_definitions};

fn register(
    proxy: bredr::ProfileProxy,
    features: AudioGatewayFeatureSupport,
) -> anyhow::Result<ProfileClient> {
    // Register the service advertisement for the Audio Gateway role implemented by us.
    let service_definition = service_definitions::audio_gateway(features);
    let mut profile = ProfileClient::advertise(
        proxy,
        &mut vec![service_definition],
        bredr::ChannelParameters::EMPTY,
    )?;

    // Register a search for remote peers that support the Hands Free role.
    profile.add_search(bredr::ServiceClassProfileIdentifier::Handsfree, &[])?;

    Ok(profile)
}

/// Register the Audio Gateway profile. Returns a `ProfileClient` to interact
/// with the `bredr.Profile` and the ProfileProxy on success or Error on failure.
pub fn register_audio_gateway(
    features: AudioGatewayFeatureSupport,
) -> anyhow::Result<(ProfileClient, bredr::ProfileProxy)> {
    let proxy = fuchsia_component::client::connect_to_protocol::<bredr::ProfileMarker>()
        .context("Failed to connect to Bluetooth Profile service")?;
    let profile_client = register(proxy.clone(), features)?;
    Ok((profile_client, proxy))
}

#[cfg(test)]
pub(crate) mod test_server {
    use {super::*, fidl_fuchsia_bluetooth_bredr as bredr, futures::StreamExt};

    /// Register a new Profile object, and create an associated test server.
    pub(crate) fn setup_profile_and_test_server(
    ) -> (ProfileClient, bredr::ProfileProxy, LocalProfileTestServer) {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Create new profile connection");

        let profile = register(proxy.clone(), Default::default()).expect("register profile");
        (profile, proxy, stream.into())
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
        pub connections: Vec<fuchsia_zircon::Socket>,
    }

    impl From<bredr::ProfileRequestStream> for LocalProfileTestServer {
        fn from(stream: bredr::ProfileRequestStream) -> Self {
            Self { stream, responder: None, receiver: None, results: None, connections: vec![] }
        }
    }

    impl LocalProfileTestServer {
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
