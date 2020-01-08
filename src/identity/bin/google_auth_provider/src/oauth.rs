// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::constants::{
    FUCHSIA_CLIENT_ID, OAUTH_AUTHORIZE_URI, OAUTH_DEFAULT_SCOPES, OAUTH_REVOCATION_URI,
    OAUTH_TOKEN_EXCHANGE_URI, REDIRECT_URI,
};
use crate::error::{ResultExt, TokenProviderError};
use crate::http::{HttpClient, HttpRequest, HttpRequestBuilder};
use crate::oauth_open_id_connect;
use crate::time::Clock;
use crate::web::{StandaloneWebFrame, WebFrameSupplier};

use anyhow::format_err;
use core::marker::PhantomData;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_auth::AuthenticationUiContextMarker;
use fidl_fuchsia_identity_external::{
    Error as ApiError, OauthAccessTokenFromOauthRefreshTokenRequest, OauthRefreshTokenRequest,
    OauthRequest, OauthRequestStream,
};
use fidl_fuchsia_identity_tokens::{OauthAccessToken, OauthRefreshToken};
use fuchsia_scenic::ViewTokenPair;
use fuchsia_zircon::Duration;
use futures::prelude::*;
use hyper::StatusCode;
use log::warn;
use serde_derive::Deserialize;
use serde_json::from_str;
use std::borrow::Cow;
use std::collections::HashMap;
use url::{form_urlencoded, Url};

type TokenProviderResult<T> = Result<T, TokenProviderError>;

/// An implementation of the `Oauth` FIDL protocol that communicates
/// with the Google identity system to perform authentication for and issue
/// Oauth tokens for Google accounts.
pub struct Oauth<'a, WFS, HC, C>
where
    WFS: WebFrameSupplier,
    HC: HttpClient,
    C: Clock,
{
    /// A supplier used to generate web frames on demand.
    web_frame_supplier: WFS,
    /// A client used for making HTTP requests.
    http_client: HC,
    /// A marker that denotes which clock to use.
    _clock: PhantomData<&'a C>,
}

