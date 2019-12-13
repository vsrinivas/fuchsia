// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_identity_authentication::{
    AttemptedEvent, Enrollment, Error as ApiError, StorageUnlockMechanismRequest,
    StorageUnlockMechanismRequestStream,
};
use fuchsia_zircon::{ClockId, Time};
use futures::prelude::*;
use lazy_static::lazy_static;

type EnrollmentData = Vec<u8>;
type PrekeyMaterial = Vec<u8>;

lazy_static! {
    /// The enrollment data always reported by this authenticator.
    static ref FIXED_ENROLLMENT_DATA: Vec<u8> = vec![0, 1, 2];
    /// The prekey always reported by this authenticator.
    static ref FIXED_PREKEY: Vec<u8> = vec![34, 127, 0, 12];
}

/// A development-only implementation of the
/// fuchsia.identity.authentication.StorageUnlockMechanism fidl protocol
/// that always responds with fixed, successful responses.
pub struct StorageUnlockMechanism {}

impl StorageUnlockMechanism {
    /// Asynchronously handle fidl requests received on the provided stream.
    pub async fn handle_requests_from_stream(
        mut stream: StorageUnlockMechanismRequestStream,
    ) -> Result<(), fidl::Error> {
        while let Some(request) = stream.try_next().await? {
            Self::handle_request(request)?;
        }
        Ok(())
    }

    fn handle_request(request: StorageUnlockMechanismRequest) -> Result<(), fidl::Error> {
        match request {
            StorageUnlockMechanismRequest::Authenticate { enrollments, responder } => {
                responder.send(&mut Self::authenticate(enrollments))
            }
            StorageUnlockMechanismRequest::Enroll { responder } => {
                responder.send(&mut Self::enroll())
            }
        }
    }

    /// Implementation of `authenticate` fidl method.  Always takes the first enrollment
    /// and returns fixed prekey material.
    fn authenticate(enrollments: Vec<Enrollment>) -> Result<AttemptedEvent, ApiError> {
        let enrollment = enrollments.into_iter().next().ok_or(ApiError::InvalidRequest)?;
        let Enrollment { id, .. } = enrollment;
        Ok(AttemptedEvent {
            timestamp: Time::get(ClockId::UTC).into_nanos(),
            enrollment_id: id,
            updated_enrollment_data: None,
            prekey_material: FIXED_PREKEY.clone(),
        })
    }

    /// Implementation of `enroll` fidl method.  Always returns fixed enrollment data and
    /// prekey material.
    fn enroll() -> Result<(EnrollmentData, PrekeyMaterial), ApiError> {
        Ok((FIXED_ENROLLMENT_DATA.clone(), FIXED_PREKEY.clone()))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_identity_authentication::{
        StorageUnlockMechanismMarker, StorageUnlockMechanismProxy,
    };
    use fuchsia_async as fasync;
    use futures::future::join;

    const TEST_ENROLLMENT_ID: u64 = 0x42;
    const TEST_ENROLLMENT_ID_2: u64 = 0xabba;

    async fn run_proxy_test<Fn, Fut>(test_fn: Fn)
    where
        Fn: FnOnce(StorageUnlockMechanismProxy) -> Fut,
        Fut: Future<Output = Result<(), fidl::Error>>,
    {
        let (proxy, stream) = create_proxy_and_stream::<StorageUnlockMechanismMarker>().unwrap();
        let server_fut = StorageUnlockMechanism::handle_requests_from_stream(stream);
        let test_fut = test_fn(proxy);

        let (test_result, server_result) = join(test_fut, server_fut).await;
        assert!(test_result.is_ok());
        assert!(server_result.is_ok());
    }

    #[fasync::run_until_stalled(test)]
    async fn test_enroll_authenticate_produce_same_prekey() {
        run_proxy_test(|proxy| {
            async move {
                let (enrollment_data, enrollment_prekey) = proxy.enroll().await?.unwrap();

                let enrollment =
                    Enrollment { id: TEST_ENROLLMENT_ID, data: enrollment_data.clone() };

                let AttemptedEvent {
                    enrollment_id, updated_enrollment_data, prekey_material, ..
                } = proxy.authenticate(&mut vec![enrollment].iter_mut()).await?.unwrap();

                assert_eq!(enrollment_id, TEST_ENROLLMENT_ID);
                assert!(updated_enrollment_data.is_none());
                assert_eq!(prekey_material, enrollment_prekey);
                Ok(())
            }
        })
        .await
    }

    #[fasync::run_until_stalled(test)]
    async fn test_authenticate_multiple_enrollments() {
        run_proxy_test(|proxy| {
            async move {
                let enrollment = Enrollment { id: TEST_ENROLLMENT_ID, data: vec![3] };
                let enrollment_2 = Enrollment { id: TEST_ENROLLMENT_ID_2, data: vec![12] };

                let AttemptedEvent {
                    enrollment_id, updated_enrollment_data, prekey_material, ..
                } = proxy
                    .authenticate(&mut vec![enrollment, enrollment_2].iter_mut())
                    .await?
                    .unwrap();

                assert_eq!(enrollment_id, TEST_ENROLLMENT_ID);
                assert!(updated_enrollment_data.is_none());
                assert_eq!(prekey_material, FIXED_PREKEY.clone());
                Ok(())
            }
        })
        .await
    }
}
