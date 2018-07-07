// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use]
extern crate failure;
#[macro_use]
extern crate log;
extern crate serde;
extern crate serde_json;

#[macro_use]
mod macros;
pub mod file;
mod serializer;

use failure::Error;
use std::result;

pub type Result<T> = result::Result<T, AuthDbError>;

/// Errors that may result from operations on an AuthDb.
#[derive(Debug, Fail)]
pub enum AuthDbError {
    /// An illegal input argument was supplied, such as an invalid path.
    #[fail(display = "invalid argument")]
    InvalidArguments,
    /// A lower level failure occured while serialization and writing the data.
    /// See logs for more information.
    #[fail(display = "unexpected error serializing or deserialing the database")]
    SerializationError,
    /// A lower level failure occured while reading and deserialization the
    /// data.
    #[fail(display = "unexpected IO error accessing the database: {}", _0)]
    IoError(#[cause] std::io::Error),
    /// The existing contents of the DB are not valid. This could be caused by
    /// a change in file format or by data corruption.
    #[fail(display = "database contents could not be parsed")]
    DbInvalid,
    /// The requested credential is not present in the DB.
    #[fail(display = "credential not found")]
    CredentialNotFound,
}

/// Unique identifier for a user credential as stored in the auth db.
#[derive(Clone, PartialEq, Eq, PartialOrd, Ord, Debug)]
pub struct CredentialKey {
    /// A string identifier for an configured identity provider, such as
    /// 'Google'.
    identity_provider: String,
    /// A string identifier provided by the identity provider, typically the
    /// user's email address or profile url.
    id: String,
}

impl CredentialKey {
    /// Create a new CredentialKey, or returns an Error if any input is
    /// empty.
    pub fn new(identity_provider: String, id: String) -> result::Result<CredentialKey, Error> {
        if identity_provider.is_empty() {
            Err(format_err!("identity_provider cannot be empty"))
        } else if id.is_empty() {
            Err(format_err!("id cannot be empty"))
        } else {
            Ok(CredentialKey {
                identity_provider,
                id,
            })
        }
    }
}

/// The set of data to be stored for a user credential.
#[derive(Clone, PartialEq, Eq, Debug)]
pub struct CredentialValue {
    /// A unique identifier for credential including the IdP and account.
    credential_key: CredentialKey,
    /// An OAuth refresh token.
    refresh_token: String,
}

impl CredentialValue {
    /// Create a new CredentialValue, or returns an Error if any input is empty.
    pub fn new(
        identity_provider: String,
        id: String,
        refresh_token: String,
    ) -> result::Result<CredentialValue, Error> {
        if refresh_token.is_empty() {
            Err(format_err!("refresh_token cannot be empty"))
        } else {
            Ok(CredentialValue {
                credential_key: CredentialKey::new(identity_provider, id)?,
                refresh_token,
            })
        }
    }
}

/// A trait expressing the functionality that all auth databases must provide.
pub trait AuthDb {
    /// Adds a new user credential to the database. The operation may insert a
    /// new user credential or replace an existing user credential.
    /// Replacement of an existing credential is useful when the credential
    /// has been expired or invalidated by the identity provider.
    fn add_credential(&mut self, credential: CredentialValue) -> Result<()>;

    /// Deletes the specified existing user credential from the database.
    fn delete_credential(&mut self, credential_key: &CredentialKey) -> Result<()>;

    /// Returns all the credentials provisioned in this instance of the
    /// database.
    fn get_all_credentials<'a>(&'a self) -> Result<Vec<&'a CredentialValue>>;

    /// Returns the refresh token for a specified user credential.
    fn get_refresh_token<'a>(&'a self, credential_key: &CredentialKey) -> Result<&'a str>;
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_IDP: &str = "test.com";
    const TEST_ID: &str = "user@test.com";
    const TEST_REFRESH_TOKEN: &str = "123456789@#$&*(%)_@(&";

    #[test]
    fn test_new_valid_credential() {
        let cred = CredentialValue::new(
            TEST_IDP.to_string(),
            TEST_ID.to_string(),
            TEST_REFRESH_TOKEN.to_string(),
        ).unwrap();
        assert_eq!(cred.credential_key.identity_provider, TEST_IDP);
        assert_eq!(cred.credential_key.id, TEST_ID);
        assert_eq!(cred.refresh_token, TEST_REFRESH_TOKEN);
    }

    #[test]
    fn test_new_invalid_credential() {
        assert!(
            CredentialValue::new(
                "".to_string(),
                TEST_ID.to_string(),
                TEST_REFRESH_TOKEN.to_string()
            ).is_err()
        );
        assert!(
            CredentialValue::new(
                TEST_IDP.to_string(),
                "".to_string(),
                TEST_REFRESH_TOKEN.to_string()
            ).is_err()
        );
        assert!(
            CredentialValue::new(TEST_IDP.to_string(), TEST_ID.to_string(), "".to_string())
                .is_err()
        );
    }
}
