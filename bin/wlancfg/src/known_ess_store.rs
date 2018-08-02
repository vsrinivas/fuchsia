// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use failure;
use parking_lot::{Mutex, MutexGuard};
use serde_json;
use std::collections::HashMap;
use std::{fs, io, mem};
use std::path::{Path, PathBuf};

const KNOWN_NETWORKS_PATH: &str = "/data/known_networks.json";
const TMP_KNOWN_NETWORKS_PATH: &str = "/data/known_networks.json.tmp";

#[derive(Clone, Debug, PartialEq)]
pub struct KnownEss {
    pub password: Vec<u8>,
}

type EssMap = HashMap<Vec<u8>, KnownEss>;

pub struct KnownEssStore {
    storage_path: PathBuf,
    tmp_storage_path: PathBuf,
    ess_by_ssid: Mutex<EssMap>,
}

// Warning: changing this struct will break persistence
#[derive(Deserialize)]
struct EssJsonRead {
    ssid: Vec<u8>,
    password: Vec<u8>,
}

// Warning: changing this struct will break persistence
#[derive(Serialize)]
struct EssJsonWrite<'a> {
    ssid: &'a [u8],
    password: &'a [u8],
}

impl KnownEssStore {
    pub fn new() -> Result<Self, failure::Error> {
        Self::new_with_paths(PathBuf::from(KNOWN_NETWORKS_PATH),
                        PathBuf::from(TMP_KNOWN_NETWORKS_PATH))
    }

    pub fn new_with_paths(storage_path: PathBuf, tmp_storage_path: PathBuf)
        -> Result<Self, failure::Error>
    {
        let ess_list: Vec<EssJsonRead> = match fs::File::open(&storage_path) {
            Ok(file) => match serde_json::from_reader(file) {
                Ok(list) => list,
                Err(e) => {
                    error!("Failed to parse the list of known wireless networks from JSON\
                            in {}: {}. Starting with an empty list.", storage_path.display(), e);
                    fs::remove_file(&storage_path)
                        .map_err(|e| format_err!("Failed to delete {}: {}",
                                                 storage_path.display(), e))?;
                    Vec::new()
                }
            },
            Err(e) => match e.kind() {
                io::ErrorKind::NotFound => Vec::new(),
                _ => bail!("Failed to open {}: {}", storage_path.display(), e),
            }
        };
        let mut ess_by_ssid = HashMap::with_capacity(ess_list.len());
        for ess in ess_list {
            ess_by_ssid.insert(ess.ssid, KnownEss {
                password: ess.password,
            });
        }
        let ess_by_ssid = Mutex::new(ess_by_ssid);
        Ok(KnownEssStore{ storage_path, tmp_storage_path, ess_by_ssid })
    }

    pub fn lookup(&self, ssid: &[u8]) -> Option<KnownEss> {
        self.ess_by_ssid.lock().get(ssid).map(Clone::clone)
    }

    pub fn store(&self, ssid: Vec<u8>, ess: KnownEss) -> Result<(), failure::Error> {
        let mut guard = self.ess_by_ssid.lock();
        // Even if writing into the file fails, it is still okay
        // to modify the in-memory map. We are not too worried about consistency here.
        guard.insert(ssid, ess);
        self.write(guard)
    }

    pub fn known_network_count(&self) -> usize {
        self.ess_by_ssid.lock().len()
    }

    fn write(&self, guard: MutexGuard<EssMap>) -> Result<(), failure::Error> {
        let temp_file = TempFile::create(&self.tmp_storage_path)?;
        let mut list = Vec::with_capacity(guard.len());
        for (ssid, ess) in guard.iter() {
            list.push(EssJsonWrite {
                ssid: &ssid[..],
                password: &ess.password[..],
            })
        }
        serde_json::to_writer(&temp_file.file, &list)
            .map_err(|e| format_err!("Failed to serialize JSON into {}: {}",
                             self.tmp_storage_path.display(), e))?;
        temp_file.close_and_rename(&self.storage_path)
            .map_err(|e| format_err!("Failed to rename {} into {}: {}",
                             self.tmp_storage_path.display(), self.storage_path.display(), e))?;
        // Ensure that the lock is held until we are done writing
        let _ = &guard;
        Ok(())
    }
}

