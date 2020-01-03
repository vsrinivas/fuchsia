// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::format_err;
use async_trait::async_trait;
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_auth::Status;
use fidl_fuchsia_identity_external::{OauthMarker, OauthOpenIdConnectMarker, OpenIdConnectMarker};
use fidl_fuchsia_identity_internal::AccountHandlerContextProxy;
use token_manager::TokenManagerError;

/// A type capable of acquiring `AuthProvider` connections from components that implement it.
///
/// The functionality is delegated to the component that launched the AccountHandler, through
/// methods it implements on the `AccountHandlerContext` interface.
pub struct AuthProviderSupplier {
    /// The `AccountHandlerContext` interface supplied by the component that launched
    /// AccountHandler
    account_handler_context: AccountHandlerContextProxy,
}

impl AuthProviderSupplier {
    /// Creates a new `AuthProviderSupplier` from the supplied `AccountHandlerContextProxy`.
    pub fn new(account_handler_context: AccountHandlerContextProxy) -> Self {
        AuthProviderSupplier { account_handler_context }
    }
}

#[async_trait]
impl token_manager::AuthProviderSupplier for AuthProviderSupplier {
    async fn get_oauth(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OauthMarker>, TokenManagerError> {
        let (client_end, server_end) = create_endpoints()
            .map_err(|err| TokenManagerError::new(Status::UnknownError).with_cause(err))?;
        match self.account_handler_context.get_oauth(auth_provider_type, server_end).await {
            Ok(Ok(())) => Ok(client_end),
            Ok(Err(status)) => Err(TokenManagerError::new(Status::AuthProviderServiceUnavailable)
                .with_cause(format_err!("AccountHandlerContext returned {:?}", status))),
            Err(err) => Err(TokenManagerError::new(Status::UnknownError).with_cause(err)),
        }
    }

    async fn get_open_id_connect(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OpenIdConnectMarker>, TokenManagerError> {
        let (client_end, server_end) = create_endpoints()
            .map_err(|err| TokenManagerError::new(Status::UnknownError).with_cause(err))?;
        match self.account_handler_context.get_open_id_connect(auth_provider_type, server_end).await
        {
            Ok(Ok(())) => Ok(client_end),
            Ok(Err(status)) => Err(TokenManagerError::new(Status::AuthProviderServiceUnavailable)
                .with_cause(format_err!("AccountHandlerContext returned {:?}", status))),
            Err(err) => Err(TokenManagerError::new(Status::UnknownError).with_cause(err)),
        }
    }

    async fn get_oauth_open_id_connect(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OauthOpenIdConnectMarker>, TokenManagerError> {
        let (client_end, server_end) = create_endpoints()
            .map_err(|err| TokenManagerError::new(Status::UnknownError).with_cause(err))?;
        match self
            .account_handler_context
            .get_oauth_open_id_connect(auth_provider_type, server_end)
            .await
        {
            Ok(Ok(())) => Ok(client_end),
            Ok(Err(status)) => Err(TokenManagerError::new(Status::AuthProviderServiceUnavailable)
                .with_cause(format_err!("AccountHandlerContext returned {:?}", status))),
            Err(err) => Err(TokenManagerError::new(Status::UnknownError).with_cause(err)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl::endpoints::{create_endpoints, ClientEnd, ServerEnd};
    use fidl_fuchsia_identity_internal::{
        AccountHandlerContextMarker, AccountHandlerContextRequest,
    };
    use fuchsia_async as fasync;
    use futures::TryStreamExt;
    use token_manager::AuthProviderSupplier as AuthProviderSupplierTrait;

    const TEST_AUTH_PROVIDER_TYPE: &str = "test_auth_provider";

    /// Runs a asynchronous test on the supplied executor to contruct a new AuthProviderSupplier
    /// and request TEST_AUTH_PROVIDER.
    fn do_get_test(
        mut executor: fasync::Executor,
        client_end: ClientEnd<AccountHandlerContextMarker>,
        expected_error: Option<Status>,
    ) {
        let proxy = client_end.into_proxy().unwrap();
        let auth_provider_supplier = AuthProviderSupplier::new(proxy);
        executor.run_singlethreaded(async move {
            let result = auth_provider_supplier.get_oauth(TEST_AUTH_PROVIDER_TYPE).await;
            match expected_error {
                Some(status) => {
                    assert!(result.is_err());
                    assert_eq!(status, result.unwrap_err().status);
                }
                None => {
                    assert!(result.is_ok());
                }
            }
        });
    }

    /// Spawns a trivial task to respond to the first AccountHandlerContextRequest with the
    /// supplied result, only if the first request is a get request for TEST_AUTH_PROVIDER_TYPE
    fn spawn_account_handler_context_server(
        server_end: ServerEnd<AccountHandlerContextMarker>,
        mut result: Result<(), fidl_fuchsia_identity_account::Error>,
    ) {
        fasync::spawn(async move {
            let mut request_stream = server_end.into_stream().unwrap();
            // Only respond to the first received message, only when its of the intended type.
            if let Ok(Some(AccountHandlerContextRequest::GetOauth {
                auth_provider_type,
                oauth: _,
                responder,
            })) = request_stream.try_next().await
            {
                if auth_provider_type == TEST_AUTH_PROVIDER_TYPE {
                    responder.send(&mut result).expect("Failed to send test response");
                }
            }
        });
    }

    #[test]
    fn test_get_valid() {
        let executor = fasync::Executor::new().expect("Failed to create executor");
        let (client_end, server_end) = create_endpoints::<AccountHandlerContextMarker>().unwrap();

        spawn_account_handler_context_server(server_end, Ok(()));
        do_get_test(executor, client_end, None);
    }

    #[test]
    fn test_get_invalid() {
        let executor = fasync::Executor::new().expect("Failed to create executor");
        let (client_end, server_end) = create_endpoints::<AccountHandlerContextMarker>().unwrap();

        spawn_account_handler_context_server(
            server_end,
            Err(fidl_fuchsia_identity_account::Error::NotFound),
        );
        do_get_test(executor, client_end, Some(Status::AuthProviderServiceUnavailable));
    }
}
