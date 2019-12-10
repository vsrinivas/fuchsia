// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::{
    FUCHSIA_CLIENT_ID, OAUTH_AUTHORIZE_URI, OAUTH_DEFAULT_SCOPES, REDIRECT_URI,
};
use crate::error::{AuthProviderError, AuthProviderResultExt};
use crate::firebase::{build_firebase_token_request, parse_firebase_token_response};
use crate::http::HttpClient;
use crate::oauth::{
    build_request_with_auth_code, build_request_with_refresh_token, build_revocation_request,
    parse_auth_code_from_redirect, parse_response_with_refresh_token,
    parse_response_without_refresh_token, parse_revocation_response, AccessToken, AuthCode,
    RefreshToken,
};
use crate::openid::{
    build_id_token_request, build_user_info_request, parse_id_token_response,
    parse_user_info_response, IdToken,
};
use crate::web::StandaloneWebFrame;
use failure::Error;
use fidl;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_auth::{
    AssertionJwtParams, AttestationJwtParams, AttestationSignerMarker, AuthChallenge,
    AuthProviderGetAppAccessTokenFromAssertionJwtResponder, AuthProviderGetAppAccessTokenResponder,
    AuthProviderGetAppFirebaseTokenResponder, AuthProviderGetAppIdTokenResponder,
    AuthProviderGetPersistentCredentialFromAttestationJwtResponder,
    AuthProviderGetPersistentCredentialResponder, AuthProviderRequest, AuthProviderRequestStream,
    AuthProviderRevokeAppOrPersistentCredentialResponder, AuthProviderStatus, AuthToken,
    AuthenticationUiContextMarker, FirebaseToken, TokenType, UserProfileInfo,
};
use fuchsia_scenic::ViewTokenPair;
use futures::prelude::*;
use log::{info, warn};
use url::Url;

type AuthProviderResult<T> = Result<T, AuthProviderError>;

/// Trait for structs capable of creating new Web frames.
pub trait WebFrameSupplier {
    /// The concrete `StandaloneWebFrame` type the supplier produces.
    type Frame: StandaloneWebFrame;
    /// Creates a new `StandaloneWebFrame`.  This method guarantees that the
    /// new frame is in its own web context.
    /// Although implementation of this method does not require state, `self`
    /// is added here to allow injection of mocks with canned responses.
    fn new_standalone_frame(&self) -> Result<Self::Frame, Error>;
}

/// An implementation of the `AuthProvider` FIDL protocol that communicates
/// with the Google identity system to perform authentication for and issue
/// tokens for Google accounts.
pub struct GoogleAuthProvider<W, H>
where
    W: WebFrameSupplier,
    H: HttpClient,
{
    /// A supplier used to generate web frames on demand.
    web_frame_supplier: W,
    /// A client used for making HTTP requests.
    http_client: H,
}

