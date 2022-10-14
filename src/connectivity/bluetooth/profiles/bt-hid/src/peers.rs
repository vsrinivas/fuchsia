// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_utils::stream::FutureMap,
    fidl_fuchsia_bluetooth_bredr as bredr,
    fuchsia_bluetooth::types::PeerId,
    futures::StreamExt,
    profile_client::{ProfileClient, ProfileEvent},
    std::collections::hash_map::Entry,
    tracing::{debug, error, info},
};

use crate::peer_task::PeerTask;

/// Struct containing all known peers and the profile client.
pub struct Peers {
    profile_client: ProfileClient,
    profile_proxy: bredr::ProfileProxy,
    peers: FutureMap<PeerId, PeerTask>,
}

// TODO(fxb/83269) Clean up when a peer task finishes.
impl Peers {
    pub fn new(profile_client: ProfileClient, profile_proxy: bredr::ProfileProxy) -> Self {
        Self { profile_client, profile_proxy, peers: FutureMap::new() }
    }

    pub async fn run(&mut self) {
        loop {
            info!("In main loop awaiting profile events.");
            match self.profile_client.next().await {
                Some(Ok(event)) => {
                    info!("Received profile event: {:?}", event);
                    self.handle_profile_event(event).await;
                }
                Some(Err(e)) => {
                    error!("Error encountered wating for profile events: {}.", e);
                    break;
                }
                None => {
                    debug!("Profile event stream terminated.");
                    break;
                }
            }
        }
    }

    async fn handle_profile_event(&mut self, event: ProfileEvent) {
        let peer_id = event.peer_id();
        debug!("Handling profile event: {:?}", event);

        let task = self.make_task_if_nonexistent(peer_id);

        let send_result = task.handle_profile_event(event).await;
        if let Err(err) = send_result {
            // At this point we've just created a task but can't communicate with it.  This is an unrecoverable error.
            error!("Unable to send profile event to peer {:} with error {:?}", peer_id, err);
            let _ = self.peers.remove(&peer_id);
        }
    }

    fn make_task_if_nonexistent(&mut self, peer_id: PeerId) -> &mut PeerTask {
        match self.peers.inner().entry(peer_id) {
            Entry::Occupied(task_entry) => task_entry.into_mut(),
            Entry::Vacant(task_entry) => {
                let task = PeerTask::spawn_new(peer_id, self.profile_proxy.clone());
                task_entry.insert(Box::pin(task))
            }
        }
    }

    #[cfg(test)]
    fn get_peer_map(&mut self) -> &mut FutureMap<PeerId, PeerTask> {
        &mut self.peers
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        async_test_helpers::run_while, async_utils::PollExt, fuchsia_async as fasync,
        futures::pin_mut,
    };

    #[fuchsia::test]
    fn search_result_creates_peer_task() {
        // Set up peers.
        let mut exec = fasync::TestExecutor::new().unwrap();
        let (proxy, client, mut server) = test_profile_server::setup_profile_and_test_server();
        let mut peers = Peers::new(client, proxy);

        assert_eq!(peers.get_peer_map().inner().len(), 0);

        // A service found event causes a task to be added to the peers map.
        {
            let peers_fut = peers.run();
            pin_mut!(peers_fut);

            run_while(&mut exec, &mut peers_fut, server.complete_registration());
            run_while(&mut exec, &mut peers_fut, server.service_found(PeerId(1), None, Vec::new()));

            exec.run_until_stalled(&mut peers_fut).expect_pending("Handling connected first 1");
        }
        assert_eq!(peers.get_peer_map().inner().len(), 1);

        // A second service found event for the same peer doesn't add a new task to the peers map.
        {
            let peers_fut = peers.run();
            pin_mut!(peers_fut);

            run_while(&mut exec, &mut peers_fut, server.service_found(PeerId(1), None, Vec::new()));

            exec.run_until_stalled(&mut peers_fut).expect_pending("Handling connected second 1");
        }
        assert_eq!(peers.get_peer_map().inner().len(), 1);

        // A service found event for a second peer causes a task to be added to the peers map.
        {
            let peers_fut = peers.run();
            pin_mut!(peers_fut);

            run_while(&mut exec, &mut peers_fut, server.service_found(PeerId(2), None, Vec::new()));

            exec.run_until_stalled(&mut peers_fut).expect_pending("Handling connected 2");
        }
        assert_eq!(peers.get_peer_map().inner().len(), 2);
    }
}

// TODO(84459) Move this to a common location for all BT profiles.
#[cfg(test)]
pub(crate) mod test_profile_server {
    use fidl;
    use {super::*, fidl_fuchsia_bluetooth_bredr as bredr, futures::StreamExt};

    /// Register a new Profile object, and create an associated test server.
    pub(crate) fn setup_profile_and_test_server(
    ) -> (bredr::ProfileProxy, ProfileClient, LocalProfileTestServer) {
        let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<bredr::ProfileMarker>()
            .expect("Create new profile connection");

        // Create a profile that does no advertising.
        let mut client = ProfileClient::new(proxy.clone());

        // Register a search for remote peers that support BR/EDR HID.
        client
            .add_search(bredr::ServiceClassProfileIdentifier::HumanInterfaceDevice, &[])
            .expect("Failed to search for peers.");

        (proxy, client, stream.into())
    }

    /// Holds all the server side resources associated with a `Profile`'s connection to
    /// fuchsia.bluetooth.bredr.Profile. Provides helper methods for common test related tasks.
    /// Some fields are optional because they are not populated until the Profile has completed
    /// registration.
    pub(crate) struct LocalProfileTestServer {
        profile_request_stream: bredr::ProfileRequestStream,
        search_results: Option<bredr::SearchResultsProxy>,
    }

    impl From<bredr::ProfileRequestStream> for LocalProfileTestServer {
        fn from(profile_request_stream: bredr::ProfileRequestStream) -> Self {
            Self { profile_request_stream, search_results: None }
        }
    }

    impl LocalProfileTestServer {
        /// Run through the registration process of a new `Profile`.
        pub async fn complete_registration(&mut self) {
            let request = self.profile_request_stream.next().await;
            match request {
                Some(Ok(bredr::ProfileRequest::Search { results, .. })) => {
                    self.search_results = Some(results.into_proxy().unwrap());
                }
                _ => panic!("unexpected result on profile request stream: {:?}", request),
            }
        }

        pub async fn service_found(
            &mut self,
            peer_id: PeerId,
            mut protocol: Option<Vec<bredr::ProtocolDescriptor>>,
            mut attributes: Vec<bredr::Attribute>,
        ) {
            let mut protocol = protocol.as_mut().map(|v| v.iter_mut());
            let protocol = protocol
                .as_mut()
                .map(|i| i as &mut dyn ExactSizeIterator<Item = &mut bredr::ProtocolDescriptor>);
            self.search_results
                .as_ref()
                .expect("Search result proxy")
                .service_found(&mut (peer_id).into(), protocol, &mut attributes.iter_mut())
                .await
                .expect("Service found");
        }
    }
}
