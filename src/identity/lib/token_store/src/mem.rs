// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains an in-memory implementation of the AuthDb trait.

use crate::{AuthDb, AuthDbError, CredentialKey, CredentialValue};

use std::collections::BTreeMap;
use std::result;

pub type Result<T> = result::Result<T, AuthDbError>;

/// An in-memory implementation of the AuthDb trait.
pub struct AuthDbInMemory {
    /// The database contents, stored as a BTreeMap to retain a consistent
    /// order.
    credentials: BTreeMap<CredentialKey, CredentialValue>,
}

impl AuthDbInMemory {
    /// Creates a new AuthDbInMemory instance.
    pub fn new() -> AuthDbInMemory {
        Self { credentials: BTreeMap::new() }
    }
}

impl AuthDb for AuthDbInMemory {
    fn add_credential(&mut self, credential: CredentialValue) -> Result<()> {
        self.credentials.insert(credential.credential_key.clone(), credential);
        Ok(())
    }

    fn delete_credential(&mut self, credential_key: &CredentialKey) -> Result<()> {
        match self.credentials.remove(credential_key) {
            Some(_) => Ok(()),
            None => Err(AuthDbError::CredentialNotFound),
        }
    }

    fn get_all_credential_keys<'a>(&'a self) -> Result<Vec<&'a CredentialKey>> {
        Ok(self.credentials.keys().collect())
    }

    fn get_refresh_token<'a>(&'a self, credential_key: &CredentialKey) -> Result<&'a str> {
        match self.credentials.get(credential_key) {
            None => Err(AuthDbError::CredentialNotFound),
            Some(value) => Ok(&value.refresh_token),
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;

    fn build_test_creds(user_profile_id: &str, refresh_token: &str) -> CredentialValue {
        CredentialValue::new(
            "test".to_string(),
            user_profile_id.to_string(),
            refresh_token.to_string(),
            None, /* do not include a private key */
        )
        .unwrap()
    }

    fn assert_keys_equal<'a>(actual: Vec<&'a CredentialKey>, expected: Vec<&'a CredentialKey>) {
        assert_eq!(actual.len(), expected.len());
        for (a, b) in actual.iter().zip(expected) {
            assert_eq!(a, &b);
        }
    }

    /// Check a couple of easy base cases for an empty collection
    #[test]
    fn test_empty() -> Result<()> {
        let cred_1 = build_test_creds("user1", "iuhaiedwufh");

        let mut db = AuthDbInMemory::new();
        assert_match!(
            db.get_refresh_token(&cred_1.credential_key),
            Err(AuthDbError::CredentialNotFound)
        );
        assert_match!(
            db.delete_credential(&cred_1.credential_key),
            Err(AuthDbError::CredentialNotFound)
        );
        assert_keys_equal(db.get_all_credential_keys()?, vec![]);

        Ok(())
    }

    /// Add a couple of credentials and check for their existence
    #[test]
    fn test_add_and_get() -> Result<()> {
        let cred_1 = build_test_creds("user1", "iuhaiedwufh");
        let cred_2 = build_test_creds("user2", "feouihefiuh");

        let mut db = AuthDbInMemory::new();

        db.add_credential(cred_1.clone())?;
        assert_eq!(db.get_refresh_token(&cred_1.credential_key)?, &cred_1.refresh_token);
        assert_keys_equal(db.get_all_credential_keys()?, vec![&cred_1.credential_key]);

        db.add_credential(cred_2.clone())?;
        assert_eq!(db.get_refresh_token(&cred_2.credential_key)?, &cred_2.refresh_token);
        assert_keys_equal(
            db.get_all_credential_keys()?,
            vec![&cred_1.credential_key, &cred_2.credential_key],
        );

        Ok(())
    }

    /// Add a credential and check that it can be modified
    #[test]
    fn test_add_and_modify() -> Result<()> {
        let cred_original = build_test_creds("user1", "iuhaiedwufh");
        let cred_modified = build_test_creds("user1", "feouihefiuh");

        // Sanity check that the keys compare as equivalent
        assert_eq!(cred_original.credential_key, cred_modified.credential_key);

        let mut db = AuthDbInMemory::new();

        db.add_credential(cred_original.clone())?;
        db.add_credential(cred_modified.clone())?;
        // Both credentials have the same key, so we use cred_original for lookup to verify
        assert_keys_equal(db.get_all_credential_keys()?, vec![&cred_original.credential_key]);
        assert_eq!(db.get_refresh_token(&cred_original.credential_key)?,
            &cred_modified.refresh_token);

        Ok(())
    }

    /// Add a couple of credentials and check that we can remove them
    #[test]
    fn test_delete() -> Result<()> {
        let cred_1 = build_test_creds("user1", "iuhaiedwufh");
        let cred_2 = build_test_creds("user2", "feouihefiuh");

        // Load a database and insert one credential.
        let mut db = AuthDbInMemory::new();
        db.add_credential(cred_1.clone())?;
        db.add_credential(cred_2.clone())?;

        // Remove cred_1
        assert!(db.delete_credential(&cred_1.credential_key).is_ok());
        // Check the credential can be deleted only once.
        assert_match!(
            db.delete_credential(&cred_1.credential_key),
            Err(AuthDbError::CredentialNotFound)
        );
        assert_keys_equal(db.get_all_credential_keys()?, vec![&cred_2.credential_key]);

        // Remove cred_2
        assert!(db.delete_credential(&cred_2.credential_key).is_ok());
        assert_keys_equal(db.get_all_credential_keys()?, vec![]);

        Ok(())
    }
}
