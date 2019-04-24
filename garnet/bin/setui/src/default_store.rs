// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::*;
use failure::Error;
use fidl_fuchsia_setui::*;
use serde_json::Value;
use std::fs::File;
use std::io::BufReader;
use std::io::Write;
use std::path::Path;

pub struct DefaultStore {
    file_path: String,
    codec: BoxedSettingCodec<Value>,
}

impl DefaultStore {
    pub fn new(file_path: String, codec: BoxedSettingCodec<Value>) -> DefaultStore {
        return DefaultStore { file_path: file_path, codec: codec };
    }
}

impl Store for DefaultStore {
    /// Writes value to presistent storage.
    fn write(&self, data: SettingData) -> Result<(), Error> {
        let encoded_data = self.codec.encode(data)?;
        let mut file = File::create(self.file_path.clone())?;
        file.write_all(encoded_data.to_string().as_bytes())?;

        return Ok(());
    }

    /// Reads value from persistent storage
    fn read(&self) -> Result<Option<SettingData>, Error> {
        if !Path::new(&self.file_path).exists() {
            return Ok(None);
        }

        let reader = BufReader::new(File::open(self.file_path.clone())?);
        let json = serde_json::from_reader(reader)?;

        return Ok(Some(self.codec.decode(json)?));
    }
}
