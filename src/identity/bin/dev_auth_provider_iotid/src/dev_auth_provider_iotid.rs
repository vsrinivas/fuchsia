// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::encoding::OutOfLine;
use fidl::endpoints::ServerEnd;
use fidl::Error;
use fidl_fuchsia_auth::{AuthProviderGetAppAccessTokenFromAssertionJwtResponder,
                        AuthProviderGetAppAccessTokenResponder,
                        AuthProviderGetAppFirebaseTokenResponder,
                        AuthProviderGetAppIdTokenResponder,
                        AuthProviderGetPersistentCredentialFromAttestationJwtResponder,
                        AuthProviderGetPersistentCredentialResponder, AuthProviderMarker,
                        AuthProviderRequest, AuthProviderRevokeAppOrPersistentCredentialResponder,
                        AuthProviderStatus, UserProfileInfo};
use fuchsia_async as fasync;
use futures::future;
use futures::prelude::*;
use log::warn;
use rand::{thread_rng, Rng};
use rand::distributions::Alphanumeric;
use std::time::Duration;

const TOKEN_LIFETIME: Duration = Duration::from_secs(3600); // one hour lifetime
const CHALLENGE_LIFETIME: Duration = Duration::from_secs(24 * 3600); // 24 hour lifetime
const USER_PROFILE_INFO_ID_DOMAIN: &str = "@example.com";
const USER_PROFILE_INFO_DISPLAY_NAME: &str = "test_user_display_name";
const USER_PROFILE_INFO_URL: &str = "http://test_user/profile/url";
const USER_PROFILE_INFO_IMAGE_URL: &str = "http://test_user/profile/image/url";
const RANDOM_STRING_LENGTH: usize = 10;

