// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Context},
    bind::interpreter::{
        common::BytecodeError,
        decode_bind_rules::{DecodedCompositeBindRules, DecodedRules},
        match_bind::{match_bind, DeviceProperties, MatchBindData},
    },
    cm_rust::FidlIntoNative,
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_driver_development as fdd,
    fidl_fuchsia_driver_framework as fdf, fidl_fuchsia_io as fio,
};

// TODO(fxbug.dev/92329): Use `fallback` and remove dead code attribute.
#[allow(dead_code)]
pub struct ResolvedDriver {
    pub component_url: url::Url,
    pub v1_driver_path: Option<String>,
    pub bind_rules: DecodedRules,
    pub colocate: bool,
    pub fallback: bool,
}

impl std::fmt::Display for ResolvedDriver {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", &self.component_url)
    }
}

impl ResolvedDriver {
    pub async fn resolve(
        component_url: url::Url,
        resolver: &fidl_fuchsia_pkg::PackageResolverProxy,
    ) -> Result<ResolvedDriver, anyhow::Error> {
        let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()?;
        let mut base_url = component_url.clone();
        base_url.set_fragment(None);

        let res = resolver.resolve(&base_url.as_str(), dir_server_end);
        res.await?.map_err(|e| {
            anyhow::anyhow!("{}: Failed to resolve package: {:?}", component_url.as_str(), e)
        })?;

        let driver = load_driver(&dir, component_url.clone()).await?;

        return driver.ok_or(anyhow::anyhow!(
            "{}: Component was not a driver-component",
            component_url.as_str()
        ));
    }

    pub fn matches(
        &self,
        properties: &DeviceProperties,
    ) -> Result<Option<fdf::MatchedDriver>, bind::interpreter::common::BytecodeError> {
        match &self.bind_rules {
            DecodedRules::Normal(rules) => {
                let matches = match_bind(
                    MatchBindData {
                        symbol_table: &rules.symbol_table,
                        instructions: &rules.instructions,
                    },
                    properties,
                )
                .map_err(|e| {
                    log::error!("Driver {}: bind error: {}", self, e);
                    e
                })?;

                if !matches {
                    return Ok(None);
                }

                Ok(Some(self.create_matched_driver()))
            }
            DecodedRules::Composite(composite) => {
                let result = matches_composite_device(composite, properties).map_err(|e| {
                    log::error!("Driver {}: bind error: {}", self, e);
                    e
                })?;

                if result.is_none() {
                    return Ok(None);
                }

                let node_index = result.unwrap();
                let mut matched_driver = self.create_matched_driver();
                matched_driver.node_index = Some(node_index);
                matched_driver.num_nodes = Some((composite.additional_nodes.len() + 1) as u32);
                matched_driver.composite_name =
                    Some(composite.symbol_table[&composite.device_name_id].clone());

                let mut node_names = vec![];
                node_names.push(composite.symbol_table[&composite.primary_node.name_id].clone());
                for node in &composite.additional_nodes {
                    node_names.push(composite.symbol_table[&node.name_id].clone());
                }
                matched_driver.composite_node_names = Some(node_names);

                Ok(Some(matched_driver))
            }
        }
    }

    fn create_matched_driver(&self) -> fdf::MatchedDriver {
        let driver_url = match self.v1_driver_path.as_ref() {
            Some(p) => {
                let mut driver_url = self.component_url.clone();
                driver_url.set_fragment(Some(p));
                Some(driver_url.to_string())
            }
            None => None,
        };

        fdf::MatchedDriver {
            url: Some(self.component_url.as_str().to_string()),
            driver_url: driver_url,
            colocate: Some(self.colocate),
            ..fdf::MatchedDriver::EMPTY
        }
    }

