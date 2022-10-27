// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_tpm_cr50::{
        InsertLeafResponse, PinWeaverRequest, PinWeaverRequestStream, TryAuthFailed,
        TryAuthRateLimited, TryAuthResponse, TryAuthSuccess,
    },
    fuchsia_async as fasync,
    fuchsia_component::server as fserver,
    fuchsia_component_test::LocalComponentHandles,
    futures::stream::{StreamExt, TryStreamExt},
    parking_lot::Mutex,
    std::collections::VecDeque,
    std::sync::Arc,
};

/// Struct that builds a set of mock responses for CR50 agent requests.
/// Mock responses should be created (via add_* functions) in FIFO order and
/// will panic at test time if the order of requests does not match the order
/// of responses added to the builder.
/// Successful TryAuth responses take neither a `he_secret` nor a
/// `reset_secret` as those are generated via cprng by password_authenticator.
/// Instead, a successful TryAuth response will always return the `he_secret`
/// provided to the most recent InsertLeaf call, and will always return an
/// empty `reset_secret`.
/// TODO(fxb/89060, arkay): This logic could be improved upon to match the
/// `he_secret` to the credential label if necessary.
pub(crate) struct MockCr50AgentBuilder {
    responses: VecDeque<MockResponse>,
}

/// Defines the type of a Hash as CR50 expects it.
pub(crate) type Hash = [u8; 32];

/// Defines an enum of known MockResponse types.
#[derive(Clone, Debug)]
pub(crate) enum MockResponse {
    GetVersion { version: u8 },
    ResetTree { root_hash: Hash },
    InsertLeaf { response: InsertLeafResponse },
    RemoveLeaf { root_hash: Hash },
    TryAuth { response: TryAuthResponse },
}

#[allow(dead_code)]
impl MockCr50AgentBuilder {
    /// Initializes a new MockCr50AgentBuilder.
    pub(crate) fn new() -> Self {
        MockCr50AgentBuilder { responses: VecDeque::new() }
    }

    /// Adds a GetVersion response.
    pub(crate) fn add_get_version_response(mut self, version: u8) -> Self {
        self.responses.push_back(MockResponse::GetVersion { version });
        self
    }

    /// Adds a ResetTree response.
    pub(crate) fn add_reset_tree_response(mut self, root_hash: Hash) -> Self {
        self.responses.push_back(MockResponse::ResetTree { root_hash });
        self
    }

    /// Adds an InsertLeaf response.
    /// This function does not take an he_secret or reset_secret, see
    /// [`MockCr50AgentBuilder`] for more information.
    pub(crate) fn add_insert_leaf_response(
        mut self,
        root_hash: Hash,
        mac: Hash,
        cred_metadata: Vec<u8>,
    ) -> Self {
        let response = InsertLeafResponse {
            root_hash: Some(root_hash),
            mac: Some(mac),
            cred_metadata: Some(cred_metadata),
            ..InsertLeafResponse::EMPTY
        };
        self.responses.push_back(MockResponse::InsertLeaf { response });
        self
    }

    /// Adds a RemoveLeaf response.
    pub(crate) fn add_remove_leaf_response(mut self, root_hash: Hash) -> Self {
        self.responses.push_back(MockResponse::RemoveLeaf { root_hash });
        self
    }

    /// Adds a successful TryAuth response.
    pub(crate) fn add_try_auth_success_response(
        mut self,
        root_hash: Hash,
        cred_metadata: Vec<u8>,
        mac: Hash,
    ) -> Self {
        let success = TryAuthSuccess {
            root_hash: Some(root_hash),
            cred_metadata: Some(cred_metadata),
            mac: Some(mac),
            ..TryAuthSuccess::EMPTY
        };
        self.responses
            .push_back(MockResponse::TryAuth { response: TryAuthResponse::Success(success) });
        self
    }

    /// Adds a failed TryAuth response.
    pub(crate) fn add_try_auth_failed_response(
        mut self,
        root_hash: Hash,
        cred_metadata: Vec<u8>,
        mac: Hash,
    ) -> Self {
        let failed = TryAuthFailed {
            root_hash: Some(root_hash),
            cred_metadata: Some(cred_metadata),
            mac: Some(mac),
            ..TryAuthFailed::EMPTY
        };
        self.responses
            .push_back(MockResponse::TryAuth { response: TryAuthResponse::Failed(failed) });
        self
    }

    /// Adds a rate limited TryAuth response.
    pub(crate) fn add_try_auth_rate_limited_response(mut self, time_to_wait: i64) -> Self {
        let ratelimited =
            TryAuthRateLimited { time_to_wait: Some(time_to_wait), ..TryAuthRateLimited::EMPTY };
        self.responses.push_back(MockResponse::TryAuth {
            response: TryAuthResponse::RateLimited(ratelimited),
        });
        self
    }

