// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate tempdir;

use super::serializer::{JsonSerializer, Serializer};
use super::{AuthDb, AuthDbError, CredentialKey, CredentialValue};

use std::collections::BTreeMap;
use std::fs::{self, File};
use std::path::{Path, PathBuf};
use std::result;

pub type Result<T> = result::Result<T, AuthDbError>;

/// A file-based implementation of the AuthDb trait.
pub struct AuthDbFile<S: Serializer> {
    /// The path of the database file.
    file_path: PathBuf,
    /// The database contents, stored as a BTreeMap to retain a consistent
    /// order.
    credentials: BTreeMap<CredentialKey, CredentialValue>,
    /// An object implementing the Serializer trait that we use to convert
    /// between CredentialValues and byte strings.
    serializer: S,
}

impl AuthDbFile<JsonSerializer> {
    /// Creates a new AuthDbFile instance using the specified file path and the
    /// default serialization mechanism.
    pub fn new(credentials_path: &Path) -> Result<AuthDbFile<JsonSerializer>> {
        AuthDbFile::<JsonSerializer>::new_with_serializer(credentials_path, JsonSerializer)
    }
}

impl<S: Serializer> AuthDbFile<S> {
    /// Creates a new AuthDbFile instance using the specified file path and the
    /// supplied serializer.
    pub fn new_with_serializer(credentials_path: &Path, serializer: S) -> Result<AuthDbFile<S>> {
        let credentials = if credentials_path.is_file() {
            // If the file exists, attempt to read its contents into a map...
            let credentials_file = Self::read_file(credentials_path)?;
            let credentials_vec = serializer.deserialize(credentials_file)?;
            credentials_vec
                .into_iter()
                .map(|cred| (cred.credential_key.clone(), cred))
                .collect()
        } else {
            // ...if not, create its directory and start with an empty map.
            Self::create_directory(credentials_path)?;
            BTreeMap::new()
        };

        Ok(AuthDbFile {
            file_path: credentials_path.to_path_buf(),
            credentials,
            serializer,
        })
    }

    /// Saves the credentials currently in memory to the file supplied at
    /// creation.
    fn save(&self) -> Result<()> {
        // TODO(jsankey): Reduce the chances that any errors will delete the credential
        // db by first saving into a tempfile and then replacing the previous file with
        // the tempfile.
        let f = Self::truncate_file(&self.file_path)?;
        self.serializer.serialize(f, self.credentials.values())?;
        Ok(())
    }

    /// Attempts to read the supplied file path.
    fn read_file(path: &Path) -> Result<File> {
        File::open(path).map_err(|err| {
            warn!("AuthDbFile failed to read credential file: {:?}", err);
            AuthDbError::IoError(err)
        })
    }

    /// Attempts to create or truncate the supplied file path.
    fn truncate_file(path: &Path) -> Result<File> {
        File::create(path).map_err(|err| {
            warn!("AuthDbFile failed to truncate credential file: {:?}", err);
            AuthDbError::IoError(err)
        })
    }

    /// Attempts to create the directory containing the supplied path. Returns
    /// OK if the directory already exists or the creation succeeds. Returns an
    /// error if the directory could not be calculated or the creation fails.
    fn create_directory(path: &Path) -> Result<()> {
        match Path::new(path).parent() {
            Some(dir_path) if dir_path.is_dir() => Ok(()),
            Some(dir_path) => fs::create_dir(dir_path).map_err(|err| {
                warn!("Failed to create directory for credential file: {:?}", err);
                AuthDbError::IoError(err)
            }),
            None => {
                warn!("Could not determine directory for credential file");
                Err(AuthDbError::InvalidArguments)
            }
        }
    }
}

impl<S: Serializer> AuthDb for AuthDbFile<S> {
    fn add_credential(&mut self, credential: CredentialValue) -> Result<()> {
        self.credentials
            .insert(credential.credential_key.clone(), credential);
        self.save()
    }

    fn delete_credential(&mut self, credential_key: &CredentialKey) -> Result<()> {
        if self.credentials.remove(credential_key).is_none() {
            Err(AuthDbError::CredentialNotFound)
        } else {
            self.save()
        }
    }

    fn get_all_credentials<'a>(&'a self) -> Result<Vec<&'a CredentialValue>> {
        Ok(self.credentials.values().collect())
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
    use self::tempdir::TempDir;
    use super::*;

    struct TempLocation {
        // A fresh temp directory that will be deleted when this object is dropped.
        _dir: TempDir,
        // A path within the temp dir to use for writing the db.
        path: PathBuf,
    }

    fn build_test_creds(id: &str, refresh_token: &str) -> CredentialValue {
        CredentialValue::new(
            "test".to_string(),
            id.to_string(),
            refresh_token.to_string(),
        ).unwrap()
    }

    fn create_temp_location() -> TempLocation {
        let dir = TempDir::new("AuthStoreUnitTest").unwrap();
        let path = dir.path().join("authdb");
        TempLocation { _dir: dir, path }
    }

    #[test]
    fn test_write_and_get() {
        let temp_location = create_temp_location();
        let cred_1 = build_test_creds("user1", "iuhaiedwufh");
        let cred_2 = build_test_creds("user2", "feouihefiuh");

        {
            // Load a database and insert one credential.
            let mut db = AuthDbFile::new(&temp_location.path).unwrap();
            db.add_credential(cred_1.clone()).unwrap();
            assert_eq!(
                db.get_refresh_token(&cred_1.credential_key).unwrap(),
                &cred_1.refresh_token
            );
            assert_match!(
                db.get_refresh_token(&cred_2.credential_key),
                Err(AuthDbError::CredentialNotFound)
            );
        }

        {
            // Load a second database from the same file and verify contents are retained.
            let mut db = AuthDbFile::new(&temp_location.path).unwrap();
            assert_eq!(
                db.get_refresh_token(&cred_1.credential_key).unwrap(),
                &cred_1.refresh_token
            );
            db.add_credential(cred_2.clone()).unwrap();
            assert_eq!(
                db.get_refresh_token(&cred_2.credential_key).unwrap(),
                &cred_2.refresh_token
            );
            assert_eq!(db.get_all_credentials().unwrap(), vec![&cred_1, &cred_2]);
        }
    }

    #[test]
    fn test_delete() {
        let temp_location = create_temp_location();
        let cred_1 = build_test_creds("user1", "iuhaiedwufh");

        {
            // Load a database and insert one credential.
            let mut db = AuthDbFile::new(&temp_location.path).unwrap();
            db.add_credential(cred_1.clone()).unwrap();
            // Check the credential can be deleted only once.
            assert!(db.delete_credential(&cred_1.credential_key).is_ok());
            assert_match!(
                db.delete_credential(&cred_1.credential_key),
                Err(AuthDbError::CredentialNotFound)
            );
        }

        {
            // Loading the database again should work even though we deleted all the
            // entries.
            AuthDbFile::new(&temp_location.path).unwrap();
        }
    }
}
