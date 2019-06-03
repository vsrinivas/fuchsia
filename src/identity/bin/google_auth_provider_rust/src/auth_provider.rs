// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::error::AuthProviderError;
use failure::Error;
use fidl;
use fidl::encoding::OutOfLine;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_auth::{
    AssertionJwtParams, AttestationJwtParams, AttestationSignerMarker, AuthChallenge,
    AuthProviderGetAppAccessTokenFromAssertionJwtResponder, AuthProviderGetAppAccessTokenResponder,
    AuthProviderGetAppFirebaseTokenResponder, AuthProviderGetAppIdTokenResponder,
    AuthProviderGetPersistentCredentialFromAttestationJwtResponder,
    AuthProviderGetPersistentCredentialResponder, AuthProviderRequest, AuthProviderRequestStream,
    AuthProviderRevokeAppOrPersistentCredentialResponder, AuthProviderStatus, AuthToken,
    AuthenticationUiContextMarker, FirebaseToken, UserProfileInfo,
};
use futures::prelude::*;
use log::warn;

type AuthProviderResult<T> = Result<T, AuthProviderError>;

/// An implementation of the `AuthProvider` FIDL protocol that communicates
/// with the Google identity system to perform authentication for and issue
/// tokens for Google accounts.
pub struct GoogleAuthProvider;

impl GoogleAuthProvider {
    /// Create a new GoogleAuthProvider.
    pub fn new() -> Self {
        GoogleAuthProvider
    }

    /// Handle requests passed to the supplied stream.
    pub async fn handle_requests_from_stream(
        &self,
        mut stream: AuthProviderRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = await!(stream.try_next())? {
            await!(self.handle_request(request));
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
            } => responder.send_result(await!(
                self.get_persistent_credential(auth_ui_context, user_profile_id)
            )),
            AuthProviderRequest::GetAppAccessToken { credential, client_id, scopes, responder } => {
                responder
                    .send_result(await!(self.get_app_access_token(credential, client_id, scopes)))
            }
            AuthProviderRequest::GetAppIdToken { credential, audience, responder } => {
                responder.send_result(await!(self.get_app_id_token(credential, audience)))
            }
            AuthProviderRequest::GetAppFirebaseToken { id_token, firebase_api_key, responder } => {
                responder
                    .send_result(await!(self.get_app_firebase_token(id_token, firebase_api_key)))
            }
            AuthProviderRequest::RevokeAppOrPersistentCredential { credential, responder } => {
                responder.send_result(await!(self.revoke_app_or_persistent_credential(credential)))
            }
            AuthProviderRequest::GetPersistentCredentialFromAttestationJwt {
                attestation_signer,
                jwt_params,
                auth_ui_context,
                user_profile_id,
                responder,
            } => responder.send_result(await!(self
                .get_persistent_credential_from_attestation_jwt(
                    attestation_signer,
                    jwt_params,
                    auth_ui_context,
                    user_profile_id
                ))),
            AuthProviderRequest::GetAppAccessTokenFromAssertionJwt {
                attestation_signer,
                jwt_params,
                credential,
                scopes,
                responder,
            } => responder.send_result(await!(self.get_app_access_token_from_assertion_jwt(
                attestation_signer,
                jwt_params,
                credential,
                scopes
            ))),
        }
    }

    /// Implementation of `GetPersistentCredential` method for the
    /// `AuthProvider` interface.
    async fn get_persistent_credential(
        &self,
        _auth_ui_context: Option<ClientEnd<AuthenticationUiContextMarker>>,
        _user_profile_id: Option<String>,
    ) -> AuthProviderResult<(String, UserProfileInfo)> {
        // TODO(satsukiu): implement
        Err(AuthProviderError::new(AuthProviderStatus::InternalError))
    }

    /// Implementation of `GetAppAccessToken` method for the `AuthProvider`
    /// interface.
    async fn get_app_access_token(
        &self,
        _credential: String,
        _client_id: Option<String>,
        _scopes: Vec<String>,
    ) -> AuthProviderResult<AuthToken> {
        // TODO(satsukiu): implement
        Err(AuthProviderError::new(AuthProviderStatus::InternalError))
    }

    /// Implementation of `GetAppIdToken` method for the `AuthProvider`
    /// interface.
    async fn get_app_id_token(
        &self,
        _credential: String,
        _audience: Option<String>,
    ) -> AuthProviderResult<AuthToken> {
        // TODO(satsukiu): implement
        Err(AuthProviderError::new(AuthProviderStatus::InternalError))
    }

    /// Implementation of `GetAppFirebaseToken` method for the `AuthProvider`
    /// interface.
    async fn get_app_firebase_token(
        &self,
        _id_token: String,
        _firebase_api_key: String,
    ) -> AuthProviderResult<FirebaseToken> {
        // TODO(satsukiu): implement
        Err(AuthProviderError::new(AuthProviderStatus::InternalError))
    }

    /// Implementation of `RevokeAppOrPersistentCredential` method for the
    /// `AuthProvider` interface.
    async fn revoke_app_or_persistent_credential(
        &self,
        _credential: String,
    ) -> AuthProviderResult<()> {
        // TODO(satsukiu): implement
        Err(AuthProviderError::new(AuthProviderStatus::InternalError))
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
            Some((credential, mut user_profile_info)) => self.send(
                status,
                Some(credential.as_str()),
                Some(OutOfLine(&mut user_profile_info)),
            ),
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
        self.send(status, data.as_mut().map(OutOfLine))
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
        self.send(status, data.as_mut().map(OutOfLine))
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
        self.send(status, data.as_mut().map(OutOfLine))
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
                    Some(OutOfLine(&mut auth_token)),
                    Some(OutOfLine(&mut auth_challenge)),
                    Some(OutOfLine(&mut user_profile_info)),
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
                Some(OutOfLine(&mut auth_token)),
                Some(OutOfLine(&mut auth_challenge)),
            ),
        }
    }
}

