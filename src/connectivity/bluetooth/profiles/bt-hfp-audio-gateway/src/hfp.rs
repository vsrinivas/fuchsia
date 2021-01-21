// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use futures::{select, stream::StreamExt};

use crate::{
    error::Error,
    profile::{Profile, ProfileEvent},
};

/// Manages operation of the HFP functionality.
pub struct Hfp {
    /// The `profile` provides Hfp with a means to drive the fuchsia.bluetooth.bredr related APIs.
    profile: Profile,
}

impl Hfp {
    /// Create a new `Hfp` with the provided `profile`.
    pub fn new(profile: Profile) -> Self {
        Self { profile }
    }

    /// Run the Hfp object to completion. Runs until an unrecoverable error occurs or there is no
    /// more work to perform because all managed resource have been closed.
    pub async fn run(mut self) -> Result<(), Error> {
        loop {
            select! {
                event = self.profile.select_next_some() => {
                    self.handle_profile_event(event?).await;
                }
            }
        }
    }

    /// Handle a single `ProfileEvent` from `profile`.
    async fn handle_profile_event(&mut self, _event: ProfileEvent) {
        unimplemented!("profile event handler")
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::profile::test_server::setup_profile_and_test_server,
        fidl_fuchsia_bluetooth as bt, fuchsia_async as fasync,
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

        let hfp = Hfp::new(profile);
        hfp.run().await.expect("'not implemented' panic expected to occur before run completes");
    }

    #[fasync::run_until_stalled(test)]
    async fn profile_error_propagates_error_from_hfp_run() {
        let (profile, server) = setup_profile_and_test_server();
        // dropping the server is expected to produce an error from Hfp::run
        drop(server);

        let hfp = Hfp::new(profile);
        let result = hfp.run().await;
        assert!(result.is_err());
    }
}
