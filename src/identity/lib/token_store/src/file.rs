// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::serializer::{JsonSerializer, Serializer};
use crate::{AuthDb, AuthDbError, CredentialKey, CredentialValue};

use log::warn;
use std::collections::BTreeMap;
use std::ffi::OsString;
use std::fs::{self, File};
use std::io::{BufReader, BufWriter, Write};
use std::path::{Path, PathBuf};
use std::result;

pub type Result<T> = result::Result<T, AuthDbError>;

/// An extension added to the database filename to name the temporary staging file.
const TMP_EXT: &str = ".tmp";

/// A file-based implementation of the AuthDb trait.
pub struct AuthDbFile<S: Serializer> {
    /// The path of the database file.
    file_path: PathBuf,
    /// The path of the temporary database staging file.
    tmp_file_path: PathBuf,
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
            credentials_vec.into_iter().map(|cred| (cred.credential_key.clone(), cred)).collect()
        } else {
            // ...if not, create its directory and start with an empty map.
            Self::create_directory(credentials_path)?;
            BTreeMap::new()
        };

        let mut tmp_file: OsString = credentials_path.into();
        tmp_file.push(TMP_EXT);

        Ok(AuthDbFile {
            file_path: credentials_path.to_path_buf(),
            tmp_file_path: tmp_file.into(),
            credentials,
            serializer,
        })
    }

    /// Saves the credentials currently in memory to the file supplied at creation.
    fn save(&self) -> Result<()> {
        // Note that we first write into a temporary staging file then rename to provide atomicity.
        // We use a fixed tempfile rather than dynamically generating filenames to ensure we
        // never accumulate multiple tempfiles.
        let mut buffer = BufWriter::new(Self::truncate_file(&self.tmp_file_path)?);
        self.serializer.serialize(&mut buffer, self.credentials.values())?;
        buffer.flush().map_err(|err| {
            warn!("AuthDbFile failed to flush serialized file: {:?}", err);
            AuthDbError::IoError(err)
        })?;
        Self::rename_file(&self.tmp_file_path, &self.file_path)?;
        Ok(())
    }

    /// Attempts to read the supplied file path.
    fn read_file(path: &Path) -> Result<BufReader<File>> {
        File::open(path).map(|file| BufReader::new(file)).map_err(|err| {
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

    /// Attempts to rename the file at `source` to `dest`, overwriting any existing file with that
    /// name.
    fn rename_file(source: &Path, dest: &Path) -> Result<()> {
        fs::rename(source, dest).map_err(|err| {
            warn!("AuthDbFile failed to rename temporary credential file: {:?}", err);
            AuthDbError::IoError(err)
        })
    }

    /// Attempts to create the directory containing the supplied path. Returns OK if the directory
    /// already exists or the creation succeeds. Returns an error if the directory could not be
    /// calculated or the creation fails.
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
        self.credentials.insert(credential.credential_key.clone(), credential);
        self.save()
    }

    fn delete_credential(&mut self, credential_key: &CredentialKey) -> Result<()> {
        if self.credentials.remove(credential_key).is_none() {
            Err(AuthDbError::CredentialNotFound)
        } else {
            self.save()
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
    use tempfile::TempDir;

    struct TempLocation {
        /// A fresh temp directory that will be deleted when this object is dropped.
        _dir: TempDir,
        /// A path within the temp dir to use for writing the db.
        path: PathBuf,
    }

    fn build_test_creds(user_profile_id: &str, refresh_token: &str) -> CredentialValue {
        CredentialValue::new(
            "test".to_string(),
            user_profile_id.to_string(),
            refresh_token.to_string(),
            None, /* do not include a private key */
        )
        .unwrap()
    }

    fn create_temp_location() -> TempLocation {
        let dir = TempDir::new().unwrap();
        let path = dir.path().join("authdb");
        TempLocation { _dir: dir, path }
    }

    #[test]
    fn test_write_and_get() -> Result<()> {
        let temp_location = create_temp_location();
        let cred_1 = build_test_creds("user1", "iuhaiedwufh");
        let cred_2 = build_test_creds("user2", "feouihefiuh");

        {
            // Load a database and insert one credential.
            let mut db = AuthDbFile::new(&temp_location.path)?;
            db.add_credential(cred_1.clone())?;
            assert_eq!(db.get_refresh_token(&cred_1.credential_key)?, &cred_1.refresh_token);
            assert_match!(
                db.get_refresh_token(&cred_2.credential_key),
                Err(AuthDbError::CredentialNotFound)
            );
        }

        {
            // Load a second database from the same file and verify contents are retained.
            let mut db = AuthDbFile::new(&temp_location.path)?;
            assert_eq!(db.get_refresh_token(&cred_1.credential_key)?, &cred_1.refresh_token);
            db.add_credential(cred_2.clone())?;
            assert_eq!(db.get_refresh_token(&cred_2.credential_key)?, &cred_2.refresh_token);
            assert_eq!(
                db.get_all_credential_keys().unwrap(),
                vec![&cred_1.credential_key, &cred_2.credential_key]
            );
        }
        Ok(())
    }

    #[test]
    fn test_modify_and_get() -> Result<()> {
        let temp_location = create_temp_location();
        let cred_1 = build_test_creds("user1", "iuhaiedwufh");
        let cred_2 = build_test_creds("user2", "feouihefiuh");

        {
            // Load a database and insert one credential.
            let mut db = AuthDbFile::new(&temp_location.path)?;
            db.add_credential(cred_1.clone())?;
        }

        {
            // Load a separate instance of the database and insert the second credential.
            let mut db = AuthDbFile::new(&temp_location.path)?;
            db.add_credential(cred_2.clone())?;
        }

        {
            // Load another separate instance of the database and checks both credentials are
            // present.
            let db = AuthDbFile::new(&temp_location.path)?;
            assert_eq!(db.get_refresh_token(&cred_1.credential_key)?, &cred_1.refresh_token);
            assert_eq!(db.get_refresh_token(&cred_2.credential_key)?, &cred_2.refresh_token);
        }
        Ok(())
    }

    #[test]
    fn test_delete() -> Result<()> {
        let temp_location = create_temp_location();
        let cred_1 = build_test_creds("user1", "iuhaiedwufh");

        {
            // Load a database and insert one credential.
            let mut db = AuthDbFile::new(&temp_location.path)?;
            db.add_credential(cred_1.clone())?;
            // Check the credential can be deleted only once.
            assert!(db.delete_credential(&cred_1.credential_key).is_ok());
            assert_match!(
                db.delete_credential(&cred_1.credential_key),
                Err(AuthDbError::CredentialNotFound)
            );
        }

        {
            // Loading the database again should work even though we deleted all the entries.
            AuthDbFile::new(&temp_location.path)?;
        }
        Ok(())
    }
}