struct TempPath<'a> {
    path: &'a Path,
}

impl<'a> Drop for TempPath<'a> {
    fn drop(&mut self) {
        fs::remove_file(self.path).unwrap_or_else(|e|
            error!("Failed to delete temporary file {}: {}", self.path.display(), e));
    }
}

struct TempFile<'a> {
    path: TempPath<'a>,
    file: fs::File,
}

impl<'a> TempFile<'a> {
    pub fn create(path: &'a Path) -> Result<Self, failure::Error> {
        let file = fs::File::create(path)
            .map_err(|e| format_err!("Failed to open {} for writing: {}", path.display(), e))?;
        let path = TempPath{ path };
        Ok(TempFile{ path, file })
    }

    pub fn close_and_rename(self, new_name: &Path) -> Result<(), failure::Error> {
        mem::drop(self.file);
        fs::rename(&self.path.path, new_name)?;
        mem::forget(self.path);
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Write;
    use tempdir;

    const STORE_JSON_PATH: &str = "store.json";

    #[test]
    fn store_and_lookup() {
        let temp_dir = tempdir::TempDir::new("ess_store_test").expect("failed to create temp dir");

        // Expect the store to be constructed successfully even if the file doesn't exist yet
        let store = create_ess_store(temp_dir.path());

        assert_eq!(None, store.lookup(b"foo"));
        assert_eq!(0, store.known_network_count());
        store.store(b"foo".to_vec(), ess(b"qwerty")).expect("storing 'foo' failed");
        assert_eq!(Some(ess(b"qwerty")), store.lookup(b"foo"));
        assert_eq!(1, store.known_network_count());
        store.store(b"foo".to_vec(), ess(b"12345")).expect("storing 'foo' again failed");
        assert_eq!(Some(ess(b"12345")), store.lookup(b"foo"));
        assert_eq!(1, store.known_network_count());

        // Make sure that storage is persistent
        let store = create_ess_store(temp_dir.path());
        assert_eq!(Some(ess(b"12345")), store.lookup(b"foo"));
        assert_eq!(1, store.known_network_count());

        // Make sure that overwriting the existing file works
        store.store(b"bar".to_vec(), ess(b"zxcvb")).expect("storing 'bar' failed");
        let store = create_ess_store(temp_dir.path());
        assert_eq!(Some(ess(b"12345")), store.lookup(b"foo"));
        assert_eq!(Some(ess(b"zxcvb")), store.lookup(b"bar"));
        assert_eq!(2, store.known_network_count());
    }

    #[test]
    fn recover_from_bad_file() {
        let temp_dir = tempdir::TempDir::new("ess_store_test").expect("failed to create temp dir");
        let path = temp_dir.path().join(STORE_JSON_PATH);
        let mut file = fs::File::create(&path)
            .expect("failed to open file for writing");
        // Write invalid JSON and close the file
        file.write(b"{").expect("failed to write broken json into file");
        mem::drop(file);
        assert!(path.exists());

        // Constructing an EssStore should still succeed,
        // but the invalid file should be gone now
        let store = create_ess_store(temp_dir.path());
        assert!(!path.exists());

        // Writing an entry should create the file
        store.store(b"foo".to_vec(), ess(b"qwerty")).expect("storing 'foo' failed");
        assert!(path.exists());
    }

    #[test]
    fn bail_if_path_is_bad() {
        match KnownEssStore::new_with_paths(PathBuf::from("/dev/null/foo"), PathBuf::from("/dev/null")) {
            Ok(_) => panic!("expected constructor to fail"),
            Err(e) => assert!(e.to_string().contains("Failed to open /dev/null/foo"),
                              format!("error message was: {}", e))
        }
    }

    fn create_ess_store(path: &Path) -> KnownEssStore {
        KnownEssStore::new_with_paths(path.join(STORE_JSON_PATH), path.join("store.json.tmp"))
            .expect("Failed to create an KnownEssStore")
    }

    fn ess(password: &[u8]) -> KnownEss {
        KnownEss {
            password: password.to_vec(),
        }
    }
}
