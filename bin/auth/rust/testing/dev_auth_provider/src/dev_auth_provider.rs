// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_auth;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;

use fidl::encoding2::OutOfLine;
use fidl::endpoints2::ServerEnd;
use fidl::Error;
use fidl_fuchsia_auth::{AuthProviderGetAppAccessTokenResponder,
                        AuthProviderGetAppFirebaseTokenResponder,
                        AuthProviderGetAppIdTokenResponder,
                        AuthProviderGetPersistentCredentialResponder, AuthProviderMarker,
                        AuthProviderRequest, AuthProviderRevokeAppOrPersistentCredentialResponder,
                        AuthProviderStatus, UserProfileInfo};
use futures::future::FutureResult;
use futures::prelude::*;
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
    thread_rng()
        .gen_ascii_chars()
        .take(RANDOM_STRING_LENGTH)
        .collect()
}

/// The AuthProvider struct is holding implementation of the `AuthProvider` fidl interface.
/// This implementation is serving as a testing endpoint for token manager.
pub struct AuthProvider;

impl AuthProvider {
    /// Spawn a new task of handling request from the `AuthProviderRequestStream` by calling the
    /// `handle_request` method. Create a warning on error.
    pub fn spawn(server_end: ServerEnd<AuthProviderMarker>) {
        match server_end.into_stream() {
            Err(err) => {
                warn!("Error creating AuthProvider request stream {:?}", err);
            }
            Ok(request_stream) => async::spawn(
                request_stream
                    .for_each(Self::handle_request)
                    .map(|_| ())
                    .recover(|e| warn!("Error running AuthProvider{:?}", e)),
            ),
        };
    }

    /// Handle single `AuthProviderRequest` by calling the corresponding method according to the
    /// actual variant of the `AuthProviderRequest` enum.
    fn handle_request(req: AuthProviderRequest) -> FutureResult<(), Error> {
        match req {
            AuthProviderRequest::GetPersistentCredential { responder, .. } => {
                Self::get_persistent_credential(responder)
            }

            AuthProviderRequest::GetAppAccessToken {
                credential,
                client_id,
                responder,
                ..
            } => Self::get_app_access_token(credential, client_id, responder),

            AuthProviderRequest::GetAppIdToken {
                credential,
                responder,
                ..
            } => Self::get_app_id_token(credential, responder),

            AuthProviderRequest::GetAppFirebaseToken {
                firebase_api_key,
                responder,
                ..
            } => Self::get_app_firebase_token(firebase_api_key, responder),

            AuthProviderRequest::RevokeAppOrPersistentCredential { responder, .. } => {
                Self::revoke_app_or_persistent_credential(responder)
            }
        }
    }

    /// Implementation of the `GetPersistenCredential` method for the `AuthProvider` fidl interface.
    /// The field auth_ui_context is removed here as we will never use it in the dev auth provider.
    fn get_persistent_credential(
        responder: AuthProviderGetPersistentCredentialResponder,
    ) -> FutureResult<(), Error> {
        let mut user_profile_info = Some(UserProfileInfo {
            id: generate_random_string() + USER_PROFILE_INFO_ID_DOMAIN,
            display_name: Some(USER_PROFILE_INFO_DISPLAY_NAME.to_string()),
            url: Some(USER_PROFILE_INFO_URL.to_string()),
            image_url: Some(USER_PROFILE_INFO_IMAGE_URL.to_string()),
        });

        let credential = Some("rt_".to_string() + &generate_random_string());
        responder
            .send(
                AuthProviderStatus::Ok,
                credential.as_ref().map(|s| &**s),
                user_profile_info.as_mut().map(OutOfLine),
            )
            .into_future()
    }

    /// Implementation of the `GetAppAccessToken` method for the `AuthProvider` fidl interface.
    fn get_app_access_token(
        credential: String, client_id: Option<String>,
        responder: AuthProviderGetAppAccessTokenResponder,
    ) -> FutureResult<(), Error> {
        let mut auth_token = Some(fidl_fuchsia_auth::AuthToken {
            token_type: fidl_fuchsia_auth::TokenType::AccessToken,
            token: credential
                + ":client_id_"
                + &client_id.unwrap_or("none".to_string())
                + ":at_"
                + &generate_random_string(),
            expires_in: TOKEN_LIFETIME.as_secs(),
        });

        responder
            .send(AuthProviderStatus::Ok, auth_token.as_mut().map(OutOfLine))
            .into_future()
    }

    /// Implementation of the `GetAppIdToken` method for the `AuthProvider` fidl interface.
    fn get_app_id_token(
        credential: String, responder: AuthProviderGetAppIdTokenResponder,
    ) -> FutureResult<(), Error> {
        let mut auth_token = Some(fidl_fuchsia_auth::AuthToken {
            token_type: fidl_fuchsia_auth::TokenType::IdToken,
            token: credential + ":idt_" + &generate_random_string(),
            expires_in: TOKEN_LIFETIME.as_secs(),
        });

        responder
            .send(AuthProviderStatus::Ok, auth_token.as_mut().map(OutOfLine))
            .into_future()
    }

