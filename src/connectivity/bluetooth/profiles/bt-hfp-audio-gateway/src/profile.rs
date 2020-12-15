// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl::{client::QueryResponseFut, encoding::Decodable, endpoints::create_request_stream},
    fidl_fuchsia_bluetooth_bredr::{
        ConnectionReceiverRequestStream, ProfileAdvertiseResult, ProfileMarker, ProfileProxy,
        SearchResultsRequestStream, ServiceClassProfileIdentifier, ServiceDefinition,
    },
};

use crate::{config::AudioGatewayFeatureSupport, service_definitions};

/// Represents a BR/EDR based Bluetooth HFP profile which is advertising as a specific role and
/// searching for the complimentary role.
pub struct Profile {
    _proxy: ProfileProxy,
    _advertisement: QueryResponseFut<ProfileAdvertiseResult>,
    _connect_requests: ConnectionReceiverRequestStream,
    _search_results: SearchResultsRequestStream,
}

impl Profile {
    /// Register as an Audio Gateway using the provided feature support config. Constructing and
    /// returning a `Profile` on success.
    pub async fn register_audio_gateway(
        features: AudioGatewayFeatureSupport,
    ) -> Result<Self, Error> {
        let proxy = fuchsia_component::client::connect_to_service::<ProfileMarker>()
            .context("Failed to connect to Bluetooth Profile service")?;
        Self::register(
            proxy,
            service_definitions::audio_gateway(features),
            ServiceClassProfileIdentifier::Handsfree,
        )
        .await
    }

    /// Advertise `service` and search for `search_id` using `proxy`.
    async fn register(
        proxy: ProfileProxy,
        service: ServiceDefinition,
        search_id: ServiceClassProfileIdentifier,
    ) -> Result<Self, Error> {
        let (results_client, _search_results) =
            create_request_stream().context("SearchResults creation")?;
        proxy.search(search_id, &[], results_client)?;

        let (connect_client, _connect_requests) =
            create_request_stream().context("ConnectionReceiver creation")?;
        let _advertisement = proxy
            .advertise(&mut vec![service].into_iter(), Decodable::new_empty(), connect_client)
            .check()
            .context("Advertise request")?;

        Ok(Self { _proxy: proxy, _advertisement, _connect_requests, _search_results })
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_bluetooth_bredr::ProfileRequest, fuchsia_async as fasync,
        futures::StreamExt,
    };

    #[fasync::run_singlethreaded(test)]
    async fn registration_causes_advertisement_and_search_request() {
        let (proxy, mut request_stream) =
            fidl::endpoints::create_proxy_and_stream::<ProfileMarker>()
                .expect("Create new profile connection");

        let profile_server = fasync::Task::local(async move {
            // Exactly one advertise request and one search request must be registered.
            let mut valid_advert_request_received_count = 0;
            let mut valid_search_request_received_count = 0;

            while let Some(request) = request_stream.next().await {
                match request {
                    Ok(ProfileRequest::Advertise { .. }) => {
                        valid_advert_request_received_count += 1
                    }
                    Ok(ProfileRequest::Search { .. }) => valid_search_request_received_count += 1,
                    _ => panic!("unexpected result on profile request stream: {:?}", request),
                }
            }

            assert_eq!(valid_advert_request_received_count, 1);
            assert_eq!(valid_search_request_received_count, 1);
        });

        let features = Default::default();
        let profile = Profile::register(
            proxy,
            service_definitions::audio_gateway(features),
            ServiceClassProfileIdentifier::Handsfree,
        )
        .await
        .expect("register profile");

        // Close the profile client handle and wait for the server task to complete.
        drop(profile);
        profile_server.await;
    }
}
