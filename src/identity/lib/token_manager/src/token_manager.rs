// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::auth_provider_cache::AuthProviderCache;
use crate::tokens::{AccessTokenKey, IdTokenKey, OAuthToken};
use crate::{AuthProviderSupplier, ResultExt, TokenManagerContext, TokenManagerError};
use anyhow::format_err;
use fidl;
use fidl::endpoints::{create_endpoints, ClientEnd};
use fidl_fuchsia_auth::{
    AppConfig, AuthenticationUiContextMarker, Status, TokenManagerAuthorizeResponder,
    TokenManagerDeleteAllTokensResponder, TokenManagerGetAccessTokenResponder,
    TokenManagerGetIdTokenResponder, TokenManagerListProfileIdsResponder, TokenManagerRequest,
    TokenManagerRequestStream, UserProfileInfo,
};
use fidl_fuchsia_identity_external::{
    OauthAccessTokenFromOauthRefreshTokenRequest, OauthRefreshTokenRequest,
    OpenIdTokenFromOauthRefreshTokenRequest, OpenIdUserInfoFromOauthAccessTokenRequest,
};
use fidl_fuchsia_identity_tokens::OauthRefreshToken;
use futures::prelude::*;
use identity_common::{cancel_or, TaskGroup, TaskGroupCancel};
use log::{error, info, warn};
use parking_lot::Mutex;
use std::convert::TryInto;
use std::path::Path;
use std::sync::Arc;
use token_cache::{AuthCacheError, TokenCache};
use token_store::file::AuthDbFile;
use token_store::mem::AuthDbInMemory;
use token_store::{AuthDb, AuthDbError, CredentialKey, CredentialValue};

/// The maximum number of entries to be stored in the `TokenCache`.
const CACHE_SIZE: usize = 128;

type TokenManagerResult<T> = Result<T, TokenManagerError>;

/// The supplier references and mutable state used to create, store, and cache authentication
/// tokens for a particular user across a range of third party services.
pub struct TokenManager<APS: AuthProviderSupplier> {
    /// An in-memory cache that retrieves and caches token provider proxies.
    auth_provider_cache: AuthProviderCache<APS>,
    /// A persistent store of long term credentials.
    token_store: Mutex<Box<dyn AuthDb + Send + Sync>>,
    /// An in-memory cache of recently used tokens.
    token_cache: Mutex<TokenCache>,
    /// Collection of tasks that are using this instance.
    task_group: TaskGroup,
}

impl<APS: AuthProviderSupplier> TokenManager<APS> {
    /// Creates a new TokenManager, backed by a file db.
    pub fn new(
        db_path: &Path,
        auth_provider_supplier: APS,
        task_group: TaskGroup,
    ) -> Result<Self, anyhow::Error> {
        let token_store = AuthDbFile::new(db_path)
            .map_err(|err| format_err!("Error creating AuthDb at {:?}, {:?}", db_path, err))?;
        let token_cache = TokenCache::new(CACHE_SIZE);

        Ok(TokenManager {
            auth_provider_cache: AuthProviderCache::new(auth_provider_supplier),
            token_store: Mutex::new(Box::new(token_store)),
            token_cache: Mutex::new(token_cache),
            task_group,
        })
    }

