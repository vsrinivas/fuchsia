// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::verify::collection::V2ComponentModel;
use anyhow::{Context as _, Result};
use config_encoder::ConfigFields;
use scrutiny::prelude::*;
use serde::{Deserialize, Serialize};
use std::{
    collections::{BTreeMap, HashSet},
    path::PathBuf,
    sync::Arc,
};

/// A controller to extract all of the configuration values in a given build's component topology.
#[derive(Default)]
pub struct ExtractStructuredConfigController {}

impl DataController for ExtractStructuredConfigController {
    fn query(&self, model: Arc<DataModel>, _query: serde_json::Value) -> Result<serde_json::Value> {
        let V2ComponentModel { component_model, deps, .. } =
            &*model.get::<V2ComponentModel>().context("getting component model")?;
        let config_by_url = component_model
            .collect_config_by_url()
            .context("collecting configuration from model")?;

        let components =
            config_by_url.into_iter().map(|(url, fields)| (url, fields.into())).collect();
        Ok(serde_json::json!(ExtractStructuredConfigResponse { components, deps: deps.to_owned() }))
    }
}

/// Configuration extracted from a particular component topology.
#[derive(Debug, Deserialize, Serialize)]
pub struct ExtractStructuredConfigResponse {
    /// A map from component URL to configuration values.
    pub components: BTreeMap<String, ComponentConfig>,
    /// Files read in the process of extracting configuration, for build integration.
    pub deps: HashSet<PathBuf>,
}

/// Configuration for a single component.
#[derive(Debug, Deserialize, Serialize)]
pub struct ComponentConfig {
    #[serde(flatten)]
    pub fields: BTreeMap<String, serde_json::Value>,
}

impl From<ConfigFields> for ComponentConfig {
    fn from(fields: ConfigFields) -> Self {
        Self {
            fields: fields
                .fields
                .into_iter()
                .map(|field| (field.key, config_value_to_json_value(field.value)))
                .collect(),
        }
    }
}

// We can't make this the behavior of Serialize because then we wouldn't be able to Deserialize
// as a round-trip, and we can't add this as an Into impl on cm_rust::Value because we don't want
// to add a serde_json dependency to that crate.
fn config_value_to_json_value(value: cm_rust::Value) -> serde_json::Value {
    use cm_rust::{SingleValue, Value, VectorValue};
    match value {
        Value::Single(sv) => match sv {
            SingleValue::Bool(b) => b.into(),
            SingleValue::Uint8(n) => n.into(),
            SingleValue::Uint16(n) => n.into(),
            SingleValue::Uint32(n) => n.into(),
            SingleValue::Uint64(n) => n.into(),
            SingleValue::Int8(n) => n.into(),
            SingleValue::Int16(n) => n.into(),
            SingleValue::Int32(n) => n.into(),
            SingleValue::Int64(n) => n.into(),
            SingleValue::String(s) => s.into(),
        },
        Value::Vector(vv) => match vv {
            VectorValue::BoolVector(bv) => bv.into(),
            VectorValue::Uint8Vector(nv) => nv.into(),
            VectorValue::Uint16Vector(nv) => nv.into(),
            VectorValue::Uint32Vector(nv) => nv.into(),
            VectorValue::Uint64Vector(nv) => nv.into(),
            VectorValue::Int8Vector(nv) => nv.into(),
            VectorValue::Int16Vector(nv) => nv.into(),
            VectorValue::Int32Vector(nv) => nv.into(),
            VectorValue::Int64Vector(nv) => nv.into(),
            VectorValue::StringVector(sv) => sv.into(),
        },
    }
}
