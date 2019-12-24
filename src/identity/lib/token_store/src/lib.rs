// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
mod macros;
pub mod file;
pub mod mem;
mod serializer;
/// FIXME remove when https://github.com/rust-lang/rust/issues/57264 is fixed
pub use self::serializer::JsonSerializer;

use anyhow::{format_err, Error};
use std::result;
use thiserror::Error;

pub type Result<T> = result::Result<T, AuthDbError>;

/// Errors that may result from operations on an AuthDb.
#[derive(Debug, Error)]
pub enum AuthDbError {
    /// An illegal input argument was supplied, such as an invalid path.
    #[error("invalid argument")]
    InvalidArguments,
    /// A lower level failure occurred while serializing and writing the data.
    /// See logs for more information.
    #[error("unexpected error serializing or deserializing the database")]
    SerializationError,
    /// A lower level failure occurred while reading and deserializing the data.
    #[error("unexpected IO error accessing the database: {}", _0)]
    IoError(#[from] std::io::Error),
    /// The existing contents of the DB are not valid. This could be caused by a change in file
    /// format or by data corruption.
    #[error("database contents could not be parsed")]
    DbInvalid,
    /// The requested credential is not present in the DB.
    #[error("credential not found")]
    CredentialNotFound,
}

/// Unique identifier for a user credential as stored in the auth db.
#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct CredentialKey {
    /// The type string for the auth provider that created this credential, inferring both the
    /// identity provider and the implementation used for authentication.
    auth_provider_type: String,
    /// A string identifier provided by the identity provider, typically the user's email address
    /// or profile url.
    user_profile_id: String,
}

impl CredentialKey {
    /// Create a new CredentialKey, or returns an Error if any input is empty.
    pub fn new(
        auth_provider_type: String,
        user_profile_id: String,
    ) -> result::Result<CredentialKey, Error> {
        if auth_provider_type.is_empty() {
            Err(format_err!("auth_provider_type cannot be empty"))
        } else if user_profile_id.is_empty() {
            Err(format_err!("user_profile_id cannot be empty"))
        } else {
            Ok(CredentialKey { auth_provider_type, user_profile_id })
        }
    }

    /// Gets the current value of the `auth_provider_type` field.
    pub fn auth_provider_type(&self) -> &str {
        &self.auth_provider_type
    }

    /// Gets the current value of the `user_profile_id` field.
    pub fn user_profile_id(&self) -> &str {
        &self.user_profile_id
    }
}

/// The set of data to be stored for a user credential.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct CredentialValue {
    /// A unique identifier for this credential including the IdP and account.
    credential_key: CredentialKey,
    /// An OAuth refresh token.
    refresh_token: String,
    /// A DER-encoded private key for signing requests that use this credential, if the credential
    /// is bound to a key pair.
    private_key: Option<Vec<u8>>,
}

impl CredentialValue {
    /// Create a new CredentialValue, or returns an Error if any input is empty.
    pub fn new(
        auth_provider_type: String,
        user_profile_id: String,
        refresh_token: String,
        private_key: Option<Vec<u8>>,
    ) -> result::Result<CredentialValue, Error> {
        if refresh_token.is_empty() {
            Err(format_err!("refresh_token cannot be empty"))
        } else {
            Ok(CredentialValue {
                credential_key: CredentialKey::new(auth_provider_type, user_profile_id)?,
                refresh_token,
                private_key,
            })
        }
    }
}

/// A trait expressing the functionality that all auth databases must provide.
pub trait AuthDb {
    /// Adds a new user credential to the database. The operation may insert a new user credential
    /// or replace an existing user credential. Replacement of an existing credential is useful
    /// when the credential has been expired or invalidated by the identity provider.
    fn add_credential(&mut self, credential: CredentialValue) -> Result<()>;

    /// Deletes the specified existing user credential from the database.
    fn delete_credential(&mut self, credential_key: &CredentialKey) -> Result<()>;

    /// Returns keys for all the credentials provisioned in this instance of the database.
    fn get_all_credential_keys<'a>(&'a self) -> Result<Vec<&'a CredentialKey>>;

    /// Returns the refresh token for a specified user credential.
    fn get_refresh_token<'a>(&'a self, credential_key: &CredentialKey) -> Result<&'a str>;
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_AUTH_PROVIDER: &str = "test_provider";
    const TEST_ID: &str = "user@test.com";
    const TEST_REFRESH_TOKEN: &str = "123456789@#$&*(%)_@(&";
    const TEST_PRIVATE_KEY: &[u8] = &[9, 8, 7, 6, 5];

    #[test]
    fn test_new_valid_credential() {
        let cred = CredentialValue::new(
            TEST_AUTH_PROVIDER.to_string(),
            TEST_ID.to_string(),
            TEST_REFRESH_TOKEN.to_string(),
            None,
        )
        .unwrap();
        assert_eq!(cred.credential_key.auth_provider_type, TEST_AUTH_PROVIDER);
        assert_eq!(cred.credential_key.user_profile_id, TEST_ID);
        assert_eq!(cred.refresh_token, TEST_REFRESH_TOKEN);
        assert_eq!(cred.private_key, None);
    }

    #[test]
    fn test_new_valid_credential_with_private_key() {
        let cred = CredentialValue::new(
            TEST_AUTH_PROVIDER.to_string(),
            TEST_ID.to_string(),
            TEST_REFRESH_TOKEN.to_string(),
            Some(TEST_PRIVATE_KEY.to_vec()),
        )
        .unwrap();
        assert_eq!(cred.credential_key.auth_provider_type, TEST_AUTH_PROVIDER);
        assert_eq!(cred.credential_key.user_profile_id, TEST_ID);
        assert_eq!(cred.refresh_token, TEST_REFRESH_TOKEN);
        assert_eq!(cred.private_key, Some(TEST_PRIVATE_KEY.to_vec()));
    }

    #[test]
    fn test_new_invalid_credential() {
        assert!(CredentialValue::new(
            "".to_string(),
            TEST_ID.to_string(),
            TEST_REFRESH_TOKEN.to_string(),
            None
        )
        .is_err());
        assert!(CredentialValue::new(
            TEST_AUTH_PROVIDER.to_string(),
            "".to_string(),
            TEST_REFRESH_TOKEN.to_string(),
            None
        )
        .is_err());
        assert!(CredentialValue::new(
            TEST_AUTH_PROVIDER.to_string(),
            TEST_ID.to_string(),
            "".to_string(),
            None
        )
        .is_err());
    }
}