    pub fn create_driver_info(&self) -> fdd::DriverInfo {
        let bind_rules = match &self.bind_rules {
            DecodedRules::Normal(rules) => {
                Some(fdd::BindRulesBytecode::BytecodeV2(rules.instructions.clone()))
            }
            // TODO(fxbug.dev/85651): Support composite bytecode in DriverInfo.
            DecodedRules::Composite(_) => None,
        };
        let mut libname = self.component_url.clone();
        libname.set_fragment(self.v1_driver_path.as_deref());
        fdd::DriverInfo {
            url: Some(self.component_url.clone().to_string()),
            libname: Some(libname.to_string()),
            bind_rules: bind_rules,
            ..fdd::DriverInfo::EMPTY
        }
    }
}

fn matches_composite_device(
    rules: &DecodedCompositeBindRules,
    properties: &DeviceProperties,
) -> Result<Option<u32>, BytecodeError> {
    let matches = match_bind(
        MatchBindData {
            symbol_table: &rules.symbol_table,
            instructions: &rules.primary_node.instructions,
        },
        properties,
    )?;

    if matches {
        return Ok(Some(0));
    }

    for (i, node) in rules.additional_nodes.iter().enumerate() {
        let matches = match_bind(
            MatchBindData { symbol_table: &rules.symbol_table, instructions: &node.instructions },
            properties,
        )?;
        if matches {
            return Ok(Some((i + 1) as u32));
        }
    }
    Ok(None)
}

// Load the `component_url` driver out of `dir` which should be the root directory
// of that component. Will return Ok(None) if `component_url` is a valid component
// but it's not a driver component.
pub async fn load_driver(
    dir: &fio::DirectoryProxy,
    component_url: url::Url,
) -> Result<Option<ResolvedDriver>, anyhow::Error> {
    let component = io_util::open_file(
        &dir,
        std::path::Path::new(
            component_url
                .fragment()
                .ok_or(anyhow::anyhow!("{}: URL is missing fragment", component_url.as_str()))?,
        ),
        fio::OPEN_RIGHT_READABLE,
    )?;
    let component: fdecl::Component = io_util::read_file_fidl(&component)
        .await
        .with_context(|| format!("{}: Failed to read component", component_url.as_str()))?;
    let component = component.fidl_into_native();

    let runner = match component.get_runner() {
        Some(r) => r,
        None => return Ok(None),
    };
    if runner.str() != "driver" {
        return Ok(None);
    }

    let bind_path = get_rules_string_value(&component, "bind")
        .ok_or(anyhow::anyhow!("{}: Missing bind path", component_url.as_str()))?;
    let bind = io_util::open_file(&dir, std::path::Path::new(&bind_path), fio::OPEN_RIGHT_READABLE)
        .with_context(|| format!("{}: Failed to open bind", component_url.as_str()))?;
    let bind = io_util::read_file_bytes(&bind)
        .await
        .with_context(|| format!("{}: Failed to read bind", component_url.as_str()))?;
    let bind = DecodedRules::new(bind)
        .with_context(|| format!("{}: Failed to parse bind", component_url.as_str()))?;

    let v1_driver_path = get_rules_string_value(&component, "compat");
    let fallback = get_rules_string_value(&component, "fallback");
    let fallback = match fallback {
        Some(s) => s == "true",
        None => false,
    };
    let colocate = get_rules_string_value(&component, "colocate");
    let colocate = match colocate {
        Some(s) => s == "true",
        None => false,
    };

    Ok(Some(ResolvedDriver {
        component_url: component_url,
        v1_driver_path: v1_driver_path,
        bind_rules: bind,
        colocate: colocate,
        fallback,
    }))
}

fn get_rules_string_value(component: &cm_rust::ComponentDecl, key: &str) -> Option<String> {
    for entry in component.program.as_ref()?.info.entries.as_ref()? {
        if entry.key == key {
            match entry.value.as_ref()?.as_ref() {
                fidl_fuchsia_data::DictionaryValue::Str(s) => {
                    return Some(s.to_string());
                }
                _ => {
                    return None;
                }
            }
        }
    }
    return None;
}
