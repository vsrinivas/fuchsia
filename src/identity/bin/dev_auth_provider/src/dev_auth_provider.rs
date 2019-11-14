// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::encoding::OutOfLine;
use fidl::endpoints::ServerEnd;
use fidl::Error;
use fidl_fuchsia_auth::{
    AuthProviderGetAppAccessTokenFromAssertionJwtResponder, AuthProviderGetAppAccessTokenResponder,
    AuthProviderGetAppFirebaseTokenResponder, AuthProviderGetAppIdTokenResponder,
    AuthProviderGetPersistentCredentialFromAttestationJwtResponder,
    AuthProviderGetPersistentCredentialResponder, AuthProviderMarker, AuthProviderRequest,
    AuthProviderRevokeAppOrPersistentCredentialResponder, AuthProviderStatus, UserProfileInfo,
};
use fuchsia_async as fasync;
use futures::future;
use futures::prelude::*;
use log::warn;
use rand::distributions::Alphanumeric;
use rand::{thread_rng, Rng};
use std::time::Duration;

const TOKEN_LIFETIME: Duration = Duration::from_secs(3600); // one hour lifetime
const USER_PROFILE_INFO_ID_DOMAIN: &str = "@example.com";
const USER_PROFILE_INFO_DISPLAY_NAME: &str = "test_user_display_name";
const USER_PROFILE_INFO_URL: &str = "http://test_user/profile/url";
const USER_PROFILE_INFO_IMAGE_URL: &str = "http://test_user/profile/image/url";
const FIREBASE_TOKEN_EMAIL_DOMAIN: &str = "@firebase.example.com";
const RANDOM_STRING_LENGTH: usize = 10;

/// Generate random alphanumeric string of fixed length RANDOM_STRING_LENGTH
/// for creating unique tokens or id.
fn generate_random_string() -> String {
    thread_rng().sample_iter(&Alphanumeric).take(RANDOM_STRING_LENGTH).collect()
}

/// The AuthProvider struct is holding implementation of the `AuthProvider` fidl
/// interface. This implementation is serving as a testing endpoint for token
/// manager.
pub struct AuthProvider;

impl AuthProvider {
    /// Spawn a new task of handling request from the
    /// `AuthProviderRequestStream` by calling the `handle_request` method.
    /// Create a warning on error.
    pub fn spawn(server_end: ServerEnd<AuthProviderMarker>) {
        match server_end.into_stream() {
            Err(err) => {
                warn!("Error creating AuthProvider request stream {:?}", err);
            }
            Ok(request_stream) => fasync::spawn(
                request_stream
                    .try_for_each(|r| future::ready(Self::handle_request(r)))
                    .unwrap_or_else(|e| warn!("Error running AuthProvider{:?}", e)),
            ),
        };
    }

    /// Handle single `AuthProviderRequest` by calling the corresponding method
    /// according to the actual variant of the `AuthProviderRequest` enum.
    fn handle_request(req: AuthProviderRequest) -> Result<(), Error> {
        match req {
            AuthProviderRequest::GetPersistentCredential { user_profile_id, responder, .. } => {
                Self::get_persistent_credential(user_profile_id, responder)
            }

            AuthProviderRequest::GetAppAccessToken { credential, client_id, responder, .. } => {
                Self::get_app_access_token(credential, client_id, responder)
            }

            AuthProviderRequest::GetAppIdToken { credential, responder, .. } => {
                Self::get_app_id_token(credential, responder)
            }

            AuthProviderRequest::GetAppFirebaseToken { firebase_api_key, responder, .. } => {
                Self::get_app_firebase_token(firebase_api_key, responder)
            }

            AuthProviderRequest::RevokeAppOrPersistentCredential { responder, .. } => {
                Self::revoke_app_or_persistent_credential(responder)
            }

            AuthProviderRequest::GetPersistentCredentialFromAttestationJwt {
                responder, ..
            } => Self::get_persistent_credential_from_attestation_jwt(responder),

            AuthProviderRequest::GetAppAccessTokenFromAssertionJwt { responder, .. } => {
                Self::get_app_access_token_from_assertion_jwt(responder)
            }
        }
    }

