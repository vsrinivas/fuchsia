// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::{artifact::ArtifactGetter, package_getter::PackageGetter},
    anyhow::{anyhow, Result},
    scrutiny::{model::controller::DataController, model::model::DataModel},
    serde::{Deserialize, Serialize},
    serde_json::{self, value::Value},
    std::env,
    std::path::Path,
    std::sync::Arc,
};

pub const REPOSITORY_PATH: &str = "out/default/amber-files/repository";

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
pub struct BlobController {
    getter: Option<ArtifactGetter>,
}

impl BlobController {
    pub fn new() -> Self {
        if let Ok(fuchsia_dir) = env::var("FUCHSIA_DIR") {
            let repository_path = Path::new(&fuchsia_dir).join(REPOSITORY_PATH);
            Self { getter: Some(ArtifactGetter::new(&repository_path)) }
        } else {
            Self { getter: None }
        }
    }
}

impl DataController for BlobController {
    fn query(&self, _: Arc<DataModel>, query: Value) -> Result<Value> {
        if let Some(blob_getter) = &self.getter {
            let req: BlobRequest = serde_json::from_value(query)?;
            let data = blob_getter.read_raw(&format!("blobs/{}", req.merkle))?;
            let resp = BlobResponse {
                merkle: req.merkle.clone(),
                encoding: "base64".to_string(),
                data: base64::encode(&data),
            };
            Ok(serde_json::to_value(resp)?)
        } else {
            return Err(anyhow!("Unable to retrieve blobs, failed to construct getter."));
        }
    }
    fn description(&self) -> String {
        "Returns a base64 encoded blob for the given merkle.".to_string()
    }
}

#[cfg(test)]
mod tests {
    use {super::*, tempfile::tempdir};

    #[test]
    fn test_blob_controller_bad_merkle() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let blob_controller = BlobController::new();
        let request = BlobRequest { merkle: "invalid_merkle".to_string() };
        let query = serde_json::to_value(request).unwrap();
        let response = blob_controller.query(model, query);
        assert_eq!(response.is_ok(), false);
    }
}