    /// Implementation of the `GetAppFirebaseToken` method for the `AuthProvider` fidl interface.
    fn get_app_firebase_token(
        firebase_api_key: String, responder: AuthProviderGetAppFirebaseTokenResponder,
    ) -> FutureResult<(), Error> {
        let mut firebase_token = Some(fidl_fuchsia_auth::FirebaseToken {
            id_token: firebase_api_key + ":fbt_" + &generate_random_string(),
            local_id: Some("local_id_".to_string() + &generate_random_string()),
            email: Some(generate_random_string() + FIREBASE_TOKEN_EMAIL_DOMAIN),
            expires_in: TOKEN_LIFETIME.as_secs(),
        });

        responder
            .send(
                AuthProviderStatus::Ok,
                firebase_token.as_mut().map(OutOfLine),
            )
            .into_future()
    }

    /// Implementation of the `RevokeAppOrPersistentCredential` method for the `AuthProvider` fidl
    /// interface.
    fn revoke_app_or_persistent_credential(
        responder: AuthProviderRevokeAppOrPersistentCredentialResponder,
    ) -> FutureResult<(), Error> {
        responder.send(AuthProviderStatus::Ok).into_future()
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use fidl_fuchsia_auth::AuthProviderProxy;

    fn set_up() -> Result<(async::Executor, AuthProviderProxy), failure::Error> {
        let exec = async::Executor::new()?;
        let (server_chan, client_chan) = zx::Channel::create()?;
        let client_chan = async::Channel::from_channel(client_chan)?;
        let server_end = ServerEnd::<AuthProviderMarker>::new(server_chan);
        AuthProvider::spawn(server_end);
        let proxy = AuthProviderProxy::new(client_chan);
        Ok((exec, proxy))
    }

    fn async_test<F, Fut>(f: F)
    where
        F: FnOnce(AuthProviderProxy) -> Fut,
        Fut: Future<Item = (), Error = Error>,
    {
        let (mut exec, dev_auth_provider) = set_up().expect("Test set up should not have failed.");
        let test_fut = f(dev_auth_provider);
        exec.run_singlethreaded(test_fut)
            .expect("executor run failed.")
    }

    #[test]
    fn test_get_persistent_credential() {
        async_test(|dev_auth_provider| {
            dev_auth_provider
                .get_persistent_credential(None)
                .and_then(move |response| {
                    let (status, credential, user_profile_info) = response;
                    assert_eq!(status, AuthProviderStatus::Ok);
                    assert!(credential.unwrap().contains("rt_"));

                    let UserProfileInfo {
                        id,
                        display_name,
                        url,
                        image_url,
                    } = *(user_profile_info.unwrap());
                    assert!(
                        display_name
                            .unwrap()
                            .contains(USER_PROFILE_INFO_DISPLAY_NAME)
                    );
                    assert!(id.contains(USER_PROFILE_INFO_ID_DOMAIN));
                    assert!(url.unwrap().contains(USER_PROFILE_INFO_URL));
                    assert!(image_url.unwrap().contains(USER_PROFILE_INFO_IMAGE_URL));
                    Ok(())
                })
        });
    }

    #[test]
    fn test_get_app_access_token() {
        async_test(|dev_auth_provider| {
            let credential = "rt_".to_string() + &generate_random_string();
            let client_id = generate_random_string();
            let mut scopes = vec![].into_iter();
            dev_auth_provider
                .get_app_access_token(&credential, Some(&client_id), &mut scopes)
                .and_then(move |response| match response {
                    (AuthProviderStatus::Ok, Some(access_token)) => {
                        assert_eq!(
                            access_token.token_type,
                            fidl_fuchsia_auth::TokenType::AccessToken
                        );
                        assert_eq!(access_token.expires_in, TOKEN_LIFETIME.as_secs());
                        assert!(access_token.token.contains(&credential));
                        assert!(access_token.token.contains(&client_id));
                        assert!(access_token.token.contains("at_"));
                        Ok(())
                    }
                    _ => panic!(
                        "AuthProviderStatus not correct. Or response doesn't contain access_token."
                    ),
                })
        });
    }

    #[test]
    fn test_get_app_id_token() {
        async_test(|dev_auth_provider| {
            let credential = "rt_".to_string() + &generate_random_string();
            dev_auth_provider
                .get_app_id_token(&credential, None)
                .and_then(move |response| match response {
                    (AuthProviderStatus::Ok, Some(id_token)) => {
                        assert_eq!(id_token.token_type, fidl_fuchsia_auth::TokenType::IdToken);
                        assert_eq!(id_token.expires_in, TOKEN_LIFETIME.as_secs());
                        assert!(id_token.token.contains(&credential));
                        assert!(id_token.token.contains("idt_"));
                        Ok(())
                    }
                    _ => panic!(
                        "AuthProviderStatus not correct. Or response doesn't contain id_token."
                    ),
                })
        });
    }

    #[test]
    fn test_get_app_firebase_token() {
        async_test(|dev_auth_provider| {
            dev_auth_provider
                .get_app_firebase_token("test_id_token", "test_firebase_api_key")
                .and_then(move |response| match response {
                    (AuthProviderStatus::Ok, Some(firebase_token)) => {
                        assert!(firebase_token.id_token.contains("test_firebase_api_key"));
                        assert_eq!(firebase_token.expires_in, TOKEN_LIFETIME.as_secs());
                        Ok(())
                    }
                    _ => panic!(
                        "AuthProviderStatus not correct. Or response doesn't contain \
                         firebase_token."
                    ),
                })
        });
    }

    #[test]
    fn test_revoke_app_or_persistent_credential() {
        async_test(|dev_auth_provider| {
            let credential = "testing_credential";
            dev_auth_provider
                .revoke_app_or_persistent_credential(credential)
                .and_then(move |response| {
                    assert_eq!(response, AuthProviderStatus::Ok);
                    Ok(())
                })
        });
    }

}
