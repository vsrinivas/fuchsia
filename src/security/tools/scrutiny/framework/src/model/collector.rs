// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {super::model::DataModel, anyhow::Result, std::sync::Arc};

/// The `DataCollector` trait is responsible for populating the `DataModel.`
pub trait DataCollector: Send + Sync {
    fn collect(&self, model: Arc<DataModel>) -> Result<()>;
}