/// Generate random alphanumeric string of fixed length RANDOM_STRING_LENGTH
/// for creating unique tokens or id.
fn generate_random_string() -> String {
    thread_rng()
        .sample_iter(&Alphanumeric)
        .take(RANDOM_STRING_LENGTH)
        .collect()
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

            AuthProviderRequest::GetPersistentCredentialFromAttestationJwt {
                user_profile_id,
                responder,
                ..
            } => Self::get_persistent_credential_from_attestation_jwt(user_profile_id, responder),

            AuthProviderRequest::GetAppAccessTokenFromAssertionJwt {
                credential,
                responder,
                ..
            } => Self::get_app_access_token_from_assertion_jwt(credential, responder),
        }
    }

    /// Implementation of the `GetPersistenCredential` method for the `AuthProvider` fidl
    /// interface. The field auth_ui_context is removed here as we will never use it in the dev
    /// auth provider.
    fn get_persistent_credential(
        responder: AuthProviderGetPersistentCredentialResponder,
    ) -> Result<(), Error> {
        responder.send(AuthProviderStatus::BadRequest, None, None)
    }

    /// Implementation of the `GetAppAccessToken` method for the `AuthProvider` fidl interface.
    fn get_app_access_token(
        _: String, _: Option<String>, responder: AuthProviderGetAppAccessTokenResponder,
    ) -> Result<(), Error> {
        responder.send(AuthProviderStatus::BadRequest, None)
    }

    /// Implementation of the `GetAppIdToken` method for the `AuthProvider` fidl interface.
    fn get_app_id_token(
        _: String, responder: AuthProviderGetAppIdTokenResponder,
    ) -> Result<(), Error> {
        responder.send(AuthProviderStatus::BadRequest, None)
    }

    /// Implementation of the `GetAppFirebaseToken` method for the `AuthProvider` fidl interface.
    fn get_app_firebase_token(
        _: String, responder: AuthProviderGetAppFirebaseTokenResponder,
    ) -> Result<(), Error> {
        responder.send(AuthProviderStatus::BadRequest, None)
    }

    /// Implementation of the `RevokeAppOrPersistentCredential` method for the `AuthProvider` fidl
    /// interface.
    fn revoke_app_or_persistent_credential(
        responder: AuthProviderRevokeAppOrPersistentCredentialResponder,
    ) -> Result<(), Error> {
        responder.send(AuthProviderStatus::Ok)
    }

    /// Implementation of the `GetPersistentCredentialFromAttestationJwt` method for the
    /// `AuthProvider` fidl interface.
    fn get_persistent_credential_from_attestation_jwt(
        user_profile_id: Option<String>,
        responder: AuthProviderGetPersistentCredentialFromAttestationJwtResponder,
    ) -> Result<(), Error> {
        let credential = Some("rt_".to_string() + &generate_random_string());
        let mut auth_token = Some(fidl_fuchsia_auth::AuthToken {
            token_type: fidl_fuchsia_auth::TokenType::AccessToken,
            token: "at_".to_string() + &generate_random_string(),
            expires_in: TOKEN_LIFETIME.as_secs(),
        });
        let mut auth_challenge = Some(fidl_fuchsia_auth::AuthChallenge {
            challenge: "ch_".to_string() + &generate_random_string(),
            expires_in: CHALLENGE_LIFETIME.as_secs(),
        });
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

        responder.send(
            AuthProviderStatus::Ok,
            credential.as_ref().map(|s| &**s),
            auth_token.as_mut().map(OutOfLine),
            auth_challenge.as_mut().map(OutOfLine),
            user_profile_info.as_mut().map(OutOfLine),
        )
    }

    /// Implementation of the `GetAppAccessTokenFromAssertionJwt` method for the
    /// `AuthProvider` fidl interface.
    fn get_app_access_token_from_assertion_jwt(
        credential: String, responder: AuthProviderGetAppAccessTokenFromAssertionJwtResponder,
    ) -> Result<(), Error> {
        let mut auth_token = Some(fidl_fuchsia_auth::AuthToken {
            token_type: fidl_fuchsia_auth::TokenType::AccessToken,
            token: credential.clone() + ":at_" + &generate_random_string(),
            expires_in: TOKEN_LIFETIME.as_secs(),
        });
        let mut auth_challenge = Some(fidl_fuchsia_auth::AuthChallenge {
            challenge: credential.clone() + ":ch_" + &generate_random_string(),
            expires_in: CHALLENGE_LIFETIME.as_secs(),
        });
        let updated_credential = Some("up_cr_".to_string() + &generate_random_string());

        responder.send(
            AuthProviderStatus::Ok,
            updated_credential.as_ref().map(|s| &**s),
            auth_token.as_mut().map(OutOfLine),
            auth_challenge.as_mut().map(OutOfLine),
        )
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use fidl::endpoints::ClientEnd;
    use fidl_fuchsia_auth::AssertionJwtParams;
    use fidl_fuchsia_auth::AttestationJwtParams;
    use fidl_fuchsia_auth::AttestationSignerMarker;
    use fidl_fuchsia_auth::AuthChallenge;
    use fidl_fuchsia_auth::AuthProviderProxy;
    use fidl_fuchsia_auth::AuthToken;
    use fidl_fuchsia_auth::CredentialEcKey;
    use fuchsia_zircon as zx;

    fn get_auth_provider_proxy() -> AuthProviderProxy {
        let (server_chan, client_chan) = zx::Channel::create().expect("Failed to create channel");
        let client_chan = fasync::Channel::from_channel(client_chan)
            .expect("Failed to create channel client");
        let server_end = ServerEnd::<AuthProviderMarker>::new(server_chan);
        AuthProvider::spawn(server_end);
        AuthProviderProxy::new(client_chan)
    }

    // Returns the client_end of the 'AttestationSigner' component.
    fn get_attestation_signer() -> ClientEnd<AttestationSignerMarker> {
        // TODO(ukode): Add a functional attestation signer component here.
        let (_server_chan, client_chan) =
            zx::Channel::create().expect("Failed to create zx channels");
        return ClientEnd::<AttestationSignerMarker>::new(client_chan);
    }

    // Generates and returns an elliptic curve credential key.
    fn get_credential_key() -> CredentialEcKey {
        return CredentialEcKey {
            curve: String::from("P-256"),
            key_x_val: String::from("x-coordinates"),
            key_y_val: String::from("y-coordinates"),
            fingerprint_sha_256: String::from("sha_256_hash_of_public_key"),
        };
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persistent_credential() -> Result<(), Error> {
        let dev_auth_provider_iotid = get_auth_provider_proxy();
        await!(
            dev_auth_provider_iotid
                .get_persistent_credential(None, None)
                .map_ok(move |response| {
                    let (status, _, _) = response;
                    assert_eq!(status, AuthProviderStatus::BadRequest);
                })
        )
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_access_token() -> Result<(), Error> {
        let dev_auth_provider_iotid = get_auth_provider_proxy();
        let credential = "rt_".to_string() + &generate_random_string();
        let client_id = generate_random_string();
        let mut scopes = vec![].into_iter();
        await!(
            dev_auth_provider_iotid
                .get_app_access_token(&credential, Some(&client_id), &mut scopes)
                .map_ok(move |response| {
                    let (status, _) = response;
                    assert_eq!(status, AuthProviderStatus::BadRequest);
                })
        )
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_id_token() -> Result<(), Error> {
        let dev_auth_provider_iotid = get_auth_provider_proxy();
        let credential = "rt_".to_string() + &generate_random_string();
        await!(
            dev_auth_provider_iotid
                .get_app_id_token(&credential, None)
                .map_ok(move |response| {
                    let (status, _) = response;
                    assert_eq!(status, AuthProviderStatus::BadRequest);
                })
        )
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_firebase_token() -> Result<(), Error> {
        let dev_auth_provider_iotid = get_auth_provider_proxy();
        await!(
            dev_auth_provider_iotid
                .get_app_firebase_token("test_id_token", "test_firebase_api_key")
                .map_ok(move |response| {
                    let (status, _) = response;
                    assert_eq!(status, AuthProviderStatus::BadRequest);
                })
        )
    }

    #[fasync::run_until_stalled(test)]
    async fn test_revoke_app_or_persistent_credential() -> Result<(), Error> {
        let dev_auth_provider_iotid = get_auth_provider_proxy();
        let credential = "testing_credential";
        await!(
            dev_auth_provider_iotid
                .revoke_app_or_persistent_credential(credential)
                .map_ok(move |response| {
                    assert_eq!(response, AuthProviderStatus::Ok);
                })
        )
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persistent_credential_from_attestation_jwt() -> Result<(), Error> {
        let dev_auth_provider_iotid = get_auth_provider_proxy();
        let attestation_signer = get_attestation_signer();
        let mut jwt_params = AttestationJwtParams {
            credential_eckey: get_credential_key(),
            certificate_chain: vec![],
            auth_code: "test_auth_code".to_string(),
        };
        await!(
            dev_auth_provider_iotid
                .get_persistent_credential_from_attestation_jwt(
                    attestation_signer,
                    &mut jwt_params,
                    None,
                    None,
                )
                .map_ok(move |response| {
                    let (status, credential, access_token, auth_challenge, user_profile_info) =
                        response;
                    assert_eq!(status, AuthProviderStatus::Ok);
                    assert!(credential.unwrap().contains("rt_"));

                    let AuthToken {
                        token_type,
                        token,
                        expires_in,
                    } = *(access_token.unwrap());
                    assert_eq!(token_type, fidl_fuchsia_auth::TokenType::AccessToken);
                    assert_eq!(expires_in, TOKEN_LIFETIME.as_secs());
                    assert!(token.contains("at_"));

                    let AuthChallenge {
                        challenge,
                        expires_in,
                    } = *(auth_challenge.unwrap());
                    assert_eq!(expires_in, CHALLENGE_LIFETIME.as_secs());
                    assert!(challenge.contains("ch_"));

                    let UserProfileInfo {
                        id, display_name, ..
                    } = *(user_profile_info.unwrap());
                    assert!(
                        display_name
                            .unwrap()
                            .contains(USER_PROFILE_INFO_DISPLAY_NAME)
                    );
                    assert!(id.contains(USER_PROFILE_INFO_ID_DOMAIN));
                })
        )
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_access_token_from_assertion_jwt() -> Result<(), Error> {
        let dev_auth_provider_iotid = get_auth_provider_proxy();
        let attestation_signer = get_attestation_signer();
        let mut jwt_params = AssertionJwtParams {
            credential_eckey: get_credential_key(),
            challenge: Some("test_challenge".to_string()),
        };

        let credential = "rt_".to_string() + &generate_random_string();
        let mut scopes = vec![].into_iter();
        await!(
            dev_auth_provider_iotid
                .get_app_access_token_from_assertion_jwt(
                    attestation_signer,
                    &mut jwt_params,
                    &credential,
                    &mut scopes,
                )
                .map_ok(move |response| {
                    let (status, updated_credential, access_token, auth_challenge) = response;
                    assert_eq!(status, AuthProviderStatus::Ok);
                    assert!(updated_credential.unwrap().contains("up_cr_"));

                    let AuthToken {
                        token_type,
                        token,
                        expires_in,
                    } = *(access_token.unwrap());
                    assert_eq!(token_type, fidl_fuchsia_auth::TokenType::AccessToken);
                    assert_eq!(expires_in, TOKEN_LIFETIME.as_secs());
                    assert!(token.contains(&credential));
                    assert!(token.contains("at_"));

                    let AuthChallenge {
                        challenge,
                        expires_in,
                    } = *(auth_challenge.unwrap());
                    assert_eq!(expires_in, CHALLENGE_LIFETIME.as_secs());
                    assert!(challenge.contains("ch_"));
                })
        )
    }
}