impl<WFS, HC, C> Oauth<'_, WFS, HC, C>
where
    WFS: WebFrameSupplier,
    HC: HttpClient,
    C: Clock,
{
    /// Create a new Oauth.
    pub fn new(web_frame_supplier: WFS, http_client: HC) -> Self {
        Oauth { web_frame_supplier, http_client, _clock: PhantomData }
    }

    /// Handle requests passed to the supplied stream.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: OauthRequestStream,
    ) -> Result<(), fidl::Error> {
        while let Some(request) = stream.try_next().await? {
            self.handle_request(request).await?;
        }
        Ok(())
    }

    async fn handle_request(&self, request: OauthRequest) -> Result<(), fidl::Error> {
        match request {
            OauthRequest::CreateRefreshToken { request, responder } => {
                let mut response =
                    self.create_refresh_token(request).await.map_err(|e| e.api_error);
                responder.send(&mut response).map_err(|e| {
                    warn!("Error sending response for CreateRefreshToken: {:?}", e);
                    e
                })
            }
            OauthRequest::GetAccessTokenFromRefreshToken { request, responder } => {
                let mut response = self
                    .get_access_token_from_refresh_token(request)
                    .await
                    .map_err(|e| e.api_error);
                responder.send(&mut response).map_err(|e| {
                    warn!("Error sending response for GetAccessTokenFromRefreshToken: {:?}", e);
                    e
                })
            }
            OauthRequest::RevokeRefreshToken { refresh_token, responder } => {
                let mut response =
                    self.revoke_refresh_token(refresh_token).await.map_err(|e| e.api_error);
                responder.send(&mut response).map_err(|e| {
                    warn!("Error sending response for RevokeRefreshToken: {:?}", e);
                    e
                })
            }
            OauthRequest::RevokeAccessToken { access_token, responder } => {
                let mut response =
                    self.revoke_access_token(access_token).await.map_err(|e| e.api_error);
                responder.send(&mut response).map_err(|e| {
                    warn!("Error sending response for RevokeAccessToken: {:?}", e);
                    e
                })
            }
        }
    }

    async fn create_refresh_token(
        &self,
        request: OauthRefreshTokenRequest,
    ) -> TokenProviderResult<(OauthRefreshToken, OauthAccessToken)> {
        let account_id = request.account_id;
        let ui_context =
            request.ui_context.ok_or(TokenProviderError::new(ApiError::InvalidRequest))?;

        let auth_code = self.get_auth_code(ui_context, account_id).await?;
        self.exchange_auth_code(auth_code).await
    }

    async fn get_access_token_from_refresh_token(
        &self,
        request: OauthAccessTokenFromOauthRefreshTokenRequest,
    ) -> TokenProviderResult<OauthAccessToken> {
        let refresh_token_contents = request
            .refresh_token
            .ok_or(TokenProviderError::new(ApiError::InvalidRequest))?
            .content
            .ok_or(TokenProviderError::new(ApiError::InvalidRequest))?;

        let request = build_request_with_refresh_token(
            RefreshToken(refresh_token_contents),
            request.scopes.unwrap_or(vec![]),
            request.client_id,
        )?;
        let (response_body, status) = self.http_client.request(request).await?;
        let (access_token, expires_in) =
            parse_response_without_refresh_token(response_body, status)?;
        let expiry_time = C::current_time() + expires_in;
        Ok(OauthAccessToken {
            content: Some(access_token.0),
            expiry_time: Some(expiry_time.into_nanos()),
        })
    }

    async fn revoke_refresh_token(
        &self,
        refresh_token: OauthRefreshToken,
    ) -> TokenProviderResult<()> {
        let refresh_token_contents =
            refresh_token.content.ok_or(TokenProviderError::new(ApiError::InvalidRequest))?;
        if refresh_token_contents.is_empty() {
            return Err(TokenProviderError::new(ApiError::InvalidRequest));
        }

        let request = build_revocation_request(refresh_token_contents)?;
        let (response_body, status) = self.http_client.request(request).await?;
        parse_revocation_response(response_body, status)
    }

    async fn revoke_access_token(&self, access_token: OauthAccessToken) -> TokenProviderResult<()> {
        let access_token_contents =
            access_token.content.ok_or(TokenProviderError::new(ApiError::InvalidRequest))?;
        if access_token_contents.is_empty() {
            return Err(TokenProviderError::new(ApiError::InvalidRequest));
        }

        let request = build_revocation_request(access_token_contents)?;
        let (response_body, status) = self.http_client.request(request).await?;
        parse_revocation_response(response_body, status)
    }

    /// Direct user through Google OAuth authorization flow and obtain an auth
    /// code.
    async fn get_auth_code(
        &self,
        auth_ui_context: ClientEnd<AuthenticationUiContextMarker>,
        account_id: Option<String>,
    ) -> TokenProviderResult<AuthCode> {
        let auth_ui_context =
            auth_ui_context.into_proxy().token_provider_error(ApiError::Resource)?;
        let mut web_frame = self
            .web_frame_supplier
            .new_standalone_frame()
            .token_provider_error(ApiError::Resource)?;
        let ViewTokenPair { view_token, mut view_holder_token } =
            ViewTokenPair::new().token_provider_error(ApiError::Resource)?;
        let authorize_url = Self::authorize_url(account_id)?;

        web_frame.display_url(view_token, authorize_url).await?;
        auth_ui_context
            .start_overlay(&mut view_holder_token)
            .token_provider_error(ApiError::Resource)?;

        let redirect_url = web_frame.wait_for_redirect(REDIRECT_URI.clone()).await?;

        if let Err(err) = auth_ui_context.stop_overlay() {
            warn!("Error while attempting to stop UI overlay: {:?}", err);
        }

        parse_auth_code_from_redirect(redirect_url)
    }

    /// Construct an authorize URL to visit for authorizing a Google user.  If account_id
    /// is given then the URL authorizes against the given user.
    fn authorize_url(account_id: Option<String>) -> TokenProviderResult<Url> {
        let mut params = vec![
            ("scope", OAUTH_DEFAULT_SCOPES.as_str()),
            ("glif", "false"),
            ("response_type", "code"),
            ("redirect_uri", REDIRECT_URI.as_str()),
            ("client_id", FUCHSIA_CLIENT_ID),
        ]; // TODO(satsukiu): add 'state' parameter here and verify it in the redirect.
        if let Some(user) = account_id.as_ref() {
            params.push(("login_hint", user));
        }

        Url::parse_with_params(OAUTH_AUTHORIZE_URI.as_str(), &params)
            .token_provider_error(ApiError::Internal)
    }

    /// Trades an OAuth auth code for a refresh token and access token.
    async fn exchange_auth_code(
        &self,
        auth_code: AuthCode,
    ) -> TokenProviderResult<(OauthRefreshToken, OauthAccessToken)> {
        let request = build_request_with_auth_code(auth_code)?;
        let (response_body, status_code) = self.http_client.request(request).await?;
        let (refresh_token, access_token, expires_in) =
            parse_response_with_refresh_token(response_body, status_code)?;
        let expiry_time = C::current_time() + expires_in;

        // Google's Authorize endpoint accepts user ID via the login_hint query parameter,
        // but does not return the user ID in response.  We use Google's OpenID Connect
        // UserInfo endpoint to acquire the standard user ID, rather than generating a
        // device-local identifier, so that we could use the Authorize endpoint for any
        // future reauthentication attempts.
        let request = oauth_open_id_connect::build_user_info_request(&access_token)?;
        let (response_body, status_code) = self.http_client.request(request).await?;
        let user_info =
            oauth_open_id_connect::parse_user_info_response(response_body, status_code)?;

        Ok((
            OauthRefreshToken { content: Some(refresh_token.0), account_id: Some(user_info.sub) },
            OauthAccessToken {
                content: Some(access_token.0),
                expiry_time: Some(expiry_time.into_nanos()),
            },
        ))
    }
}

