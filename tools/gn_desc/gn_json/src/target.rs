// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Types used for parsing the `target` entries in `gn desc --format=json`
//! output.

use serde::Deserialize;
use serde_json::Value;
use std::{collections::HashMap, default::Default};

pub type AllTargets = HashMap<String, TargetDescription>;

#[derive(Clone, Debug, Deserialize, PartialEq)]
#[serde(untagged)]
pub enum Public {
    StringVal(String),
    ListVal(Vec<String>),
}

impl Default for Public {
    fn default() -> Public {
        Public::StringVal("*".to_owned())
    }
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq)]
#[serde(default)]
pub struct TargetDescription {
    #[serde(skip_serializing_if = "Option::is_none")]
    pub all_dependent_configs: Option<Vec<String>>,
    pub args: Vec<String>,
    pub deps: Vec<String>,
    pub metadata: HashMap<String, Value>,
    pub public: Public,
    pub testonly: bool,
    pub toolchain: String,

    #[serde(rename = "type")]
    pub target_type: String,

    #[serde(skip_serializing_if = "Option::is_none")]
    pub script: Option<String>,

    pub inputs: Vec<String>,
    pub sources: Vec<String>,
    pub outputs: Vec<String>,

    #[serde(rename = "configs")]
    pub config_targets: Vec<String>,

    #[serde(flatten)]
    pub config_values: ConfigValues,

    #[serde(skip_serializing_if = "Option::is_none")]
    pub crate_name: Option<String>,

    #[serde(skip_serializing_if = "Option::is_none")]
    pub crate_root: Option<String>,
}

#[derive(Clone, Debug, Default, Deserialize, PartialEq)]
#[serde(default)]
pub struct ConfigValues {
    pub arflags: Vec<String>,
    pub asmflags: Vec<String>,
    pub cflags: Vec<String>,
    pub cflags_c: Vec<String>,
    pub cflags_cc: Vec<String>,
    pub cflags_objc: Vec<String>,
    pub clfags_objcc: Vec<String>,
    pub defines: Vec<String>,
    pub include_dirs: Vec<String>,
    pub framework_dirs: Vec<String>,
    pub frameworks: Vec<String>,
    pub weak_frameworks: Vec<String>,
    pub ldflags: Vec<String>,
    pub lib_dirs: Vec<String>,
    pub libs: Vec<String>,
    pub rustflags: Vec<String>,
    pub rustenv: Vec<String>,
    pub swiftflags: Vec<String>,
    pub externs: HashMap<String, String>,
}

#[derive(Default)]
pub struct TargetDescriptionBuilder {
    inner: TargetDescription,
}

impl TargetDescriptionBuilder {
    pub fn build(self) -> TargetDescription {
        self.inner
    }

    pub fn deps(self, deps: Vec<impl ToString>) -> Self {
        Self { inner: TargetDescription { deps: collect_as_strings(deps), ..self.inner } }
    }

    pub fn all_dependent_configs(self, dep_configs: Vec<impl ToString>) -> Self {
        Self {
            inner: TargetDescription {
                all_dependent_configs: Some(collect_as_strings(dep_configs)),
                ..self.inner
            },
        }
    }

    pub fn target_type(self, target_type: impl ToString) -> Self {
        Self { inner: TargetDescription { target_type: target_type.to_string(), ..self.inner } }
    }

    pub fn toolchain(self, toolchain: impl ToString) -> Self {
        Self { inner: TargetDescription { toolchain: toolchain.to_string(), ..self.inner } }
    }
}

fn collect_as_strings(refs: Vec<impl ToString>) -> Vec<String> {
    refs.iter().map(|s| s.to_string()).collect()
}

#[cfg(test)]
mod tests {
    use super::*;
    use pretty_assertions::assert_eq;
    use serde_json::json;

