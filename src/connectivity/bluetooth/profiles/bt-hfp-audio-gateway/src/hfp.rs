// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{select, stream::StreamExt};

use crate::{
    call_manager::{CallManager, CallManagerEvent},
    error::Error,
    profile::{Profile, ProfileEvent},
};

/// Manages operation of the HFP functionality.
pub struct Hfp {
    /// The `profile` provides Hfp with a means to drive the fuchsia.bluetooth.bredr related APIs.
    profile: Profile,
    /// The `call_manager` provides Hfp with a means to interact with clients of the
    /// fuchsia.bluetooth.hfp.Hfp and fuchsia.bluetooth.hfp.CallManager protocols.
    call_manager: CallManager,
}

impl Hfp {
    /// Create a new `Hfp` with the provided `profile`.
    pub fn new(profile: Profile, call_manager: CallManager) -> Self {
        Self { profile, call_manager }
    }

    /// Run the Hfp object to completion. Runs until an unrecoverable error occurs or there is no
    /// more work to perform because all managed resource have been closed.
    pub async fn run(mut self) -> Result<(), Error> {
        loop {
            select! {
                event = self.profile.select_next_some() => {
                    self.handle_profile_event(event?).await;
                }
                event = self.call_manager.select_next_some() => {
                    self.handle_call_manager_event(event).await;
                }
            }
        }
    }

    /// Handle a single `ProfileEvent` from `profile`.
    async fn handle_profile_event(&mut self, _event: ProfileEvent) {
        unimplemented!("profile event handler")
    }

    /// Handle a single `CallManagerEvent` from `call_manager`.
    async fn handle_call_manager_event(&mut self, _event: CallManagerEvent) {
        unimplemented!("call service event handler")
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::profile::test_server::setup_profile_and_test_server,
        fidl_fuchsia_bluetooth as bt, fidl_fuchsia_bluetooth_hfp::CallManagerMarker,
        fuchsia_async as fasync,
    };

    #[fasync::run_until_stalled(test)]
    #[should_panic(expected = "not implemented: profile event handler")]
    async fn new_profile_event_calls_handler() {
        let (profile, mut server) = setup_profile_and_test_server();
        let _server_task = fasync::Task::local(async move {
            server.complete_registration().await;
            server
                .results
                .as_ref()
                .unwrap()
                .service_found(&mut bt::PeerId { value: 1 }, None, &mut vec![].iter_mut())
                .await
                .expect("service found to send");
        });

        let hfp = Hfp::new(profile, CallManager::new());
        hfp.run().await.expect("'not implemented' panic expected to occur before run completes");
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic(expected = "not implemented: call service event handler")]
    async fn new_call_manager_event_calls_handler() {
        let (profile, mut server) = setup_profile_and_test_server();
        let _server_task = fasync::Task::local(async move {
            server.complete_registration().await;
            // Profile server will wait indefinitely for a request that will not come during this
            // test.
            server.stream.next().await;
        });

        let mut call_manager = CallManager::new();
        let service_provider = call_manager.service_provider().unwrap();
        let (_client_end, stream) =
            fidl::endpoints::create_request_stream::<CallManagerMarker>().unwrap();
        service_provider.register(stream).await;

        let hfp = Hfp::new(profile, call_manager);
        hfp.run().await.expect("'not implemented' panic expected to occur before run completes");
    }

    #[fasync::run_until_stalled(test)]
    async fn profile_error_propagates_error_from_hfp_run() {
        let (profile, server) = setup_profile_and_test_server();
        // dropping the server is expected to produce an error from Hfp::run
        drop(server);

        let hfp = Hfp::new(profile, CallManager::new());
        let result = hfp.run().await;
        assert!(result.is_err());
    }
}
