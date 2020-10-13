// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::model::DataModel, anyhow::Result, serde_json::value::Value, std::sync::Arc};

/// The type of connection that is allowed to access this data controller.
#[derive(PartialEq, Eq, PartialOrd, Clone, Copy)]
pub enum ConnectionMode {
    Local = 1,
    Remote = 2,
}

/// The data type of the hint improving the intelligence of the hinter about how
/// to fill in the next parameter.
#[derive(Copy, Clone)]
pub enum HintDataType {
    // There is no hintable data type to match this hint type.
    NoType,
    // This parameter refers to a file and so we should hint files.
    File,
    // This parameter refers to a package url so we should hint package urls.
    PackageUrl,
}

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

    /// An optional configuration that prevents this controller being called
    /// over the remote API. This is important to set if the query in any way
    /// modifies the local system.
    fn connection_mode(&self) -> ConnectionMode {
        ConnectionMode::Remote
    }

    /// Returns a vector of parameter hints for the command line shell followed
    /// by an optional hinter type like a file or a url etc.
    fn hints(&self) -> Vec<(String, HintDataType)> {
        Vec::new()
    }
}
