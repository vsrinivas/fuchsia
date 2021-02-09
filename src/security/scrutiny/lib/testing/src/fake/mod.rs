// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    scrutiny::model::model::{DataModel, ModelEnvironment},
    std::sync::Arc,
    tempfile::tempdir,
};

/// Constructs a simple fake data model with a tempdata() uri and tempdata()
/// build directory.
pub fn fake_data_model() -> Arc<DataModel> {
    let store_tmp_dir = tempdir().unwrap();
    let build_tmp_dir = tempdir().unwrap();
    let uri = store_tmp_dir.into_path().into_os_string().into_string().unwrap();
    let build_path = build_tmp_dir.into_path();
    Arc::new(DataModel::connect(ModelEnvironment { uri, build_path }).unwrap())
}
