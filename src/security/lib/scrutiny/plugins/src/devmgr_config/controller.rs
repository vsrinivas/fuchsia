// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::devmgr_config::collection::DevmgrConfigCollection,
    anyhow::{Context, Result},
    scrutiny::{model::controller::DataController, model::model::*},
    scrutiny_utils::usage::UsageBuilder,
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::sync::Arc,
};

#[derive(Deserialize, Serialize)]
pub struct ExtractDevmgrConfigRequest;

#[derive(Default)]
pub struct ExtractDevmgrConfigController;

impl DataController for ExtractDevmgrConfigController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        Ok(json!(&*model.get::<DevmgrConfigCollection>().context(
            "Failed to read data modeled data from ZBI-extract-devmgr-config collector"
        )?))
    }

    fn description(&self) -> String {
        "Extracts the devmgr config from a ZBI".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("devmgr.config - Extracts devmgr config ")
            .summary("devmgr.config")
            .description(
                "Extracts zircon boot images and retrieves the devmgr config.
  Note: Path to ZBI file is loaded from model configuration (not as a
  controller parameter) because ZBI is loaded by a collector.",
            )
            .build()
    }
}
