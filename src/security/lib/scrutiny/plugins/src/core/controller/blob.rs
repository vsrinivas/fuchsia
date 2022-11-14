// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    fuchsia_merkle::Hash,
    scrutiny::{
        model::controller::{DataController, HintDataType},
        model::model::DataModel,
    },
    scrutiny_utils::{
        artifact::{ArtifactReader, FileArtifactReader},
        usage::UsageBuilder,
    },
    serde::{Deserialize, Serialize},
    serde_json::{self, value::Value},
    std::{path::Path, path::PathBuf, sync::Arc},
};

#[derive(Deserialize, Serialize)]
struct BlobRequest {
    merkle: Hash,
}

#[derive(Deserialize, Serialize)]
struct BlobResponse {
    merkle: Hash,
    encoding: String,
    data: String,
}

#[derive(Default)]
pub struct BlobController {}

impl DataController for BlobController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let model_config = model.config();
        let mut artifact_reader =
            FileArtifactReader::new(&PathBuf::new(), &model_config.blobs_directory());
        let req: BlobRequest = serde_json::from_value(query)?;
        let merkle_string = format!("{}", req.merkle);
        let data = artifact_reader
            .read_bytes(Path::new(&merkle_string))
            .context("Failed to read blob for blob controller")?;
        let resp = BlobResponse {
            merkle: req.merkle.clone(),
            encoding: "base64".to_string(),
            data: base64::encode(&data),
        };
        Ok(serde_json::to_value(resp)?)
    }

    fn description(&self) -> String {
        "Returns a base64 encoded blob for the given merkle.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("blob - Returns a base64 encoded blob for a given merkle.")
            .summary("blob")
            .description(
                "Provides a base64 encoded blob for any given merkle \
            This is useful for extracting the contents of certain merkles in the \
            system quickly.",
            )
            .arg("--merkle", "The merkle you want to extract")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--merkle".to_string(), HintDataType::NoType)]
    }
}