impl<W, H> GoogleAuthProvider<W, H>
where
    W: WebFrameSupplier,
    H: HttpClient,
{
    /// Create a new GoogleAuthProvider.
    pub fn new(web_frame_supplier: W, http_client: H) -> Self {
        GoogleAuthProvider { web_frame_supplier, http_client }
    }

    /// Handle requests passed to the supplied stream.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AuthProviderRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            self.handle_request(request).await;
        }
        Ok(())
    }

    /// Handle a single `AuthProviderRequest`.
    async fn handle_request(&self, req: AuthProviderRequest) {
        match req {
            AuthProviderRequest::GetPersistentCredential {
                auth_ui_context,
                user_profile_id,
                responder,
            } => responder.send_result(
                self.get_persistent_credential(auth_ui_context, user_profile_id).await,
            ),
            AuthProviderRequest::GetAppAccessToken { credential, client_id, scopes, responder } => {
                responder
                    .send_result(self.get_app_access_token(credential, client_id, scopes).await)
            }
            AuthProviderRequest::GetAppIdToken { credential, audience, responder } => {
                responder.send_result(self.get_app_id_token(credential, audience).await)
            }
            AuthProviderRequest::GetAppFirebaseToken { id_token, firebase_api_key, responder } => {
                responder.send_result(self.get_app_firebase_token(id_token, firebase_api_key).await)
            }
            AuthProviderRequest::RevokeAppOrPersistentCredential { credential, responder } => {
                responder.send_result(self.revoke_app_or_persistent_credential(credential).await)
            }
            AuthProviderRequest::GetPersistentCredentialFromAttestationJwt {
                attestation_signer,
                jwt_params,
                auth_ui_context,
                user_profile_id,
                responder,
            } => responder.send_result(
                self.get_persistent_credential_from_attestation_jwt(
                    attestation_signer,
                    jwt_params,
                    auth_ui_context,
                    user_profile_id,
                )
                .await,
            ),
            AuthProviderRequest::GetAppAccessTokenFromAssertionJwt {
                attestation_signer,
                jwt_params,
                credential,
                scopes,
                responder,
            } => responder.send_result(
                self.get_app_access_token_from_assertion_jwt(
                    attestation_signer,
                    jwt_params,
                    credential,
                    scopes,
                )
                .await,
            ),
        }
    }

    /// Implementation of `GetPersistentCredential` method for the
    /// `AuthProvider` interface.
    async fn get_persistent_credential(
        &self,
        auth_ui_context: Option<ClientEnd<AuthenticationUiContextMarker>>,
        user_profile_id: Option<String>,
    ) -> AuthProviderResult<(String, UserProfileInfo)> {
        match auth_ui_context {
            Some(ui_context) => {
                let auth_code = self.get_auth_code(ui_context, user_profile_id).await?;
                info!("Received auth code of length: {:?}", &auth_code.0.len());
                let (refresh_token, access_token) = self.exchange_auth_code(auth_code).await?;
                info!("Received refresh token of length {:?}", &refresh_token.0.len());
                let user_profile_info = self.get_user_profile_info(access_token).await?;
                Ok((refresh_token.0, user_profile_info))
            }
            None => Err(AuthProviderError::new(AuthProviderStatus::BadRequest)),
        }
    }

    /// Implementation of `GetAppAccessToken` method for the `AuthProvider`
    /// interface.
    async fn get_app_access_token(
        &self,
        credential: String,
        client_id: Option<String>,
        scopes: Vec<String>,
    ) -> AuthProviderResult<AuthToken> {
        if client_id.as_ref().map(String::is_empty) == Some(true) || credential.is_empty() {
            return Err(AuthProviderError::new(AuthProviderStatus::BadRequest));
        }

        let request =
            build_request_with_refresh_token(RefreshToken(credential), scopes, client_id)?;
        let (response_body, status) = self.http_client.request(request).await?;
        let (access_token, expires_in) =
            parse_response_without_refresh_token(response_body, status)?;
        Ok(AuthToken {
            token_type: TokenType::AccessToken,
            token: access_token.0,
            expires_in: expires_in.into_seconds() as u64,
        })
    }

    /// Implementation of `GetAppIdToken` method for the `AuthProvider`
    /// interface.
    async fn get_app_id_token(
        &self,
        credential: String,
        audience: Option<String>,
    ) -> AuthProviderResult<AuthToken> {
        if audience.as_ref().map(String::is_empty) == Some(true) || credential.is_empty() {
            return Err(AuthProviderError::new(AuthProviderStatus::BadRequest));
        }

        let request = build_id_token_request(RefreshToken(credential), audience)?;
        let (response_body, status) = self.http_client.request(request).await?;
        let (id_token, expires_in) = parse_id_token_response(response_body, status)?;
        Ok(AuthToken {
            token_type: TokenType::IdToken,
            token: id_token.0,
            expires_in: expires_in.into_seconds() as u64,
        })
    }

    /// Implementation of `GetAppFirebaseToken` method for the `AuthProvider`
    /// interface.
    async fn get_app_firebase_token(
        &self,
        id_token: String,
        firebase_api_key: String,
    ) -> AuthProviderResult<FirebaseToken> {
        if id_token.is_empty() || firebase_api_key.is_empty() {
            return Err(AuthProviderError::new(AuthProviderStatus::BadRequest));
        }

        let request = build_firebase_token_request(IdToken(id_token), firebase_api_key)?;
        let (response_body, status) = self.http_client.request(request).await?;
        parse_firebase_token_response(response_body, status).map_err(AuthProviderError::from)
    }

    /// Implementation of `RevokeAppOrPersistentCredential` method for the
    /// `AuthProvider` interface.
    async fn revoke_app_or_persistent_credential(
        &self,
        credential: String,
    ) -> AuthProviderResult<()> {
        if credential.is_empty() {
            return Err(AuthProviderError::new(AuthProviderStatus::BadRequest));
        }

        let request = build_revocation_request(credential)?;
        let (response_body, status) = self.http_client.request(request).await?;
        parse_revocation_response(response_body, status).map_err(AuthProviderError::from)
    }

    /// Implementation of `GetPersistentCredentialFromAttestationJWT` method
    /// for the `AuthProvider` interface.
    async fn get_persistent_credential_from_attestation_jwt(
        &self,
        _attestation_signer: ClientEnd<AttestationSignerMarker>,
        _jwt_params: AttestationJwtParams,
        _auth_ui_context: Option<ClientEnd<AuthenticationUiContextMarker>>,
        _user_profile_id: Option<String>,
    ) -> AuthProviderResult<(String, AuthToken, AuthChallenge, UserProfileInfo)> {
        // Remote attestation flow is not supported for traditional OAuth.
        Err(AuthProviderError::new(AuthProviderStatus::InternalError))
    }

    /// Implementation of `GetAppAccessTokenFromAssertionJWT` method for the
    /// `AuthProvider` interface.
    async fn get_app_access_token_from_assertion_jwt(
        &self,
        _attestation_signer: ClientEnd<AttestationSignerMarker>,
        _jwt_params: AssertionJwtParams,
        _credential: String,
        _scopes: Vec<String>,
    ) -> AuthProviderResult<(String, AuthToken, AuthChallenge)> {
        // Remote attestation flow is not supported for traditional OAuth.
        Err(AuthProviderError::new(AuthProviderStatus::InternalError))
    }

    /// Direct user through Google OAuth authorization flow and obtain an auth
    /// code.
    async fn get_auth_code(
        &self,
        auth_ui_context: ClientEnd<AuthenticationUiContextMarker>,
        user_profile_id: Option<String>,
    ) -> AuthProviderResult<AuthCode> {
        let auth_ui_context =
            auth_ui_context.into_proxy().auth_provider_status(AuthProviderStatus::UnknownError)?;
        let mut web_frame = self
            .web_frame_supplier
            .new_standalone_frame()
            .auth_provider_status(AuthProviderStatus::UnknownError)?;
        let ViewTokenPair { view_token, mut view_holder_token } =
            ViewTokenPair::new().auth_provider_status(AuthProviderStatus::UnknownError)?;
        let authorize_url = Self::authorize_url(user_profile_id)?;

        web_frame.display_url(view_token, authorize_url).await?;
        auth_ui_context
            .start_overlay(&mut view_holder_token)
            .auth_provider_status(AuthProviderStatus::UnknownError)?;

        let redirect_url = web_frame.wait_for_redirect(REDIRECT_URI.clone()).await?;

        if let Err(err) = auth_ui_context.stop_overlay() {
            warn!("Error while attempting to stop UI overlay: {:?}", err);
        }

        let auth_code = parse_auth_code_from_redirect(redirect_url)?;
        Ok(auth_code)
    }

    fn authorize_url(user_profile_id: Option<String>) -> AuthProviderResult<Url> {
        let mut params = vec![
            ("scope", OAUTH_DEFAULT_SCOPES.as_str()),
            ("glif", "false"), // TODO(satsukiu): add a command line parameter to set this
            ("response_type", "code"),
            ("redirect_uri", REDIRECT_URI.as_str()),
            ("client_id", FUCHSIA_CLIENT_ID),
        ]; // TODO(satsukiu): add 'state' parameter here and verify it in the redirect.
        if let Some(user) = user_profile_id.as_ref() {
            params.push(("login_hint", user));
        }

        Url::parse_with_params(OAUTH_AUTHORIZE_URI.as_str(), &params)
            .auth_provider_status(AuthProviderStatus::InternalError)
    }

    /// Trades an OAuth auth code for a refresh token and access token.
    async fn exchange_auth_code(
        &self,
        auth_code: AuthCode,
    ) -> AuthProviderResult<(RefreshToken, AccessToken)> {
        let request = build_request_with_auth_code(auth_code)?;
        let (response_body, status_code) = self.http_client.request(request).await?;
        parse_response_with_refresh_token(response_body, status_code)
            .map_err(AuthProviderError::from)
    }

    /// Use an access token to retrieve profile information.
    async fn get_user_profile_info(
        &self,
        access_token: AccessToken,
    ) -> AuthProviderResult<UserProfileInfo> {
        let request = build_user_info_request(access_token)?;
        let (response_body, status_code) = self.http_client.request(request).await?;
        parse_user_info_response(response_body, status_code).map_err(AuthProviderError::from)
    }
}