    /// Create a new in-memory TokenManager.
    pub fn new_in_memory(auth_provider_supplier: APS, task_group: TaskGroup) -> Self {
        let token_store = AuthDbInMemory::new();
        let token_cache = TokenCache::new(CACHE_SIZE);
        TokenManager {
            auth_provider_cache: AuthProviderCache::new(auth_provider_supplier),
            token_store: Mutex::new(Box::new(token_store)),
            token_cache: Mutex::new(token_cache),
            task_group,
        }
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
    ) -> Result<(), anyhow::Error> {
        // TODO(dnordstrom): Allow cancellation within long running requests
        while let Some(result) = cancel_or(&cancel, stream.try_next()).await {
            match result? {
                Some(request) => self.handle_request(context, request).await?,
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
    ) -> Result<(), anyhow::Error> {
        // TODO(jsankey): Determine how best to enforce the application_url in context.
        match req {
            TokenManagerRequest::Authorize {
                app_config,
                auth_ui_context,
                user_profile_id,
                app_scopes,
                auth_code,
                responder,
            } => responder.send_result(
                self.authorize(
                    context,
                    app_config,
                    auth_ui_context,
                    user_profile_id,
                    app_scopes,
                    auth_code,
                )
                .await,
            ),
            TokenManagerRequest::GetAccessToken {
                app_config,
                user_profile_id,
                app_scopes,
                responder,
            } => responder
                .send_result(self.get_access_token(app_config, user_profile_id, app_scopes).await),
            TokenManagerRequest::GetIdToken {
                app_config,
                user_profile_id,
                audience,
                responder,
            } => responder
                .send_result(self.get_id_token(app_config, user_profile_id, audience).await),
            TokenManagerRequest::DeleteAllTokens {
                app_config,
                user_profile_id,
                force,
                responder,
            } => responder
                .send_result(self.delete_all_tokens(app_config, user_profile_id, force).await),
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
        _app_scopes: Vec<String>,
        _auth_code: Option<String>,
    ) -> TokenManagerResult<UserProfileInfo> {
        // TODO(jsankey): Currently auth_ui_context is neither supplied by Topaz nor allowed to
        // override the auth UI context supplied at token manager construction (fxbug.dev/459).
        // Depending on the outcome of design discussions either pass it through or remove it
        // entirely.
        let auth_provider_type = &app_config.auth_provider_type;
        let oauth_proxy = self.auth_provider_cache.get_oauth_proxy(auth_provider_type).await?;

        let (ui_context_client_end, ui_context_server_end) =
            create_endpoints().token_manager_status(Status::UnknownError)?;
        context
            .auth_ui_context_provider
            .get_authentication_ui_context(ui_context_server_end)
            .token_manager_status(Status::InvalidAuthContext)?;

        let (refresh_token, access_token) = oauth_proxy
            .create_refresh_token(OauthRefreshTokenRequest {
                account_id: user_profile_id,
                ui_context: Some(ui_context_client_end),
            })
            .await
            .map_err(|err| {
                TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
            })??;

        // Get user profile info
        let oauth_open_id_proxy =
            self.auth_provider_cache.get_oauth_open_id_connect_proxy(auth_provider_type).await?;

        let user_profile_info = oauth_open_id_proxy
            .get_user_info_from_access_token(OpenIdUserInfoFromOauthAccessTokenRequest {
                access_token: Some(access_token),
            })
            .await
            .map_err(|err| {
                TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
            })??;
        let db_value = CredentialValue::new(
            app_config.auth_provider_type,
            user_profile_info.subject.clone().ok_or(Status::AuthProviderServerError)?,
            refresh_token.content.ok_or(Status::AuthProviderServerError)?,
            None,
        )
        .map_err(|_| Status::AuthProviderServerError)?;
        self.token_store.lock().add_credential(db_value)?;

        Ok(UserProfileInfo {
            id: user_profile_info.subject.ok_or(Status::AuthProviderServerError)?,
            display_name: user_profile_info.name,
            url: None,
            image_url: user_profile_info.picture,
        })
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
        .token_manager_status(Status::InvalidRequest)?;

        // Attempt to read the token from cache.
        if let Some(cached_token) = self.token_cache.lock().get(&cache_key) {
            return Ok(cached_token);
        }

        // If no cached entry was found use an auth provider to mint a new one from the refresh
        // token, then place it in the cache.
        let db_key = Self::create_db_key(&app_config, &user_profile_id)?;
        let refresh_token = self.get_refresh_token(&db_key)?;

        self.handle_get_access_token(app_config, refresh_token, app_scopes, cache_key).await
    }

    /// Implements the FIDL TokenManager.GetAccessToken method using an auth provider that does not
    /// support IoT ID.
    async fn handle_get_access_token(
        &self,
        app_config: AppConfig,
        refresh_token: OauthRefreshToken,
        app_scopes: Vec<String>,
        cache_key: AccessTokenKey,
    ) -> TokenManagerResult<Arc<OAuthToken>> {
        let auth_provider_type = &app_config.auth_provider_type;
        let oauth_proxy = self.auth_provider_cache.get_oauth_proxy(auth_provider_type).await?;

        let provider_token = oauth_proxy
            .get_access_token_from_refresh_token(OauthAccessTokenFromOauthRefreshTokenRequest {
                refresh_token: Some(refresh_token),
                client_id: app_config.client_id.clone(),
                scopes: Some(app_scopes),
            })
            .await
            .map_err(|err| {
                TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
            })??;

        let native_token = Arc::new(provider_token.try_into()?);
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
        .token_manager_status(Status::InvalidRequest)?;

        // Attempt to read the token from cache.
        if let Some(cached_token) = self.token_cache.lock().get(&cache_key) {
            return Ok(cached_token);
        }

        // If no cached entry was found use an auth provider to mint a new one from the refresh
        // token, then place it in the cache.
        let db_key = Self::create_db_key(&app_config, &user_profile_id)?;
        let refresh_token = self.get_refresh_token(&db_key)?;
        let auth_provider_type = &app_config.auth_provider_type;
        let oauth_open_id_proxy =
            self.auth_provider_cache.get_oauth_open_id_connect_proxy(auth_provider_type).await?;
        let get_id_token_request = OpenIdTokenFromOauthRefreshTokenRequest {
            refresh_token: Some(refresh_token),
            audiences: audience.map(|audience| vec![audience]),
        };

        let provider_token =
            match oauth_open_id_proxy.get_id_token_from_refresh_token(get_id_token_request).await {
                Ok(api_result) => api_result?,
                Err(fidl_err) => {
                    return Err(TokenManagerError::new(Status::AuthProviderServerError)
                        .with_cause(fidl_err));
                }
            };

        // TODO(satsukiu): At the moment, the ID token is stored as an AuthToken type because
        // the fuchsia.auth.TokenManager protocol returns AuthTokens.  As part of the migration
        // to fuchsia.identity.tokens ID tokens should be stored as a separate type from Oauth
        // tokens.
        let native_token = Arc::new(provider_token.try_into()?);
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
        let oauth_proxy = self.auth_provider_cache.get_oauth_proxy(auth_provider_type).await?;
        let result = oauth_proxy
            .revoke_refresh_token(OauthRefreshToken {
                content: Some(refresh_token),
                account_id: None,
            })
            .await
            .map_err(|err| {
                TokenManagerError::new(Status::AuthProviderServerError).with_cause(err)
            })?;

        if let Err(api_error) = result {
            if force {
                warn!("Removing stored tokens even though revocation failed with {:?}", api_error)
            } else {
                return Err(TokenManagerError::from(api_error));
            }
        }

        // TODO(fxbug.dev/43340): define when revocation for id tokens are called, and behaviors when the
        // corresponding protocols return errors or are not implemented.

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

    /// Returns the current refresh token for a user from the data store.  Failure to find the user
    /// leads to an Error.
    fn get_refresh_token(
        &self,
        db_key: &CredentialKey,
    ) -> Result<OauthRefreshToken, TokenManagerError> {
        match (**self.token_store.lock()).get_refresh_token(db_key) {
            Ok(rt) => Ok(OauthRefreshToken {
                content: Some(rt.to_string()),
                account_id: Some(db_key.user_profile_id().to_string()),
            }),
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
    ) -> Result<(), anyhow::Error> {
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
                    Err(anyhow::Error::from(err))
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
        self.send(status, data.as_mut())
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
    use fidl::endpoints::create_proxy_and_stream;
    use fidl_fuchsia_auth::{
        AuthenticationContextProviderMarker, TokenManagerMarker, TokenManagerProxy,
    };
    use fidl_fuchsia_identity_external::{OauthOpenIdConnectRequest, OauthRequest};
    use fidl_fuchsia_identity_tokens::{OauthAccessToken, OpenIdToken, OpenIdUserInfo};
    use fuchsia_async as fasync;
    use fuchsia_zircon::{ClockId, Duration, Time};
    use futures::channel::oneshot;
    use futures::future::join3;
    use tempfile::TempDir;

    /// Generate a pre-populated AppConfig fidl struct
    fn create_app_config(auth_provider_type: &str) -> AppConfig {
        AppConfig {
            auth_provider_type: auth_provider_type.to_string(),
            client_id: None,
            client_secret: None,
            redirect_uri: None,
        }
    }

    /// Generate a TokenManagerContext which answers standard calls
    fn create_token_manager_context() -> TokenManagerContext {
        let (ui_context_provider_proxy, mut stream) =
            create_proxy_and_stream::<AuthenticationContextProviderMarker>().unwrap();
        fasync::Task::spawn(async move { while let Some(_) = stream.try_next().await.unwrap() {} })
            .detach();
        TokenManagerContext {
            application_url: "APPLICATION_URL".to_string(),
            auth_ui_context_provider: ui_context_provider_proxy,
        }
    }

    /// Creates a TokenManager and serves requests for it.
    async fn create_and_serve_token_manager(
        stream: TokenManagerRequestStream,
        auth_provider_supplier: FakeAuthProviderSupplier,
    ) -> Result<(), anyhow::Error> {
        let tmp_dir = TempDir::new().unwrap();
        let db_path = tmp_dir.path().join("tokens.json");
        create_and_serve_token_manager_with_db_path(&db_path, stream, auth_provider_supplier).await
    }

    /// Creates an in-memory TokenManager and serves requests for it.
    async fn create_and_serve_in_memory_token_manager(
        stream: TokenManagerRequestStream,
        auth_provider_supplier: FakeAuthProviderSupplier,
    ) -> Result<(), anyhow::Error> {
        let task_group = TaskGroup::new();
        let token_manager = TokenManager::new_in_memory(auth_provider_supplier, task_group.clone());
        let context = create_token_manager_context();
        let (_sender, receiver) = oneshot::channel();
        token_manager.handle_requests_from_stream(&context, stream, receiver.shared()).await
    }

    /// Creates a TokenManager given a location to the file db, and serves requests for it.
    async fn create_and_serve_token_manager_with_db_path(
        db_path: &Path,
        stream: TokenManagerRequestStream,
        auth_provider_supplier: FakeAuthProviderSupplier,
    ) -> Result<(), anyhow::Error> {
        let task_group = TaskGroup::new();

        let token_manager = TokenManager::new(&db_path, auth_provider_supplier, task_group.clone())
            .expect("failed creating TokenManager");
        let context = create_token_manager_context();
        let (_sender, receiver) = oneshot::channel();
        token_manager.handle_requests_from_stream(&context, stream, receiver.shared()).await
    }

    /// Expect a CreateRefreshToken request, configurable with user_id. Generates a response.
    fn expect_create_refresh_token(
        request: OauthRequest,
        user_id: &str,
    ) -> Result<(), fidl::Error> {
        match request {
            OauthRequest::CreateRefreshToken { request, responder } => {
                assert!(request.ui_context.is_some());
                assert_eq!(request.account_id, Some(user_id.to_string()));
                responder.send(&mut Ok((
                    OauthRefreshToken {
                        content: Some(format!("{}_CREDENTIAL", user_id)),
                        account_id: Some(user_id.to_string()),
                    },
                    OauthAccessToken {
                        content: Some("ACCESS_TOKEN".to_string()),
                        expiry_time: None,
                    },
                )))?;
            }
            _ => panic!("Unexpected message received"),
        }
        Ok(())
    }

    /// Expect a GetAppAccessToken request, configurable with a token. Generates a response.
    fn expect_get_access_token(
        oauth_request: OauthRequest,
        token: &str,
    ) -> Result<(), fidl::Error> {
        match oauth_request {
            OauthRequest::GetAccessTokenFromRefreshToken { request, responder } => {
                assert_eq!(request.refresh_token.unwrap().content.unwrap(), token);
                assert_eq!(request.client_id, None);
                assert_eq!(request.scopes.unwrap(), Vec::<String>::default());
                let access_token = OauthAccessToken {
                    content: Some(format!("{}_ACCESS", token)),
                    expiry_time: None, // todo(before-submit)
                };
                responder.send(&mut Ok(access_token))?;
            }
            _ => panic!("Unexpected message received"),
        }
        Ok(())
    }

    fn expect_get_user_info(
        request: OauthOpenIdConnectRequest,
        user_id: &str,
    ) -> Result<(), fidl::Error> {
        match request {
            OauthOpenIdConnectRequest::GetUserInfoFromAccessToken { request, responder } => {
                assert_eq!(request.access_token.unwrap().content.unwrap(), "ACCESS_TOKEN");
                responder.send(&mut Ok(OpenIdUserInfo {
                    subject: Some(user_id.to_string()),
                    name: Some(format!("{}_DISPLAY", user_id)),
                    email: Some(format!("{}@example.org", user_id)),
                    picture: Some(format!("http://example.org/{}/img", user_id)),
                }))
            }
            _ => panic!("Unexpected message received"),
        }
    }

    /// Expect a GetIdTokenFromRefreshToken request, configurable with a token. Generates a response.
    fn expect_get_id_token(
        request: OauthOpenIdConnectRequest,
        expected_refresh_token: &str,
    ) -> Result<(), fidl::Error> {
        match request {
            OauthOpenIdConnectRequest::GetIdTokenFromRefreshToken { request, responder } => {
                let OpenIdTokenFromOauthRefreshTokenRequest { refresh_token, audiences } = request;
                assert_eq!(&refresh_token.unwrap().content.unwrap(), expected_refresh_token);
                assert_eq!(audiences, Some(vec!["AUDIENCE".to_string()]));
                let expiry_time = Time::get(ClockId::UTC) + Duration::from_seconds(3600);
                let mut id_token = Ok(OpenIdToken {
                    content: Some(format!("{}_ID", expected_refresh_token)),
                    expiry_time: Some(expiry_time.into_nanos()),
                });
                responder.send(&mut id_token)?;
            }
            _ => panic!("Unexpected message received"),
        }
        Ok(())
    }

    /// Expect a RevokeAppOrPersistentCredential request, configurable with a token. Responds Ok.
    fn expect_revoke_refresh_token(request: OauthRequest, token: &str) -> Result<(), fidl::Error> {
        match request {
            OauthRequest::RevokeRefreshToken { refresh_token, responder } => {
                assert_eq!(refresh_token.content.unwrap().as_str(), token);
                responder.send(&mut Ok(()))?;
            }
            _ => panic!("Unexpected message received"),
        }
        Ok(())
    }

    /// Wrapper for TokenManager::Authorize
    async fn authorize_simple<'a>(
        tm_proxy: &'a TokenManagerProxy,
        auth_provider_type: &'a str,
        user_profile_id: &'a str,
    ) -> Result<(Status, Option<Box<UserProfileInfo>>), fidl::Error> {
        let mut app_config = create_app_config(auth_provider_type);
        let auth_ui_context = None;
        let app_scopes = Vec::<String>::default();
        let auth_code = None;
        tm_proxy
            .authorize(
                &mut app_config,
                auth_ui_context,
                &mut app_scopes.iter().map(|x| &**x),
                Some(user_profile_id),
                auth_code,
            )
            .await
    }

    /// Wrapper for TokenManager::GetAccessToken
    async fn get_access_token_simple<'a>(
        tm_proxy: &'a TokenManagerProxy,
        auth_provider_type: &'a str,
        user_profile_id: &'a str,
    ) -> Result<(Status, Option<String>), fidl::Error> {
        let mut app_config = create_app_config(auth_provider_type);
        let app_scopes = Vec::<String>::default();
        tm_proxy
            .get_access_token(
                &mut app_config,
                user_profile_id,
                &mut app_scopes.iter().map(|x| &**x),
            )
            .await
    }

    /// Wrapper for TokenManager::GetIdToken
    async fn get_id_token_simple<'a>(
        tm_proxy: &'a TokenManagerProxy,
        auth_provider_type: &'a str,
        user_profile_id: &'a str,
    ) -> Result<(Status, Option<String>), fidl::Error> {
        let mut app_config = create_app_config(auth_provider_type);
        tm_proxy.get_id_token(&mut app_config, user_profile_id, Some("AUDIENCE")).await
    }

    /// Wrapper for TokenManager::DeleteAllTokens
    async fn delete_all_tokens_simple<'a>(
        tm_proxy: &'a TokenManagerProxy,
        auth_provider_type: &'a str,
        user_profile_id: &'a str,
    ) -> Result<Status, fidl::Error> {
        let mut app_config = create_app_config(auth_provider_type);
        let force = true;
        tm_proxy.delete_all_tokens(&mut app_config, user_profile_id, force).await
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
        token_manager
            .task_group()
            .spawn(|cancel| async move {
                let context = create_token_manager_context();
                assert!(token_manager_clone
                    .handle_requests_from_stream(&context, stream, cancel)
                    .await
                    .is_ok());
            })
            .await
            .expect("spawning failed");
        assert!(token_manager.task_group().cancel().await.is_ok());
        // Check that the accessor's task group cancelled the task group that was passed to new()
        assert!(task_group.cancel().await.is_err());

        // Now the channel should be closed
        let mut app_config = create_app_config("hooli");
        assert!(proxy.list_profile_ids(&mut app_config).await.is_err());
    }

    /// Authorize with multiple auth providers and multiple users per auth provider
    #[fasync::run_until_stalled(test)]
    async fn authorize() {
        let auth_provider_supplier = FakeAuthProviderSupplier::new();
        auth_provider_supplier.add_oauth("hooli", |mut stream| async move {
            expect_create_refresh_token(stream.try_next().await?.expect("End of stream"), "GAVIN")?;
            expect_create_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "DENPAK",
            )?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth_open_id_connect("hooli", |mut stream| async move {
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "GAVIN")?;
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "DENPAK")?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth("pied-piper", |mut stream| async move {
            expect_create_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD",
            )?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth_open_id_connect("pied-piper", |mut stream| async move {
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "RICHARD")?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });

        let (tm_proxy, tm_stream) = create_proxy_and_stream::<TokenManagerMarker>().unwrap();
        let client_fut = async move {
            // Authorize Gavin to Hooli
            let (status, user_profile_info) = authorize_simple(&tm_proxy, "hooli", "GAVIN").await?;
            assert_eq!(status, Status::Ok);
            let user_profile_info = user_profile_info.expect("user_profile_info is empty");
            assert_eq!(user_profile_info.id, "GAVIN");
            assert_eq!(user_profile_info.display_name, Some("GAVIN_DISPLAY".to_string()));
            assert_eq!(user_profile_info.url, None);
            assert_eq!(
                user_profile_info.image_url,
                Some("http://example.org/GAVIN/img".to_string())
            );

            // Authorize Richard to Pied Piper
            let (status, user_profile_info) =
                authorize_simple(&tm_proxy, "pied-piper", "RICHARD").await?;
            assert_eq!(status, Status::Ok);
            let user_profile_info = user_profile_info.expect("user_profile_info is empty");
            assert_eq!(user_profile_info.id, "RICHARD");
            assert_eq!(user_profile_info.display_name, Some("RICHARD_DISPLAY".to_string()));
            assert_eq!(user_profile_info.url, None);
            assert_eq!(
                user_profile_info.image_url,
                Some("http://example.org/RICHARD/img".to_string())
            );

            // Again, authorize Gavin to Hooli (with just basic assertions)
            let (status, user_profile_info) =
                authorize_simple(&tm_proxy, "hooli", "DENPAK").await?;
            assert_eq!(status, Status::Ok);
            assert_eq!(user_profile_info.expect("user_profile_info is empty").id, "DENPAK");

            // Authorize against a non-existing auth provider
            assert_eq!(
                (Status::AuthProviderServiceUnavailable, None),
                authorize_simple(&tm_proxy, "myspace", "MR_ROBOT").await?
            );

            Result::<(), fidl::Error>::Ok(())
        };
        let (ap_result, client_result, tm_result) = join3(
            auth_provider_supplier.run(),
            client_fut,
            create_and_serve_token_manager(tm_stream, auth_provider_supplier),
        )
        .await;
        assert!(ap_result.is_ok());
        assert!(client_result.is_ok());
        assert!(tm_result.is_ok());
    }

    /// Check that user profiles can be listed
    #[fasync::run_until_stalled(test)]
    async fn list_profile_ids() {
        let auth_provider_supplier = FakeAuthProviderSupplier::new();
        auth_provider_supplier.add_oauth("hooli", |mut stream| async move {
            expect_create_refresh_token(stream.try_next().await?.expect("End of stream"), "GAVIN")?;
            expect_create_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "DENPAK",
            )?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth_open_id_connect("hooli", |mut stream| async move {
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "GAVIN")?;
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "DENPAK")?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth("pied-piper", |mut stream| async move {
            expect_create_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD",
            )?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth_open_id_connect("pied-piper", |mut stream| async move {
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "RICHARD")?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });

        let (tm_proxy, tm_stream) = create_proxy_and_stream::<TokenManagerMarker>().unwrap();
        let client_fut = async move {
            assert_eq!(authorize_simple(&tm_proxy, "hooli", "GAVIN").await?.0, Status::Ok);
            assert_eq!(authorize_simple(&tm_proxy, "hooli", "DENPAK").await?.0, Status::Ok);
            assert_eq!(authorize_simple(&tm_proxy, "pied-piper", "RICHARD").await?.0, Status::Ok);

            let profile_ids = tm_proxy.list_profile_ids(&mut create_app_config("hooli")).await?;
            assert_eq!(profile_ids, (Status::Ok, vec!["DENPAK".to_string(), "GAVIN".to_string()]));
            let profile_ids =
                tm_proxy.list_profile_ids(&mut create_app_config("pied-piper")).await?;
            assert_eq!(profile_ids, (Status::Ok, vec!["RICHARD".to_string()]));
            let profile_ids = tm_proxy.list_profile_ids(&mut create_app_config("myspace")).await?;
            assert_eq!(profile_ids, (Status::Ok, vec![]));

            Result::<(), fidl::Error>::Ok(())
        };
        let (ap_result, client_result, tm_result) = join3(
            auth_provider_supplier.run(),
            client_fut,
            create_and_serve_token_manager(tm_stream, auth_provider_supplier),
        )
        .await;
        assert!(ap_result.is_ok());
        assert!(client_result.is_ok());
        assert!(tm_result.is_ok());
    }

    /// Check that we can get access and id tokens, both by exchanging the refresh token and by
    /// using the cache to retrieve the same value (without calling the auth provider).
    #[fasync::run_until_stalled(test)]
    async fn get_tokens() {
        let auth_provider_supplier = FakeAuthProviderSupplier::new();
        auth_provider_supplier.add_oauth("pied-piper", |mut stream| async move {
            expect_create_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD",
            )?;
            expect_get_access_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD_CREDENTIAL",
            )?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth_open_id_connect("pied-piper", |mut stream| async move {
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "RICHARD")?;
            expect_get_id_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD_CREDENTIAL",
            )?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });

        let (tm_proxy, tm_stream) = create_proxy_and_stream::<TokenManagerMarker>().unwrap();
        let client_fut = async move {
            assert_eq!(authorize_simple(&tm_proxy, "pied-piper", "RICHARD").await?.0, Status::Ok);

            // Check that "_CREDENTIAL" was added by the authorize call and that "_ACCESS" or "_ID"
            // respectively, was added by the token exchange calls. In the second
            // iteration, make the same calls, but this time they should be delivered from cache.
            for _ in 0..2 {
                assert_eq!(
                    get_access_token_simple(&tm_proxy, "pied-piper", "RICHARD").await?,
                    (Status::Ok, Some("RICHARD_CREDENTIAL_ACCESS".to_string()))
                );
                assert_eq!(
                    get_id_token_simple(&tm_proxy, "pied-piper", "RICHARD").await?,
                    (Status::Ok, Some("RICHARD_CREDENTIAL_ID".to_string()))
                );
            }

            // Errors
            assert_eq!(
                get_access_token_simple(&tm_proxy, "pied-piper", "DINESH").await?,
                (Status::UserNotFound, None)
            );
            assert_eq!(
                get_access_token_simple(&tm_proxy, "myspace", "MR_ROBOT").await?,
                (Status::UserNotFound, None)
            );

            Result::<(), fidl::Error>::Ok(())
        };
        let (ap_result, client_result, tm_result) = join3(
            auth_provider_supplier.run(),
            client_fut,
            create_and_serve_token_manager(tm_stream, auth_provider_supplier),
        )
        .await;
        assert!(ap_result.is_ok());
        assert!(client_result.is_ok());
        assert!(tm_result.is_ok());
    }

    /// Check that tokens are correctly deleted, including the cached ones such as access tokens
    #[fasync::run_until_stalled(test)]
    async fn delete_all_tokens() {
        let auth_provider_supplier = FakeAuthProviderSupplier::new();
        auth_provider_supplier.add_oauth("pied-piper", |mut stream| async move {
            expect_create_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD",
            )?;
            expect_create_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "DINESH",
            )?;
            expect_get_access_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD_CREDENTIAL",
            )?;
            expect_revoke_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD_CREDENTIAL",
            )?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth_open_id_connect("pied-piper", |mut stream| async move {
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "RICHARD")?;
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "DINESH")?;
            expect_get_id_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD_CREDENTIAL",
            )?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });

        let (tm_proxy, tm_stream) = create_proxy_and_stream::<TokenManagerMarker>().unwrap();
        let client_fut = async move {
            assert_eq!(authorize_simple(&tm_proxy, "pied-piper", "RICHARD").await?.0, Status::Ok);
            assert_eq!(authorize_simple(&tm_proxy, "pied-piper", "DINESH").await?.0, Status::Ok);

            // Put an access token and an id token in the cache
            assert_eq!(
                get_access_token_simple(&tm_proxy, "pied-piper", "RICHARD").await?.0,
                Status::Ok
            );
            assert_eq!(
                get_id_token_simple(&tm_proxy, "pied-piper", "RICHARD").await?.0,
                Status::Ok
            );

            // Deleting an existing token succeeds
            assert_eq!(
                delete_all_tokens_simple(&tm_proxy, "pied-piper", "RICHARD").await?,
                Status::Ok
            );
            // Deleting a non-existing token succeeds
            assert_eq!(
                delete_all_tokens_simple(&tm_proxy, "myspace", "MR_ROBOT").await?,
                Status::Ok
            );

            // No longer present in list
            let profile_ids =
                tm_proxy.list_profile_ids(&mut create_app_config("pied-piper")).await?;
            assert_eq!(profile_ids, (Status::Ok, vec!["DINESH".to_string()]));

            // Check that they was also removed from cache
            assert_eq!(
                get_access_token_simple(&tm_proxy, "pied-piper", "RICHARD").await?,
                (Status::UserNotFound, None)
            );
            assert_eq!(
                get_id_token_simple(&tm_proxy, "pied-piper", "RICHARD").await?,
                (Status::UserNotFound, None)
            );

            Result::<(), fidl::Error>::Ok(())
        };
        let (ap_result, client_result, tm_result) = join3(
            auth_provider_supplier.run(),
            client_fut,
            create_and_serve_token_manager(tm_stream, auth_provider_supplier),
        )
        .await;
        assert!(ap_result.is_ok());
        assert!(client_result.is_ok());
        assert!(tm_result.is_ok());
    }

    /// Check that state is preserved when a TokenManager is recreated with the same file db.
    #[fasync::run_until_stalled(test)]
    async fn file_db() {
        let tmp_dir = TempDir::new().expect("Could not create temp dir");
        let db_path = tmp_dir.path().join("tokens.json");

        // Part 1: Create some state in a token manager
        let auth_provider_supplier = FakeAuthProviderSupplier::new();
        auth_provider_supplier.add_oauth("pied-piper", |mut stream| async move {
            expect_create_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD",
            )?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth_open_id_connect("pied-piper", |mut stream| async move {
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "RICHARD")?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });

        let (tm_proxy, tm_stream) = create_proxy_and_stream::<TokenManagerMarker>().unwrap();
        let client_fut = async move {
            assert_eq!(authorize_simple(&tm_proxy, "pied-piper", "RICHARD").await?.0, Status::Ok);

            Result::<(), fidl::Error>::Ok(())
        };
        let (ap_result, client_result, tm_result) = join3(
            auth_provider_supplier.run(),
            client_fut,
            create_and_serve_token_manager_with_db_path(
                &db_path,
                tm_stream,
                auth_provider_supplier,
            ),
        )
        .await;
        assert!(ap_result.is_ok());
        assert!(client_result.is_ok());
        assert!(tm_result.is_ok());

        // Part 2: Create a new token manager and check that the state survived
        let auth_provider_supplier = FakeAuthProviderSupplier::new();
        auth_provider_supplier.add_oauth("pied-piper", |mut stream| async move {
            expect_get_access_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD_CREDENTIAL",
            )?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });

        let (tm_proxy, tm_stream) = create_proxy_and_stream::<TokenManagerMarker>().unwrap();
        let client_fut = async move {
            // Check that the profile list survived
            let profile_ids =
                tm_proxy.list_profile_ids(&mut create_app_config("pied-piper")).await?;
            assert_eq!(profile_ids, (Status::Ok, vec!["RICHARD".to_string()]));

            // Check that the credential survived, and that it triggers a call to the auth provider
            // since the cache did not survive
            assert_eq!(
                get_access_token_simple(&tm_proxy, "pied-piper", "RICHARD").await?,
                (Status::Ok, Some("RICHARD_CREDENTIAL_ACCESS".to_string()))
            );

            Result::<(), fidl::Error>::Ok(())
        };
        let (ap_result, client_result, tm_result) = join3(
            auth_provider_supplier.run(),
            client_fut,
            create_and_serve_token_manager_with_db_path(
                &db_path,
                tm_stream,
                auth_provider_supplier,
            ),
        )
        .await;
        assert!(ap_result.is_ok());
        assert!(client_result.is_ok());
        assert!(tm_result.is_ok());
    }

    /// Check a number of use-cases for the in-memory TokenManager
    #[fasync::run_until_stalled(test)]
    async fn in_memory() {
        let auth_provider_supplier = FakeAuthProviderSupplier::new();
        auth_provider_supplier.add_oauth("hooli", |mut stream| async move {
            expect_create_refresh_token(stream.try_next().await?.expect("End of stream"), "GAVIN")?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth_open_id_connect("hooli", |mut stream| async move {
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "GAVIN")?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth("pied-piper", |mut stream| async move {
            expect_create_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD",
            )?;
            expect_create_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "DINESH",
            )?;
            expect_get_access_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD_CREDENTIAL",
            )?;
            expect_revoke_refresh_token(
                stream.try_next().await?.expect("End of stream"),
                "RICHARD_CREDENTIAL",
            )?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });
        auth_provider_supplier.add_oauth_open_id_connect("pied-piper", |mut stream| async move {
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "RICHARD")?;
            expect_get_user_info(stream.try_next().await?.expect("End of stream"), "DINESH")?;
            Ok(assert!(stream.try_next().await?.is_none()))
        });

        let (tm_proxy, tm_stream) = create_proxy_and_stream::<TokenManagerMarker>().unwrap();
        let client_fut = async move {
            assert_eq!(authorize_simple(&tm_proxy, "pied-piper", "RICHARD").await?.0, Status::Ok);
            assert_eq!(authorize_simple(&tm_proxy, "hooli", "GAVIN").await?.0, Status::Ok);
            assert_eq!(authorize_simple(&tm_proxy, "pied-piper", "DINESH").await?.0, Status::Ok);

            // Check listing
            assert_eq!(
                tm_proxy.list_profile_ids(&mut create_app_config("hooli")).await?,
                (Status::Ok, vec!["GAVIN".to_string()])
            );

            assert_eq!(
                tm_proxy.list_profile_ids(&mut create_app_config("pied-piper")).await?,
                (Status::Ok, vec!["DINESH".to_string(), "RICHARD".to_string()])
            );

            assert_eq!(
                tm_proxy.list_profile_ids(&mut create_app_config("myspace")).await?,
                (Status::Ok, vec![])
            );

            // This puts an access token in the cache
            assert_eq!(
                get_access_token_simple(&tm_proxy, "pied-piper", "RICHARD").await?.0,
                Status::Ok
            );

            // Ask for the same token again, expecting it delivered from the cache
            assert_eq!(
                delete_all_tokens_simple(&tm_proxy, "pied-piper", "RICHARD").await?,
                Status::Ok
            );
            // Deleting a non-existing token succeeds
            assert_eq!(
                delete_all_tokens_simple(&tm_proxy, "myspace", "MR_ROBOT").await?,
                Status::Ok
            );

            // Richard no longer present in list
            let profile_ids =
                tm_proxy.list_profile_ids(&mut create_app_config("pied-piper")).await?;
            assert_eq!(profile_ids, (Status::Ok, vec!["DINESH".to_string()]));

            // Check that it was also removed from cache
            assert_eq!(
                get_access_token_simple(&tm_proxy, "pied-piper", "RICHARD").await?,
                (Status::UserNotFound, None)
            );

            Result::<(), fidl::Error>::Ok(())
        };
        let (ap_result, client_result, tm_result) = join3(
            auth_provider_supplier.run(),
            client_fut,
            create_and_serve_in_memory_token_manager(tm_stream, auth_provider_supplier),
        )
        .await;
        assert!(ap_result.is_ok());
        assert!(client_result.is_ok());
        assert!(tm_result.is_ok());
    }
}
