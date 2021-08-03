// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    scrutiny::model::model::{DataModel, ModelEnvironment},
    std::sync::Arc,
    tempfile::tempdir,
};

/// Constructs a simple fake data model with an in memory uri and tempdata()
/// build directory.
pub fn fake_data_model() -> Arc<DataModel> {
    Arc::new(
        DataModel::connect(ModelEnvironment {
            uri: "{memory}".to_string(),
            build_path: tempdir().unwrap().into_path(),
            repository_path: tempdir().unwrap().into_path(),
        })
        .unwrap(),
    )
}
