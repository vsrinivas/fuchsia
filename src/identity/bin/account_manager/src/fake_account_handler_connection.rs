// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains a fake version of an AccountHandlerConnection used for
//! unit tests of business logic that depend on AccountHandlerConnection without
//! instantiating real component instances. Different magic account ids can be
//! used to trigger different errors and responses.

#![cfg(test)]
use {
    crate::account_handler_connection::AccountHandlerConnection,
    account_common::{AccountId, AccountManagerError},
    async_trait::async_trait,
    fidl::endpoints::create_proxy_and_stream,
    fidl_fuchsia_identity_account::{Error as ApiError, Lifetime},
    fidl_fuchsia_identity_internal::{
        AccountHandlerControlMarker, AccountHandlerControlProxy, AccountHandlerControlRequest,
    },
    fuchsia_async as fasync,
    futures::prelude::*,
    lazy_static::lazy_static,
};

const EMPTY_PRE_AUTH_STATE: Vec<u8> = vec![];

lazy_static! {
    // Indicating en error-free, standard response.
    pub static ref DEFAULT_ACCOUNT_ID: AccountId = AccountId::new(10000);

    // Triggers an ApiError::Resource while trying to create a connection.
    pub static ref CORRUPT_HANDLER_ACCOUNT_ID: AccountId = AccountId::new(20000);

    // Triggers an ApiError::Unknown while trying initialize an account.
    pub static ref UNKNOWN_ERROR_ACCOUNT_ID: AccountId = AccountId::new(30000);
}

/// Fake implementation of AccountHandlerConnection which provides a rudimentary
/// server for the proxy. Different magic account ids can be used to trigger
/// different responses and errors.
#[derive(Debug)]
pub struct FakeAccountHandlerConnection {
    lifetime: Lifetime,
    account_id: AccountId,
    proxy: AccountHandlerControlProxy,
}

impl FakeAccountHandlerConnection {
    /// Returns a new FakeAccountHandlerConnection.
    pub async fn new_with_defaults(
        lifetime: Lifetime,
        account_id: AccountId,
    ) -> Result<Self, AccountManagerError> {
        Self::new(account_id, lifetime).await
    }
}

#[async_trait]
impl AccountHandlerConnection for FakeAccountHandlerConnection {
    async fn new(account_id: AccountId, lifetime: Lifetime) -> Result<Self, AccountManagerError> {
        if account_id == *CORRUPT_HANDLER_ACCOUNT_ID {
            return Err(AccountManagerError::new(ApiError::Resource));
        }
        let generate_unknown_err = account_id == *UNKNOWN_ERROR_ACCOUNT_ID;
        let (proxy, mut stream) = create_proxy_and_stream::<AccountHandlerControlMarker>()?;
        let lifetime_clone = lifetime;
        fasync::Task::spawn(async move {
            while let Some(req) = stream.try_next().await.unwrap() {
                match req {
                    AccountHandlerControlRequest::CreateAccount { responder, .. } => {
                        let mut response = Ok(EMPTY_PRE_AUTH_STATE.clone());
                        if generate_unknown_err {
                            response = Err(ApiError::Unknown);
                        }
                        responder.send(&mut response).unwrap();
                    }
                    AccountHandlerControlRequest::Preload { responder, .. } => {
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
                    AccountHandlerControlRequest::RemoveAccount { .. }
                    | AccountHandlerControlRequest::GetAccount { .. }
                    // TODO(dnordstrom): Support LockAccount and UnlockAccount
                    | AccountHandlerControlRequest::LockAccount { .. }
                    | AccountHandlerControlRequest::UnlockAccount{..}
                    => {
                        panic!("Unsupported method call");
                    }
                };
            }
        })
        .detach();
        Ok(Self { account_id, lifetime, proxy })
    }

    fn get_account_id(&self) -> &AccountId {
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
    use {super::*, fidl_fuchsia_identity_internal::AccountHandlerControlCreateAccountRequest};

    #[fuchsia_async::run_until_stalled(test)]
    async fn corrupt_handler() {
        assert_eq!(
            FakeAccountHandlerConnection::new(
                CORRUPT_HANDLER_ACCOUNT_ID.clone(),
                Lifetime::Persistent,
            )
            .await
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
        )
        .await?;
        assert_eq!(conn.get_lifetime(), &Lifetime::Persistent);
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn new_ephemeral() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Ephemeral,
            DEFAULT_ACCOUNT_ID.clone(),
        )
        .await?;
        assert_eq!(conn.get_lifetime(), &Lifetime::Ephemeral);
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn preload_success() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            DEFAULT_ACCOUNT_ID.clone(),
        )
        .await?;
        assert!(conn.proxy().preload(&EMPTY_PRE_AUTH_STATE).await.unwrap().is_ok());
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn preload_corrupt() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            UNKNOWN_ERROR_ACCOUNT_ID.clone(),
        )
        .await?;
        assert_eq!(
            conn.proxy().preload(&EMPTY_PRE_AUTH_STATE).await.unwrap().unwrap_err(),
            ApiError::Unknown
        );
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn preload_ephemeral() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Ephemeral,
            DEFAULT_ACCOUNT_ID.clone(),
        )
        .await?;
        assert_eq!(
            conn.proxy().preload(&EMPTY_PRE_AUTH_STATE).await.unwrap().unwrap_err(),
            ApiError::Internal
        );
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn create_success() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            DEFAULT_ACCOUNT_ID.clone(),
        )
        .await?;
        assert!(conn
            .proxy()
            .create_account(AccountHandlerControlCreateAccountRequest::EMPTY)
            .await
            .unwrap()
            .is_ok());
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn create_corrupt() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            UNKNOWN_ERROR_ACCOUNT_ID.clone(),
        )
        .await?;
        assert_eq!(
            conn.proxy()
                .create_account(AccountHandlerControlCreateAccountRequest::EMPTY)
                .await
                .unwrap()
                .unwrap_err(),
            ApiError::Unknown
        );
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    async fn create_after_terminate() -> Result<(), AccountManagerError> {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            DEFAULT_ACCOUNT_ID.clone(),
        )
        .await?;
        assert!(conn.proxy().terminate().is_ok());
        assert!(conn
            .proxy()
            .create_account(AccountHandlerControlCreateAccountRequest::EMPTY)
            .await
            .unwrap_err()
            .is_closed());
        Ok(())
    }

    #[fuchsia_async::run_until_stalled(test)]
    #[should_panic(expected = "Unsupported method call")]
    async fn unsupported_method() {
        let conn = FakeAccountHandlerConnection::new_with_defaults(
            Lifetime::Persistent,
            DEFAULT_ACCOUNT_ID.clone(),
        )
        .await
        .unwrap();
        assert!(conn.proxy().remove_account().await.unwrap_err().is_closed());
    }
}