    #[test]
    fn simple_gn_group_test() {
        let input = json!({
           "all_dependent_configs": [
               "//zircon/system/ulib/fidl:fidl-tracing-config(//build/toolchain/fuchsia:arm64-shared)",
               "//zircon/system/ulib/fidl:fidl-tracing-config" ],
           "deps": [
               "//build/info:build-info",
               "//garnet/bin/log_listener:log_listener",
               "//garnet/bin/log_listener:log_listener_shell",
               "//garnet/bin/setui:setui_service",
               "//garnet/bin/sshd-host:config",
               "//garnet/bin/sshd-host:sshd-host",
               "//garnet/bin/sysmgr:network_config",
               "//garnet/bin/sysmgr:services_config",
               "//garnet/bin/sysmgr:sysmgr"],
           "metadata": {

           },
           "public": "*",
           "toolchain": "//build/toolchain/fuchsia:arm64",
           "type": "group",
           "visibility": [ "//build/images:base_packages" ]
        });
        let expected = TargetDescriptionBuilder::default()
            .all_dependent_configs(vec![
                "//zircon/system/ulib/fidl:fidl-tracing-config(//build/toolchain/fuchsia:arm64-shared)",
                "//zircon/system/ulib/fidl:fidl-tracing-config"])
            .deps(vec![
                "//build/info:build-info",
                "//garnet/bin/log_listener:log_listener",
                "//garnet/bin/log_listener:log_listener_shell",
                "//garnet/bin/setui:setui_service",
                "//garnet/bin/sshd-host:config",
                "//garnet/bin/sshd-host:sshd-host",
                "//garnet/bin/sysmgr:network_config",
                "//garnet/bin/sysmgr:services_config",
                "//garnet/bin/sysmgr:sysmgr",
                ])
            .toolchain("//build/toolchain/fuchsia:arm64")
            .target_type("group")
            .build();
        let actual = serde_json::from_value(input).unwrap();

        assert_eq!(expected, actual);
    }

    #[test]
    fn simple_gn_all_targets_group_test() {
        let input = json!({
            "//additional_base_packages": {
                "all_dependent_configs": [
                    "//zircon/system/ulib/fidl:fidl-tracing-config(//build/toolchain/fuchsia:arm64-shared)",
                    "//zircon/system/ulib/fidl:fidl-tracing-config" ],
                "deps": [
                    "//build/info:build-info",
                    "//garnet/bin/log_listener:log_listener",
                    "//garnet/bin/log_listener:log_listener_shell",
                    "//garnet/bin/setui:setui_service",
                    "//garnet/bin/sshd-host:config",
                    "//garnet/bin/sshd-host:sshd-host",
                    "//garnet/bin/sysmgr:network_config",
                    "//garnet/bin/sysmgr:services_config",
                    "//garnet/bin/sysmgr:sysmgr"],
                    "metadata": {

               },
               "public": "*",
               "testonly": true,
               "toolchain": "//build/toolchain/fuchsia:arm64",
               "type": "group",
               "visibility": [ "//build/images:base_packages" ]
            },
        });
        let expected: AllTargets = vec![("//additional_base_packages".to_owned(), TargetDescription{
            all_dependent_configs: Some(collect_as_strings(vec![
                "//zircon/system/ulib/fidl:fidl-tracing-config(//build/toolchain/fuchsia:arm64-shared)",
                "//zircon/system/ulib/fidl:fidl-tracing-config"])),
            deps: collect_as_strings(vec![
                "//build/info:build-info",
                "//garnet/bin/log_listener:log_listener",
                "//garnet/bin/log_listener:log_listener_shell",
                "//garnet/bin/setui:setui_service",
                "//garnet/bin/sshd-host:config",
                "//garnet/bin/sshd-host:sshd-host",
                "//garnet/bin/sysmgr:network_config",
                "//garnet/bin/sysmgr:services_config",
                "//garnet/bin/sysmgr:sysmgr",
                ]),
            testonly: true,
            toolchain: "//build/toolchain/fuchsia:arm64".to_owned(),
            target_type: "group".to_owned(),
            ..TargetDescription::default()
        })].into_iter().collect();
        let actual = serde_json::from_value(input).unwrap();

        assert_eq!(expected, actual);
    }
}
