// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::auth_context_client::AuthContextClient;
use super::auth_provider_client::AuthProviderClient;
use super::error::TokenManagerError;
use async;
use auth_cache::{AuthCacheError, CacheKey, FirebaseAuthToken, OAuthToken, TokenCache};
use auth_store::file::AuthDbFile;
use auth_store::{AuthDb, AuthDbError, CredentialKey, CredentialValue};
use failure;
use fidl;
use fidl::encoding2::OutOfLine;
use fidl::endpoints2::{ClientEnd, ServerEnd};
use fidl_fuchsia_auth::{AppConfig, AuthProviderConfig, AuthProviderProxy, AuthProviderStatus,
                        AuthenticationContextProviderMarker, Status,
                        TokenManagerAuthorizeResponder, TokenManagerDeleteAllTokensResponder,
                        TokenManagerGetAccessTokenResponder,
                        TokenManagerGetFirebaseTokenResponder, TokenManagerGetIdTokenResponder,
                        TokenManagerMarker, TokenManagerRequest, UserProfileInfo};
use futures::future;
use futures::prelude::*;
use futures::FutureExt;
use std::collections::HashMap;
use std::path::Path;
use std::sync::{Arc, Mutex};

const CACHE_SIZE: usize = 128;
const DB_DIR: &str = "/data/auth";
const DB_POSTFIX: &str = "_token_store.json";

type TokenManagerFuture<T> = Box<Future<Item = T, Error = TokenManagerError> + Send>;

/// An object capable of creating authentication tokens for a user across a
/// range of services as represented by AuthProviderConfigs. Uses the supplied
/// `AuthenticationContextProvider` to render UI where necessary.
pub struct TokenManager {
    /// A map of clients capable of communicating with each AuthProvider.
    auth_providers: HashMap<String, AuthProviderClient>,
    /// A client for creating new AuthenticationUIContexts.
    auth_context: AuthContextClient,
    /// A persistent store of long term credentials.
    token_store: Arc<Mutex<Box<AuthDb + Send + Sync>>>,
    /// An in-memory cache of recently used tokens.
    token_cache: Arc<Mutex<TokenCache>>,
}

impl TokenManager {
    /// Creates a new TokenManager to handle requests for the specified user
    /// over the supplied channel.
    pub fn spawn(
        user_id: String, application_url: String, auth_provider_configs: Vec<AuthProviderConfig>,
        auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
        server_end: ServerEnd<TokenManagerMarker>,
    ) {
        let mut manager = match Self::new(
            user_id,
            application_url,
            auth_provider_configs,
            auth_context_provider,
        ) {
            Ok(manager) => manager,
            Err(err) => {
                error!("Error creating TokenManager: {:?}", err);
                return;
            }
        };

        match server_end.into_stream() {
            Ok(request_stream) => async::spawn(
                request_stream
                    .err_into::<failure::Error>()
                    .for_each(move |req| manager.handle_request(req))
                    .map(|_| ())
                    .recover(|err| error!("Fatal error, closing TokenManager: {:?}", err)),
            ),
            Err(err) => {
                error!("Error creating TokenManager request stream {:?}", err);
            }
        };
    }

    /// Creates a new TokenManager.
    pub fn new(
        user_id: String, _application_url: String, auth_provider_configs: Vec<AuthProviderConfig>,
        auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
    ) -> Result<Self, failure::Error> {
        // TODO(jsankey): The C++ implmentation ignores application_url. Decide how to handle it.
        // TODO(jsankey): Decide if its worth storing the user ID.
        let db_path = Path::new(DB_DIR).join(user_id.clone() + DB_POSTFIX);
        let token_store = AuthDbFile::new(&db_path)
            .map_err(|err| format_err!("Error creating AuthDb at {:?}, {:?}", db_path, err))?;

        let token_cache = TokenCache::new(CACHE_SIZE);

        let auth_providers = auth_provider_configs
            .into_iter()
            .map(|apc| {
                (
                    apc.auth_provider_type.clone(),
                    AuthProviderClient::from_config(apc),
                )
            })
            .collect();

        let auth_context = AuthContextClient::from_client_end(auth_context_provider)
            .map_err(|err| format_err!("Error creating AuthContext {:?}", err))?;

        Ok(TokenManager {
            auth_providers,
            auth_context,
            token_store: Arc::new(Mutex::new(Box::new(token_store))),
            token_cache: Arc::new(Mutex::new(token_cache)),
        })
    }