/// Trait containing logic for sending responses.  Enables `Result` returntype
/// for API implementations.
trait Responder: Sized {
    type Data;
    /// Consume the responder to send a result.  Logs any errors encountered
    /// while sending response.
    fn send_result(self, result: AuthProviderResult<Self::Data>) {
        let send_result = match result {
            Ok(val) => self.send_raw(AuthProviderStatus::Ok, Some(val)),
            Err(err) => self.send_raw(err.status, None),
        };
        if let Err(err) = send_result {
            warn!("Error sending response to {}: {:?}", Self::METHOD_NAME, err);
        }
    }

    /// Send response without handling failures.
    fn send_raw(
        self,
        status: AuthProviderStatus,
        data: Option<Self::Data>,
    ) -> Result<(), fidl::Error>;

    const METHOD_NAME: &'static str;
}

impl Responder for AuthProviderGetPersistentCredentialResponder {
    type Data = (String, UserProfileInfo);
    const METHOD_NAME: &'static str = "GetPersistentCredential";

    fn send_raw(
        self,
        status: AuthProviderStatus,
        data: Option<(String, UserProfileInfo)>,
    ) -> Result<(), fidl::Error> {
        match data {
            None => self.send(status, None, None),
            Some((refresh_token, mut user_profile_info)) => {
                self.send(status, Some(refresh_token.as_str()), Some(&mut user_profile_info))
            }
        }
    }
}

