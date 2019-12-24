// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context as _,
    fuchsia_merkle::Hash,
    fuchsia_syslog::fx_log_err,
    serde_derive::{Deserialize, Serialize},
    std::{
        fs::{self, File},
        io,
        path::PathBuf,
    },
};

pub trait LastUpdateStorage {
    fn load(&self) -> Option<Hash>;
    fn store(&self, update: &Hash);
}

pub struct LastUpdateStorageFile {
    pub data_dir: PathBuf,
}

const LAST_UPDATE_FILENAME: &str = "last_update.json";
const LAST_UPDATE_FILENAME_PART: &str = "last_update.json.part";

impl LastUpdateStorage for LastUpdateStorageFile {
    fn load(&self) -> Option<Hash> {
        self.load_impl().unwrap_or_else(|e| {
            fx_log_err!("error loading last_update.json: {:?}", e);
            None
        })
    }
    fn store(&self, update: &Hash) {
        self.store_impl(update).unwrap_or_else(|e| {
            fx_log_err!("error storing last_update info, continuing anyway; {:?}", e);
        })
    }
}
impl LastUpdateStorageFile {
    fn load_impl(&self) -> Result<Option<Hash>, anyhow::Error> {
        let open_result = File::open(self.data_dir.join(LAST_UPDATE_FILENAME));
        if let Err(e) = &open_result {
            if e.kind() == io::ErrorKind::NotFound {
                return Ok(None);
            }
        }
        let file = open_result.context("opening last update file")?;
        let value: StorageFormat =
            serde_json::from_reader(file).context("reading last update file")?;
        let StorageFormat::Version1(v1) = value;
        let hash =
            v1.update_package_merkle.parse::<Hash>().context("parsing update_package_merkle")?;
        Ok(Some(hash))
    }
    fn store_impl(&self, update: &Hash) -> Result<(), anyhow::Error> {
        let wrapped =
            StorageFormat::Version1(StorageFormatV1 { update_package_merkle: update.to_string() });
        let part_filename = self.data_dir.join(LAST_UPDATE_FILENAME_PART);
        {
            let file = File::create(&part_filename).context("opening last update file")?;
            serde_json::to_writer(&file, &wrapped).context("writing last update file")?;
            file.sync_all().context("sync last update file")?;
        }
        fs::rename(part_filename, self.data_dir.join(LAST_UPDATE_FILENAME))
            .context("swap last update file into place")?;
        Ok(())
    }
}

#[derive(Deserialize, Serialize)]
#[serde(tag = "version", content = "content", deny_unknown_fields)]
enum StorageFormat {
    #[serde(rename = "1")]
    Version1(StorageFormatV1),
}

#[derive(Deserialize, Serialize)]
struct StorageFormatV1 {
    update_package_merkle: String,
}

#[cfg(test)]
mod tests {
    use {super::*, lazy_static::lazy_static, serde_json::json, std::fs, tempfile::TempDir};

    lazy_static! {
        static ref EXAMPLE_UPDATE_HASH: Hash = { [0x22; 32].into() };
    }

    #[test]
    fn test_load_works() {
        let tempdir = TempDir::new().expect("create tempdir");
        fs::write(
            tempdir.path().join("last_update.json"),
            r#"{"version":"1","content":{"update_package_merkle":"2222222222222222222222222222222222222222222222222222222222222222"}}"#,
        )
        .expect("write last_update.json");
        let storage = LastUpdateStorageFile { data_dir: tempdir.path().into() };
        assert_eq!(storage.load(), Some(*EXAMPLE_UPDATE_HASH));
    }

    #[test]
    fn test_load_without_file_returns_none() {
        let tempdir = TempDir::new().expect("create tempdir");
        let storage = LastUpdateStorageFile { data_dir: tempdir.path().into() };
        assert_eq!(storage.load(), None);
    }

    #[test]
    fn test_load_impl_without_file_returns_none() {
        let tempdir = TempDir::new().expect("create tempdir");
        let storage = LastUpdateStorageFile { data_dir: tempdir.path().into() };
        // We don't want it to print a log if the file isn't found, so it should return Ok(None) instead of an error.
        assert_eq!(storage.load_impl().map_err(|e| e.to_string()), Ok(None));
    }

    #[test]
    fn test_load_with_invalid_file_returns_none() {
        let tempdir = TempDir::new().expect("create tempdir");
        let storage = LastUpdateStorageFile { data_dir: tempdir.path().into() };
        fs::write(
            tempdir.path().join("last_update.json"),
            r#""2222222222222222222222222222222222222222222222222222222222222222""#,
        )
        .expect("write last_update.json");

        assert_eq!(storage.load(), None);
    }

    #[test]
    fn test_store_works() {
        let tempdir = TempDir::new().expect("create tempdir");
        let storage = LastUpdateStorageFile { data_dir: tempdir.path().into() };
        storage.store(&*EXAMPLE_UPDATE_HASH);
        let data = fs::read(tempdir.path().join("last_update.json")).expect("read file");
        let parsed: serde_json::Value = serde_json::from_slice(&data).expect("parse json");
        assert_eq!(
            parsed,
            json!({
                "version": "1",
                "content": {
                    "update_package_merkle": "2222222222222222222222222222222222222222222222222222222222222222"
                }
            })
        )
    }

    #[test]
    fn test_store_roundtrip() {
        let tempdir = TempDir::new().expect("create tempdir");
        let storage = LastUpdateStorageFile { data_dir: tempdir.path().into() };
        storage.store(&*EXAMPLE_UPDATE_HASH);
        assert_eq!(storage.load().expect("a stored value"), *EXAMPLE_UPDATE_HASH);
    }

    #[test]
    fn test_store_overwrite() {
        let tempdir = TempDir::new().expect("create tempdir");
        let storage = LastUpdateStorageFile { data_dir: tempdir.path().into() };
        storage.store(&[0x33; 32].into());
        storage.store(&*EXAMPLE_UPDATE_HASH);
        assert_eq!(storage.load().expect("a stored value"), *EXAMPLE_UPDATE_HASH);
    }
}
