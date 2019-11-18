// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_auth::{AuthProviderConfig, AuthProviderMarker};
use fidl_fuchsia_identity_internal::{
    AccountHandlerContextRequest, AccountHandlerContextRequestStream,
};
use futures::prelude::*;
use std::collections::HashMap;
use token_manager::AuthProviderConnection;

/// A type that can respond to`AccountHandlerContext` requests from the AccountHandler components
/// that we launch. These requests provide contextual and service information to the
/// AccountHandlers, such as connections to components implementing the `AuthProvider`
/// protocol.
pub struct AccountHandlerContext {
    /// A map from auth_provider_type to an `AuthProviderConnection` used to launch the associated
    /// component.
    auth_provider_connections: HashMap<String, AuthProviderConnection>,
}

impl AccountHandlerContext {
    /// Creates a new `AccountHandlerContext` from the supplied vector of `AuthProviderConfig`
    /// objects.
    pub fn new(auth_provider_configs: &[AuthProviderConfig]) -> AccountHandlerContext {
        AccountHandlerContext {
            auth_provider_connections: auth_provider_configs
                .iter()
                .map(|apc| {
                    (apc.auth_provider_type.clone(), AuthProviderConnection::from_config_ref(apc))
                })
                .collect(),
        }
    }

    /// Asynchronously handles the supplied stream of `AccountHandlerContextRequest` messages.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AccountHandlerContextRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    /// Asynchronously handles a single `AccountHandlerContextRequest`.
    async fn handle_request(&self, req: AccountHandlerContextRequest) -> Result<(), fidl::Error> {
        match req {
            AccountHandlerContextRequest::GetAuthProvider {
                auth_provider_type,
                auth_provider,
                responder,
            } => responder
                .send(&mut self.get_auth_provider(&auth_provider_type, auth_provider).await),
        }
    }

    async fn get_auth_provider<'a>(
        &'a self,
        auth_provider_type: &'a str,
        auth_provider: ServerEnd<AuthProviderMarker>,
    ) -> Result<(), fidl_fuchsia_identity_account::Error> {
        match self.auth_provider_connections.get(auth_provider_type) {
            Some(apc) => apc
                .connect(auth_provider)
                .await
                .map_err(|_| fidl_fuchsia_identity_account::Error::Unknown),
            None => Err(fidl_fuchsia_identity_account::Error::NotFound),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Note: Most AccountHandlerContext methods launch instances of an AuthProvider. Since its
    /// currently not convenient to mock out this component launching in Rust, we rely on the
    /// hermetic component test to provide coverage for these areas and only cover the in-process
    /// behavior with this unit-test.

    #[test]
    fn test_new() {
        let dummy_configs = vec![
            AuthProviderConfig {
                auth_provider_type: "dummy_1".to_string(),
                url: "fuchsia-pkg://fuchsia.com/dummy_ap_1#meta/ap.cmx".to_string(),
                params: Some(vec!["test_arg_1".to_string()]),
            },
            AuthProviderConfig {
                auth_provider_type: "dummy_2".to_string(),
                url: "fuchsia-pkg://fuchsia.com/dummy_ap_2#meta/ap.cmx".to_string(),
                params: None,
            },
        ];
        let dummy_config_1 = &dummy_configs[0];
        let dummy_config_2 = &dummy_configs[1];

        let test_object = AccountHandlerContext::new(&dummy_configs);
        let test_connection_1 =
            test_object.auth_provider_connections.get(&dummy_config_1.auth_provider_type).unwrap();
        let test_connection_2 =
            test_object.auth_provider_connections.get(&dummy_config_2.auth_provider_type).unwrap();

        assert_eq!(test_connection_1.component_url(), dummy_config_1.url);
        assert_eq!(test_connection_2.component_url(), dummy_config_2.url);
        assert!(test_object.auth_provider_connections.get("bad url").is_none());
    }
}