impl Responder for AuthProviderGetAppAccessTokenResponder {
    type Data = AuthToken;
    const METHOD_NAME: &'static str = "GetAppAccessToken";

    fn send_raw(
        self,
        status: AuthProviderStatus,
        mut data: Option<AuthToken>,
    ) -> Result<(), fidl::Error> {
        self.send(status, data.as_mut())
    }
}

impl Responder for AuthProviderGetAppIdTokenResponder {
    type Data = AuthToken;
    const METHOD_NAME: &'static str = "GetAppIdToken";

    fn send_raw(
        self,
        status: AuthProviderStatus,
        mut data: Option<AuthToken>,
    ) -> Result<(), fidl::Error> {
        self.send(status, data.as_mut())
    }
}

impl Responder for AuthProviderGetAppFirebaseTokenResponder {
    type Data = FirebaseToken;
    const METHOD_NAME: &'static str = "GetAppFirebaseToken";

    fn send_raw(
        self,
        status: AuthProviderStatus,
        mut data: Option<FirebaseToken>,
    ) -> Result<(), fidl::Error> {
        self.send(status, data.as_mut())
    }
}

impl Responder for AuthProviderRevokeAppOrPersistentCredentialResponder {
    type Data = ();
    const METHOD_NAME: &'static str = "RevokeAppOrPersistentCredential";

    fn send_raw(self, status: AuthProviderStatus, _data: Option<()>) -> Result<(), fidl::Error> {
        self.send(status)
    }
}

impl Responder for AuthProviderGetPersistentCredentialFromAttestationJwtResponder {
    type Data = (String, AuthToken, AuthChallenge, UserProfileInfo);
    const METHOD_NAME: &'static str = "GetPersistentCredentialFromAttestationJwt";

    fn send_raw(
        self,
        status: AuthProviderStatus,
        data: Option<(String, AuthToken, AuthChallenge, UserProfileInfo)>,
    ) -> Result<(), fidl::Error> {
        match data {
            None => self.send(status, None, None, None, None),
            Some((credential, mut auth_token, mut auth_challenge, mut user_profile_info)) => self
                .send(
                    status,
                    Some(credential.as_str()),
                    Some(&mut auth_token),
                    Some(&mut auth_challenge),
                    Some(&mut user_profile_info),
                ),
        }
    }
}

