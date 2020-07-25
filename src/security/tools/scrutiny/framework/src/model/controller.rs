// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::model::DataModel, anyhow::Result, serde_json::value::Value, std::sync::Arc};

/// The DataController trait is responsible for querying the data model.
pub trait DataController: Send + Sync {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value>;
}