#[derive(Debug, PartialEq)]
pub struct AuthCode(pub String);
#[derive(Debug, PartialEq)]
pub struct RefreshToken(pub String);
#[derive(Debug, PartialEq)]
pub struct AccessToken(pub String);

/// Response type for Oauth access token requests with a refresh token.
#[derive(Debug, Deserialize)]
struct AccessTokenResponseWithRefreshToken {
    pub access_token: String,
    pub refresh_token: String,
    pub expires_in: u32,
}

/// Response type for Oauth access token requests where a refresh token is not expected.
#[derive(Debug, Deserialize)]
struct AccessTokenResponseWithoutRefreshToken {
    pub access_token: String,
    pub expires_in: u32,
}

/// Error response type for Oauth requests.
#[derive(Debug, Deserialize)]
struct OAuthErrorResponse {
    pub error: String,
    pub error_description: Option<String>,
}

/// Construct an Oauth access token request using an authorization code.
fn build_request_with_auth_code(auth_code: AuthCode) -> TokenProviderResult<HttpRequest> {
    let request_body = form_urlencoded::Serializer::new(String::new())
        .append_pair("code", auth_code.0.as_str())
        .append_pair("redirect_uri", REDIRECT_URI.as_str())
        .append_pair("client_id", FUCHSIA_CLIENT_ID)
        .append_pair("grant_type", "authorization_code")
        .finish();

    HttpRequestBuilder::new(OAUTH_TOKEN_EXCHANGE_URI.as_str(), "POST")
        .with_header("content-type", "application/x-www-form-urlencoded")
        .set_body(&request_body)
        .finish()
}

/// Construct an Oauth access token request using a refresh token grant.  If `client_id` is not
/// given the Fuchsia client id is used.  If `scopes` is empty, it is ommitted from the request,
/// resulting in a request for the same scopes as the refresh_token
/// (https://tools.ietf.org/html/rfc6749#section-6)
pub fn build_request_with_refresh_token(
    refresh_token: RefreshToken,
    scopes: Vec<String>,
    client_id: Option<String>,
) -> TokenProviderResult<HttpRequest> {
    let mut request_body_serializer = form_urlencoded::Serializer::new(String::new());
    request_body_serializer
        .append_pair("refresh_token", refresh_token.0.as_str())
        .append_pair("client_id", client_id.as_ref().map_or(FUCHSIA_CLIENT_ID, String::as_str))
        .append_pair("grant_type", "refresh_token");
    if !scopes.is_empty() {
        request_body_serializer.append_pair("scope", &scopes.join(" "));
    }
    let request_body = request_body_serializer.finish();

    HttpRequestBuilder::new(OAUTH_TOKEN_EXCHANGE_URI.as_str(), "POST")
        .with_header("content-type", "application/x-www-form-urlencoded")
        .set_body(&request_body)
        .finish()
}

