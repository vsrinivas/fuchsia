// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::store::Store,
    anyhow::Result,
    log::info,
    serde_json::value::Value,
    std::fs::{self, File},
    std::io::BufReader,
    std::path::Path,
    uuid::Uuid,
};

/// The EmbeddedStore represents a directory of JSON files accessed by UUIDs.
/// This is a very simple storage model that is portable and doesn't require
/// importing a fully featured third party database. It makes no attempt to
/// understand or interpret the data. It simply takes Value types and either
/// reads them from disk or writes them to the disk. The store itself is
/// not thread safe. Writes should be done one at a time and a higher level
/// in memory cache should be implemented at a higher level.
pub struct EmbeddedStore {
    uri: String,
}

impl Store for EmbeddedStore {
    /// Connect to the database and return a handle to that database.
    fn connect(uri: String) -> Result<Self> {
        let path = Path::new(&uri);
        if !path.exists() {
            info!("Creating data model at: {}", uri);
            fs::create_dir(&path)?;
        }
        Ok(Self { uri })
    }

    /// Returns the database URI.
    fn uri(&self) -> &str {
        &self.uri
    }

    /// Lists all the collectionsin the database.
    fn collections(&self) -> Result<Vec<Uuid>> {
        let mut uuids = vec![];
        for entry in fs::read_dir(&self.uri)? {
            let entry = entry?;
            let path = entry.path();
            if let Some(ext) = path.extension() {
                if ext == "bin" {
                    let name = path.file_stem().unwrap().to_os_string().into_string().unwrap();
                    uuids.push(Uuid::parse_str(&name)?);
                }
            }
        }
        Ok(uuids)
    }

    /// Create a new table, initializing it and adding it to the database.
    fn set(&mut self, name: Uuid, collection: Value) -> Result<()> {
        let file_path = Path::new(&self.uri).join(name.to_hyphenated().to_string());
        let file = File::create(file_path)?;
        serde_cbor::to_writer(file, &collection)?;
        Ok(())
    }

    /// Returns a collection if it exists.
    fn get(&mut self, name: &Uuid) -> Result<Value> {
        let file_path = Path::new(&self.uri).join(name.to_hyphenated().to_string());
        let file = File::open(file_path)?;
        let reader = BufReader::new(file);
        let result: Value = serde_cbor::from_reader(reader)?;
        Ok(result)
    }

    /// Drop a table, deleting it and removing it from the store.
    fn remove(&mut self, name: &Uuid) -> Result<()> {
        let file_path = Path::new(&self.uri).join(name.to_hyphenated().to_string());
        if file_path.exists() {
            fs::remove_file(file_path)?;
        }
        Ok(())
    }
}
