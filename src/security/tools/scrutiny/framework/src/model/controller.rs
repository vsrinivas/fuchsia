// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::model::DataModel, anyhow::Result, serde_json::value::Value, std::sync::Arc};

/// The DataController trait is responsible for querying the data model.
pub trait DataController: Send + Sync {
    /// Takes an immutable copy of the model and some query specific to this
    /// controller and produces a custom result.
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value>;

    /// An optional one line description about what this DataController does.
    fn description(&self) -> String {
        "".to_string()
    }

    /// An optional long form usage description about what this DataController
    /// does.
    fn usage(&self) -> String {
        "No usage information available.".to_string()
    }
}
