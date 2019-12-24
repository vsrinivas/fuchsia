// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fuchsia_zircon::{ClockId, Duration, Time};
use std::ops::Deref;
use token_cache::{CacheKey, CacheToken, KeyFor};

/// Representation of a single OAuth token including its expiry time.
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct OAuthToken {
    expiry_time: Time,
    token: String,
}

impl CacheToken for OAuthToken {
    fn expiry_time(&self) -> &Time {
        &self.expiry_time
    }
}

impl Deref for OAuthToken {
    type Target = str;

    fn deref(&self) -> &str {
        &*self.token
    }
}

impl From<fidl_fuchsia_auth::AuthToken> for OAuthToken {
    fn from(auth_token: fidl_fuchsia_auth::AuthToken) -> OAuthToken {
        OAuthToken {
            expiry_time: get_current_time() + Duration::from_seconds(auth_token.expires_in as i64),
            token: auth_token.token,
        }
    }
}

/// Key for storing OAuth access tokens in the token cache.
#[derive(Debug, PartialEq, Eq)]
pub struct AccessTokenKey {
    auth_provider_type: String,
    user_profile_id: String,
    scopes: String,
}

impl CacheKey for AccessTokenKey {
    fn auth_provider_type(&self) -> &str {
        &self.auth_provider_type
    }

    fn user_profile_id(&self) -> &str {
        &self.user_profile_id
    }

    fn subkey(&self) -> &str {
        &self.scopes
    }
}

impl KeyFor for AccessTokenKey {
    type TokenType = OAuthToken;
}

impl AccessTokenKey {
    /// Create a new access token key.
    pub fn new<T: Deref<Target = str>>(
        auth_provider_type: String,
        user_profile_id: String,
        scopes: &[T],
    ) -> Result<AccessTokenKey, Error> {
        validate_provider_and_id(&auth_provider_type, &user_profile_id)?;
        Ok(AccessTokenKey {
            auth_provider_type: auth_provider_type,
            user_profile_id: user_profile_id,
            scopes: Self::combine_scopes(scopes),
        })
    }

    fn combine_scopes<T: Deref<Target = str>>(scopes: &[T]) -> String {
        // Use the scope strings concatenated with a newline as the key. Note that this
        // is order dependent; a client that requested the same scopes with two
        // different orders would create two cache entries. We argue that the
        // harm of this is limited compared to the cost of sorting scopes to
        // create a canonical ordering on every access. Most clients are likely
        // to use a consistent order anyway and we request this behaviour in the
        // interface. TODO(satsukiu): Consider a zero-copy solution for the
        // simple case of a single scope.
        match scopes.len() {
            0 => String::from(""),
            1 => scopes.first().unwrap().to_string(),
            _ => String::from(scopes.iter().fold(String::new(), |acc, el| {
                let sep = if acc.is_empty() { "" } else { "\n" };
                acc + sep + el
            })),
        }
    }
}

/// Key for storing OpenID tokens in the token cache.
#[derive(Debug, PartialEq, Eq)]
pub struct IdTokenKey {
    auth_provider_type: String,
    user_profile_id: String,
    audience: String,
}

impl CacheKey for IdTokenKey {
    fn auth_provider_type(&self) -> &str {
        &self.auth_provider_type
    }

    fn user_profile_id(&self) -> &str {
        &self.user_profile_id
    }

    fn subkey(&self) -> &str {
        &self.audience
    }
}

impl KeyFor for IdTokenKey {
    type TokenType = OAuthToken;
}

impl IdTokenKey {
    /// Create a new ID token key.
    pub fn new(
        auth_provider_type: String,
        user_profile_id: String,
        audience: String,
    ) -> Result<IdTokenKey, Error> {
        validate_provider_and_id(&auth_provider_type, &user_profile_id)?;
        Ok(IdTokenKey {
            auth_provider_type: auth_provider_type,
            user_profile_id: user_profile_id,
            audience: audience,
        })
    }
}

/// Validates that the given auth_provider_type and user_profile_id are
/// nonempty.
fn validate_provider_and_id(auth_provider_type: &str, user_profile_id: &str) -> Result<(), Error> {
    if auth_provider_type.is_empty() {
        Err(format_err!("auth_provider_type cannot be empty"))
    } else if user_profile_id.is_empty() {
        Err(format_err!("user_profile_id cannot be empty"))
    } else {
        Ok(())
    }
}