impl Responder for AuthProviderGetAppAccessTokenFromAssertionJwtResponder {
    type Data = (String, AuthToken, AuthChallenge);
    const METHOD_NAME: &'static str = "GetAppAccessTokenFromAssertionJwt";

    fn send_raw(
        self,
        status: AuthProviderStatus,
        data: Option<(String, AuthToken, AuthChallenge)>,
    ) -> Result<(), fidl::Error> {
        match data {
            None => self.send(status, None, None, None),
            Some((credential, mut auth_token, mut auth_challenge)) => self.send(
                status,
                Some(credential.as_str()),
                Some(&mut auth_token),
                Some(&mut auth_challenge),
            ),
        }
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use crate::error::TokenProviderError;
    use crate::http::mock::TestHttpClient;
    use crate::web::mock::TestWebFrame;
    use fidl::endpoints::{create_proxy_and_stream, create_request_stream};
    use fidl_fuchsia_auth::{AuthProviderMarker, AuthProviderProxy};
    use fidl_fuchsia_identity_external::Error as ApiError;
    use fuchsia_async as fasync;
    use hyper::StatusCode;

    type TokenProviderResult<T> = Result<T, TokenProviderError>;

    /// Clones a TokenProviderResult.  This is provided instead of a Clone
    /// implementation due to orphan rules.
    fn clone_result<T: Clone>(result: &TokenProviderResult<T>) -> TokenProviderResult<T> {
        match result {
            Ok(res) => Ok(res.clone()),
            // error cause cannot be cloned so don't replicate it.
            Err(err) => Err(TokenProviderError::new(err.api_error)),
        }
    }

    /// A mock implementation of `WebFrameSupplier` that supplies `TestWebFrames`.
    /// The supplied `TestWebFrames` will return the responses provided during
    /// creation of the `TestWebFrameSupplier`.
    struct TestWebFrameSupplier {
        display_url_response: TokenProviderResult<()>,
        wait_for_redirect_response: TokenProviderResult<Url>,
    }

    impl TestWebFrameSupplier {
        fn new(
            display_url_response: TokenProviderResult<()>,
            wait_for_redirect_response: TokenProviderResult<Url>,
        ) -> Self {
            TestWebFrameSupplier { display_url_response, wait_for_redirect_response }
        }
    }

    impl WebFrameSupplier for TestWebFrameSupplier {
        type Frame = TestWebFrame;
        fn new_standalone_frame(&self) -> Result<TestWebFrame, Error> {
            Ok(TestWebFrame::new(
                clone_result(&self.display_url_response),
                clone_result(&self.wait_for_redirect_response),
            ))
        }
    }

    /// Creates an auth provider.  If frame_supplier is not given, the auth
    /// provider is instantiated with a `TestWebFrameProvider` that supplies
    /// `StandaloneWebFrame`s that return an `UnsupportedOperation` error.
    /// Similarly, if http_client is not given, the auth provider is
    /// instantiated with a `TestHttpClient` that returns an
    /// `UnsupportedOperation` error.
    fn get_auth_provider_proxy(
        frame_supplier: Option<TestWebFrameSupplier>,
        http_client: Option<TestHttpClient>,
    ) -> AuthProviderProxy {
        let (provider_proxy, provider_request_stream) =
            create_proxy_and_stream::<AuthProviderMarker>()
                .expect("Failed to create proxy and stream");

        let frame_supplier = frame_supplier.unwrap_or(TestWebFrameSupplier::new(
            Err(TokenProviderError::new(ApiError::UnsupportedOperation)),
            Err(TokenProviderError::new(ApiError::UnsupportedOperation)),
        ));
        let http =
            http_client.unwrap_or(TestHttpClient::with_error(ApiError::UnsupportedOperation));

        let auth_provider = GoogleAuthProvider::new(frame_supplier, http);
        fasync::spawn(async move {
            auth_provider
                .handle_requests_from_stream(provider_request_stream)
                .await
                .expect("Error handling AuthProvider channel");
        });

        provider_proxy
    }

    fn get_authentication_ui_context() -> ClientEnd<AuthenticationUiContextMarker> {
        let (client, mut stream) = create_request_stream::<AuthenticationUiContextMarker>()
            .expect("Failed to create authentication UI context stream");
        fasync::spawn(async move { while let Some(_) = stream.try_next().await.unwrap() {} });
        client
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persistent_credential_success() -> Result<(), Error> {
        let mock_frame_supplier = TestWebFrameSupplier::new(
            Ok(()),
            Ok(Url::parse_with_params(REDIRECT_URI.as_str(), vec![("code", "test-auth-code")])
                .unwrap()),
        );
        let refresh_token_response =
            "{\"refresh_token\": \"test-refresh-token\", \"access_token\": \"test-access-token\"}";
        let user_info_response =
            "{\"sub\": \"test-id\", \"name\": \"Bill\", \"profile\": \"profile-url\", \
             \"picture\": \"picture-url\"}";

        let mock_http = TestHttpClient::with_responses(vec![
            Ok((Some(refresh_token_response.to_string()), StatusCode::OK)),
            Ok((Some(user_info_response.to_string()), StatusCode::OK)),
        ]);
        let auth_provider = get_auth_provider_proxy(Some(mock_frame_supplier), Some(mock_http));

        let ui_context = get_authentication_ui_context();
        let (status, refresh_token, user_info) =
            auth_provider.get_persistent_credential(Some(ui_context), None).await?;

        assert_eq!(status, AuthProviderStatus::Ok);
        assert_eq!(refresh_token.unwrap(), "test-refresh-token".to_string());
        assert_eq!(
            user_info.unwrap(),
            Box::new(UserProfileInfo {
                id: "test-id".to_string(),
                display_name: Some("Bill".to_string()),
                url: Some("profile-url".to_string()),
                image_url: Some("picture-url".to_string()),
            })
        );
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persistent_credential_exchange_web_errors() -> Result<(), Error> {
        // User denies access
        let frame_supplier = TestWebFrameSupplier::new(
            Ok(()),
            Ok(Url::parse_with_params(REDIRECT_URI.as_str(), vec![("error", "access_denied")])
                .unwrap()),
        );
        assert_persistent_credential_web_err(frame_supplier, AuthProviderStatus::UserCancelled)
            .await?;

        // Web frame error - UI maybe canceled
        let frame_supplier =
            TestWebFrameSupplier::new(Ok(()), Err(TokenProviderError::new(ApiError::Unknown)));
        assert_persistent_credential_web_err(frame_supplier, AuthProviderStatus::UnknownError)
            .await?;

        // Network error
        let frame_supplier = TestWebFrameSupplier::new(
            Err(TokenProviderError::new(ApiError::Network)),
            Err(TokenProviderError::new(ApiError::Network)),
        );
        assert_persistent_credential_web_err(frame_supplier, AuthProviderStatus::NetworkError)
            .await?;
        Ok(())
    }

    /// Assert GetPersistentCredential returns the given error when using the given
    /// WebFrameSupplier mock
    async fn assert_persistent_credential_web_err(
        mock_frame_supplier: TestWebFrameSupplier,
        expected_status: AuthProviderStatus,
    ) -> Result<(), Error> {
        let auth_provider = get_auth_provider_proxy(Some(mock_frame_supplier), None);

        let ui_context = get_authentication_ui_context();
        let (status, refresh_token, user_info) =
            auth_provider.get_persistent_credential(Some(ui_context), None).await?;
        assert_eq!(status, expected_status);
        assert!(refresh_token.is_none());
        assert!(user_info.is_none());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persistent_credential_exchange_auth_code_failures() -> Result<(), Error> {
        // Error response
        let http = TestHttpClient::with_response(
            Some("{\"error\": \"invalid_grant\", \"error_description\": \"ouch\"}"),
            StatusCode::BAD_REQUEST,
        );
        assert_persistent_credential_http_err(http, AuthProviderStatus::ReauthRequired).await?;

        // Network error
        let http = TestHttpClient::with_error(ApiError::Network);
        assert_persistent_credential_http_err(http, AuthProviderStatus::NetworkError).await?;
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persistent_credential_get_user_info_failures() -> Result<(), Error> {
        let auth_code_exchange_body =
            "{\"refresh_token\": \"test-refresh-token\", \"access_token\": \"test-access-token\"}"
                .to_string();
        let auth_code_exchange_response = Ok((Some(auth_code_exchange_body), StatusCode::OK));

        // Error response
        let http = TestHttpClient::with_responses(vec![
            clone_result(&auth_code_exchange_response),
            Ok((None, StatusCode::INTERNAL_SERVER_ERROR)),
        ]);
        assert_persistent_credential_http_err(http, AuthProviderStatus::OauthServerError).await?;

        // Network error
        let http = TestHttpClient::with_responses(vec![
            auth_code_exchange_response,
            Err(TokenProviderError::new(ApiError::Network)),
        ]);
        assert_persistent_credential_http_err(http, AuthProviderStatus::NetworkError).await?;
        Ok(())
    }

    /// Assert GetPersistentCredential returns the given error when the authentication
    /// flow succeeds and using the given HttpClient mock
    async fn assert_persistent_credential_http_err(
        mock_http: TestHttpClient,
        expected_status: AuthProviderStatus,
    ) -> Result<(), Error> {
        let frame_supplier = TestWebFrameSupplier::new(
            Ok(()),
            Ok(Url::parse_with_params(REDIRECT_URI.as_str(), vec![("code", "test-auth-code")])
                .unwrap()),
        );
        let auth_provider = get_auth_provider_proxy(Some(frame_supplier), Some(mock_http));

        let ui_context = get_authentication_ui_context();
        let (status, refresh_token, user_info) =
            auth_provider.get_persistent_credential(Some(ui_context), None).await?;
        assert_eq!(status, expected_status);
        assert!(refresh_token.is_none());
        assert!(user_info.is_none());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persistent_credential_requires_ui_context() -> Result<(), Error> {
        let auth_provider = get_auth_provider_proxy(None, None);
        let result = auth_provider.get_persistent_credential(None, None).await?;
        assert_eq!(result.0, AuthProviderStatus::BadRequest);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_access_token_success() -> Result<(), Error> {
        let http_result = "{\"access_token\": \"test-access-token\", \"expires_in\": 3600}";
        let mock_http = TestHttpClient::with_response(Some(http_result), StatusCode::OK);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let (result_status, result_token) =
            auth_provider.get_app_access_token("credential", None, &mut vec![].into_iter()).await?;
        assert_eq!(result_status, AuthProviderStatus::Ok);
        assert_eq!(
            result_token.unwrap(),
            Box::new(AuthToken {
                token_type: TokenType::AccessToken,
                token: "test-access-token".to_string(),
                expires_in: 3600,
            })
        );

        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_access_token_failures() -> Result<(), Error> {
        // Invalid request
        let mock_http = TestHttpClient::with_error(ApiError::Internal);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result = auth_provider.get_app_access_token("", None, &mut vec![].into_iter()).await?;
        assert_eq!(result.0, AuthProviderStatus::BadRequest);

        // Empty client_id string
        let mock_http = TestHttpClient::with_error(ApiError::Internal);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result = auth_provider
            .get_app_access_token("credential", Some(""), &mut vec![].into_iter())
            .await?;
        assert_eq!(result.0, AuthProviderStatus::BadRequest);

        // Error response
        let http_result = "{\"error\": \"invalid_scope\", \"error_description\": \"bad scope\"}";
        let mock_http = TestHttpClient::with_response(Some(http_result), StatusCode::BAD_REQUEST);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result = auth_provider
            .get_app_access_token("credential", None, &mut vec!["bad-scope"].into_iter())
            .await?;
        assert_eq!(result.0, AuthProviderStatus::OauthServerError);

        // Network error
        let mock_http = TestHttpClient::with_error(ApiError::Network);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result =
            auth_provider.get_app_access_token("credential", None, &mut vec![].into_iter()).await?;
        assert_eq!(result.0, AuthProviderStatus::NetworkError);

        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_id_token_success() -> Result<(), Error> {
        let http_result = "{\"id_token\": \"test-id-token\", \"expires_in\": 3600}";
        let mock_http = TestHttpClient::with_response(Some(http_result), StatusCode::OK);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));

        let (status, result_token) = auth_provider.get_app_id_token("credential", None).await?;
        assert_eq!(status, AuthProviderStatus::Ok);
        assert_eq!(
            result_token.unwrap(),
            Box::new(AuthToken {
                token_type: TokenType::IdToken,
                token: "test-id-token".to_string(),
                expires_in: 3600,
            })
        );

        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_id_token_failures() -> Result<(), Error> {
        // Invalid request
        let mock_http = TestHttpClient::with_error(ApiError::Internal);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result = auth_provider.get_app_id_token("", None).await?;
        assert_eq!(result.0, AuthProviderStatus::BadRequest);

        // Empty audience string
        let mock_http = TestHttpClient::with_error(ApiError::Internal);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result = auth_provider.get_app_id_token("cred", Some("")).await?;
        assert_eq!(result.0, AuthProviderStatus::BadRequest);

        // Error response
        let http_result = "{\"error\": \"invalid_client\"}";
        let mock_http = TestHttpClient::with_response(Some(http_result), StatusCode::BAD_REQUEST);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result = auth_provider.get_app_id_token("credential", None).await?;
        assert_eq!(result.0, AuthProviderStatus::OauthServerError);

        // Network error
        let mock_http = TestHttpClient::with_error(ApiError::Network);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result = auth_provider.get_app_id_token("credential", None).await?;
        assert_eq!(result.0, AuthProviderStatus::NetworkError);

        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_firebase_token_success() -> Result<(), Error> {
        let response_body = "{\"idToken\": \"test-firebase-token\", \"localId\": \"test-id\",\
                             \"email\": \"test@example.com\", \"expiresIn\": \"3600\"}";
        let mock_http = TestHttpClient::with_response(Some(response_body), StatusCode::OK);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));

        let (status, result_token) =
            auth_provider.get_app_firebase_token("id_token", "api_key").await?;
        assert_eq!(status, AuthProviderStatus::Ok);
        assert_eq!(
            result_token.unwrap(),
            Box::new(FirebaseToken {
                id_token: "test-firebase-token".to_string(),
                email: Some("test@example.com".to_string()),
                local_id: Some("test-id".to_string()),
                expires_in: 3600,
            })
        );
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_firebase_token_failures() -> Result<(), Error> {
        // Invalid request
        let mock_http = TestHttpClient::with_error(ApiError::Internal);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result = auth_provider.get_app_firebase_token("", "").await?;
        assert_eq!(result.0, AuthProviderStatus::BadRequest);

        // Error response
        let http_result = "{\"message\": \"invalid api key\"}";
        let mock_http = TestHttpClient::with_response(Some(http_result), StatusCode::BAD_REQUEST);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result = auth_provider.get_app_firebase_token("id_token", "api_key").await?;
        assert_eq!(result.0, AuthProviderStatus::OauthServerError);

        // Network error
        let mock_http = TestHttpClient::with_error(ApiError::Network);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result = auth_provider.get_app_firebase_token("id_token", "api_key").await?;
        assert_eq!(result.0, AuthProviderStatus::NetworkError);

        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_revoke_app_or_persistent_credential_success() -> Result<(), Error> {
        let mock_http = TestHttpClient::with_response(None, StatusCode::OK);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        let result = auth_provider.revoke_app_or_persistent_credential("credential").await?;
        assert_eq!(result, AuthProviderStatus::Ok);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_revoke_app_or_persistent_credential_failures() -> Result<(), Error> {
        // Empty credential
        let auth_provider = get_auth_provider_proxy(None, None);
        let result = auth_provider.revoke_app_or_persistent_credential("").await?;
        assert_eq!(result, AuthProviderStatus::BadRequest);

        // Error response
        let http_response = "bad response";
        let mock_http = TestHttpClient::with_response(Some(http_response), StatusCode::BAD_REQUEST);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        assert_eq!(
            auth_provider.revoke_app_or_persistent_credential("credential").await?,
            AuthProviderStatus::OauthServerError
        );

        // Network error
        let mock_http = TestHttpClient::with_error(ApiError::Network);
        let auth_provider = get_auth_provider_proxy(None, Some(mock_http));
        assert_eq!(
            auth_provider.revoke_app_or_persistent_credential("credential").await?,
            AuthProviderStatus::NetworkError
        );

        Ok(())
    }
}
