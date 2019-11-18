// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::auth_provider_supplier::AuthProviderSupplier;
use failure::{Error, ResultExt};
use fidl_fuchsia_auth::{
    AuthProviderConfig, TokenManagerFactoryRequest, TokenManagerFactoryRequestStream,
};
use futures::prelude::*;
use identity_common::TaskGroup;
use log::{info, warn};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::path::PathBuf;
use std::sync::Arc;
use token_manager::{TokenManagerContext, TokenManagerError};

// The file suffix to use for token manager databases. This string is appended to the user id.
const DB_SUFFIX: &str = "_token_store.json";

type TokenManager = token_manager::TokenManager<AuthProviderSupplier>;

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

    /// An object capable of launching and opening connections on components implementing the
    /// AuthProvider interface. This is populated on the first call that provides auth
    /// provider configuration.
    auth_provider_supplier: Mutex<Option<AuthProviderSupplier>>,

    /// The directory to use for token manager databases.
    db_dir: PathBuf,
}

impl TokenManagerFactory {
    /// Creates a new TokenManagerFactory.
    pub fn new(db_dir: PathBuf) -> TokenManagerFactory {
        TokenManagerFactory {
            user_to_token_manager: Mutex::new(HashMap::new()),
            auth_provider_configs: Mutex::new(Vec::new()),
            auth_provider_supplier: Mutex::new(None),
            db_dir,
        }
    }

    /// Asynchronously handles the supplied stream of `TokenManagerFactoryRequest` messages.
    ///
    /// This method will only return an Err for errors that should be considered fatal for the
    /// factory. Not that currently no failure modes meet this condition since requests for
    /// different users are independant.
    pub async fn handle_requests_from_stream(&self, mut stream: TokenManagerFactoryRequestStream) {
        while let Ok(Some(req)) = stream.try_next().await {
            self.handle_request(req).await.unwrap_or_else(|err| {
                warn!("Error handling TokenManagerFactoryRequest: {:?}", err);
            });
        }
    }

    /// Asynchronously handles a single request to the TokenManagerFactory.
    async fn handle_request(&self, req: TokenManagerFactoryRequest) -> Result<(), Error> {
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
                    warn!("Auth provider config inconsistent with previous request");
                    return Ok(());
                };

                let token_manager = self
                    .get_token_manager(user_id, auth_provider_configs)
                    .context("Error creating TokenManager")?;
                let context = TokenManagerContext {
                    application_url,
                    auth_ui_context_provider: auth_context_provider.into_proxy()?,
                };
                let stream = token_manager_server_end
                    .into_stream()
                    .context("Error creating request stream")?;

                let token_manager_clone = Arc::clone(&token_manager);
                token_manager
                    .task_group()
                    .spawn(|cancel| {
                        async move {
                            token_manager_clone
                                .handle_requests_from_stream(&context, stream, cancel)
                                .await
                                .unwrap_or_else(|e| {
                                    warn!("Error handling TokenManager channel {:?}", e)
                                })
                        }
                    })
                    .await?;
                Ok(())
            }
        }
    }

    /// Returns true iff the supplied `auth_provider_configs` are equal to any previous invocations
    /// on this factory.
    fn is_auth_provider_config_consistent(
        &self,
        auth_provider_configs: &Vec<AuthProviderConfig>,
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

    /// Returns an `AuthProviderSupplier` for the supplied `auth_provider_configs`, or any errors
    /// encountered while creating one. If an auth provider supplier has been previously created,
    /// it will be cloned.
    fn get_auth_provider_supplier(
        &self,
        auth_provider_configs: Vec<AuthProviderConfig>,
    ) -> Result<AuthProviderSupplier, TokenManagerError> {
        let mut auth_provider_supplier_lock = self.auth_provider_supplier.lock();
        match &*auth_provider_supplier_lock {
            Some(auth_provider_supplier) => Ok(auth_provider_supplier.clone()),
            None => {
                let auth_provider_supplier = AuthProviderSupplier::new(auth_provider_configs);
                auth_provider_supplier_lock.get_or_insert(auth_provider_supplier.clone());
                Ok(auth_provider_supplier)
            }
        }
    }

    /// Returns a Result containing a TokenManager, or any errors encountered while creating one.
    /// The TokenManager is retrieved from the user map if one already exists, or is created
    /// and added to the map if not.
    fn get_token_manager(
        &self,
        user_id: String,
        auth_provider_configs: Vec<AuthProviderConfig>,
    ) -> Result<Arc<TokenManager>, Error> {
        let mut user_to_token_manager = self.user_to_token_manager.lock();
        if let Some(token_manager) = user_to_token_manager.get(&user_id) {
            return Ok(Arc::clone(token_manager));
        };

        info!("Creating token manager for user {}", user_id);
        let db_path = self.db_dir.join(user_id.clone() + DB_SUFFIX);
        let token_manager = Arc::new(TokenManager::new(
            &db_path,
            self.get_auth_provider_supplier(auth_provider_configs)?,
            TaskGroup::new(),
        )?);
        user_to_token_manager.insert(user_id, Arc::clone(&token_manager));
        Ok(token_manager)
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

// TODO(dnordstrom): Add unit tests
