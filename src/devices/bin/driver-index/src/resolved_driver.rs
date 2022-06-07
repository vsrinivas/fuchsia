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
    fidl_fuchsia_driver_index as fdi, fidl_fuchsia_io as fio,
    futures::TryFutureExt,
};

// Cached drivers don't exist yet so we allow dead code.
#[derive(Copy, Clone, Debug)]
#[allow(dead_code)]
pub enum DriverPackageType {
    Boot = 0,
    Base = 1,
    Cached = 2,
    Universe = 3,
}

// TODO(fxbug.dev/92329): Use `fallback` and remove dead code attribute.
#[allow(dead_code)]
pub struct ResolvedDriver {
    pub component_url: url::Url,
    pub v1_driver_path: Option<String>,
    pub bind_rules: DecodedRules,
    pub bind_bytecode: Vec<u8>,
    pub colocate: bool,
    pub fallback: bool,
    pub package_type: DriverPackageType,
}

impl std::fmt::Display for ResolvedDriver {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", &self.component_url)
    }
}

impl ResolvedDriver {
    pub fn get_libname(&self) -> String {
        let mut libname = self.component_url.clone();
        libname.set_fragment(self.v1_driver_path.as_deref());
        libname.into_string()
    }

    pub async fn resolve(
        component_url: url::Url,
        resolver: &fidl_fuchsia_pkg::PackageResolverProxy,
        package_type: DriverPackageType,
    ) -> Result<ResolvedDriver, fuchsia_zircon::Status> {
        let (dir, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .map_err(|e| {
                log::warn!("Failed to create DirectoryMarker Proxy: {}", e);
                fuchsia_zircon::Status::INTERNAL
            })?;
        let mut base_url = component_url.clone();
        base_url.set_fragment(None);

        let res = resolver
            .resolve(&base_url.as_str(), dir_server_end)
            .map_err(|e| {
                log::warn!("Resolve call failed: {}", e);
                fuchsia_zircon::Status::INTERNAL
            })
            .await?;

        res.map_err(|e| {
            log::warn!("{}: Failed to resolve package: {:?}", component_url.as_str(), e);
            map_resolve_err_to_zx_status(e)
        })?;

        let driver = load_driver(&dir, component_url.clone(), package_type)
            .map_err(|e| {
                log::warn!("Could not load driver: {}", e);
                fuchsia_zircon::Status::INTERNAL
            })
            .await?;

        return driver.ok_or_else(|| {
            log::warn!("{}: Component was not a driver-component", component_url.as_str());
            fuchsia_zircon::Status::INTERNAL
        });
    }

    pub fn matches(
        &self,
        properties: &DeviceProperties,
    ) -> Result<Option<fdi::MatchedDriver>, bind::interpreter::common::BytecodeError> {
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

                Ok(Some(fdi::MatchedDriver::Driver(self.create_matched_driver_info())))
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

                let mut node_names = vec![];
                node_names.push(composite.symbol_table[&composite.primary_node.name_id].clone());
                for node in &composite.additional_nodes {
                    node_names.push(composite.symbol_table[&node.name_id].clone());
                }

                Ok(Some(fdi::MatchedDriver::CompositeDriver(fdi::MatchedCompositeInfo {
                    node_index: Some(node_index),
                    num_nodes: Some((composite.additional_nodes.len() + 1) as u32),
                    composite_name: Some(composite.symbol_table[&composite.device_name_id].clone()),
                    node_names: Some(node_names),
                    driver_info: Some(self.create_matched_driver_info()),
                    ..fdi::MatchedCompositeInfo::EMPTY
                })))
            }
        }
    }

    fn get_driver_url(&self) -> Option<String> {
        match self.v1_driver_path.as_ref() {
            Some(p) => {
                let mut driver_url = self.component_url.clone();
                driver_url.set_fragment(Some(p));
                Some(driver_url.to_string())
            }
            None => None,
        }
    }

    fn create_matched_driver_info(&self) -> fdi::MatchedDriverInfo {
        fdi::MatchedDriverInfo {
            url: Some(self.component_url.as_str().to_string()),
            driver_url: self.get_driver_url(),
            colocate: Some(self.colocate),
            package_type: fdi::DriverPackageType::from_primitive(self.package_type as u8),
            is_fallback: Some(self.fallback),
            ..fdi::MatchedDriverInfo::EMPTY
        }
    }

    pub fn create_driver_info(&self) -> fdd::DriverInfo {
        let bind_rules = match &self.bind_rules {
            DecodedRules::Normal(_) => {
                Some(fdd::BindRulesBytecode::BytecodeV2(self.bind_bytecode.clone()))
            }
            // TODO(fxbug.dev/85651): Support composite bytecode in DriverInfo.
            DecodedRules::Composite(_) => None,
        };
        fdd::DriverInfo {
            url: Some(self.component_url.clone().to_string()),
            libname: Some(self.get_libname()),
            bind_rules: bind_rules,
            package_type: fdi::DriverPackageType::from_primitive(self.package_type as u8),
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
    package_type: DriverPackageType,
) -> Result<Option<ResolvedDriver>, anyhow::Error> {
    let component = io_util::open_file(
        &dir,
        std::path::Path::new(
            component_url
                .fragment()
                .ok_or(anyhow::anyhow!("{}: URL is missing fragment", component_url.as_str()))?,
        ),
        fio::OpenFlags::RIGHT_READABLE,
    )?;
    let component: fdecl::Component = io_util::read_file_fidl(&component)
        .await
        .with_context(|| format!("{}: Failed to read component", component_url.as_str()))?;
    let component: cm_rust::ComponentDecl = component.fidl_into_native();

    let runner = match component.get_runner() {
        Some(r) => r,
        None => return Ok(None),
    };
    if runner.str() != "driver" {
        return Ok(None);
    }

    let bind_path = get_rules_string_value(&component, "bind")
        .ok_or(anyhow::anyhow!("{}: Missing bind path", component_url.as_str()))?;
    let bind =
        io_util::open_file(&dir, std::path::Path::new(&bind_path), fio::OpenFlags::RIGHT_READABLE)
            .with_context(|| format!("{}: Failed to open bind", component_url.as_str()))?;
    let bind = io_util::read_file_bytes(&bind)
        .await
        .with_context(|| format!("{}: Failed to read bind", component_url.as_str()))?;
    let bind_rules = DecodedRules::new(bind.clone())
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
        bind_rules: bind_rules,
        bind_bytecode: bind,
        colocate: colocate,
        fallback,
        package_type,
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

fn map_resolve_err_to_zx_status(
    resolve_error: fidl_fuchsia_pkg::ResolveError,
) -> fuchsia_zircon::Status {
    match resolve_error {
        fidl_fuchsia_pkg::ResolveError::Internal => fuchsia_zircon::Status::INTERNAL,
        fidl_fuchsia_pkg::ResolveError::AccessDenied => fuchsia_zircon::Status::ACCESS_DENIED,
        fidl_fuchsia_pkg::ResolveError::Io => fuchsia_zircon::Status::IO,
        fidl_fuchsia_pkg::ResolveError::BlobNotFound => fuchsia_zircon::Status::NOT_FOUND,
        fidl_fuchsia_pkg::ResolveError::PackageNotFound => fuchsia_zircon::Status::NOT_FOUND,
        fidl_fuchsia_pkg::ResolveError::RepoNotFound => fuchsia_zircon::Status::NOT_FOUND,
        fidl_fuchsia_pkg::ResolveError::NoSpace => fuchsia_zircon::Status::NO_SPACE,
        fidl_fuchsia_pkg::ResolveError::UnavailableBlob => fuchsia_zircon::Status::UNAVAILABLE,
        fidl_fuchsia_pkg::ResolveError::UnavailableRepoMetadata => {
            fuchsia_zircon::Status::UNAVAILABLE
        }
        fidl_fuchsia_pkg::ResolveError::InvalidUrl => fuchsia_zircon::Status::INVALID_ARGS,
    }
}
