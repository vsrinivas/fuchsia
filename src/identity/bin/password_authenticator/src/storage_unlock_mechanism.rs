// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    fidl_fuchsia_identity_authentication::{
        AttemptedEvent, Enrollment, Error as ApiError, InteractionProtocolServerEnd,
        StorageUnlockMechanismRequest, StorageUnlockMechanismRequestStream,
    },
    futures::TryStreamExt,
    tracing::log::error,
};

type EnrollmentData = Vec<u8>;
type PrekeyMaterial = Vec<u8>;

pub struct StorageUnlockMechanism {}

impl StorageUnlockMechanism {
    pub fn new() -> Self {
        Self {}
    }

    /// Serially process a stream of incoming StorageUnlockMechanism FIDL requests.
    pub async fn handle_requests_for_storage_unlock_mechanism(
        &self,
        mut stream: StorageUnlockMechanismRequestStream,
    ) {
        // TODO(fxb/109121): Ensure that a client closing the channel does not cause all clients to fail.
        while let Some(request) = stream.try_next().await.expect("read request") {
            self.handle_storage_unlock_mechanism_request(request).unwrap_or_else(|e| {
                error!("error handling StorageUnlockMechanism fidl request: {:#}", anyhow!(e));
            })
        }
    }

    /// Process a single StorageUnlockMechanism FIDL request and send a reply.
    fn handle_storage_unlock_mechanism_request(
        &self,
        request: StorageUnlockMechanismRequest,
    ) -> Result<(), fidl::Error> {
        match request {
            StorageUnlockMechanismRequest::Authenticate { interaction, enrollments, responder } => {
                responder.send(&mut self.authenticate(interaction, enrollments))
            }
            StorageUnlockMechanismRequest::Enroll { interaction, responder } => {
                responder.send(&mut self.enroll(interaction))
            }
        }
    }

    fn authenticate(
        &self,
        _interaction: InteractionProtocolServerEnd,
        _enrollments: Vec<Enrollment>,
    ) -> Result<AttemptedEvent, ApiError> {
        unimplemented!()
    }

    fn enroll(
        &self,
        _interaction: InteractionProtocolServerEnd,
    ) -> Result<(EnrollmentData, PrekeyMaterial), ApiError> {
        unimplemented!()
    }
}

#[cfg(test)]
mod test {
    use {super::*, fidl_fuchsia_identity_authentication::TestInteractionMarker};

    #[fuchsia::test]
    #[should_panic(expected = "not implemented")]
    async fn test_autheticate_not_implemented() {
        let storage_unlock_mechanism = StorageUnlockMechanism::new();
        let (_, interaction_server) =
            fidl::endpoints::create_endpoints::<TestInteractionMarker>().unwrap();

        let _ = storage_unlock_mechanism
            .authenticate(InteractionProtocolServerEnd::Test(interaction_server), vec![]);
    }

    #[fuchsia::test]
    #[should_panic(expected = "not implemented")]
    async fn test_enroll_not_implemented() {
        let storage_unlock_mechanism = StorageUnlockMechanism::new();
        let (_, interaction_server) =
            fidl::endpoints::create_endpoints::<TestInteractionMarker>().unwrap();

        let _ =
            storage_unlock_mechanism.enroll(InteractionProtocolServerEnd::Test(interaction_server));
    }
}
