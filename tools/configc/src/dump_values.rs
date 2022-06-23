// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{Context, Error};
use argh::FromArgs;
use cm_rust::FidlIntoNative;
use fidl::encoding::decode_persistent;
use fidl_fuchsia_component_config as fconfig;
use std::{collections::BTreeMap, path::PathBuf};

/// dump configuration values for a component in a human-readable format
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "dump-values")]
pub struct DumpValues {
    /// path to the compiled manifest
    #[argh(option)]
    cm: PathBuf,

    /// path to the configuration value file
    #[argh(option)]
    cvf: PathBuf,

    /// path to the JSON output, print to stdout if not provided
    #[argh(option)]
    output: Option<PathBuf>,
}

impl DumpValues {
    pub fn dump(self) -> Result<(), Error> {
        let DumpValues { cm, cvf, output } = self;
        let decl = crate::common::load_manifest(&cm).context("loading compiled manifest")?;
        let config_decl = decl.config.context("component must declare a config schema")?;

        let cvf_bytes =
            std::fs::read(&cvf).with_context(|| format!("reading {}", cvf.display()))?;
        let values: fconfig::ValuesData =
            decode_persistent(&cvf_bytes).context("decoding value file")?;
        let values = values.fidl_into_native();

        let resolved = config_encoder::ConfigFields::resolve(&config_decl, values)
            .context("resolving config values")?;

        let fields_map = resolved
            .fields
            .into_iter()
            .map(|f| (f.key, json_value_from_config_value(f.value)))
            .collect::<BTreeMap<_, _>>();

        let for_humans = serde_json::to_string_pretty(&fields_map)
            .context("serializing config values as json")?;

        if let Some(output) = output {
            std::fs::write(&output, for_humans)
                .with_context(|| format!("writing to {}", output.display()))?;
        } else {
            println!("{}", for_humans);
        }

        Ok(())
    }
}

fn json_value_from_config_value(v: cm_rust::Value) -> serde_json::Value {
    match v {
        cm_rust::Value::Single(s) => match s {
            cm_rust::SingleValue::Bool(b) => serde_json::Value::Bool(b),
            cm_rust::SingleValue::Uint8(n) => serde_json::Value::Number(n.into()),
            cm_rust::SingleValue::Uint16(n) => serde_json::Value::Number(n.into()),
            cm_rust::SingleValue::Uint32(n) => serde_json::Value::Number(n.into()),
            cm_rust::SingleValue::Uint64(n) => serde_json::Value::Number(n.into()),
            cm_rust::SingleValue::Int8(n) => serde_json::Value::Number(n.into()),
            cm_rust::SingleValue::Int16(n) => serde_json::Value::Number(n.into()),
            cm_rust::SingleValue::Int32(n) => serde_json::Value::Number(n.into()),
            cm_rust::SingleValue::Int64(n) => serde_json::Value::Number(n.into()),
            cm_rust::SingleValue::String(s) => serde_json::Value::String(s),
        },
        cm_rust::Value::Vector(v) => match v {
            cm_rust::VectorValue::BoolVector(bv) => serde_json::Value::Array(
                bv.into_iter().map(|b| serde_json::Value::Bool(b)).collect(),
            ),
            cm_rust::VectorValue::Uint8Vector(nv) => serde_json::Value::Array(
                nv.into_iter().map(|n| serde_json::Value::Number(n.into())).collect(),
            ),
            cm_rust::VectorValue::Uint16Vector(nv) => serde_json::Value::Array(
                nv.into_iter().map(|n| serde_json::Value::Number(n.into())).collect(),
            ),
            cm_rust::VectorValue::Uint32Vector(nv) => serde_json::Value::Array(
                nv.into_iter().map(|n| serde_json::Value::Number(n.into())).collect(),
            ),
            cm_rust::VectorValue::Uint64Vector(nv) => serde_json::Value::Array(
                nv.into_iter().map(|n| serde_json::Value::Number(n.into())).collect(),
            ),
            cm_rust::VectorValue::Int8Vector(nv) => serde_json::Value::Array(
                nv.into_iter().map(|n| serde_json::Value::Number(n.into())).collect(),
            ),
            cm_rust::VectorValue::Int16Vector(nv) => serde_json::Value::Array(
                nv.into_iter().map(|n| serde_json::Value::Number(n.into())).collect(),
            ),
            cm_rust::VectorValue::Int32Vector(nv) => serde_json::Value::Array(
                nv.into_iter().map(|n| serde_json::Value::Number(n.into())).collect(),
            ),
            cm_rust::VectorValue::Int64Vector(nv) => serde_json::Value::Array(
                nv.into_iter().map(|n| serde_json::Value::Number(n.into())).collect(),
            ),
            cm_rust::VectorValue::StringVector(sv) => serde_json::Value::Array(
                sv.into_iter().map(|s| serde_json::Value::String(s)).collect(),
            ),
        },
    }
}