    /// Handles a single request to the TokenManager by dispatching to more
    /// specific functions for each method.
    fn handle_request(
        &mut self, req: TokenManagerRequest,
    ) -> Box<Future<Item = (), Error = failure::Error> + Send> {
        match req {
            TokenManagerRequest::Authorize {
                app_config,
                user_profile_id,
                app_scopes,
                responder,
            } => Box::new(
                self.authorize(app_config, user_profile_id, app_scopes)
                    .then(move |result| responder.send_result(result)),
            ),
            TokenManagerRequest::GetAccessToken {
                app_config,
                user_profile_id,
                app_scopes,
                responder,
            } => Box::new(
                self.get_access_token(app_config, user_profile_id, app_scopes)
                    .then(move |result| responder.send_result(result)),
            ),
            TokenManagerRequest::GetIdToken {
                app_config,
                user_profile_id,
                audience,
                responder,
            } => Box::new(
                self.get_id_token(app_config, user_profile_id, audience)
                    .then(move |result| responder.send_result(result)),
            ),
            TokenManagerRequest::GetFirebaseToken {
                app_config,
                user_profile_id,
                audience,
                firebase_api_key,
                responder,
            } => Box::new(
                self.get_firebase_token(app_config, user_profile_id, audience, firebase_api_key)
                    .then(move |result| responder.send_result(result)),
            ),
            TokenManagerRequest::DeleteAllTokens {
                app_config,
                user_profile_id,
                responder,
            } => Box::new(
                self.delete_all_tokens(app_config, user_profile_id)
                    .then(move |result| responder.send_result(result)),
            ),
        }
    }

    /// Performs initial authorization for a user_profile.
    fn authorize(
        &self, app_config: AppConfig, _user_profile_id: Option<String>, _app_scopes: Vec<String>,
    ) -> TokenManagerFuture<UserProfileInfo> {
        // TODO(jsankey): The C++ code ignores the scopes input, decide what to do with it.
        // TODO(jsankey): The C++ code ignores the profile ID input, decide what to do with it.
        let auth_provider_type = app_config.auth_provider_type.clone();
        let store = self.token_store.clone();
        let ui_context = future_try!(
            self.auth_context
                .get_new_ui_context()
                .map_err(|err| TokenManagerError::new(Status::InvalidAuthContext).with_cause(err))
        );

        Box::new(
            self.get_auth_provider_proxy(&app_config.auth_provider_type)
                .and_then(move |proxy| {
                    proxy
                        .get_persistent_credential(Some(ui_context))
                        .map_err(|err| {
                            TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
                        })
                })
                .and_then(move |(status, credential, user_profile_info)| {
                    match (credential, user_profile_info) {
                        (Some(credential), Some(user_profile_info)) => {
                            let db_value = CredentialValue::new(
                                auth_provider_type,
                                user_profile_info.id.clone(),
                                credential,
                            ).map_err(|_| Status::AuthProviderServerError)?;
                            store.lock().unwrap().add_credential(db_value)?;
                            Ok(*user_profile_info)
                        }
                        _ => Err(TokenManagerError::from(status)),
                    }
                }),
        )
    }