/// Obtains the current time in UTC.
fn get_current_time() -> Time {
    Time::get(ClockId::UTC)
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_auth::TokenType;

    const LONG_EXPIRY: Duration = Duration::from_seconds(3000);
    const TEST_ACCESS_TOKEN: &str = "access token";
    const TEST_AUTH_PROVIDER_TYPE: &str = "test-provider";
    const TEST_USER_PROFILE_ID: &str = "test-user-123";
    const TEST_SCOPE_1: &str = "scope-1";
    const TEST_SCOPE_2: &str = "scope-2";
    const TEST_AUDIENCE: &str = "audience";

    #[test]
    fn test_oauth_from_fidl() {
        let fidl_type = fidl_fuchsia_auth::AuthToken {
            token_type: TokenType::AccessToken,
            expires_in: LONG_EXPIRY.into_seconds() as u64,
            token: TEST_ACCESS_TOKEN.to_string(),
        };

        let time_before_conversion = get_current_time();
        let native_type = OAuthToken::from(fidl_type);
        let time_after_conversion = get_current_time();

        assert_eq!(&native_type.token, TEST_ACCESS_TOKEN);
        assert!(native_type.expiry_time >= time_before_conversion + LONG_EXPIRY);
        assert!(native_type.expiry_time <= time_after_conversion + LONG_EXPIRY);

        // Also verify our implementation of the Deref trait
        assert_eq!(&*native_type, TEST_ACCESS_TOKEN);
    }

    #[test]
    fn test_create_access_token_key() {
        let scopes = vec![TEST_SCOPE_1, TEST_SCOPE_2];
        let auth_token_key = AccessTokenKey::new(
            TEST_AUTH_PROVIDER_TYPE.to_string(),
            TEST_USER_PROFILE_ID.to_string(),
            &scopes,
        )
        .unwrap();
        assert_eq!(
            AccessTokenKey {
                auth_provider_type: TEST_AUTH_PROVIDER_TYPE.to_string(),
                user_profile_id: TEST_USER_PROFILE_ID.to_string(),
                scopes: TEST_SCOPE_1.to_string() + "\n" + TEST_SCOPE_2,
            },
            auth_token_key
        );

        // Verify single scope creation
        let single_scope = vec![TEST_SCOPE_1];
        let auth_token_key = AccessTokenKey::new(
            TEST_AUTH_PROVIDER_TYPE.to_string(),
            TEST_USER_PROFILE_ID.to_string(),
            &single_scope,
        )
        .unwrap();
        assert_eq!(
            AccessTokenKey {
                auth_provider_type: TEST_AUTH_PROVIDER_TYPE.to_string(),
                user_profile_id: TEST_USER_PROFILE_ID.to_string(),
                scopes: TEST_SCOPE_1.to_string(),
            },
            auth_token_key
        );

        // Verify no scopes creation
        let no_scopes: Vec<&str> = vec![];
        let auth_token_key = AccessTokenKey::new(
            TEST_AUTH_PROVIDER_TYPE.to_string(),
            TEST_USER_PROFILE_ID.to_string(),
            &no_scopes,
        )
        .unwrap();
        assert_eq!(
            AccessTokenKey {
                auth_provider_type: TEST_AUTH_PROVIDER_TYPE.to_string(),
                user_profile_id: TEST_USER_PROFILE_ID.to_string(),
                scopes: "".to_string(),
            },
            auth_token_key
        );

        // Verify empty auth provider and user profile id cases fail.
        assert!(AccessTokenKey::new("".to_string(), TEST_USER_PROFILE_ID.to_string(), &no_scopes)
            .is_err());
        assert!(AccessTokenKey::new(
            TEST_AUTH_PROVIDER_TYPE.to_string(),
            "".to_string(),
            &no_scopes
        )
        .is_err());
    }

    #[test]
    fn test_create_id_token_key() {
        assert_eq!(
            IdTokenKey::new(
                TEST_AUTH_PROVIDER_TYPE.to_string(),
                TEST_USER_PROFILE_ID.to_string(),
                TEST_AUDIENCE.to_string()
            )
            .unwrap(),
            IdTokenKey {
                auth_provider_type: TEST_AUTH_PROVIDER_TYPE.to_string(),
                user_profile_id: TEST_USER_PROFILE_ID.to_string(),
                audience: TEST_AUDIENCE.to_string()
            }
        );

        // Verify empty auth provider and user profile id cases fail.
        assert!(IdTokenKey::new(
            "".to_string(),
            TEST_USER_PROFILE_ID.to_string(),
            TEST_AUDIENCE.to_string()
        )
        .is_err());
        assert!(IdTokenKey::new(
            TEST_AUTH_PROVIDER_TYPE.to_string(),
            "".to_string(),
            TEST_AUDIENCE.to_string()
        )
        .is_err());
    }
}
