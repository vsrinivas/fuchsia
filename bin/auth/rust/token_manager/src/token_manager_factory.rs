// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ClientEnd;
use fidl::Error;
use fidl_fuchsia_auth::{AuthProviderConfig, AuthenticationContextProviderMarker,
                        TokenManagerFactoryRequest};
use fuchsia_async as fasync;
use futures::prelude::*;
use log::{error, info};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::path::Path;
use std::sync::Arc;

use crate::token_manager::{TokenManager, TokenManagerContext};

// The directory to use for token manager databases
const DB_DIR: &str = "/data/auth";
// The file suffix to use for token manager databases. This string is appended to the user id.
const DB_SUFFIX: &str = "_token_store.json";

/// A factory to create instances of the TokenManager for individual users.
pub struct TokenManagerFactory {
    /// A map storing the single TokenManager instance that will be used for each account.
    ///
    /// Entries are created and added on the first request for a user, and are defined as an `Arc`
    /// `Mutex` so they can be used in asynchronous closures that require mutable access and static
    /// lifetime.
    user_to_token_manager: Mutex<HashMap<String, Arc<TokenManager>>>,

    /// The auth provider configuration that this token manager was initially created with.
    ///
    /// Note: We are migrating to a build time configuration of auth providers, at which time this
    /// will be unnecessary, but in the meantime a caller could attempt to specify two different
    /// auth provider configuations in different calls to the factory. We do not support this since
    /// it would infer separate token databases for each potential caller. Instead, we remember the
    /// set of auth provider configuration that was used on the first call and return an error if
    /// this is not constant across future calls.
    auth_provider_configs: Mutex<Vec<AuthProviderConfig>>,
}

impl TokenManagerFactory {
    /// Creates a new TokenManagerFactory.
    pub fn new() -> TokenManagerFactory {
        TokenManagerFactory {
            user_to_token_manager: Mutex::new(HashMap::new()),
            auth_provider_configs: Mutex::new(Vec::new()),
        }
    }

    /// Returns true iff the supplied `auth_provider_configs` are equal to any previous invocations
    /// on this factory.
    fn is_auth_provider_config_consistent(
        &self, auth_provider_configs: &Vec<AuthProviderConfig>,
    ) -> bool {
        let mut previous_auth_provider_configs = self.auth_provider_configs.lock();
        if previous_auth_provider_configs.is_empty() {
            previous_auth_provider_configs
                .extend(auth_provider_configs.iter().map(clone_auth_provider_config));
            true
        } else {
            *previous_auth_provider_configs == *auth_provider_configs
        }
    }

    /// Returns an Ok containing a TokenManager, or an Err if any errors were encountered creating
    /// one. The TokenManager is retrieved from the user map if one already exists, or is created
    /// and added to the map if not.
    fn get_or_create_token_manager(
        &self, user_id: String, auth_provider_configs: Vec<AuthProviderConfig>,
        auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
    ) -> Result<Arc<TokenManager>, failure::Error> {
        let mut user_to_token_manager = self.user_to_token_manager.lock();
        match user_to_token_manager.get(&user_id) {
            Some(token_manager) => Ok(Arc::clone(token_manager)),
            None => {
                info!("Creating token manager for user {}", user_id);
                let db_path = Path::new(DB_DIR).join(user_id.clone() + DB_SUFFIX);
                let token_manager = Arc::new(TokenManager::new(
                    &db_path,
                    auth_provider_configs,
                    auth_context_provider,
                )?);
                user_to_token_manager.insert(user_id, Arc::clone(&token_manager));
                Ok(token_manager)
            }
        }
    }

    /// Handles a single request to the TokenManagerFactory.
    ///
    /// Output is defined as a Result where Err is used for errors that should be considered
    /// fatal for the factory. Note that currently no failure modes meet this condition since
    /// requests for different users are independant.
    pub async fn handle_request(&self, req: TokenManagerFactoryRequest) -> Result<(), Error> {
        match req {
            TokenManagerFactoryRequest::GetTokenManager {
                user_id,
                application_url,
                auth_provider_configs,
                auth_context_provider,
                token_manager: token_manager_server_end,
                ..
            } => {
                if !self.is_auth_provider_config_consistent(&auth_provider_configs) {
                    error!("Auth provider config inconsistent with previous request");
                    return Ok(());
                }

                let token_manager = match self.get_or_create_token_manager(
                    user_id,
                    auth_provider_configs,
                    auth_context_provider,
                ) {
                    Ok(token_manager) => token_manager,
                    Err(err) => {
                        error!("Error creating TokenManager: {:?}", err);
                        return Ok(());
                    }
                };
                let context = TokenManagerContext { application_url };

                match token_manager_server_end.into_stream() {
                    Ok(mut stream) => {
                        fasync::spawn(
                            (async move {
                                while let Some(req) = await!(stream.try_next())? {
                                    await!(token_manager.handle_request(&context, req))?;
                                }
                                Ok(())
                            })
                                .unwrap_or_else(
                                    |e: failure::Error| {
                                        error!("Fatal error, closing TokenManager channel: {:?}", e)
                                    },
                                ),
                        );
                    }
                    Err(err) => {
                        error!("Error creating TokenManager channel: {:?}", err);
                    }
                }
                Ok(())
            }
        }
    }
}

/// A helper function to clone an `AuthProviderConfig`, currently required since the FIDL bindings
/// do not derive clone.
fn clone_auth_provider_config(other: &AuthProviderConfig) -> AuthProviderConfig {
    AuthProviderConfig {
        auth_provider_type: other.auth_provider_type.clone(),
        url: other.url.clone(),
        params: other.params.clone(),
    }
}
