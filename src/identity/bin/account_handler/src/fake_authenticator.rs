// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A fake implementation of `StorageUnlockMechanism` to simplify unit testing.

use fidl_fuchsia_identity_authentication::{
    AttemptedEvent, Enrollment, Error as ApiError, StorageUnlockMechanismRequest,
    StorageUnlockMechanismRequestStream,
};
use fuchsia_async::futures::lock::Mutex;
use futures::prelude::*;
use std::collections::VecDeque;

/// A fake implementation of a `StorageUnlockMechanism` authenticator.
///
/// Expected messages and response values are added before the test
/// business logic that exercise the methods. If the calls to the fake
/// authenticator do not match exectly the set of expected messages,
/// it will panic.
pub struct FakeAuthenticator {
    messages: Mutex<VecDeque<Expected>>,
}

impl FakeAuthenticator {
    /// Create a new fake authenticator with no expected requests.
    pub fn new() -> Self {
        Self { messages: Mutex::new(VecDeque::new()) }
    }

    /// Asynchronously handles a `StorageUnlockMechanism` request stream.
    /// Can be called multiple times.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: StorageUnlockMechanismRequestStream,
    ) -> Result<(), fidl::Error> {
        while let Some(request) = stream.try_next().await? {
            self.handle_request(request).await?;
        }
        Ok(())
    }

    async fn handle_request(
        &self,
        request: StorageUnlockMechanismRequest,
    ) -> Result<(), fidl::Error> {
        let next = self.messages.lock().await.pop_front().expect("got an unexpected message");
        match request {
            StorageUnlockMechanismRequest::Authenticate { enrollments, responder } => match next {
                Expected::Authenticate { req, mut resp } => {
                    assert_eq!(enrollments, req);
                    responder.send(&mut resp)
                }
                _ => panic!("got a different message than expected"),
            },
            StorageUnlockMechanismRequest::Enroll { responder } => match next {
                Expected::Enroll { mut resp } => responder.send(&mut resp),
                _ => panic!("got a different message than expected"),
            },
        }
    }

    /// Enqueue an expected message pair.
    pub fn enqueue(&self, expected: Expected) {
        self.messages.try_lock().unwrap().push_back(expected);
    }
}

impl Drop for FakeAuthenticator {
    fn drop(&mut self) {
        if !self.messages.try_lock().unwrap().is_empty() {
            panic!("did not get all expected messages")
        }
    }
}

/// An enum representing an expected request and a corresponding
/// injected response.
pub enum Expected {
    /// An expected Authenticate request and an injected response.
    Authenticate { req: Vec<Enrollment>, resp: Result<AttemptedEvent, ApiError> },

    /// An expected Enrollment request and an injected response.
    /// Since there is no request body for the enroll method,
    /// it has no `req` field. The response consists of enrollment data
    /// and pre-key material.
    Enroll { resp: Result<(Vec<u8>, Vec<u8>), ApiError> },
}

