// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::zbi::collection::{BootFsCollection, CmdlineCollection},
    anyhow::{Context, Result},
    scrutiny::model::controller::DataController,
    scrutiny::prelude::DataModel,
    scrutiny_utils::usage::UsageBuilder,
    serde::{Deserialize, Serialize},
    serde_json::{json, Value},
    std::sync::Arc,
};

/// The request for querying the bootfs files.
#[derive(Deserialize, Serialize)]
pub struct BootFsRequest;

/// The controller for querying the bootfs files in a product.
#[derive(Default)]
pub struct BootFsController;

impl DataController for BootFsController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        Ok(json!(&*model
            .get::<BootFsCollection>()
            .context("Failed to read bootfs data from the BootFsCollector")?))
    }

    fn description(&self) -> String {
        "Extracts the bootfs files from a ZBI".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("bootfs - Extracts bootfs files")
            .summary("bootfs")
            .description("Extracts the bootfs files from a ZBI.")
            .build()
    }
}

/// The request for querying the kernel cmdline.
#[derive(Deserialize, Serialize)]
pub struct CmdlineRequest;

/// The controller for querying the kernel cmdline in a product.
#[derive(Default)]
pub struct CmdlineController;

impl DataController for CmdlineController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        Ok(json!(&*model
            .get::<CmdlineCollection>()
            .context("Failed to read the kernel cmdline from the CmdlineCollector")?))
    }

    fn description(&self) -> String {
        "Extracts the kernel cmdline from a zbi".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("cmdline - Extracts the kernel cmdline")
            .summary("cmdline")
            .description("Extracts the kernel cmdline from a ZBI.")
            .build()
    }
}