    /// Sends a downscoped OAuth access token for a given `AppConfig`, user,
    /// and set of scopes.
    fn get_access_token(
        &self, app_config: AppConfig, user_profile_id: String, app_scopes: Vec<String>,
    ) -> TokenManagerFuture<Arc<OAuthToken>> {
        let (db_key, cache_key) = future_try!(Self::create_keys(&app_config, &user_profile_id));

        // Attempt to read the token from cache.
        if let Some(cached_token) = self
            .token_cache
            .lock()
            .unwrap()
            .get_access_token(&cache_key, &app_scopes)
        {
            return Box::new(future::ok(cached_token));
        }

        // If no cached entry was found use an auth provider to mint a new one from the
        // refresh token, then place it in the cache.
        let refresh_token = future_try!(self.get_refresh_token(&db_key));
        let cache = self.token_cache.clone();
        let app_scopes_1 = Arc::new(app_scopes);
        let app_scopes_2 = app_scopes_1.clone();
        Box::new(
            self.get_auth_provider_proxy(&app_config.auth_provider_type)
                .and_then(move |proxy| {
                    let scopes_copy = app_scopes_1.iter().map(|x| &**x).collect::<Vec<_>>();
                    proxy
                        .get_app_access_token(
                            &refresh_token,
                            app_config.client_id.as_ref().map(|x| &**x),
                            &mut scopes_copy.into_iter(),
                        )
                        .map_err(|err| {
                            TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
                        })
                })
                .and_then(move |(status, provider_token)| match provider_token {
                    Some(provider_token) => {
                        let native_token = Arc::new(OAuthToken::from(*provider_token));
                        cache.lock().unwrap().put_access_token(
                            cache_key,
                            &app_scopes_2,
                            native_token.clone(),
                        );
                        Ok(native_token)
                    }
                    None => Err(TokenManagerError::from(status)),
                }),
        )
    }

    /// Returns a JWT Identity token for a given `AppConfig`, user, and
    /// audience.
    fn get_id_token(
        &self, app_config: AppConfig, user_profile_id: String, audience: Option<String>,
    ) -> TokenManagerFuture<Arc<OAuthToken>> {
        let (db_key, cache_key) = future_try!(Self::create_keys(&app_config, &user_profile_id));
        let audience_str = audience.clone().unwrap_or("".to_string());

        // Attempt to read the token from cache.
        if let Some(cached_token) = self
            .token_cache
            .lock()
            .unwrap()
            .get_id_token(&cache_key, &audience_str)
        {
            return Box::new(future::ok(cached_token));
        }

        // If no cached entry was found use an auth provider to mint a new one from the
        // refresh token, then place it in the cache.
        let refresh_token = future_try!(self.get_refresh_token(&db_key));
        let cache = self.token_cache.clone();
        Box::new(
            self.get_auth_provider_proxy(&app_config.auth_provider_type)
                .and_then(move |proxy| {
                    proxy
                        .get_app_id_token(&refresh_token, audience.as_ref().map(|x| &**x))
                        .map_err(|err| {
                            TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
                        })
                })
                .and_then(move |(status, provider_token)| match provider_token {
                    Some(provider_token) => {
                        let native_token = Arc::new(OAuthToken::from(*provider_token));
                        cache.lock().unwrap().put_id_token(
                            cache_key,
                            audience_str,
                            native_token.clone(),
                        );
                        Ok(native_token)
                    }
                    None => Err(TokenManagerError::from(status)),
                }),
        )
    }

    /// Returns a Firebase a given `AppConfig`, user, audience, and Firebase
    /// API key, creating an ID token if necessary.
    fn get_firebase_token(
        &self, app_config: AppConfig, user_profile_id: String, audience: String, api_key: String,
    ) -> TokenManagerFuture<Arc<FirebaseAuthToken>> {
        let (_, cache_key) = future_try!(Self::create_keys(&app_config, &user_profile_id));

        // Attempt to read the token from cache.
        let cache = self.token_cache.clone();
        if let Some(cached_token) = cache
            .lock()
            .unwrap()
            .get_firebase_token(&cache_key, &api_key)
        {
            return Box::new(future::ok(cached_token));
        }

        // If no cached entry was found use ourselves to fetch or mint an ID token then
        // use that to mint a new firebase token, which we also cache.
        let api_key_clone = api_key.clone();
        let auth_provider_type = app_config.auth_provider_type.clone();
        let id_token_future =
            Box::new(self.get_id_token(app_config, user_profile_id, Some(audience)));
        let proxy_future = Box::new(self.get_auth_provider_proxy(&auth_provider_type));
        Box::new(
            id_token_future
                .join(proxy_future)
                .and_then(move |(id_token, proxy)| {
                    proxy
                        .get_app_firebase_token(&*id_token, &api_key)
                        .map_err(|err| {
                            TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
                        })
                })
                .and_then(move |(status, provider_token)| match provider_token {
                    Some(provider_token) => {
                        let native_token = Arc::new(FirebaseAuthToken::from(*provider_token));
                        cache.lock().unwrap().put_firebase_token(
                            cache_key,
                            api_key_clone,
                            native_token.clone(),
                        );
                        Ok(native_token)
                    }
                    None => Err(TokenManagerError::from(status)),
                }),
        )
    }

