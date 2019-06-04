// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::tokens::{AccessTokenKey, FirebaseAuthToken, FirebaseTokenKey, IdTokenKey, OAuthToken};
use crate::{AuthProviderSupplier, ResultExt, TokenManagerContext, TokenManagerError};
use failure::format_err;
use fidl;
use fidl::encoding::OutOfLine;
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_auth::{
    AppConfig, AssertionJwtParams, AttestationJwtParams, AttestationSignerMarker,
    AuthProviderProxy, AuthProviderStatus, AuthenticationUiContextMarker, CredentialEcKey, Status,
    TokenManagerAuthorizeResponder, TokenManagerDeleteAllTokensResponder,
    TokenManagerGetAccessTokenResponder, TokenManagerGetFirebaseTokenResponder,
    TokenManagerGetIdTokenResponder, TokenManagerListProfileIdsResponder, TokenManagerRequest,
    TokenManagerRequestStream, UserProfileInfo,
};
use fuchsia_zircon as zx;
use futures::prelude::*;
use futures::try_join;
use identity_common::{cancel_or, TaskGroup, TaskGroupCancel};
use log::{error, info, warn};
use parking_lot::Mutex;
use std::collections::HashMap;
use std::path::Path;
use std::sync::Arc;
use token_cache::{AuthCacheError, TokenCache};
use token_store::file::AuthDbFile;
use token_store::{AuthDb, AuthDbError, CredentialKey, CredentialValue};

/// The maximum number of entries to be stored in the `TokenCache`.
const CACHE_SIZE: usize = 128;

type TokenManagerResult<T> = Result<T, TokenManagerError>;

/// The supplier references and mutable state used to create, store, and cache authentication
/// tokens for a particular user across a range of third party services.
pub struct TokenManager<T: AuthProviderSupplier> {
    /// An object capable of supplying AuthProvider connections.
    auth_provider_supplier: T,
    /// A cache of proxies for previously used connections to AuthProviders.
    auth_providers: Mutex<HashMap<String, Arc<AuthProviderProxy>>>,
    /// A persistent store of long term credentials.
    token_store: Mutex<Box<AuthDb + Send + Sync>>,
    /// An in-memory cache of recently used tokens.
    token_cache: Mutex<TokenCache>,
    /// Collection of tasks that are using this instance.
    task_group: TaskGroup,
}

impl<T: AuthProviderSupplier> TokenManager<T> {
    /// Creates a new TokenManager.
    pub fn new(
        db_path: &Path,
        auth_provider_supplier: T,
        task_group: TaskGroup,
    ) -> Result<Self, failure::Error> {
        let token_store = AuthDbFile::new(db_path)
            .map_err(|err| format_err!("Error creating AuthDb at {:?}, {:?}", db_path, err))?;
        let token_cache = TokenCache::new(CACHE_SIZE);

        Ok(TokenManager {
            auth_provider_supplier,
            auth_providers: Mutex::new(HashMap::new()),
            token_store: Mutex::new(Box::new(token_store)),
            token_cache: Mutex::new(token_cache),
            task_group,
        })
    }

    /// Returns a task group which can be used to spawn and cancel tasks that use this instance.
    pub fn task_group(&self) -> &TaskGroup {
        &self.task_group
    }

