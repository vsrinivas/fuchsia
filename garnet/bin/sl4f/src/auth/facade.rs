// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::encoding::OutOfLine;
use fuchsia_zircon as zx;
use glob::glob;

use crate::auth::types::InjectAuthTokenResult;
use fidl_fuchsia_auth::UserProfileInfo;
use fidl_fuchsia_auth_testing::LegacyAuthCredentialInjectorSynchronousProxy;

/// Facade providing access to authentication testing interfaces.
#[derive(Debug)]
pub struct AuthFacade {}

impl AuthFacade {
    pub fn new() -> AuthFacade {
        AuthFacade {}
    }

    /// Discovers a |LegacyAuthCredentialInjector| service published by
    /// GoogleAuthProvider and uses it to inject the provided user profile
    /// info and credential.
    /// |credential| should be a persistent credential provided by Google
    /// identity provider.
    /// If |user_profile_info| is provided it should contain an obfuscated
    /// GAIA id.
    ///
    /// This is a short term solution for enabling end to
    /// end testing.  It should be replaced by automated authentication through
    /// Chrome driver and a long-term injection design - AUTH-161 and AUTH-185.
    pub async fn inject_auth_token(
        &self,
        mut user_profile_info: Option<UserProfileInfo>,
        credential: String,
    ) -> Result<InjectAuthTokenResult, Error> {
        let mut injection_proxy = match self.discover_injection_service()? {
            Some(proxy) => proxy,
            None => return Ok(InjectAuthTokenResult::NotReady),
        };
        injection_proxy.inject_persistent_credential(
            user_profile_info.as_mut().map(|v| OutOfLine(v)),
            &credential,
        )?;
        Ok(InjectAuthTokenResult::Success)
    }

    fn discover_injection_service(
        &self,
    ) -> Result<Option<LegacyAuthCredentialInjectorSynchronousProxy>, Error> {
        let glob_path =
            "/hub/c/google_auth_provider.cmx/*/out/debug/LegacyAuthCredentialInjector";
        let found_path = glob(glob_path)?.filter_map(|entry| entry.ok()).next();
        match found_path {
            Some(path) => {
                let (client, server) = zx::Channel::create()?;
                fdio::service_connect(path.to_string_lossy().as_ref(), server)?;
                Ok(Some(LegacyAuthCredentialInjectorSynchronousProxy::new(client)))
            }
            None => Ok(None),
        }
    }
}