    /// Deletes any existing tokens for a user in both the database and cache.
    fn delete_all_tokens(
        &self, app_config: AppConfig, user_profile_id: String,
    ) -> TokenManagerFuture<()> {
        let (db_key, cache_key) = future_try!(Self::create_keys(&app_config, &user_profile_id));

        // Try to find an associated refresh token, returning immediately with a success if we
        // can't.
        let refresh_token = match (**self.token_store.lock().unwrap()).get_refresh_token(&db_key) {
            Ok(rt) => rt.to_string(),
            Err(AuthDbError::CredentialNotFound) => return Box::new(Ok(()).into_future()),
            Err(err) => return TokenManagerError::from(err).to_boxed_future(),
        };

        let cache = self.token_cache.clone();
        let store = self.token_store.clone();

        // Request that the auth provider revoke the credential server-side.
        Box::new(
            self.get_auth_provider_proxy(&app_config.auth_provider_type)
                .and_then(move |proxy| {
                    proxy
                        .revoke_app_or_persistent_credential(&refresh_token)
                        .map_err(|err| {
                            TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
                        })
                })
                .and_then(move |status| {
                    // In the case of probably-temporary AuthProvider failures we fail this method
                    // and expect the client to retry. For other AuthProvider failures we continue
                    // to delete our copy of the credential even if the server can't revoke it.
                    // Note this means it will never be possible to ask the server to revoke the
                    // token in the future, but it does let us clean up broken tokens from our
                    // database.
                    if status == AuthProviderStatus::NetworkError {
                        return Err(TokenManagerError::from(status));
                    }

                    match cache.lock().unwrap().delete(&cache_key) {
                        Ok(()) => (),
                        Err(AuthCacheError::KeyNotFound) => (),
                        Err(err) => return Err(TokenManagerError::from(err)),
                    }

                    match store.lock().unwrap().delete_credential(&db_key) {
                        Ok(()) => (),
                        Err(AuthDbError::CredentialNotFound) => (),
                        Err(err) => return Err(TokenManagerError::from(err)),
                    }

                    Ok(())
                }),
        )
    }

    /// Returns index keys for referencing a token in both the database and
    /// cache.
    fn create_keys(
        app_config: &AppConfig, user_profile_id: &String,
    ) -> Result<(CredentialKey, CacheKey), TokenManagerError> {
        let db_key = CredentialKey::new(
            app_config.auth_provider_type.clone(),
            user_profile_id.clone(),
        ).map_err(|_| TokenManagerError::new(Status::InvalidRequest))?;
        let cache_key = CacheKey::new(
            app_config.auth_provider_type.clone(),
            user_profile_id.clone(),
        ).map_err(|_| TokenManagerError::new(Status::InvalidRequest))?;
        Ok((db_key, cache_key))
    }

    /// Returns an `AuthProviderProxy` to handle requests for the supplied
    /// `AppConfig`.
    fn get_auth_provider_proxy(
        &self, auth_provider_type: &str,
    ) -> TokenManagerFuture<Arc<AuthProviderProxy>> {
        let auth_provider = match self.auth_providers.get(auth_provider_type) {
            Some(ap) => ap,
            None => {
                return TokenManagerError::new(Status::AuthProviderServiceUnavailable)
                    .with_cause(format_err!("Unknown auth provider {}", auth_provider_type))
                    .to_boxed_future();
            }
        };
        Box::new(auth_provider.get_proxy().map_err(|err| {
            TokenManagerError::new(Status::AuthProviderServiceUnavailable).with_cause(err)
        }))
    }