/// Construct an Oauth token revocation request.  `credential` may be either
/// an access token or refresh token.
fn build_revocation_request(credential: String) -> TokenProviderResult<HttpRequest> {
    let request_body =
        form_urlencoded::Serializer::new(String::new()).append_pair("token", &credential).finish();

    HttpRequestBuilder::new(OAUTH_REVOCATION_URI.as_str(), "POST")
        .with_header("content-type", "application/x-www-form-urlencoded")
        .set_body(&request_body)
        .finish()
}

/// Parses a response for an OAuth access token request when both a refresh token
/// and access token are expected in the response.  Returns a refresh token, access
/// token, and remaining lifetime of the access token.
// TODO(satsukiu): directly return structs as defined in fuchsia.identity.tokens FIDL
// to simplify
fn parse_response_with_refresh_token(
    response_body: Option<String>,
    status: StatusCode,
) -> TokenProviderResult<(RefreshToken, AccessToken, Duration)> {
    match (response_body.as_ref(), status) {
        (Some(response), StatusCode::OK) => {
            let response = from_str::<AccessTokenResponseWithRefreshToken>(&response)
                .token_provider_error(ApiError::Server)?;
            Ok((
                RefreshToken(response.refresh_token),
                AccessToken(response.access_token),
                Duration::from_seconds(response.expires_in as i64),
            ))
        }
        (Some(response), status) if status.is_client_error() => {
            let error_response =
                from_str::<OAuthErrorResponse>(&response).token_provider_error(ApiError::Server)?;
            let error = match error_response.error.as_str() {
                "invalid_grant" => ApiError::InvalidToken,
                error_code => {
                    warn!("Got unexpected error code during auth code exchange: {}", error_code);
                    ApiError::Server
                }
            };
            Err(TokenProviderError::new(error))
        }
        _ => Err(TokenProviderError::new(ApiError::Server)),
    }
}

/// Parses a response for an OAuth access token request when a refresh token is
/// not expected.  Returns an access token and the lifetime of the token.
fn parse_response_without_refresh_token(
    response_body: Option<String>,
    status: StatusCode,
) -> TokenProviderResult<(AccessToken, Duration)> {
    match (response_body.as_ref(), status) {
        (Some(response), StatusCode::OK) => {
            let response = from_str::<AccessTokenResponseWithoutRefreshToken>(&response)
                .token_provider_error(ApiError::Server)?;
            Ok((
                AccessToken(response.access_token),
                Duration::from_seconds(response.expires_in as i64),
            ))
        }
        (Some(response), status) if status.is_client_error() => {
            let response =
                from_str::<OAuthErrorResponse>(&response).token_provider_error(ApiError::Server)?;
            let error = match response.error.as_str() {
                "invalid_grant" => ApiError::InvalidToken,
                error_code => {
                    warn!("Got unexpected error code from access token request: {}", error_code);
                    ApiError::Server
                }
            };
            Err(TokenProviderError::new(error))
        }
        _ => Err(TokenProviderError::new(ApiError::Server)),
    }
}

/// Parses a response for an Oauth revocation request.
fn parse_revocation_response(
    response_body: Option<String>,
    status: StatusCode,
) -> TokenProviderResult<()> {
    match (response_body.as_ref(), status) {
        (_, StatusCode::OK) => Ok(()),
        (Some(response), status) if status.is_client_error() => {
            let response =
                from_str::<OAuthErrorResponse>(&response).token_provider_error(ApiError::Server)?;
            warn!("Got unexpected error code during token revocation: {}", response.error);
            Err(TokenProviderError::new(ApiError::Server))
        }
        _ => Err(TokenProviderError::new(ApiError::Server)),
    }
}