#[cfg(test)]
mod tests {

    use super::*;
    use fidl::endpoints::ServerEnd;
    use fidl_fuchsia_auth::{AuthProviderMarker, AuthProviderProxy};
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;

    fn get_auth_provider_proxy() -> AuthProviderProxy {
        let (server_chan, client_chan) = zx::Channel::create().expect("Failed to create channel");

        let server_end = ServerEnd::<AuthProviderMarker>::new(server_chan);
        let request_stream = server_end.into_stream().expect("Failed to create request stream");
        let auth_provider = GoogleAuthProvider::new();
        fasync::spawn(async move {
            await!(auth_provider.handle_requests_from_stream(request_stream))
                .expect("Error handling AuthProvider channel");
        });

        let client_chan =
            fasync::Channel::from_channel(client_chan).expect("Channel client creation failed.");
        AuthProviderProxy::new(client_chan)
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_persistent_credential() -> Result<(), Error> {
        let auth_provider = get_auth_provider_proxy();
        let result = await!(auth_provider.get_persistent_credential(None, None))?;
        assert_eq!(result.0, AuthProviderStatus::InternalError);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_access_token() -> Result<(), Error> {
        let auth_provider = get_auth_provider_proxy();
        let result = await!(auth_provider.get_app_access_token(
            "credential",
            None,
            &mut vec![].into_iter()
        ))?;
        assert_eq!(result.0, AuthProviderStatus::InternalError);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_id_token() -> Result<(), Error> {
        let auth_provider = get_auth_provider_proxy();
        let result = await!(auth_provider.get_app_id_token("credential", None))?;
        assert_eq!(result.0, AuthProviderStatus::InternalError);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_get_app_firebase_token() -> Result<(), Error> {
        let auth_provider = get_auth_provider_proxy();
        let result = await!(auth_provider.get_app_firebase_token("id_token", "api_key"))?;
        assert_eq!(result.0, AuthProviderStatus::InternalError);
        Ok(())
    }

    #[fasync::run_until_stalled(test)]
    async fn test_revoke_app_or_persistent_credential() -> Result<(), Error> {
        let auth_provider = get_auth_provider_proxy();
        let result = await!(auth_provider.revoke_app_or_persistent_credential("credential"))?;
        assert_eq!(result, AuthProviderStatus::InternalError);
        Ok(())
    }
}