    /// Consumes the builder and returns the VecDeque of responses for use with `mock()`.
    pub(crate) fn build(self) -> VecDeque<MockResponse> {
        self.responses
    }
}

async fn handle_request(
    request: PinWeaverRequest,
    next_response: MockResponse,
    he_secret: &Arc<Mutex<Vec<u8>>>,
) {
    // Match the next response with the request, panicking if requests are out
    // of the expected order.
    match request {
        PinWeaverRequest::GetVersion { responder: resp } => {
            match next_response {
                MockResponse::GetVersion { version } => {
                    resp.send(version).expect("failed to send response");
                }
                _ => panic!(
                    "Next mock response type was {:?} but expected GetVersion.",
                    next_response
                ),
            };
        }
        PinWeaverRequest::ResetTree { bits_per_level: _, height: _, responder: resp } => {
            match next_response {
                MockResponse::ResetTree { root_hash } => {
                    resp.send(&mut std::result::Result::Ok(root_hash))
                        .expect("failed to send response");
                }
                _ => panic!(
                    "Next mock response type was {:?} but expected ResetTree.",
                    next_response
                ),
            };
        }
        PinWeaverRequest::InsertLeaf { params, responder: resp } => {
            match next_response {
                MockResponse::InsertLeaf { response } => {
                    // Store the he_secret received in the most recent
                    // InsertLeaf response to return in subsequent successful
                    // TryAuth responses.
                    let mut secret = he_secret.lock();
                    *secret = params.he_secret.expect("expected he_secret provided in params");
                    resp.send(&mut std::result::Result::Ok(response))
                        .expect("failed to send response");
                }
                _ => panic!(
                    "Next mock response type was {:?} but expected InsertLeaf.",
                    next_response
                ),
            };
        }
        PinWeaverRequest::RemoveLeaf { params: _, responder: resp } => {
            match next_response {
                MockResponse::RemoveLeaf { root_hash } => {
                    resp.send(&mut std::result::Result::Ok(root_hash))
                        .expect("failed to send response");
                }
                _ => panic!(
                    "Next mock response type was {:?} but expected RemoveLeaf.",
                    next_response
                ),
            };
        }
        PinWeaverRequest::TryAuth { params: _, responder: resp } => {
            match next_response {
                MockResponse::TryAuth { response } => {
                    if let TryAuthResponse::Success(success) = response {
                        // If it's a success, grab the last he_secret provided via InsertLeaf.
                        let secret = he_secret.lock();
                        resp.send(&mut std::result::Result::Ok(TryAuthResponse::Success(
                            TryAuthSuccess { he_secret: Some((*secret).clone()), ..success },
                        )))
                        .expect("failed to send response");
                    } else {
                        resp.send(&mut std::result::Result::Ok(response))
                            .expect("failed to send response");
                    }
                }
                _ => {
                    panic!("Next mock response type was {:?} but expected TryAuth.", next_response)
                }
            };
        }
        // GetLog and LogReplay are unimplemented as testing log replay is out
        // of scope for pwauth-credmgr integration tests.
        PinWeaverRequest::GetLog { root_hash: _, responder: _ } => {
            unimplemented!();
        }
        PinWeaverRequest::LogReplay { params: _, responder: _ } => {
            unimplemented!();
        }
    }
}

pub(crate) async fn mock(
    mock_responses: VecDeque<MockResponse>,
    handles: LocalComponentHandles,
) -> Result<(), Error> {
    // Create a new ServiceFs to host FIDL protocols from
    let mut fs = fserver::ServiceFs::new();
    let mut tasks = vec![];
    let last_he_secret: Arc<Mutex<Vec<u8>>> = Arc::new(Mutex::new(vec![0; 32]));

    // Add the echo protocol to the ServiceFs
    fs.dir("svc").add_fidl_service(move |mut stream: PinWeaverRequestStream| {
        // Need to clone the mock responses again because this is a FnMut not a FnOnce
        let mut task_mock_responses = mock_responses.clone();
        let he_secret = Arc::clone(&last_he_secret);
        tasks.push(fasync::Task::local(async move {
            while let Some(request) =
                stream.try_next().await.expect("failed to serve pinweaver service")
            {
                // Look at the next (FIFO) response.
                let next_response = task_mock_responses.pop_front().expect(&format!(
                    "Ran out of mock Pinweaver responses. Next request received is: {:?}",
                    request
                ));

                handle_request(request, next_response, &he_secret).await;
            }
        }));
    });

    // Run the ServiceFs on the outgoing directory handle from the mock handles
    fs.serve_connection(handles.outgoing_dir)?;
    fs.collect::<()>().await;

    Ok(())
}
