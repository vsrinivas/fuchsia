// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::Zbi,
    anyhow::Result,
    scrutiny::{model::controller::DataController, model::model::*},
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::sync::Arc,
};

#[derive(Default)]
pub struct VerifyBuildController {}

/// Defines the set of security properties that the `VerifyBuildController`
/// checks.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
struct SecurityProperties {
    pub zbi: ZbiProperties,
}

/// Defines the subset of security properties related to the ZBI that the
/// `VerifyBuildController` checks.
#[derive(Deserialize, Serialize, Debug, Clone, PartialEq, Eq)]
struct ZbiProperties {
    debug_syscalls_enabled: bool,
}

impl DataController for VerifyBuildController {
    /// Extracts information from the `DataModel` to determine what security
    /// properties are present in a given Fuchsia build. This is intended to
    /// be used by third party tests to inspect the security features
    /// present given build.
    fn query(&self, model: Arc<DataModel>, _value: Value) -> Result<Value> {
        let zbi = model.get::<Zbi>()?;
        Ok(json! {SecurityProperties {
            zbi: ZbiProperties {
                debug_syscalls_enabled: zbi.cmdline.contains("kernel.enable-debugging-syscalls=true"),
            },
        }})
    }

    fn description(&self) -> String {
        "Verifies the existence of security properties in a given build.".to_string()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::core::collection::Zbi, scrutiny_testing::fake::*,
        scrutiny_utils::zbi::ZbiSection, serde_json::json, std::collections::HashMap,
    };

    fn data_model() -> Arc<DataModel> {
        fake_data_model()
    }

    fn zbi() -> Zbi {
        let bootfs: HashMap<String, Vec<u8>> = HashMap::default();
        let sections: Vec<ZbiSection> = Vec::default();
        return Zbi { sections: sections, bootfs: bootfs, cmdline: "".to_string() };
    }

    #[test]
    fn test_zbi_cmdline_verify_no_debug_syscalls_exists() {
        let model = data_model();
        let zbi = Zbi { cmdline: "{kernel.enable-debugging-syscalls=false}".to_string(), ..zbi() };
        model.set(zbi).unwrap();
        let verify = VerifyBuildController::default();
        let response: SecurityProperties =
            serde_json::from_value(verify.query(model.clone(), json!("{}")).unwrap()).unwrap();
        assert_eq!(response.zbi.debug_syscalls_enabled, false);
    }

    #[test]
    fn test_zbi_cmdline_verify_debug_syscalls_exist() {
        let model = data_model();
        let zbi = Zbi { cmdline: "{kernel.enable-debugging-syscalls=true}".to_string(), ..zbi() };
        model.set(zbi).unwrap();
        let verify = VerifyBuildController::default();
        let response: SecurityProperties =
            serde_json::from_value(verify.query(model.clone(), json!("{}")).unwrap()).unwrap();
        assert_eq!(response.zbi.debug_syscalls_enabled, true);
    }
}
