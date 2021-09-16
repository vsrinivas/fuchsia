// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::static_pkgs::collection::StaticPkgsCollection,
    anyhow::{Context, Result},
    scrutiny::{
        model::controller::{ConnectionMode, DataController},
        model::model::DataModel,
    },
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

    /// ExtractStaticPkgs is only available to the local shell.
    fn connection_mode(&self) -> ConnectionMode {
        ConnectionMode::Local
    }
}

#[cfg(test)]
mod tests {
    use {
        super::ExtractStaticPkgsController,
        crate::static_pkgs::collection::{StaticPkgsCollection, StaticPkgsError},
        anyhow::{Context, Result},
        maplit::hashmap,
        scrutiny::model::controller::DataController,
        scrutiny::prelude::DataCollection,
        scrutiny_testing::fake::*,
        serde::{Deserialize, Serialize},
        serde_json::{json, value::Value},
        uuid::Uuid,
    };

    #[derive(Deserialize, Serialize)]
    pub struct EvilStaticPkgsCollection {
        pub incompatible_with_real_static_pkgs_collection: String,
    }

    impl DataCollection for EvilStaticPkgsCollection {
        fn uuid() -> Uuid {
            Uuid::parse_str("b55d0f7f-b776-496c-83a3-63a6745a3a71").unwrap()
        }
        fn collection_name() -> String {
            "Evil static pkgs".to_string()
        }
        fn collection_description() -> String {
            "Same uuid as static pkgs collection, but incompatible format".to_string()
        }
    }

    #[test]
    fn test_err_results() -> Result<()> {
        let model = fake_data_model();
        model
            .set(StaticPkgsCollection {
                deps: vec![],
                static_pkgs: None,
                errors: vec![StaticPkgsError::StaticPkgsPathInvalid {
                    static_pkgs_path: "".to_string(),
                }],
            })
            .context("Failed to put static pkgs data into data model")?;
        let controller = ExtractStaticPkgsController;
        let result = controller.query(model, Value::Null).context("Controller query failed")?;
        assert_eq!(
            result,
            json!(StaticPkgsCollection {
                deps: vec![],
                static_pkgs: None,
                errors: vec![StaticPkgsError::StaticPkgsPathInvalid {
                    static_pkgs_path: "".to_string()
                },],
            })
        );
        Ok(())
    }

    #[test]
    fn test_some_results() -> Result<()> {
        let model = fake_data_model();
        model.set(StaticPkgsCollection{
            deps: vec![],
            static_pkgs: Some(hashmap!{
                "alpha/0".to_string() =>
                    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef".to_string(),
                "beta/0".to_string() =>
                    "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210".to_string(),
            }),
            errors: vec![],
        }).context("Failed to put static pkgs data into data model")?;
        let controller = ExtractStaticPkgsController;
        let result = controller.query(model, Value::Null).context("Controller query failed")?;
        assert_eq!(
            result,
            json!(StaticPkgsCollection {
                deps: vec![],
                static_pkgs: Some(hashmap! {
                    "alpha/0".to_string() =>
                        "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef".to_string(),
                    "beta/0".to_string() =>
                        "fedcba9876543210fedcba9876543210fedcba9876543210fedcba9876543210".to_string(),
                }),
                errors: vec![],
            })
        );
        Ok(())
    }

    #[test]
    fn test_results_extraction_failure() -> Result<()> {
        let model = fake_data_model();
        model
            .set(EvilStaticPkgsCollection {
                incompatible_with_real_static_pkgs_collection: "Muahahaha!".to_string(),
            })
            .context("Failed to put evil static pkgs data into data model")?;
        let controller = ExtractStaticPkgsController;
        let result = controller.query(model, Value::Null);
        assert!(result.is_err());
        Ok(())
    }
}