    /// Implementation of the `GetPersistenCredential` method for the
    /// `AuthProvider` fidl interface. The field auth_ui_context is removed here
    /// as we will never use it in the dev auth provider.
    fn get_persistent_credential(
        user_profile_id: Option<String>,
        responder: AuthProviderGetPersistentCredentialResponder,
    ) -> Result<(), Error> {
        let mut user_id = user_profile_id.unwrap_or("".to_string());
        if user_id.is_empty() {
            user_id = generate_random_string() + USER_PROFILE_INFO_ID_DOMAIN;
        }
        let mut user_profile_info = Some(UserProfileInfo {
            id: user_id,
            display_name: Some(USER_PROFILE_INFO_DISPLAY_NAME.to_string()),
            url: Some(USER_PROFILE_INFO_URL.to_string()),
            image_url: Some(USER_PROFILE_INFO_IMAGE_URL.to_string()),
        });

        let credential = Some("rt_".to_string() + &generate_random_string());
        responder.send(
            AuthProviderStatus::Ok,
            credential.as_ref().map(|s| &**s),
            user_profile_info.as_mut().map(OutOfLine),
        )
    }

    /// Implementation of the `GetAppAccessToken` method for the `AuthProvider`
    /// fidl interface.
    fn get_app_access_token(
        credential: String,
        client_id: Option<String>,
        responder: AuthProviderGetAppAccessTokenResponder,
    ) -> Result<(), Error> {
        let mut auth_token = Some(fidl_fuchsia_auth::AuthToken {
            token_type: fidl_fuchsia_auth::TokenType::AccessToken,
            token: credential
                + ":client_id_"
                + &client_id.unwrap_or("none".to_string())
                + ":at_"
                + &generate_random_string(),
            expires_in: TOKEN_LIFETIME.as_secs(),
        });

        responder.send(AuthProviderStatus::Ok, auth_token.as_mut().map(OutOfLine))
    }

    /// Implementation of the `GetAppIdToken` method for the `AuthProvider` fidl
    /// interface.
    fn get_app_id_token(
        credential: String,
        responder: AuthProviderGetAppIdTokenResponder,
    ) -> Result<(), Error> {
        let mut auth_token = Some(fidl_fuchsia_auth::AuthToken {
            token_type: fidl_fuchsia_auth::TokenType::IdToken,
            token: credential + ":idt_" + &generate_random_string(),
            expires_in: TOKEN_LIFETIME.as_secs(),
        });

        responder.send(AuthProviderStatus::Ok, auth_token.as_mut().map(OutOfLine))
    }

    /// Implementation of the `GetAppFirebaseToken` method for the `AuthProvider`
    /// fidl interface.
    fn get_app_firebase_token(
        firebase_api_key: String,
        responder: AuthProviderGetAppFirebaseTokenResponder,
    ) -> Result<(), Error> {
        let mut firebase_token = Some(fidl_fuchsia_auth::FirebaseToken {
            id_token: firebase_api_key + ":fbt_" + &generate_random_string(),
            local_id: Some("local_id_".to_string() + &generate_random_string()),
            email: Some(generate_random_string() + FIREBASE_TOKEN_EMAIL_DOMAIN),
            expires_in: TOKEN_LIFETIME.as_secs(),
        });

        responder.send(AuthProviderStatus::Ok, firebase_token.as_mut().map(OutOfLine))
    }

    /// Implementation of the `RevokeAppOrPersistentCredential` method for the
    /// `AuthProvider` fidl interface.
    fn revoke_app_or_persistent_credential(
        responder: AuthProviderRevokeAppOrPersistentCredentialResponder,
    ) -> Result<(), Error> {
        responder.send(AuthProviderStatus::Ok)
    }

    /// Implementation of the `GetPersistentCredentialFromAttestationJwt` method
    /// for the `AuthProvider` fidl interface.
    fn get_persistent_credential_from_attestation_jwt(
        responder: AuthProviderGetPersistentCredentialFromAttestationJwtResponder,
    ) -> Result<(), Error> {
        responder.send(AuthProviderStatus::BadRequest, None, None, None, None)
    }

