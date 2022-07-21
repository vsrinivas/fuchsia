// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    fuchsia_archive::Utf8Reader as FarReader,
    scrutiny::{
        model::controller::{DataController, HintDataType},
        model::model::*,
    },
    scrutiny_utils::usage::*,
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::{
        collections::HashMap,
        fs::File,
        io::{prelude::*, Cursor},
        sync::Arc,
    },
};

/// Given an `input` far file extracts the meta/ data from the package into
/// a dictionary of key,value pairs where the key is the file name and the
/// value is the data contained in that file. meta/ data is data that is
/// actually embedded in the far file instead of being references to merkle
/// blobs.
#[derive(Deserialize, Serialize)]
pub struct FarMetaExtractRequest {
    /// The input path for the far package you wish to extract meta data from.
    pub input: String,
}

#[derive(Default)]
pub struct FarMetaExtractController {}

impl DataController for FarMetaExtractController {
    fn query(&self, _model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: FarMetaExtractRequest = serde_json::from_value(query)?;

        let mut pkg_file = File::open(request.input)?;
        let mut pkg_buffer = Vec::new();
        pkg_file.read_to_end(&mut pkg_buffer)?;

        let mut cursor = Cursor::new(pkg_buffer);
        let mut far = FarReader::new(&mut cursor)?;

        let pkg_files: Vec<String> = far.list().map(|e| e.path().to_string()).collect();
        let mut meta_files = HashMap::new();
        // Extract all the far meta files.
        for file_name in pkg_files.iter() {
            let data = far.read_file(file_name)?;
            meta_files.insert(file_name, String::from(std::str::from_utf8(&data)?));
        }
        Ok(json!(meta_files))
    }

    fn description(&self) -> String {
        "Extracts a Far metadata from a path.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("tool.far.meta.extract - Extracts Fuchsia package meta.")
            .summary("tool.far.meta.extract --input foo.far")
            .description("Extracts FAR meta from a given path to some provided file path.")
            .arg("--input", "Far file that you wish to extract.")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--input".to_string(), HintDataType::NoType)]
    }
}