mod test {
    use super::*;
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_identity_authentication::StorageUnlockMechanismMarker;
    use fuchsia_async as fasync;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref TEST_PREKEY_MATERIAL: Vec<u8> = vec![13; 256];
        static ref TEST_ENROLLMENT_DATA_1: Vec<u8> = vec![13, 37];
        static ref TEST_ENROLLMENT_DATA_2: Vec<u8> = vec![13, 38];
        static ref TEST_ENROLLMENT_1: Enrollment =
            Enrollment { id: 0, data: TEST_ENROLLMENT_DATA_1.clone() };
        static ref TEST_ENROLLMENT_2: Enrollment =
            Enrollment { id: 1, data: TEST_ENROLLMENT_DATA_2.clone() };
        static ref TEST_ATTEMPT_1: AttemptedEvent = AttemptedEvent {
            enrollment_id: 9,
            prekey_material: TEST_PREKEY_MATERIAL.clone(),
            timestamp: 13371337,
            updated_enrollment_data: None,
        };
        static ref TEST_ATTEMPT_2: AttemptedEvent = AttemptedEvent {
            enrollment_id: 10,
            prekey_material: TEST_PREKEY_MATERIAL.clone(),
            timestamp: 13391339,
            updated_enrollment_data: Some(TEST_ENROLLMENT_DATA_2.clone()),
        };
    }

    #[fasync::run_until_stalled(test)]
    async fn check_multiple_expected() {
        let authenticator = FakeAuthenticator::new();
        let (proxy, stream) = create_proxy_and_stream::<StorageUnlockMechanismMarker>().unwrap();

        authenticator.enqueue(Expected::Enroll {
            resp: Ok((TEST_ENROLLMENT_DATA_1.clone(), TEST_PREKEY_MATERIAL.clone())),
        });
        authenticator.enqueue(Expected::Authenticate {
            req: vec![TEST_ENROLLMENT_1.clone()],
            resp: Ok(TEST_ATTEMPT_1.clone()),
        });
        authenticator.enqueue(Expected::Authenticate {
            req: vec![TEST_ENROLLMENT_2.clone()],
            resp: Err(ApiError::Unknown),
        });
        fasync::spawn(async move {
            authenticator.handle_requests_from_stream(stream).await.unwrap();
        });

        let resp = proxy.enroll().await.unwrap();
        assert_eq!(resp, Ok((TEST_ENROLLMENT_DATA_1.clone(), TEST_PREKEY_MATERIAL.clone())));

        let mut req = vec![TEST_ENROLLMENT_1.clone()];
        let resp = proxy.authenticate(&mut req.iter_mut()).await.unwrap();
        assert_eq!(resp, Ok(TEST_ATTEMPT_1.clone()));

        let mut req = vec![TEST_ENROLLMENT_2.clone()];
        let resp = proxy.authenticate(&mut req.iter_mut()).await.unwrap();
        assert_eq!(resp, Err(ApiError::Unknown));
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic(expected = "assertion failed")]
    async fn expect_wrong_message_body() {
        let authenticator = FakeAuthenticator::new();
        let (proxy, stream) = create_proxy_and_stream::<StorageUnlockMechanismMarker>().unwrap();

        authenticator.enqueue(Expected::Authenticate {
            req: vec![TEST_ENROLLMENT_1.clone()],
            resp: Ok(TEST_ATTEMPT_1.clone()),
        });
        fasync::spawn(async move {
            authenticator.handle_requests_from_stream(stream).await.unwrap();
        });

        let mut req = vec![TEST_ENROLLMENT_2.clone()];
        let _ = proxy.authenticate(&mut req.iter_mut()).await;
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic(expected = "got a different message than expected")]
    async fn expect_wrong_message() {
        let authenticator = FakeAuthenticator::new();
        let (proxy, stream) = create_proxy_and_stream::<StorageUnlockMechanismMarker>().unwrap();

        authenticator.enqueue(Expected::Authenticate {
            req: vec![TEST_ENROLLMENT_1.clone()],
            resp: Ok(TEST_ATTEMPT_1.clone()),
        });
        fasync::spawn(async move {
            authenticator.handle_requests_from_stream(stream).await.unwrap();
        });
        let _ = proxy.enroll().await;
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic(expected = "got an unexpected message")]
    async fn unexpected_message() {
        let authenticator = FakeAuthenticator::new();
        let (proxy, stream) = create_proxy_and_stream::<StorageUnlockMechanismMarker>().unwrap();

        fasync::spawn(async move {
            authenticator.handle_requests_from_stream(stream).await.unwrap();
        });
        let _ = proxy.enroll().await;
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic(expected = "did not get all expected messages")]
    async fn expected_messages_not_exhausted() {
        let authenticator = FakeAuthenticator::new();
        let (_proxy, stream) = create_proxy_and_stream::<StorageUnlockMechanismMarker>().unwrap();

        authenticator.enqueue(Expected::Authenticate {
            req: vec![TEST_ENROLLMENT_1.clone()],
            resp: Ok(TEST_ATTEMPT_1.clone()),
        });
        fasync::spawn(async move {
            authenticator.handle_requests_from_stream(stream).await.unwrap();
        });
    }
}
