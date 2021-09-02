// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    scrutiny::model::model::DataModel, scrutiny_config::ModelConfig, std::sync::Arc,
    tempfile::tempdir,
};

/// Creates a simple fake model configuration that uses an in memory uri and
/// tempdata() directories for the required build locations.
pub fn fake_model_config() -> ModelConfig {
    ModelConfig {
        uri: "{memory}".to_string(),
        build_path: tempdir().unwrap().into_path(),
        repository_path: tempdir().unwrap().into_path(),
        repository_blobs_path: tempdir().unwrap().into_path(),
        update_package_url: "fuchsia-pkg://fuchsia.com/update".to_string(),
        config_data_package_url: "fuchsia-pkg://fuchsia.com/config-data".to_string(),
        devmgr_config_path: "config/devmgr".to_string(),
    }
}

/// Constructs a simple fake data model with an in memory uri and tempdata()
/// build directory.
pub fn fake_data_model() -> Arc<DataModel> {
    Arc::new(DataModel::connect(fake_model_config()).unwrap())
}
