// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use auth_cache::{AuthCacheError, CacheKey, FirebaseAuthToken, OAuthToken, TokenCache};
use auth_store::file::AuthDbFile;
use auth_store::{AuthDb, AuthDbError, CredentialKey, CredentialValue};
use crate::auth_context_client::AuthContextClient;
use crate::auth_provider_client::AuthProviderClient;
use crate::error::TokenManagerError;
use failure::format_err;
use fidl;
use fidl::encoding::OutOfLine;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_auth::{AppConfig, AssertionJwtParams, AttestationJwtParams,
                        AttestationSignerMarker, AuthProviderConfig, AuthProviderProxy,
                        AuthProviderStatus, AuthenticationContextProviderMarker, CredentialEcKey,
                        Status, TokenManagerAuthorizeResponder,
                        TokenManagerDeleteAllTokensResponder, TokenManagerGetAccessTokenResponder,
                        TokenManagerGetFirebaseTokenResponder, TokenManagerGetIdTokenResponder,
                        TokenManagerRequest, UserProfileInfo};
use fuchsia_zircon as zx;
use futures::prelude::*;
use futures::try_join;
use log::{error, warn};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::path::Path;
use std::sync::Arc;

const CACHE_SIZE: usize = 128;

#[allow(dead_code)] // Type is incorrectly being marked unused - Potentially rust issue #54234
type TokenManagerResult<T> = Result<T, TokenManagerError>;

/// The context that a particular request to the token manager should be executed in, capturing
/// information that was supplied on creation of the channel.
pub struct TokenManagerContext {
    /// The application that this request is being sent on behalf of.
    pub application_url: String,
}

/// The mutable state used to create, store, and cache authentication tokens for a particular user
/// across a range of third party services as configured by AuthProviderConfigs. Uses the supplied
/// `AuthenticationContextProvider` to render UI where necessary.
pub struct TokenManager {
    /// A map of clients capable of communicating with each AuthProvider.
    auth_providers: HashMap<String, AuthProviderClient>,
    /// A client for creating new AuthenticationUIContexts.
    auth_context: AuthContextClient,
    /// A persistent store of long term credentials.
    token_store: Mutex<Box<AuthDb + Send + Sync>>,
    /// An in-memory cache of recently used tokens.
    token_cache: Mutex<TokenCache>,
}