/// Parses an auth code out of a redirect URL reached through an OAuth
/// authorization flow.
fn parse_auth_code_from_redirect(url: Url) -> TokenProviderResult<AuthCode> {
    if (url.scheme(), url.domain(), url.path())
        != (REDIRECT_URI.scheme(), REDIRECT_URI.domain(), REDIRECT_URI.path())
    {
        return Err(TokenProviderError::new(ApiError::Internal)
            .with_cause(format_err!("Redirected to unexpected URL")));
    }

    let params = url.query_pairs().collect::<HashMap<Cow<'_, str>, Cow<'_, str>>>();

    if let Some(auth_code) = params.get("code") {
        Ok(AuthCode(auth_code.as_ref().to_string()))
    } else if let Some(error_code) = params.get("error") {
        let error = match error_code.as_ref() {
            "access_denied" => ApiError::Aborted,
            "server_error" => ApiError::Server,
            "temporarily_unavailable" => ApiError::Server,
            _ => ApiError::Unknown,
        };
        Err(TokenProviderError::new(error))
    } else {
        Err(TokenProviderError::new(ApiError::Unknown)
            .with_cause(format_err!("Authorize redirect contained neither code nor error")))
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::http::mock::TestHttpClient;
    use crate::time::mock::{FixedClock, TEST_CURRENT_TIME};
    use crate::web::mock::TestWebFrameSupplier;
    use fidl::endpoints::{create_proxy_and_stream, create_request_stream};
    use fidl_fuchsia_identity_external::{OauthMarker, OauthProxy};
    use fuchsia_async as fasync;

    /// Creates and runs an Oauth and returns a connection to it.  If
    /// frame_supplier is not given, the Oauth is instantiated with a
    /// `TestWebFrameProvider` that supplies `StandaloneWebFrame`s that
    /// return an `UnsupportedOperation` error.
    /// Similarly, if http_client is not given, the auth provider is
    /// instantiated with a `TestHttpClient` that returns an
    /// `UnsupportedOperation` error.
    fn get_oauth_proxy(
        frame_supplier: Option<TestWebFrameSupplier>,
        http_client: Option<TestHttpClient>,
    ) -> OauthProxy {
        let (oauth_proxy, oauth_request_stream) =
            create_proxy_and_stream::<OauthMarker>().expect("Failed to create proxy and stream");

        let frame_supplier = frame_supplier.unwrap_or(TestWebFrameSupplier::new(
            Err(TokenProviderError::new(ApiError::UnsupportedOperation)),
            Err(TokenProviderError::new(ApiError::UnsupportedOperation)),
        ));
        let http =
            http_client.unwrap_or(TestHttpClient::with_error(ApiError::UnsupportedOperation));

        let oauth = Oauth::<_, _, FixedClock>::new(frame_supplier, http);
        fasync::spawn(async move {
            oauth
                .handle_requests_from_stream(oauth_request_stream)
                .await
                .expect("Error handling AuthProvider channel");
        });

        oauth_proxy
    }

    fn get_authentication_ui_context() -> ClientEnd<AuthenticationUiContextMarker> {
        let (client, mut stream) = create_request_stream::<AuthenticationUiContextMarker>()
            .expect("Failed to create authentication UI context stream");
        fasync::spawn(async move { while let Some(_) = stream.try_next().await.unwrap() {} });
        client
    }

    fn construct_refresh_token(contents: &str, account_id: &str) -> OauthRefreshToken {
        OauthRefreshToken {
            content: Some(contents.to_string()),
            account_id: Some(account_id.to_string()),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn test_create_refresh_token_success() -> Result<(), anyhow::Error> {
        let mock_frame_supplier = TestWebFrameSupplier::new(
            Ok(()),
            Ok(Url::parse_with_params(REDIRECT_URI.as_str(), vec![("code", "test-auth-code")])?),
        );
        let refresh_token_response =
            "{\"refresh_token\": \"test-refresh-token\", \"access_token\": \"test-access-token\", \
             \"expires_in\": 3600}";
        let user_info_response =
            "{\"sub\": \"test-id\", \"name\": \"Bill\", \"profile\": \"profile-url\", \
             \"picture\": \"picture-url\"}";

        let mock_http = TestHttpClient::with_responses(vec![
            Ok((Some(refresh_token_response.to_string()), StatusCode::OK)),
            Ok((Some(user_info_response.to_string()), StatusCode::OK)),
        ]);
        let oauth = get_oauth_proxy(Some(mock_frame_supplier), Some(mock_http));

        let ui_context = get_authentication_ui_context();
        let (refresh_token, access_token) = oauth
            .create_refresh_token(OauthRefreshTokenRequest {
                account_id: None,
                ui_context: Some(ui_context),
            })
            .await?
            .unwrap();

        assert_eq!(refresh_token, construct_refresh_token("test-refresh-token", "test-id"));
        assert_eq!(
            access_token,
            OauthAccessToken {
                content: Some("test-access-token".to_string()),
                expiry_time: Some(
                    (TEST_CURRENT_TIME.clone() + Duration::from_seconds(3600)).into_nanos()
                ),
            }
        );
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_create_refresh_token_web_errors() {
        // User denies access
        let frame_supplier = TestWebFrameSupplier::new(
            Ok(()),
            Ok(Url::parse_with_params(REDIRECT_URI.as_str(), vec![("error", "access_denied")])
                .unwrap()),
        );
        assert_create_refresh_token_web_err(frame_supplier, ApiError::Aborted).await;

        // Web frame error - UI maybe canceled
        let frame_supplier =
            TestWebFrameSupplier::new(Ok(()), Err(TokenProviderError::new(ApiError::Unknown)));
        assert_create_refresh_token_web_err(frame_supplier, ApiError::Unknown).await;

        // Network error
        let frame_supplier = TestWebFrameSupplier::new(
            Err(TokenProviderError::new(ApiError::Network)),
            Err(TokenProviderError::new(ApiError::Network)),
        );
        assert_create_refresh_token_web_err(frame_supplier, ApiError::Network).await;
    }

    /// Assert GetPersistentCredential returns the given error when using the given
    /// WebFrameSupplier mock
    async fn assert_create_refresh_token_web_err(
        mock_frame_supplier: TestWebFrameSupplier,
        expected_error: ApiError,
    ) {
        let oauth = get_oauth_proxy(Some(mock_frame_supplier), None);

        assert_eq!(
            oauth
                .create_refresh_token(OauthRefreshTokenRequest {
                    account_id: None,
                    ui_context: Some(get_authentication_ui_context())
                })
                .await
                .unwrap()
                .unwrap_err(),
            expected_error
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persistent_credential_exchange_auth_code_failures() {
        // Error response
        let http = TestHttpClient::with_response(
            Some("{\"error\": \"invalid_grant\", \"error_description\": \"ouch\"}"),
            StatusCode::BAD_REQUEST,
        );
        assert_persistent_credential_http_err(http, ApiError::InvalidToken).await;

        // Network error
        let http = TestHttpClient::with_error(ApiError::Network);
        assert_persistent_credential_http_err(http, ApiError::Network).await;
    }

    /// Assert GetPersistentCredential returns the given error when the authentication
    /// flow succeeds and using the given HttpClient mock
    async fn assert_persistent_credential_http_err(
        mock_http: TestHttpClient,
        expected_error: ApiError,
    ) {
        let frame_supplier = TestWebFrameSupplier::new(
            Ok(()),
            Ok(Url::parse_with_params(REDIRECT_URI.as_str(), vec![("code", "test-auth-code")])
                .unwrap()),
        );
        let oauth = get_oauth_proxy(Some(frame_supplier), Some(mock_http));

        assert_eq!(
            oauth
                .create_refresh_token(OauthRefreshTokenRequest {
                    account_id: None,
                    ui_context: Some(get_authentication_ui_context())
                })
                .await
                .unwrap()
                .unwrap_err(),
            expected_error
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn test_create_refresh_token_requires_ui_context() -> Result<(), anyhow::Error> {
        let oauth = get_oauth_proxy(None, None);
        assert_eq!(
            oauth
                .create_refresh_token(OauthRefreshTokenRequest {
                    account_id: None,
                    ui_context: None
                })
                .await?,
            Err(ApiError::InvalidRequest)
        );
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_access_token_from_refresh_token_success() -> Result<(), anyhow::Error> {
        let http_result = "{\"access_token\": \"test-access-token\", \"expires_in\": 3600}";
        let mock_http = TestHttpClient::with_response(Some(http_result), StatusCode::OK);
        let oauth = get_oauth_proxy(None, Some(mock_http));
        let access_token = oauth
            .get_access_token_from_refresh_token(OauthAccessTokenFromOauthRefreshTokenRequest {
                refresh_token: Some(construct_refresh_token("credential", "test-account")),
                client_id: None,
                scopes: None,
            })
            .await?
            .unwrap();
        assert_eq!(access_token.content.unwrap(), "test-access-token".to_string());
        assert_eq!(
            access_token.expiry_time.unwrap(),
            (TEST_CURRENT_TIME.clone() + Duration::from_seconds(3600)).into_nanos()
        );
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_access_token_from_refresh_token_failures() -> Result<(), anyhow::Error> {
        // Invalid request
        let mock_http = TestHttpClient::with_error(ApiError::Internal);
        let oauth = get_oauth_proxy(None, Some(mock_http));
        let result = oauth
            .get_access_token_from_refresh_token(OauthAccessTokenFromOauthRefreshTokenRequest {
                refresh_token: None,
                client_id: None,
                scopes: None,
            })
            .await?;
        assert_eq!(result, Err(ApiError::InvalidRequest));

        // Error response
        let http_result = "{\"error\": \"invalid_scope\", \"error_description\": \"bad scope\"}";
        let mock_http = TestHttpClient::with_response(Some(http_result), StatusCode::BAD_REQUEST);
        let oauth = get_oauth_proxy(None, Some(mock_http));
        let result = oauth
            .get_access_token_from_refresh_token(OauthAccessTokenFromOauthRefreshTokenRequest {
                refresh_token: Some(construct_refresh_token("credential", "test-account")),
                client_id: None,
                scopes: None,
            })
            .await?;
        assert_eq!(result, Err(ApiError::Server));

        // Network error
        let mock_http = TestHttpClient::with_error(ApiError::Network);
        let oauth = get_oauth_proxy(None, Some(mock_http));
        let result = oauth
            .get_access_token_from_refresh_token(OauthAccessTokenFromOauthRefreshTokenRequest {
                refresh_token: Some(construct_refresh_token("credential", "test-account")),
                client_id: None,
                scopes: None,
            })
            .await?;
        assert_eq!(result, Err(ApiError::Network));

        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_revoke_refresh_token_success() -> Result<(), anyhow::Error> {
        let mock_http = TestHttpClient::with_response(None, StatusCode::OK);
        let oauth = get_oauth_proxy(None, Some(mock_http));
        assert!(oauth
            .revoke_refresh_token(construct_refresh_token("credential", "test-account"))
            .await?
            .is_ok());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_revoke_refresh_token_failures() -> Result<(), anyhow::Error> {
        // Empty credential
        let oauth = get_oauth_proxy(None, None);
        assert_eq!(
            oauth.revoke_refresh_token(construct_refresh_token("", "test-account")).await?,
            Err(ApiError::InvalidRequest)
        );

        // Error response
        let http_response = "bad response";
        let mock_http = TestHttpClient::with_response(Some(http_response), StatusCode::BAD_REQUEST);
        let oauth = get_oauth_proxy(None, Some(mock_http));
        assert_eq!(
            oauth
                .revoke_refresh_token(construct_refresh_token("credential", "test-account"))
                .await?,
            Err(ApiError::Server)
        );

        // Network error
        let mock_http = TestHttpClient::with_error(ApiError::Network);
        let oauth = get_oauth_proxy(None, Some(mock_http));
        assert_eq!(
            oauth
                .revoke_refresh_token(construct_refresh_token("credential", "test-account"))
                .await?,
            Err(ApiError::Network)
        );

        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_revoke_access_token_success() -> Result<(), anyhow::Error> {
        let mock_http = TestHttpClient::with_response(None, StatusCode::OK);
        let oauth = get_oauth_proxy(None, Some(mock_http));
        assert!(oauth
            .revoke_access_token(OauthAccessToken {
                content: Some("access-token".to_string()),
                expiry_time: Some(99999),
            })
            .await?
            .is_ok());
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_revoke_access_token_failures() -> Result<(), anyhow::Error> {
        // Empty credential
        let oauth = get_oauth_proxy(None, None);
        assert_eq!(
            oauth
                .revoke_access_token(OauthAccessToken {
                    content: Some("".to_string()),
                    expiry_time: Some(99999),
                })
                .await?,
            Err(ApiError::InvalidRequest)
        );

        // Error response
        let http_response = "bad response";
        let mock_http = TestHttpClient::with_response(Some(http_response), StatusCode::BAD_REQUEST);
        let oauth = get_oauth_proxy(None, Some(mock_http));
        assert_eq!(
            oauth
                .revoke_access_token(OauthAccessToken {
                    content: Some("access-token".to_string()),
                    expiry_time: Some(99999),
                })
                .await?,
            Err(ApiError::Server)
        );

        // Network error
        let mock_http = TestHttpClient::with_error(ApiError::Network);
        let oauth = get_oauth_proxy(None, Some(mock_http));
        assert_eq!(
            oauth
                .revoke_access_token(OauthAccessToken {
                    content: Some("access-token".to_string()),
                    expiry_time: Some(99999),
                })
                .await?,
            Err(ApiError::Network)
        );

        Ok(())
    }

    fn url_with_query(url_base: &Url, query: &str) -> Url {
        let mut url = url_base.clone();
        url.set_query(Some(query));
        url
    }

    #[test]
    fn test_parse_response_with_refresh_token_success() {
        let response_body = Some(
            "{\"refresh_token\": \"test-refresh-token\", \"access_token\": \"test-access-token\", \
             \"expires_in\": 3600}"
                .to_string(),
        );
        assert_eq!(
            (
                RefreshToken("test-refresh-token".to_string()),
                AccessToken("test-access-token".to_string()),
                Duration::from_seconds(3600)
            ),
            parse_response_with_refresh_token(response_body, StatusCode::OK).unwrap()
        )
    }

    #[test]
    fn test_parse_response_with_refresh_token_failures() {
        // Expired auth token
        let response =
            "{\"error\": \"invalid_grant\", \"error_description\": \"ouch\"}".to_string();
        let result = parse_response_with_refresh_token(Some(response), StatusCode::BAD_REQUEST);
        assert_eq!(result.unwrap_err().api_error, ApiError::InvalidToken);

        // Server side error
        let result = parse_response_with_refresh_token(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Invalid client error
        let response = "{\"error\": \"invalid_client\"}".to_string();
        let result = parse_response_with_refresh_token(Some(response), StatusCode::UNAUTHORIZED);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Malformed response
        let response = "{\"a malformed response\"}".to_string();
        let result = parse_response_with_refresh_token(Some(response), StatusCode::OK);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);
    }

    #[test]
    fn test_parse_response_without_refresh_token_success() {
        let response_body =
            Some("{\"access_token\": \"test-access-token\", \"expires_in\": 3600}".to_string());
        assert_eq!(
            (AccessToken("test-access-token".to_string()), Duration::from_seconds(3600)),
            parse_response_without_refresh_token(response_body, StatusCode::OK).unwrap()
        )
    }

    #[test]
    fn test_parse_response_without_refresh_token_failures() {
        // Expired auth token
        let response =
            "{\"error\": \"invalid_grant\", \"error_description\": \"expired\"}".to_string();
        let result = parse_response_without_refresh_token(Some(response), StatusCode::BAD_REQUEST);
        assert_eq!(result.unwrap_err().api_error, ApiError::InvalidToken);

        // Server side error
        let result = parse_response_without_refresh_token(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Invalid client error
        let response = "{\"error\": \"invalid_client\"}".to_string();
        let result = parse_response_without_refresh_token(Some(response), StatusCode::UNAUTHORIZED);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Malformed response
        let response = "{\"a malformed response\"}".to_string();
        let result = parse_response_without_refresh_token(Some(response), StatusCode::OK);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);
    }

    #[test]
    fn test_parse_revocation_response_success() {
        assert!(parse_revocation_response(None, StatusCode::OK).is_ok());
    }

    #[test]
    fn test_parse_revocation_response_failures() {
        // Server side error
        let result = parse_revocation_response(None, StatusCode::INTERNAL_SERVER_ERROR);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);

        // Malformed response
        let response = "bad response".to_string();
        let result = parse_revocation_response(Some(response), StatusCode::BAD_REQUEST);
        assert_eq!(result.unwrap_err().api_error, ApiError::Server);
    }

    #[test]
    fn test_auth_code_from_redirect() {
        // Success case
        let success_url = url_with_query(&REDIRECT_URI, "code=test-auth-code");
        assert_eq!(
            AuthCode("test-auth-code".to_string()),
            parse_auth_code_from_redirect(success_url).unwrap()
        );

        // Access denied case
        let canceled_url = url_with_query(&REDIRECT_URI, "error=access_denied");
        assert_eq!(
            ApiError::Aborted,
            parse_auth_code_from_redirect(canceled_url).unwrap_err().api_error
        );

        // Unexpected redirect
        let error_url = Url::parse("ftp://incorrect/some-page").unwrap();
        assert_eq!(
            ApiError::Internal,
            parse_auth_code_from_redirect(error_url).unwrap_err().api_error
        );

        // Unknown error
        let invalid_url = url_with_query(&REDIRECT_URI, "error=invalid_request");
        assert_eq!(
            ApiError::Unknown,
            parse_auth_code_from_redirect(invalid_url).unwrap_err().api_error
        );

        // No code or error in url.
        assert_eq!(
            ApiError::Unknown,
            parse_auth_code_from_redirect(REDIRECT_URI.clone()).unwrap_err().api_error
        );
    }
}
