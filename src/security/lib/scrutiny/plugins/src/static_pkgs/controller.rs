// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::static_pkgs::collection::StaticPkgsCollection,
    anyhow::{Context, Result},
    scrutiny::{model::controller::DataController, model::model::DataModel},
    scrutiny_utils::usage::UsageBuilder,
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::sync::Arc,
};

#[derive(Deserialize, Serialize)]
pub struct ExtractStaticPkgsRequest;

#[derive(Default)]
pub struct ExtractStaticPkgsController;

impl DataController for ExtractStaticPkgsController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        Ok(json!(&*model
            .get::<StaticPkgsCollection>()
            .context("Failed to read modeled data from static packages collector")?))
    }

    fn description(&self) -> String {
        "Extracts static packages listing from system image artifacts".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("static.pkgs - Extracts static packages listing")
            .summary("static.pkgs")
            .description(
                "Extracts static packages listing from system image artifacts.
Note: Path to blob manifest used to follow hashes to artifacts is loaded from
model configuration (not as a controller parameter) because all data is loaded
by collectors.",
            )
            .build()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::ExtractStaticPkgsController,
        crate::static_pkgs::collection::{StaticPkgsCollection, StaticPkgsError},
        anyhow::{Context, Result},
        fuchsia_hash::Hash,
        fuchsia_url::{PackageName, PackageVariant},
        maplit::{hashmap, hashset},
        scrutiny::model::controller::DataController,
        scrutiny_testing::fake::*,
        serde_json::{json, value::Value},
        std::str::FromStr,
    };

    #[fuchsia::test]
    fn test_err_results() -> Result<()> {
        let model = fake_data_model();
        model
            .set(StaticPkgsCollection {
                deps: hashset! {},
                static_pkgs: None,
                errors: vec![StaticPkgsError::MalformedSystemImageHash {
                    actual_hash: "0000000000000000000000000000000000000000000000000000000000000000"
                        .to_string(),
                }],
            })
            .context("Failed to put static pkgs data into data model")?;
        let controller = ExtractStaticPkgsController;
        let result = controller.query(model, Value::Null).context("Controller query failed")?;
        assert_eq!(
            result,
            json!(StaticPkgsCollection {
                deps: hashset! {},
                static_pkgs: None,
                errors: vec![StaticPkgsError::MalformedSystemImageHash {
                    actual_hash: "0000000000000000000000000000000000000000000000000000000000000000"
                        .to_string()
                }],
            })
        );
        Ok(())
    }

    #[fuchsia::test]
    fn test_some_results() {
        let model = fake_data_model();
        let static_pkgs_collection = StaticPkgsCollection {
            deps: hashset! {},
            static_pkgs: Some(hashmap! {
                (PackageName::from_str("alpha").unwrap(), Some(PackageVariant::zero())) =>
                    Hash::from_str("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef").unwrap(),
                (PackageName::from_str("beta").unwrap(), Some(PackageVariant::zero())) =>
                Hash::from_str("fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210").unwrap(),
            }),
            errors: vec![],
        };
        let expected_value = serde_json::to_value(&static_pkgs_collection).unwrap();
        model.set(static_pkgs_collection).unwrap();
        let controller = ExtractStaticPkgsController;
        let result = controller.query(model, Value::Null).unwrap();
        assert_eq!(result, expected_value);
    }
}
