// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains a fake version of an AccountHandlerConnection used for
//! unit tests of business logic that depend on AccountHandlerConnection without
//! instantiating real component instances. Different magic account ids can be
//! used to trigger different errors and responses.

#![cfg(test)]
use crate::account_handler_connection::AccountHandlerConnection;
use crate::account_handler_context::AccountHandlerContext;
use account_common::{AccountManagerError, LocalAccountId};
use async_trait::async_trait;
use fidl::endpoints::create_proxy_and_stream;
use fidl_fuchsia_identity_account::{Error as ApiError, Lifetime};
use fidl_fuchsia_identity_internal::{
    AccountHandlerControlMarker, AccountHandlerControlProxy, AccountHandlerControlRequest,
};
use fuchsia_async as fasync;
use futures::prelude::*;
use lazy_static::lazy_static;
use std::sync::Arc;

lazy_static! {
    // Indicating en error-free, standard response.
    pub static ref DEFAULT_ACCOUNT_ID: LocalAccountId = LocalAccountId::new(10000);

    // Triggers an ApiError::Resource while trying to create a connection.
    pub static ref CORRUPT_HANDLER_ACCOUNT_ID: LocalAccountId = LocalAccountId::new(20000);

    // Triggers an ApiError::Unknown while trying initialize an account.
    pub static ref UNKNOWN_ERROR_ACCOUNT_ID: LocalAccountId = LocalAccountId::new(30000);

    static ref EMPTY_ACCOUNT_HANDLER_CONTEXT: Arc<AccountHandlerContext> =
            Arc::new(AccountHandlerContext::new(&[]));
}

/// Fake implementation of AccountHandlerConnection which provides a rudimentary
/// server for the proxy. Different magic account ids can be used to trigger
/// different responses and errors.
#[derive(Debug)]
pub struct FakeAccountHandlerConnection {
    lifetime: Lifetime,
    account_id: LocalAccountId,
    proxy: AccountHandlerControlProxy,
}

impl FakeAccountHandlerConnection {
    /// Returns a new FakeAccountHandlerConnection with an empty
    /// AccountHandlerContext, for convenience.
    pub fn new_with_defaults(
        lifetime: Lifetime,
        account_id: LocalAccountId,
    ) -> Result<Self, AccountManagerError> {
        Self::new(account_id, lifetime, Arc::clone(&EMPTY_ACCOUNT_HANDLER_CONTEXT))
    }
}

#[async_trait]
impl AccountHandlerConnection for FakeAccountHandlerConnection {
    fn new(
        account_id: LocalAccountId,
        lifetime: Lifetime,
        _context: Arc<AccountHandlerContext>,
    ) -> Result<Self, AccountManagerError> {
        if account_id == *CORRUPT_HANDLER_ACCOUNT_ID {
            return Err(AccountManagerError::new(ApiError::Resource));
        }
        let generate_unknown_err = account_id == *UNKNOWN_ERROR_ACCOUNT_ID;
        let (proxy, mut stream) = create_proxy_and_stream::<AccountHandlerControlMarker>()?;
        let lifetime_clone = lifetime.clone();
        fasync::spawn(async move {
            while let Some(req) = stream.try_next().await.unwrap() {
                match req {
                    AccountHandlerControlRequest::CreateAccount { responder, .. } => {
                        let mut response = Ok(());
                        if generate_unknown_err {
                            response = Err(ApiError::Unknown);
                        }
                        responder.send(&mut response).unwrap();
                    }
                    AccountHandlerControlRequest::Preload { responder } => {
                        let mut response = Ok(());
                        if generate_unknown_err {
                            response = Err(ApiError::Unknown);
                        }
                        // Preloading an ephemeral account is always an error
                        if lifetime_clone == Lifetime::Ephemeral {
                            response = Err(ApiError::Internal)
                        }
                        responder.send(&mut response).unwrap();
                    }
                    AccountHandlerControlRequest::Terminate { .. } => {
                        break;
                    }
                    // TODO(dnordstrom): Support LockAccount/UnlockAccount
                    // TODO(dnordstrom): Remove wildcard and panic instead
                    _ => {
                        break;
                    }
                };
            }
        });
        Ok(Self { account_id, lifetime, proxy })
    }

    fn get_account_id(&self) -> &LocalAccountId {
        &self.account_id
    }

    fn get_lifetime(&self) -> &Lifetime {
        &self.lifetime
    }

    fn proxy(&self) -> &AccountHandlerControlProxy {
        &self.proxy
    }

    // TODO(dnordstrom): Either make proxy unusable or consume self.
    async fn terminate(&self) {}
}

mod tests {
    use super::*;

    #[fuchsia_async::run_until_stalled(test)]
    async fn corrupt_handler() {
        assert_eq!(
            FakeAccountHandlerConnection::new(
                CORRUPT_HANDLER_ACCOUNT_ID.clone(),
                Lifetime::Persistent,
                Arc::clone(&EMPTY_ACCOUNT_HANDLER_CONTEXT)
            )
            .unwrap_err()
            .api_error,
            ApiError::Resource
        );
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn new_persistent() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            DEFAULT_ACCOUNT_ID.clone(),
        )?;
        assert_eq!(conn.get_lifetime(), &Lifetime::Persistent);
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn new_ephemeral() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Ephemeral,
            DEFAULT_ACCOUNT_ID.clone(),
        )?;
        assert_eq!(conn.get_lifetime(), &Lifetime::Ephemeral);
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn preload_success() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            DEFAULT_ACCOUNT_ID.clone(),
        )?;
        assert!(conn.proxy().preload().await.unwrap().is_ok());
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn preload_corrupt() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            UNKNOWN_ERROR_ACCOUNT_ID.clone(),
        )?;
        assert_eq!(conn.proxy().preload().await.unwrap().unwrap_err(), ApiError::Unknown);
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn preload_ephemeral() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Ephemeral,
            DEFAULT_ACCOUNT_ID.clone(),
        )?;
        assert_eq!(conn.proxy().preload().await.unwrap().unwrap_err(), ApiError::Internal);
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn create_success() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            DEFAULT_ACCOUNT_ID.clone(),
        )?;
        assert!(conn.proxy().create_account(None).await.unwrap().is_ok());
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn create_corrupt() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            UNKNOWN_ERROR_ACCOUNT_ID.clone(),
        )?;
        assert_eq!(
            conn.proxy().create_account(None).await.unwrap().unwrap_err(),
            ApiError::Unknown
        );
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn create_after_terminate() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            DEFAULT_ACCOUNT_ID.clone(),
        )?;
        assert!(conn.proxy().terminate().is_ok());
        assert!(conn.proxy().create_account(None).await.unwrap_err().is_closed());
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn unsupported_method() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            DEFAULT_ACCOUNT_ID.clone(),
        )?;
        assert!(conn.proxy().prepare_for_account_transfer().await.unwrap_err().is_closed());
        Ok(())
    }
}