    /// Asynchronously handles the supplied stream of `TokenManagerRequest` messages.
    pub async fn handle_requests_from_stream<'a>(
        &'a self,
        context: &'a TokenManagerContext,
        mut stream: TokenManagerRequestStream,
        cancel: TaskGroupCancel,
    ) -> Result<(), failure::Error> {
        // TODO(dnordstrom): Allow cancellation within long running requests
        while let Some(result) = await!(cancel_or(&cancel, stream.try_next())) {
            match result? {
                Some(request) => await!(self.handle_request(context, request))?,
                None => break,
            }
        }
        Ok(())
    }

    /// Handles a single request to the TokenManager by dispatching to more specific functions for
    /// each method.
    pub async fn handle_request<'a>(
        &'a self,
        context: &'a TokenManagerContext,
        req: TokenManagerRequest,
    ) -> Result<(), failure::Error> {
        // TODO(jsankey): Determine how best to enforce the application_url in context.
        match req {
            TokenManagerRequest::Authorize {
                app_config,
                auth_ui_context,
                user_profile_id,
                app_scopes,
                auth_code,
                responder,
            } => responder.send_result(await!(self.authorize(
                context,
                app_config,
                auth_ui_context,
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
                force,
                responder,
            } => responder.send_result(await!(self.delete_all_tokens(
                app_config,
                user_profile_id,
                force
            ))),
            TokenManagerRequest::ListProfileIds { app_config, responder } => {
                responder.send_result(self.list_profile_ids(app_config))
            }
        }
    }

    /// Implements the FIDL TokenManager.Authorize method.
    async fn authorize<'a>(
        &'a self,
        context: &'a TokenManagerContext,
        app_config: AppConfig,
        _auth_ui_context: Option<ClientEnd<AuthenticationUiContextMarker>>,
        user_profile_id: Option<String>,
        app_scopes: Vec<String>,
        auth_code: Option<String>,
    ) -> TokenManagerResult<UserProfileInfo> {
        // TODO(jsankey): Currently auth_ui_context is neither supplied by Topaz nor allowed to
        // override the auth UI context supplied at token manager construction (AUTH-110).
        // Depending on the outcome of design discussions either pass it through or remove it
        // entirely.
        // TODO(ukode, jsankey): This iotid check against the auth_provider_type is brittle and is
        // only a short-term solution. Eventually, this information will be coming from the
        // AuthProviderConfig params in some form.
        if app_config.auth_provider_type.to_ascii_lowercase().contains("iotid") {
            await!(self.handle_iotid_authorize(
                context,
                app_config,
                user_profile_id,
                app_scopes,
                auth_code
            ))
        } else {
            await!(self.handle_authorize(context, app_config, user_profile_id, app_scopes))
        }
    }

    /// Implements the FIDL TokenManager.Authorize method using an AuthProvider that supports
    /// IoT ID.
    async fn handle_iotid_authorize<'a>(
        &'a self,
        context: &'a TokenManagerContext,
        app_config: AppConfig,
        user_profile_id: Option<String>,
        _app_scopes: Vec<String>,
        auth_code: Option<String>,
    ) -> TokenManagerResult<UserProfileInfo> {
        let auth_provider_type = &app_config.auth_provider_type;
        let auth_provider_proxy = await!(self.get_auth_provider_proxy(auth_provider_type))?;

        let (ui_context_client_end, ui_context_server_end) =
            create_endpoints().token_manager_status(Status::UnknownError)?;
        context
            .auth_ui_context_provider
            .get_authentication_ui_context(ui_context_server_end)
            .token_manager_status(Status::InvalidAuthContext)?;

        // TODO(ukode): Create a new attestation signer handle for each request using the device
        // attestation key with better error handling.
        let (attestation_signer_client_end, _) =
            create_endpoints().token_manager_status(Status::UnknownError)?;

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

        let (status, credential, _access_token, auth_challenge, user_profile_info) =
            await!(auth_provider_proxy.get_persistent_credential_from_attestation_jwt(
                attestation_signer_client_end,
                &mut attestation_jwt_params,
                Some(ui_context_client_end),
                user_profile_id.as_ref().map(|x| &**x),
            ))
            .map_err(|err| {
                self.discard_auth_provider_proxy(auth_provider_type);
                TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
            })?;

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

    /// Implements the FIDL TokenManager.Authorize method using an AuthProvider that does not
    /// support IoT ID.
    async fn handle_authorize<'a>(
        &'a self,
        context: &'a TokenManagerContext,
        app_config: AppConfig,
        user_profile_id: Option<String>,
        _app_scopes: Vec<String>,
    ) -> TokenManagerResult<UserProfileInfo> {
        let auth_provider_type = &app_config.auth_provider_type;
        let auth_provider_proxy = await!(self.get_auth_provider_proxy(auth_provider_type))?;

        let (ui_context_client_end, ui_context_server_end) =
            create_endpoints().token_manager_status(Status::UnknownError)?;
        context
            .auth_ui_context_provider
            .get_authentication_ui_context(ui_context_server_end)
            .token_manager_status(Status::InvalidAuthContext)?;

        let (status, credential, user_profile_info) = await!(auth_provider_proxy
            .get_persistent_credential(
                Some(ui_context_client_end),
                user_profile_id.as_ref().map(|x| &**x),
            ))
        .map_err(|err| {
            self.discard_auth_provider_proxy(auth_provider_type);
            TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
        })?;

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

    /// Implements the FIDL TokenManager.GetAccessToken method.
    async fn get_access_token(
        &self,
        app_config: AppConfig,
        user_profile_id: String,
        app_scopes: Vec<String>,
    ) -> TokenManagerResult<Arc<OAuthToken>> {
        let cache_key = AccessTokenKey::new(
            app_config.auth_provider_type.clone(),
            user_profile_id.clone(),
            &app_scopes,
        )
        .map_err(|_| TokenManagerError::new(Status::InvalidRequest))?;
        // Attempt to read the token from cache.
        if let Some(cached_token) = self.token_cache.lock().get(&cache_key) {
            return Ok(cached_token);
        }

        // If no cached entry was found use an auth provider to mint a new one from the refresh
        // token, then place it in the cache.
        let db_key = Self::create_db_key(&app_config, &user_profile_id)?;
        let refresh_token = self.get_refresh_token(&db_key)?;

        // TODO(ukode, jsankey): This iotid check against the auth_provider_type is brittle and is
        // only a short-term solution. Eventually, this information will be coming from the
        // AuthProviderConfig params in some form or based on existence of credential_key for the
        // given user.
        if app_config.auth_provider_type.to_ascii_lowercase().contains("iotid") {
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

    /// Implements the FIDL TokenManager.GetAccessToken method using an auth provider that supports
    /// IoT ID.
    async fn handle_iotid_get_access_token(
        &self,
        app_config: AppConfig,
        user_profile_id: String,
        refresh_token: String,
        app_scopes: Vec<String>,
        cache_key: AccessTokenKey,
    ) -> TokenManagerResult<Arc<OAuthToken>> {
        let auth_provider_type = &app_config.auth_provider_type;
        let auth_provider_proxy = await!(self.get_auth_provider_proxy(auth_provider_type))?;

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
            .token_manager_status(Status::InternalError)
            .expect("Failed to create attestation_signer");
        let attestation_signer = ClientEnd::<AttestationSignerMarker>::new(client_chan);

        // TODO(ukode): Read challenge from cache.
        let mut assertion_jwt_params = AssertionJwtParams {
            credential_eckey: credential_key,
            challenge: Some("".to_string()),
        };

        let (status, updated_credential, access_token, auth_challenge) =
            await!(auth_provider_proxy.get_app_access_token_from_assertion_jwt(
                attestation_signer,
                &mut assertion_jwt_params,
                &refresh_token,
                &mut app_scopes.iter().map(|x| &**x),
            ))
            .map_err(|err| {
                self.discard_auth_provider_proxy(auth_provider_type);
                TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
            })?;

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
                self.token_cache.lock().put(cache_key, Arc::clone(&native_token));

                // TODO(ukode): Cache auth_challenge
                Ok(native_token)
            }
            _ => Err(TokenManagerError::from(status)),
        }
    }

    /// Implements the FIDL TokenManager.GetAccessToken method using an auth provider that does not
    /// support IoT ID.
    async fn handle_get_access_token(
        &self,
        app_config: AppConfig,
        refresh_token: String,
        app_scopes: Vec<String>,
        cache_key: AccessTokenKey,
    ) -> TokenManagerResult<Arc<OAuthToken>> {
        let auth_provider_type = &app_config.auth_provider_type;
        let auth_provider_proxy = await!(self.get_auth_provider_proxy(auth_provider_type))?;

        let (status, provider_token) = await!(auth_provider_proxy.get_app_access_token(
            &refresh_token,
            app_config.client_id.as_ref().map(|x| &**x),
            &mut app_scopes.iter().map(|x| &**x),
        ))
        .map_err(|err| {
            self.discard_auth_provider_proxy(auth_provider_type);
            TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
        })?;

        let provider_token = provider_token.ok_or(TokenManagerError::from(status))?;
        let native_token = Arc::new(OAuthToken::from(*provider_token));
        self.token_cache.lock().put(cache_key, Arc::clone(&native_token));
        Ok(native_token)
    }

    /// Implements the FIDL TokenManager.GetIdToken method.
    async fn get_id_token(
        &self,
        app_config: AppConfig,
        user_profile_id: String,
        audience: Option<String>,
    ) -> TokenManagerResult<Arc<OAuthToken>> {
        let audience_str = audience.clone().unwrap_or("".to_string());
        let cache_key = IdTokenKey::new(
            app_config.auth_provider_type.clone(),
            user_profile_id.clone(),
            audience_str,
        )
        .map_err(|_| TokenManagerError::new(Status::InvalidRequest))?;

        // Attempt to read the token from cache.
        if let Some(cached_token) = self.token_cache.lock().get(&cache_key) {
            return Ok(cached_token);
        }

        // If no cached entry was found use an auth provider to mint a new one from the refresh
        // token, then place it in the cache.
        let db_key = Self::create_db_key(&app_config, &user_profile_id)?;
        let refresh_token = self.get_refresh_token(&db_key)?;
        let auth_provider_type = &app_config.auth_provider_type;
        let auth_provider_proxy = await!(self.get_auth_provider_proxy(auth_provider_type))?;
        let (status, provider_token) =
            await!(auth_provider_proxy
                .get_app_id_token(&refresh_token, audience.as_ref().map(|x| &**x)))
            .map_err(|err| {
                self.discard_auth_provider_proxy(auth_provider_type);
                TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
            })?;

        let provider_token = provider_token.ok_or(TokenManagerError::from(status))?;
        let native_token = Arc::new(OAuthToken::from(*provider_token));
        self.token_cache.lock().put(cache_key, Arc::clone(&native_token));
        Ok(native_token)
    }

    /// Implements the FIDL TokenManager.GetFirebaseToken method.
    async fn get_firebase_token(
        &self,
        app_config: AppConfig,
        user_profile_id: String,
        audience: String,
        api_key: String,
    ) -> TokenManagerResult<Arc<FirebaseAuthToken>> {
        let cache_key = FirebaseTokenKey::new(
            app_config.auth_provider_type.clone(),
            user_profile_id.clone(),
            api_key.clone(),
        )
        .map_err(|_| TokenManagerError::new(Status::InvalidRequest))?;

        // Attempt to read the token from cache.
        if let Some(cached_token) = self.token_cache.lock().get(&cache_key) {
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
        .map_err(|err| {
            self.discard_auth_provider_proxy(&auth_provider_type);
            TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
        })?;
        let provider_token = provider_token.ok_or(TokenManagerError::from(status))?;
        let native_token = Arc::new(FirebaseAuthToken::from(*provider_token));
        self.token_cache.lock().put(cache_key, Arc::clone(&native_token));
        Ok(native_token)
    }

    /// Implements the FIDL TokenManager.DeleteAllTokens method.
    ///
    /// This deletes any existing tokens for a user in both the database and cache and requests
    /// that the service provider revoke them.
    async fn delete_all_tokens(
        &self,
        app_config: AppConfig,
        user_profile_id: String,
        force: bool,
    ) -> TokenManagerResult<()> {
        let db_key = Self::create_db_key(&app_config, &user_profile_id)?;

        // Try to find an associated refresh token, returning immediately with a success if we
        // can't.
        let refresh_token = match (**self.token_store.lock()).get_refresh_token(&db_key) {
            Ok(rt) => rt.to_string(),
            Err(AuthDbError::CredentialNotFound) => return Ok(()),
            Err(err) => return Err(TokenManagerError::from(err)),
        };

        // Request that the auth provider revoke the credential server-side.
        let auth_provider_type = &app_config.auth_provider_type;
        let auth_provider_proxy = await!(self.get_auth_provider_proxy(auth_provider_type))?;
        let status =
            await!(auth_provider_proxy.revoke_app_or_persistent_credential(&refresh_token))
                .map_err(|err| {
                    self.discard_auth_provider_proxy(auth_provider_type);
                    TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
                })?;

        if status != AuthProviderStatus::Ok {
            if force {
                warn!("Removing stored tokens even though revocation failed with {:?}", status)
            } else {
                return Err(TokenManagerError::from(status));
            }
        }

        match self.token_cache.lock().delete_matching(&auth_provider_type, &user_profile_id) {
            Ok(()) | Err(AuthCacheError::KeyNotFound) => {}
            Err(err) => return Err(TokenManagerError::from(err)),
        }

        match self.token_store.lock().delete_credential(&db_key) {
            Ok(()) | Err(AuthDbError::CredentialNotFound) => {}
            Err(err) => return Err(TokenManagerError::from(err)),
        }

        Ok(())
    }

    /// Implements the FIDL TokenManager.ListProfileIds method.
    fn list_profile_ids(&self, app_config: AppConfig) -> TokenManagerResult<Vec<String>> {
        let token_store = self.token_store.lock();
        Ok(token_store
            .get_all_credential_keys()?
            .into_iter()
            .filter(|k| k.auth_provider_type() == &app_config.auth_provider_type)
            .map(|k| k.user_profile_id().to_string())
            .collect())
    }

    /// Returns index keys for referencing a token in the database.
    fn create_db_key(
        app_config: &AppConfig,
        user_profile_id: &String,
    ) -> Result<CredentialKey, TokenManagerError> {
        let db_key =
            CredentialKey::new(app_config.auth_provider_type.clone(), user_profile_id.clone())
                .map_err(|_| TokenManagerError::new(Status::InvalidRequest))?;
        Ok(db_key)
    }

    /// Returns an `AuthProviderProxy` for the specified `auth_provider_type` either by returning
    /// a previously created copy or by acquiring a new one from the `AuthProviderSupplier`.
    async fn get_auth_provider_proxy<'a>(
        &'a self,
        auth_provider_type: &'a str,
    ) -> TokenManagerResult<Arc<AuthProviderProxy>> {
        if let Some(auth_provider) = self.auth_providers.lock().get(auth_provider_type) {
            return Ok(Arc::clone(auth_provider));
        }

        let client_end = await!(self.auth_provider_supplier.get(auth_provider_type))?;
        let proxy = Arc::new(client_end.into_proxy().token_manager_status(Status::UnknownError)?);
        self.auth_providers.lock().insert(auth_provider_type.to_string(), Arc::clone(&proxy));

        // TODO(jsankey): AuthProviders might crash or close connections, leaving our cached proxy
        // in an invalid state. Currently we explicitly discard a proxy from each method that
        // observes a communication failure, but we should probably also be monitoring for the
        // close event on each channel to remove the associated proxy from the cache automatically.

        Ok(proxy)
    }

    /// Removes an `AuthProviderProxy` from the local cache, if one is found.
    fn discard_auth_provider_proxy(&self, auth_provider_type: &str) {
        self.auth_providers.lock().remove(auth_provider_type);
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
        self,
        result: Result<Self::Data, TokenManagerError>,
    ) -> Result<(), failure::Error> {
        match result {
            Ok(val) => {
                if let Err(err) = self.send_raw(Status::Ok, Some(val)) {
                    warn!("Error sending response to {}: {:?}", Self::METHOD_NAME, &err);
                }
                Ok(())
            }
            Err(err) => {
                if let Err(err) = self.send_raw(err.status, None) {
                    warn!("Error sending error response to {}: {:?}", Self::METHOD_NAME, &err);
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
        self,
        status: Status,
        mut data: Option<UserProfileInfo>,
    ) -> Result<(), fidl::Error> {
        // Explicitly log successes for the infrequent Authorize request to help debug.
        if status == Status::Ok {
            info!("Success authorizing new account");
        }
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
        self,
        status: Status,
        data: Option<Arc<FirebaseAuthToken>>,
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

impl Responder for TokenManagerListProfileIdsResponder {
    type Data = Vec<String>;
    const METHOD_NAME: &'static str = "ListProfileIds";

    fn send_raw(self, status: Status, data: Option<Vec<String>>) -> Result<(), fidl::Error> {
        match data {
            None => self.send(status, &mut std::iter::empty()),
            Some(profile_ids) => self.send(status, &mut profile_ids.iter().map(|x| &**x)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::fake_auth_provider_supplier::FakeAuthProviderSupplier;
    use fidl::endpoints::{create_proxy, create_proxy_and_stream};
    use fidl_fuchsia_auth::{AuthenticationContextProviderMarker, TokenManagerMarker};
    use fuchsia_async as fasync;
    use tempfile::TempDir;

    fn create_app_config(auth_provider_type: &str) -> AppConfig {
        AppConfig {
            auth_provider_type: auth_provider_type.to_string(),
            client_id: None,
            client_secret: None,
            redirect_uri: None,
        }
    }

    fn create_token_manager_context() -> TokenManagerContext {
        let (ui_context_provider_proxy, _) =
            create_proxy::<AuthenticationContextProviderMarker>().unwrap();
        TokenManagerContext {
            application_url: "APPLICATION_URL".to_string(),
            auth_ui_context_provider: ui_context_provider_proxy,
        }
    }

    /// Create a TokenManager and terminate it using its task group.
    #[fasync::run_until_stalled(test)]
    async fn create_and_terminate() {
        let auth_provider_supplier = FakeAuthProviderSupplier::new();
        let tmp_dir = TempDir::new().expect("failed creating temp dir");
        let db_path = tmp_dir.path().join("tokens.json");
        let task_group = TaskGroup::new();

        let token_manager = Arc::new(
            TokenManager::new(&db_path, auth_provider_supplier, task_group.clone())
                .expect("failed creating TokenManager"),
        );
        let (proxy, stream) = create_proxy_and_stream::<TokenManagerMarker>().unwrap();
        let token_manager_clone = Arc::clone(&token_manager);
        await!(token_manager.task_group().spawn(|cancel| async move {
            let context = create_token_manager_context();
            assert!(await!(
                token_manager_clone.handle_requests_from_stream(&context, stream, cancel)
            )
            .is_ok());
        }))
        .expect("spawning failed");
        assert!(await!(token_manager.task_group().cancel()).is_ok());
        // Check that the accessor's task group cancelled the task group that was passed to new()
        assert!(await!(task_group.cancel()).is_err());

        // Now the channel should be closed
        let mut app_config = create_app_config("hooli");
        assert!(await!(proxy.list_profile_ids(&mut app_config)).is_err());
    }
}
