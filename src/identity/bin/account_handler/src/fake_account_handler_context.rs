// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A fake implementation of `AccountHandlerContext` to simplify unit testing.

use crate::fake_authenticator::FakeAuthenticator;
use fidl::endpoints::{create_endpoints, create_proxy_and_stream};
use fidl_fuchsia_identity_account::Error as ApiError;
use fidl_fuchsia_identity_internal::{
    AccountHandlerContextMarker, AccountHandlerContextProxy, AccountHandlerContextRequest,
    AccountHandlerContextRequestStream,
};
use fuchsia_async as fasync;
use futures::prelude::*;
use log::error;
use std::collections::BTreeMap;
use std::sync::Arc;

/// A fake meant for tests which rely on an AccountHandlerContext, but the context itself
/// isn't under test. As opposed to the real type, this doesn't depend on any other
/// components. Panics when getting unexpected messages or args, as defined by the implementation.
pub struct FakeAccountHandlerContext {
    fake_authenticators: BTreeMap<String, Arc<FakeAuthenticator>>,
}

impl FakeAccountHandlerContext {
    /// Creates new fake account handler context
    pub fn new() -> Self {
        Self { fake_authenticators: BTreeMap::new() }
    }

    /// Inserts a fake authenticator. If an authentication mechanism
    /// identified by `auth_mechanism_id` is requested, it will connect to
    /// the fake authenticator.
    pub fn insert_authenticator(&mut self, auth_mechanism_id: &str, fake: FakeAuthenticator) {
        self.fake_authenticators.insert(auth_mechanism_id.to_string(), Arc::new(fake));
    }

    /// Asynchronously handles the supplied stream of `AccountHandlerContextRequest` messages.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AccountHandlerContextRequestStream,
    ) -> Result<(), fidl::Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    /// Asynchronously handles a single `AccountHandlerContextRequest`.
    async fn handle_request(&self, req: AccountHandlerContextRequest) -> Result<(), fidl::Error> {
        match req {
            AccountHandlerContextRequest::GetOauth { responder, .. } => {
                responder.send(&mut Err(ApiError::Internal))
            }
            AccountHandlerContextRequest::GetOpenIdConnect { responder, .. } => {
                responder.send(&mut Err(ApiError::Internal))
            }
            AccountHandlerContextRequest::GetOauthOpenIdConnect { responder, .. } => {
                responder.send(&mut Err(ApiError::Internal))
            }
            AccountHandlerContextRequest::GetStorageUnlockAuthMechanism {
                auth_mechanism_id,
                storage_unlock_mechanism,
                responder,
            } => {
                let fake_authenticator = self.fake_authenticators.get(&auth_mechanism_id);
                let mut response = match fake_authenticator {
                    Some(fake_authenticator) => {
                        let fake_authenticator = Arc::clone(fake_authenticator);
                        let stream = storage_unlock_mechanism.into_stream().unwrap();
                        fasync::spawn(async move {
                            fake_authenticator
                                .handle_requests_from_stream(stream)
                                .await
                                .expect("Failed handling requests from stream");
                        });
                        Ok(())
                    }
                    None => Err(ApiError::NotFound),
                };
                responder.send(&mut response)
            }
        }
    }
}

/// Creates a new `AccountHandlerContext` channel, spawns a task to handle requests received on
/// this channel using the supplied `FakeAccountHandlerContext`, and returns the
/// `AccountHandlerContextProxy`.
pub fn spawn_context_channel(
    context: Arc<FakeAccountHandlerContext>,
) -> AccountHandlerContextProxy {
    let (proxy, stream) = create_proxy_and_stream::<AccountHandlerContextMarker>().unwrap();
    let context_clone = Arc::clone(&context);
    fasync::spawn(async move {
        context_clone
            .handle_requests_from_stream(stream)
            .await
            .unwrap_or_else(|err| error!("Error handling FakeAccountHandlerContext: {:?}", err))
    });
    proxy
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fake_authenticator::Expected;
    use fidl::endpoints::create_proxy;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref TEST_PREKEY_MATERIAL: Vec<u8> = vec![13; 256];
        static ref TEST_ENROLLMENT_DATA: Vec<u8> = vec![13, 37];
        static ref AUTH_MECHANISM_ID_OK: &'static str = "auth-mech-id-1";
        static ref AUTH_MECHANISM_ID_BAD: &'static str = "auth-mech-id-2";
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn get_oauth() {
        let fake_context = Arc::new(FakeAccountHandlerContext::new());
        let proxy = spawn_context_channel(fake_context.clone());
        let (_, ap_server_end) = create_endpoints().expect("failed creating channel pair");
        assert_eq!(
            proxy.get_oauth("dummy_auth_provider", ap_server_end).await.unwrap(),
            Err(ApiError::Internal)
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn with_fake_authenticator() {
        let authenticator = FakeAuthenticator::new();
        authenticator.enqueue(Expected::Enroll {
            resp: Ok((TEST_ENROLLMENT_DATA.clone(), TEST_PREKEY_MATERIAL.clone())),
        });

        let mut fake_context = FakeAccountHandlerContext::new();
        fake_context.insert_authenticator(&AUTH_MECHANISM_ID_OK, authenticator);
        let proxy = spawn_context_channel(Arc::new(fake_context));

        // Non-existing authenticator.
        let (auth_mechanism_proxy, server_end) = create_proxy().unwrap();
        assert_eq!(
            proxy
                .get_storage_unlock_auth_mechanism(&AUTH_MECHANISM_ID_BAD, server_end)
                .await
                .unwrap(),
            Err(ApiError::NotFound)
        );
        assert!(auth_mechanism_proxy.enroll().await.is_err());

        // Existing authenticator.
        let (auth_mechanism_proxy, server_end) = create_proxy().unwrap();
        assert_eq!(
            proxy
                .get_storage_unlock_auth_mechanism(&AUTH_MECHANISM_ID_OK, server_end)
                .await
                .unwrap(),
            Ok(())
        );
        assert!(auth_mechanism_proxy.enroll().await.is_ok());
    }
}