impl TokenManager {
    /// Creates a new TokenManager.
    pub fn new(
        db_path: &Path, auth_provider_configs: Vec<AuthProviderConfig>,
        auth_context_provider: ClientEnd<AuthenticationContextProviderMarker>,
    ) -> Result<Self, failure::Error> {
        let token_store = AuthDbFile::new(db_path)
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
            token_store: Mutex::new(Box::new(token_store)),
            token_cache: Mutex::new(token_cache),
        })
    }

    /// Handles a single request to the TokenManager by dispatching to more specific functions for
    /// each method.
    pub async fn handle_request<'a>(
        &'a self, _context: &'a TokenManagerContext, req: TokenManagerRequest,
    ) -> Result<(), failure::Error> {
        // TODO(jsankey): The C++ implmentation ignores application_url. We supply it via the
        // context, but should decide how to handle it.
        match req {
            TokenManagerRequest::Authorize {
                app_config,
                user_profile_id,
                app_scopes,
                auth_code,
                responder,
            } => responder.send_result(await!(self.authorize(
                app_config,
                user_profile_id,
                app_scopes,
                auth_code
            ))),
            TokenManagerRequest::GetAccessToken {
                app_config,
                user_profile_id,
                app_scopes,
                responder,
            } => responder.send_result(await!(self.get_access_token(
                app_config,
                user_profile_id,
                app_scopes
            ))),
            TokenManagerRequest::GetIdToken {
                app_config,
                user_profile_id,
                audience,
                responder,
            } => responder.send_result(await!(self.get_id_token(
                app_config,
                user_profile_id,
                audience
            ))),
            TokenManagerRequest::GetFirebaseToken {
                app_config,
                user_profile_id,
                audience,
                firebase_api_key,
                responder,
            } => responder.send_result(await!(self.get_firebase_token(
                app_config,
                user_profile_id,
                audience,
                firebase_api_key
            ))),
            TokenManagerRequest::DeleteAllTokens {
                app_config,
                user_profile_id,
                responder,
            } => responder.send_result(await!(self.delete_all_tokens(app_config, user_profile_id))),
        }
    }

    /// Performs initial authorization for a user_profile.
    /// TODO: |app_scopes| will be used in the future for Authenticators.
    async fn authorize(
        &self, app_config: AppConfig, user_profile_id: Option<String>, app_scopes: Vec<String>,
        auth_code: Option<String>,
    ) -> TokenManagerResult<UserProfileInfo> {
        // TODO(ukode, jsankey): This iotid check against the auth_provider_type is brittle and is
        // only a short-term solution. Eventually, this information will be coming from the
        // AuthProviderConfig params in some form.
        if app_config
            .auth_provider_type
            .to_ascii_lowercase()
            .contains("iotid")
        {
            await!(self.handle_iotid_authorize(app_config, user_profile_id, app_scopes, auth_code))
        } else {
            await!(self.handle_authorize(app_config, user_profile_id, app_scopes, auth_code))
        }
    }

    /// Performs authorization for connected devices flow for the supplied 'AppConfig' and returns
    /// an 'UserProfileInfo' on successful user consent.
    async fn handle_iotid_authorize(
        &self, app_config: AppConfig, user_profile_id: Option<String>, _app_scopes: Vec<String>,
        auth_code: Option<String>,
    ) -> TokenManagerResult<UserProfileInfo> {
        let ui_context = self
            .auth_context
            .get_new_ui_context()
            .map_err(|err| TokenManagerError::new(Status::InvalidAuthContext).with_cause(err))?;

        let auth_provider_proxy =
            await!(self.get_auth_provider_proxy(&app_config.auth_provider_type))?;

        // TODO(ukode): Create a new attestation signer handle for each request using the device
        // attestation key with better error handling.
        let (_server_chan, client_chan) = zx::Channel::create()
            .map_err(|err| TokenManagerError::new(Status::InternalError).with_cause(err))
            .expect("Failed to create attestation_signer");
        let attestation_signer = ClientEnd::<AttestationSignerMarker>::new(client_chan);

        // TODO(ukode): Add product root certificates and device attestation certificate to this
        // certificate chain.
        let certificate_chain = Vec::<String>::new();

        // TODO(ukode): Create an ephemeral credential key and add the public key params here.
        let credential_key = CredentialEcKey {
            curve: String::from("P-256"),
            key_x_val: String::from("TODO"),
            key_y_val: String::from("TODO"),
            fingerprint_sha_256: String::from("TODO"),
        };

        let mut attestation_jwt_params = AttestationJwtParams {
            credential_eckey: credential_key,
            certificate_chain: certificate_chain,
            auth_code: auth_code.unwrap_or("".to_string()),
        };

        let (status, credential, _access_token, auth_challenge, user_profile_info) = await!(
            auth_provider_proxy.get_persistent_credential_from_attestation_jwt(
                attestation_signer,
                &mut attestation_jwt_params,
                Some(ui_context),
                user_profile_id.as_ref().map(|x| &**x),
            )
        )
        .map_err(|err| TokenManagerError::new(Status::AuthProviderServerError).with_cause(err))?;

        match (credential, auth_challenge, user_profile_info) {
            (Some(credential), Some(_auth_challenge), Some(user_profile_info)) => {
                // Store persistent credential
                let db_value = CredentialValue::new(
                    app_config.auth_provider_type,
                    user_profile_info.id.clone(),
                    credential,
                    None,
                )
                .map_err(|_| Status::AuthProviderServerError)?;
                self.token_store.lock().add_credential(db_value)?;

                // TODO(ukode): Store credential keys

                // TODO(ukode): Cache auth_challenge
                Ok(*user_profile_info)
            }
            _ => Err(TokenManagerError::from(status)),
        }
    }

    /// Performs OAuth authorization for the supplied 'AppConfig' and returns an 'UserProfileInfo'
    /// on successful user consent.
    async fn handle_authorize(
        &self, app_config: AppConfig, user_profile_id: Option<String>, _app_scopes: Vec<String>,
        _auth_code: Option<String>,
    ) -> TokenManagerResult<UserProfileInfo> {
        let ui_context = self
            .auth_context
            .get_new_ui_context()
            .map_err(|err| TokenManagerError::new(Status::InvalidAuthContext).with_cause(err))?;

        let auth_provider_proxy =
            await!(self.get_auth_provider_proxy(&app_config.auth_provider_type))?;
        let (status, credential, user_profile_info) = await!(
            auth_provider_proxy.get_persistent_credential(
                Some(ui_context),
                user_profile_id.as_ref().map(|x| &**x),
            )
        )
        .map_err(|err| TokenManagerError::new(Status::AuthProviderServerError).with_cause(err))?;

        match (credential, user_profile_info) {
            (Some(credential), Some(user_profile_info)) => {
                let db_value = CredentialValue::new(
                    app_config.auth_provider_type,
                    user_profile_info.id.clone(),
                    credential,
                    None,
                )
                .map_err(|_| Status::AuthProviderServerError)?;
                self.token_store.lock().add_credential(db_value)?;
                Ok(*user_profile_info)
            }
            _ => Err(TokenManagerError::from(status)),
        }
    }

    /// Sends a downscoped OAuth access token for a given `AppConfig`, user, and set of scopes.
    async fn get_access_token(
        &self, app_config: AppConfig, user_profile_id: String, app_scopes: Vec<String>,
    ) -> TokenManagerResult<Arc<OAuthToken>> {
        let (db_key, cache_key) = Self::create_keys(&app_config, &user_profile_id)?;

        // Attempt to read the token from cache.
        if let Some(cached_token) = self
            .token_cache
            .lock()
            .get_access_token(&cache_key, &app_scopes)
        {
            return Ok(cached_token);
        }

        // If no cached entry was found use an auth provider to mint a new one from the refresh
        // token, then place it in the cache.
        let refresh_token = self.get_refresh_token(&db_key)?;

        // TODO(ukode, jsankey): This iotid check against the auth_provider_type is brittle and is
        // only a short-term solution. Eventually, this information will be coming from the
        // AuthProviderConfig params in some form or based on existence of credential_key for the
        // given user.
        if app_config
            .auth_provider_type
            .to_ascii_lowercase()
            .contains("iotid")
        {
            await!(self.handle_iotid_get_access_token(
                app_config,
                user_profile_id,
                refresh_token,
                app_scopes,
                cache_key,
            ))
        } else {
            await!(self.handle_get_access_token(app_config, refresh_token, app_scopes, cache_key))
        }
    }

    /// Exchanges an existing user grant for supplied 'AppConfig' to a shortlived access token
    /// 'AuthToken' using the connected devices flow.
    async fn handle_iotid_get_access_token(
        &self, app_config: AppConfig, user_profile_id: String, refresh_token: String,
        app_scopes: Vec<String>, cache_key: CacheKey,
    ) -> TokenManagerResult<Arc<OAuthToken>> {
        let auth_provider_proxy =
            await!(self.get_auth_provider_proxy(&app_config.auth_provider_type))?;

        // TODO(ukode): Retrieve the ephemeral credential key from store and add the public key
        // params here.
        let credential_key = CredentialEcKey {
            curve: String::from("P-256"),
            key_x_val: String::from("TODO"),
            key_y_val: String::from("TODO"),
            fingerprint_sha_256: String::from("TODO"),
        };

        // TODO(ukode): Create a new attestation signer handle for each request using the device
        // attestation key with better error handling.
        let (_server_chan, client_chan) = zx::Channel::create()
            .map_err(|err| TokenManagerError::new(Status::InternalError).with_cause(err))
            .expect("Failed to create attestation_signer");
        let attestation_signer = ClientEnd::<AttestationSignerMarker>::new(client_chan);

        // TODO(ukode): Read challenge from cache.
        let mut assertion_jwt_params = AssertionJwtParams {
            credential_eckey: credential_key,
            challenge: Some("".to_string()),
        };

        let scopes_copy = app_scopes.iter().map(|x| &**x).collect::<Vec<_>>();

        let (status, updated_credential, access_token, auth_challenge) = await!(
            auth_provider_proxy.get_app_access_token_from_assertion_jwt(
                attestation_signer,
                &mut assertion_jwt_params,
                &refresh_token,
                &mut scopes_copy.into_iter(),
            )
        )
        .map_err(|err| TokenManagerError::new(Status::AuthProviderServerError).with_cause(err))?;

        match (updated_credential, access_token, auth_challenge) {
            (Some(updated_credential), Some(access_token), Some(_auth_challenge)) => {
                // Store updated_credential in token store
                let db_value = CredentialValue::new(
                    app_config.auth_provider_type,
                    user_profile_id.clone(),
                    updated_credential,
                    None,
                )
                .map_err(|_| Status::AuthProviderServerError)?;

                self.token_store.lock().add_credential(db_value)?;

                // Cache access token
                let native_token = Arc::new(OAuthToken::from(*access_token));
                self.token_cache.lock().put_access_token(
                    cache_key,
                    &app_scopes,
                    Arc::clone(&native_token),
                );

                // TODO(ukode): Cache auth_challenge
                Ok(native_token)
            }
            _ => Err(TokenManagerError::from(status)),
        }
    }

    /// Exchanges an existing user grant for supplied 'AppConfig' to a shortlived access token
    /// 'AuthToken' using the traditional OAuth flow.
    async fn handle_get_access_token(
        &self, app_config: AppConfig, refresh_token: String, app_scopes: Vec<String>,
        cache_key: CacheKey,
    ) -> TokenManagerResult<Arc<OAuthToken>> {
        let auth_provider_proxy =
            await!(self.get_auth_provider_proxy(&app_config.auth_provider_type))?;
        let scopes_copy = app_scopes.iter().map(|x| &**x).collect::<Vec<_>>();
        let (status, provider_token) = await!(auth_provider_proxy.get_app_access_token(
            &refresh_token,
            app_config.client_id.as_ref().map(|x| &**x),
            &mut scopes_copy.into_iter(),
        ))
        .map_err(|err| TokenManagerError::new(Status::AuthProviderServerError).with_cause(err))?;

        let provider_token = provider_token.ok_or(TokenManagerError::from(status))?;
        let native_token = Arc::new(OAuthToken::from(*provider_token));
        self.token_cache
            .lock()
            .put_access_token(cache_key, &app_scopes, Arc::clone(&native_token));
        Ok(native_token)
    }

    /// Returns a JWT Identity token for a given `AppConfig`, user, and audience.
    async fn get_id_token(
        &self, app_config: AppConfig, user_profile_id: String, audience: Option<String>,
    ) -> TokenManagerResult<Arc<OAuthToken>> {
        let (db_key, cache_key) = Self::create_keys(&app_config, &user_profile_id)?;
        let audience_str = audience.clone().unwrap_or("".to_string());

        // Attempt to read the token from cache.
        if let Some(cached_token) = self
            .token_cache
            .lock()
            .get_id_token(&cache_key, &audience_str)
        {
            return Ok(cached_token);
        }

        // If no cached entry was found use an auth provider to mint a new one from the refresh
        // token, then place it in the cache.
        let refresh_token = self.get_refresh_token(&db_key)?;
        let auth_provider_proxy =
            await!(self.get_auth_provider_proxy(&app_config.auth_provider_type))?;
        let (status, provider_token) = await!(
            auth_provider_proxy.get_app_id_token(&refresh_token, audience.as_ref().map(|x| &**x))
        )
        .map_err(|err| TokenManagerError::new(Status::AuthProviderServerError).with_cause(err))?;
        let provider_token = provider_token.ok_or(TokenManagerError::from(status))?;
        let native_token = Arc::new(OAuthToken::from(*provider_token));
        self.token_cache
            .lock()
            .put_id_token(cache_key, audience_str, Arc::clone(&native_token));
        Ok(native_token)
    }

    /// Returns a Firebase a given `AppConfig`, user, audience, and Firebase API key, creating an
    /// ID token if necessary.
    async fn get_firebase_token(
        &self, app_config: AppConfig, user_profile_id: String, audience: String, api_key: String,
    ) -> TokenManagerResult<Arc<FirebaseAuthToken>> {
        let (_, cache_key) = Self::create_keys(&app_config, &user_profile_id)?;

        // Attempt to read the token from cache.
        if let Some(cached_token) = self
            .token_cache
            .lock()
            .get_firebase_token(&cache_key, &api_key)
        {
            return Ok(cached_token);
        }

        // If no cached entry was found use ourselves to fetch or mint an ID token then use that
        // to mint a new firebase token, which we also cache.
        let auth_provider_type = app_config.auth_provider_type.clone();
        let id_token_future = self.get_id_token(app_config, user_profile_id, Some(audience));
        let proxy_future = self.get_auth_provider_proxy(&auth_provider_type);
        let (id_token, auth_provider_proxy) = try_join!(id_token_future, proxy_future)?;
        let (status, provider_token) = await!(
            auth_provider_proxy.get_app_firebase_token(&*id_token, &api_key)
        )
        .map_err(|err| TokenManagerError::new(Status::AuthProviderServerError).with_cause(err))?;
        let provider_token = provider_token.ok_or(TokenManagerError::from(status))?;
        let native_token = Arc::new(FirebaseAuthToken::from(*provider_token));
        self.token_cache
            .lock()
            .put_firebase_token(cache_key, api_key, Arc::clone(&native_token));
        Ok(native_token)
    }

    /// Deletes any existing tokens for a user in both the database and cache.
    async fn delete_all_tokens(
        &self, app_config: AppConfig, user_profile_id: String,
    ) -> TokenManagerResult<()> {
        let (db_key, cache_key) = Self::create_keys(&app_config, &user_profile_id)?;

        // Try to find an associated refresh token, returning immediately with a success if we
        // can't.
        let refresh_token = match (**self.token_store.lock()).get_refresh_token(&db_key) {
            Ok(rt) => rt.to_string(),
            Err(AuthDbError::CredentialNotFound) => return Ok(()),
            Err(err) => return Err(TokenManagerError::from(err)),
        };

        // Request that the auth provider revoke the credential server-side.
        let auth_provider_proxy =
            await!(self.get_auth_provider_proxy(&app_config.auth_provider_type))?;
        let status =
            await!(auth_provider_proxy.revoke_app_or_persistent_credential(&refresh_token))
                .map_err(|err| {
                    TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
                })?;

        // In the case of probably-temporary AuthProvider failures we fail this method and expect
        // the client to retry. For other AuthProvider failures we continue to delete our copy of
        // the credential even if the server can't revoke it.  Note this means it will never be
        // possible to ask the server to revoke the token in the future, but it does let us clean
        // up broken tokens from our database.
        if status == AuthProviderStatus::NetworkError {
            return Err(TokenManagerError::from(status));
        }

        match self.token_cache.lock().delete(&cache_key) {
            Ok(()) | Err(AuthCacheError::KeyNotFound) => {}
            Err(err) => return Err(TokenManagerError::from(err)),
        }

        match self.token_store.lock().delete_credential(&db_key) {
            Ok(()) | Err(AuthDbError::CredentialNotFound) => {}
            Err(err) => return Err(TokenManagerError::from(err)),
        }

        Ok(())
    }

    /// Returns index keys for referencing a token in both the database and cache.
    fn create_keys(
        app_config: &AppConfig, user_profile_id: &String,
    ) -> Result<(CredentialKey, CacheKey), TokenManagerError> {
        let db_key = CredentialKey::new(
            app_config.auth_provider_type.clone(),
            user_profile_id.clone(),
        )
        .map_err(|_| TokenManagerError::new(Status::InvalidRequest))?;
        let cache_key = CacheKey::new(
            app_config.auth_provider_type.clone(),
            user_profile_id.clone(),
        )
        .map_err(|_| TokenManagerError::new(Status::InvalidRequest))?;
        Ok((db_key, cache_key))
    }

    /// Returns an `AuthProviderProxy` to handle requests for the supplied `AppConfig`.
    async fn get_auth_provider_proxy<'a>(
        &'a self, auth_provider_type: &'a str,
    ) -> TokenManagerResult<Arc<AuthProviderProxy>> {
        let auth_provider = match self.auth_providers.get(auth_provider_type) {
            Some(ap) => ap,
            None => {
                return Err(
                    TokenManagerError::new(Status::AuthProviderServiceUnavailable)
                        .with_cause(format_err!("Unknown auth provider {}", auth_provider_type)),
                );
            }
        };
        await!(auth_provider.get_proxy().map_err(|err| {
            TokenManagerError::new(Status::AuthProviderServiceUnavailable).with_cause(err)
        }))
    }

    /// Returns the current refresh token for a user from the data store.  Failure to find the user
    /// leads to an Error.
    fn get_refresh_token(&self, db_key: &CredentialKey) -> Result<String, TokenManagerError> {
        match (**self.token_store.lock()).get_refresh_token(db_key) {
            Ok(rt) => Ok(rt.to_string()),
            Err(err) => Err(TokenManagerError::from(err)),
        }
    }
}

