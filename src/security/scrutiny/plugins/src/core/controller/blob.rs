// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
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
    std::sync::Arc,
};

#[derive(Deserialize, Serialize)]
struct BlobRequest {
    merkle: String,
}

#[derive(Deserialize, Serialize)]
struct BlobResponse {
    merkle: String,
    encoding: String,
    data: String,
}

#[derive(Default)]
pub struct BlobController {}

impl DataController for BlobController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let build_path = model.config().build_path();
        let repository_path = model.config().repository_path();
        let mut blob_loader = FileArtifactReader::new(&build_path, &repository_path);

        let req: BlobRequest = serde_json::from_value(query)?;
        let data = blob_loader.read_raw(&format!("blobs/{}", req.merkle))?;
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

#[cfg(test)]
mod tests {
    use {super::*, scrutiny_testing::fake::*};

    #[test]
    fn test_blob_controller_bad_merkle() {
        let model = fake_data_model();
        let blob_controller = BlobController::default();
        let request = BlobRequest { merkle: "invalid_merkle".to_string() };
        let query = serde_json::to_value(request).unwrap();
        let response = blob_controller.query(model, query);
        assert_eq!(response.is_ok(), false);
    }
}
