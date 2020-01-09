// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::authenticator_connection::{AuthenticatorConnection, AuthenticatorService};
use anyhow::Error;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_auth::AuthProviderConfig;
use fidl_fuchsia_identity_account::Error as ApiError;
use fidl_fuchsia_identity_internal::{
    AccountHandlerContextRequest, AccountHandlerContextRequestStream,
};
use futures::prelude::*;
use std::collections::HashMap;
use token_manager::{AuthProviderConnection, AuthProviderService};

/// A type that can respond to`AccountHandlerContext` requests from the AccountHandler components
/// that we launch. These requests provide contextual and service information to the
/// AccountHandlers, such as connections to components implementing the `AuthProvider`
/// protocol.
pub struct AccountHandlerContext {
    /// A map from auth_provider_type to an `AuthProviderConnection` used to launch the associated
    /// component.
    auth_provider_connections: HashMap<String, AuthProviderConnection>,

    /// A map from an auth_mechanism_id to an `AuthenticatorConnection` used to launch the
    /// associated component.
    auth_mechanism_connections: HashMap<String, AuthenticatorConnection>,
}

impl AccountHandlerContext {
    /// Creates a new `AccountHandlerContext` from the supplied vector of `AuthProviderConfig`
    /// objects.
    ///
    /// `auth_provider_configs` A list of config objects that are used to create the connections.
    /// `authenticators` A list of component urls representing authenticators implementing the
    ///                  any authentication protocols.
    pub fn new(
        auth_provider_configs: &[AuthProviderConfig],
        auth_mechanism_ids: &[String],
    ) -> AccountHandlerContext {
        AccountHandlerContext {
            auth_provider_connections: auth_provider_configs
                .iter()
                .map(|apc| {
                    (apc.auth_provider_type.clone(), AuthProviderConnection::from_config_ref(apc))
                })
                .collect(),
            auth_mechanism_connections: auth_mechanism_ids
                .iter()
                .map(|auth_mechanism_id| {
                    // For now, auth mechanism ids are Fuchsia component URLs.
                    // We use that fact to construct their authenticators.
                    let conn = AuthenticatorConnection::from_url(auth_mechanism_id);
                    (auth_mechanism_id.to_string(), conn)
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
            AccountHandlerContextRequest::GetOauth { auth_provider_type, oauth, responder } => {
                responder.send(&mut self.connect_to_auth_provider(&auth_provider_type, oauth).await)
            }
            AccountHandlerContextRequest::GetOpenIdConnect {
                auth_provider_type,
                open_id_connect,
                responder,
            } => responder.send(
                &mut self.connect_to_auth_provider(&auth_provider_type, open_id_connect).await,
            ),
            AccountHandlerContextRequest::GetOauthOpenIdConnect {
                auth_provider_type,
                oauth_open_id_connect,
                responder,
            } => responder.send(
                &mut self
                    .connect_to_auth_provider(&auth_provider_type, oauth_open_id_connect)
                    .await,
            ),
            AccountHandlerContextRequest::GetStorageUnlockAuthMechanism {
                auth_mechanism_id,
                storage_unlock_mechanism,
                responder,
            } => responder.send(
                &mut self
                    .connect_to_authenticator(&auth_mechanism_id, storage_unlock_mechanism)
                    .await,
            ),
        }
    }

    async fn connect_to_auth_provider<'a, S>(
        &'a self,
        auth_provider_type: &'a str,
        server_end: ServerEnd<S>,
    ) -> Result<(), fidl_fuchsia_identity_account::Error>
    where
        S: AuthProviderService,
    {
        match self.auth_provider_connections.get(auth_provider_type) {
            Some(apc) => apc.connect::<S>(server_end).map_err(|_| ApiError::Unknown),
            None => Err(ApiError::NotFound),
        }
    }

    async fn connect_to_authenticator<'a, S>(
        &'a self,
        auth_mechanism_id: &'a str,
        server_end: ServerEnd<S>,
    ) -> Result<(), ApiError>
    where
        S: AuthenticatorService,
    {
        match self.auth_mechanism_connections.get(auth_mechanism_id) {
            Some(ac) => ac.connect::<S>(server_end).map_err(|_| ApiError::Unknown),
            None => Err(ApiError::NotFound),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const DUMMY_AUTHENTICATOR_URL_1: &str = "fuchsia-pkg://fuchsia.com/dummy_auth_1#meta/auth.cmx";
    const DUMMY_AUTHENTICATOR_URL_2: &str = "fuchsia-pkg://fuchsia.com/dummy_auth_2#meta/auth.cmx";

    /// Note: Most AccountHandlerContext methods launch instances of an AuthProvider or an
    /// authenticator.
    /// Since it is currently not convenient to mock out this component launching in Rust, we rely on the
    /// hermetic component test to provide coverage for these areas and only cover the in-process
    /// behavior with this unit-test.

    #[test]
    fn init_with_auth_provider_configs() {
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

        let test_object = AccountHandlerContext::new(&dummy_configs, &[]);
        let test_connection_1 =
            test_object.auth_provider_connections.get(&dummy_config_1.auth_provider_type).unwrap();
        let test_connection_2 =
            test_object.auth_provider_connections.get(&dummy_config_2.auth_provider_type).unwrap();

        assert_eq!(test_connection_1.component_url(), dummy_config_1.url);
        assert_eq!(test_connection_2.component_url(), dummy_config_2.url);
        assert!(test_object.auth_provider_connections.get("bad url").is_none());
    }

    #[test]
    fn init_with_authenticators() {
        let test_object = AccountHandlerContext::new(
            &[],
            &[DUMMY_AUTHENTICATOR_URL_1.to_string(), DUMMY_AUTHENTICATOR_URL_2.to_string()],
        );
        let test_connection_1 =
            test_object.auth_mechanism_connections.get(DUMMY_AUTHENTICATOR_URL_1).unwrap();
        let test_connection_2 =
            test_object.auth_mechanism_connections.get(DUMMY_AUTHENTICATOR_URL_2).unwrap();
        assert_eq!(test_connection_1.component_url(), DUMMY_AUTHENTICATOR_URL_1);
        assert_eq!(test_connection_2.component_url(), DUMMY_AUTHENTICATOR_URL_2);
        assert!(test_object.auth_mechanism_connections.get("bad url").is_none());
    }
}