    /// Implementation of the `GetAppAccessTokenFromAssertionJwt` method for the
    /// `AuthProvider` fidl interface.
    fn get_app_access_token_from_assertion_jwt(
        responder: AuthProviderGetAppAccessTokenFromAssertionJwtResponder,
    ) -> Result<(), Error> {
        responder.send(AuthProviderStatus::BadRequest, None, None, None)
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use fidl_fuchsia_auth::AuthProviderProxy;
    use fuchsia_zircon as zx;

    fn get_auth_provider_connection_proxy() -> AuthProviderProxy {
        let (server_chan, client_chan) = zx::Channel::create().expect("Channel creation failed.");
        let client_chan =
            fasync::Channel::from_channel(client_chan).expect("Channel client creation failed.");
        let server_end = ServerEnd::<AuthProviderMarker>::new(server_chan);
        AuthProvider::spawn(server_end);
        AuthProviderProxy::new(client_chan)
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persistent_credential() -> Result<(), Error> {
        let dev_auth_provider = get_auth_provider_connection_proxy();

        dev_auth_provider
            .get_persistent_credential(None, None)
            .map_ok(move |response| {
                let (status, credential, user_profile_info) = response;
                assert_eq!(status, AuthProviderStatus::Ok);
                assert!(credential.unwrap().contains("rt_"));

                let UserProfileInfo { id, display_name, url, image_url } =
                    *(user_profile_info.unwrap());
                assert!(display_name.unwrap().contains(USER_PROFILE_INFO_DISPLAY_NAME));
                assert!(id.contains(USER_PROFILE_INFO_ID_DOMAIN));
                assert!(url.unwrap().contains(USER_PROFILE_INFO_URL));
                assert!(image_url.unwrap().contains(USER_PROFILE_INFO_IMAGE_URL));
            })
            .await
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_access_token() -> Result<(), Error> {
        let dev_auth_provider = get_auth_provider_connection_proxy();
        let credential = "rt_".to_string() + &generate_random_string();
        let client_id = generate_random_string();
        let mut scopes = vec![].into_iter();

        dev_auth_provider
            .get_app_access_token(&credential, Some(&client_id), &mut scopes)
            .map_ok(move |response| match response {
                (AuthProviderStatus::Ok, Some(access_token)) => {
                    assert_eq!(access_token.token_type, fidl_fuchsia_auth::TokenType::AccessToken);
                    assert_eq!(access_token.expires_in, TOKEN_LIFETIME.as_secs());
                    assert!(access_token.token.contains(&credential));
                    assert!(access_token.token.contains(&client_id));
                    assert!(access_token.token.contains("at_"));
                }
                _ => panic!(
                    "AuthProviderStatus not correct. Or response doesn't contain access_token."
                ),
            })
            .await
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_id_token() -> Result<(), Error> {
        let dev_auth_provider = get_auth_provider_connection_proxy();
        let credential = "rt_".to_string() + &generate_random_string();

        dev_auth_provider
            .get_app_id_token(&credential, None)
            .map_ok(move |response| match response {
                (AuthProviderStatus::Ok, Some(id_token)) => {
                    assert_eq!(id_token.token_type, fidl_fuchsia_auth::TokenType::IdToken);
                    assert_eq!(id_token.expires_in, TOKEN_LIFETIME.as_secs());
                    assert!(id_token.token.contains(&credential));
                    assert!(id_token.token.contains("idt_"));
                }
                _ => {
                    panic!("AuthProviderStatus not correct. Or response doesn't contain id_token.")
                }
            })
            .await
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_firebase_token() -> Result<(), Error> {
        let dev_auth_provider = get_auth_provider_connection_proxy();

        dev_auth_provider
            .get_app_firebase_token("test_id_token", "test_firebase_api_key")
            .map_ok(move |response| match response {
                (AuthProviderStatus::Ok, Some(firebase_token)) => {
                    assert!(firebase_token.id_token.contains("test_firebase_api_key"));
                    assert_eq!(firebase_token.expires_in, TOKEN_LIFETIME.as_secs());
                }
                _ => panic!(
                    "AuthProviderStatus not correct. Or response doesn't contain \
                     firebase_token."
                ),
            })
            .await
    }

    #[fasync::run_until_stalled(test)]
    async fn test_revoke_app_or_persistent_credential() -> Result<(), Error> {
        let dev_auth_provider = get_auth_provider_connection_proxy();
        let credential = "testing_credential";

        dev_auth_provider
            .revoke_app_or_persistent_credential(credential)
            .map_ok(move |response| {
                assert_eq!(response, AuthProviderStatus::Ok);
            })
            .await
    }
}