    /// Returns the current refresh token for a user from the data store.
    /// Failure to find the user leads to an Error.
    fn get_refresh_token(&self, db_key: &CredentialKey) -> Result<String, TokenManagerError> {
        match (**self.token_store.lock().unwrap()).get_refresh_token(db_key) {
            Ok(rt) => Ok(rt.to_string()),
            Err(err) => Err(TokenManagerError::from(err)),
        }
    }
}

/// A trait that we implement for the autogenerated FIDL responder types for
/// each method to simplify the process of responding.
trait Responder {
    type Data;

    /// Sends the supplied result logging any errors in the result or sending.
    /// The return value is an error if the input was a fatal error, or Ok(())
    /// otherwise.
    fn send_result(
        &self, result: Result<Self::Data, TokenManagerError>,
    ) -> Result<(), failure::Error> {
        match result {
            Ok(val) => {
                if let Err(err) = self.send_raw(Status::Ok, Some(val)) {
                    warn!(
                        "Error sending response to {}: {:?}",
                        Self::METHOD_NAME,
                        &err
                    );
                }
                Ok(())
            }
            Err(err) => {
                if let Err(err) = self.send_raw(err.status, None) {
                    warn!(
                        "Error sending error response to {}: {:?}",
                        Self::METHOD_NAME,
                        &err
                    );
                }
                if err.fatal {
                    error!("Fatal error during {}: {:?}", Self::METHOD_NAME, &err);
                    Err(failure::Error::from(err))
                } else {
                    warn!("Error during {}: {:?}", Self::METHOD_NAME, &err);
                    Ok(())
                }
            }
        }
    }

    /// Sends a status and optional data without logging or failure handling.
    fn send_raw(&self, status: Status, data: Option<Self::Data>) -> Result<(), fidl::Error>;

    /// Defines the name of the TokenManger method for use in logging.
    const METHOD_NAME: &'static str;
}

impl Responder for TokenManagerAuthorizeResponder {
    type Data = UserProfileInfo;
    const METHOD_NAME: &'static str = "Authorize";

    fn send_raw(
        &self, status: Status, mut data: Option<UserProfileInfo>,
    ) -> Result<(), fidl::Error> {
        self.send(status, data.as_mut().map(|v| OutOfLine(v)))
    }
}

impl Responder for TokenManagerGetAccessTokenResponder {
    type Data = Arc<OAuthToken>;
    const METHOD_NAME: &'static str = "GetAccessToken";

    fn send_raw(&self, status: Status, data: Option<Arc<OAuthToken>>) -> Result<(), fidl::Error> {
        self.send(status, data.as_ref().map(|v| &***v))
    }
}

impl Responder for TokenManagerGetIdTokenResponder {
    type Data = Arc<OAuthToken>;
    const METHOD_NAME: &'static str = "GetIdToken";

    fn send_raw(&self, status: Status, data: Option<Arc<OAuthToken>>) -> Result<(), fidl::Error> {
        self.send(status, data.as_ref().map(|v| &***v))
    }
}

impl Responder for TokenManagerGetFirebaseTokenResponder {
    type Data = Arc<FirebaseAuthToken>;
    const METHOD_NAME: &'static str = "GetFirebaseToken";

    fn send_raw(
        &self, status: Status, data: Option<Arc<FirebaseAuthToken>>,
    ) -> Result<(), fidl::Error> {
        let mut fidl_data = data.map(|v| v.to_fidl());
        self.send(status, fidl_data.as_mut().map(|v| OutOfLine(v)))
    }
}

impl Responder for TokenManagerDeleteAllTokensResponder {
    type Data = ();
    const METHOD_NAME: &'static str = "DeleteAllTokens";

    fn send_raw(&self, status: Status, _data: Option<()>) -> Result<(), fidl::Error> {
        self.send(status)
    }
}