/// A trait that we implement for the autogenerated FIDL responder types for each method to
/// simplify the process of responding.
trait Responder: Sized {
    type Data;

    /// Sends the supplied result logging any errors in the result or sending.  The return value is
    /// an error if the input was a fatal error, or Ok(()) otherwise.
    fn send_result(
        self, result: Result<Self::Data, TokenManagerError>,
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
    fn send_raw(self, status: Status, data: Option<Self::Data>) -> Result<(), fidl::Error>;

    /// Defines the name of the TokenManger method for use in logging.
    const METHOD_NAME: &'static str;
}

impl Responder for TokenManagerAuthorizeResponder {
    type Data = UserProfileInfo;
    const METHOD_NAME: &'static str = "Authorize";

    fn send_raw(
        self, status: Status, mut data: Option<UserProfileInfo>,
    ) -> Result<(), fidl::Error> {
        self.send(status, data.as_mut().map(|v| OutOfLine(v)))
    }
}

impl Responder for TokenManagerGetAccessTokenResponder {
    type Data = Arc<OAuthToken>;
    const METHOD_NAME: &'static str = "GetAccessToken";

    fn send_raw(self, status: Status, data: Option<Arc<OAuthToken>>) -> Result<(), fidl::Error> {
        self.send(status, data.as_ref().map(|v| &***v))
    }
}

impl Responder for TokenManagerGetIdTokenResponder {
    type Data = Arc<OAuthToken>;
    const METHOD_NAME: &'static str = "GetIdToken";

    fn send_raw(self, status: Status, data: Option<Arc<OAuthToken>>) -> Result<(), fidl::Error> {
        self.send(status, data.as_ref().map(|v| &***v))
    }
}

impl Responder for TokenManagerGetFirebaseTokenResponder {
    type Data = Arc<FirebaseAuthToken>;
    const METHOD_NAME: &'static str = "GetFirebaseToken";

    fn send_raw(
        self, status: Status, data: Option<Arc<FirebaseAuthToken>>,
    ) -> Result<(), fidl::Error> {
        let mut fidl_data = data.map(|v| v.to_fidl());
        self.send(status, fidl_data.as_mut().map(|v| OutOfLine(v)))
    }
}

impl Responder for TokenManagerDeleteAllTokensResponder {
    type Data = ();
    const METHOD_NAME: &'static str = "DeleteAllTokens";

    fn send_raw(self, status: Status, _data: Option<()>) -> Result<(), fidl::Error> {
        self.send(status)
    }
}
