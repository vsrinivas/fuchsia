// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        core::{
            collection::{Components, CoreDataDeps, ManifestData, Manifests, Zbi},
            package::collector::ROOT_RESOURCE,
        },
        verify::collection::V2ComponentModel,
    },
    anyhow::{anyhow, Context, Result},
    cm_fidl_analyzer::{component_model::ModelBuilderForAnalyzer, node_path::NodePath},
    cm_rust::{ComponentDecl, FidlIntoNative, RegistrationSource, RunnerRegistration},
    config_encoder::ConfigFields,
    fidl::encoding::decode_persistent,
    fidl_fuchsia_component_config as fconfig, fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_component_internal as component_internal,
    fuchsia_url::{boot_url::BootUrl, AbsoluteComponentUrl},
    once_cell::sync::Lazy,
    routing::{
        component_id_index::ComponentIdIndex, config::RuntimeConfig, environment::RunnerRegistry,
    },
    scrutiny::model::{collector::DataCollector, model::DataModel},
    serde::{Deserialize, Serialize},
    serde_json5::from_reader,
    std::{collections::HashMap, convert::TryFrom, fs::File, path::PathBuf, sync::Arc},
    tracing::{error, info, warn},
    url::Url,
};

// The default root component URL used to identify the root instance of the component model
// unless the RuntimeConfig specifies a different root URL.
pub static DEFAULT_ROOT_URL: Lazy<Url> = Lazy::new(|| {
    Url::parse(
        &BootUrl::new_resource("/".to_string(), ROOT_RESOURCE.to_string()).unwrap().to_string(),
    )
    .unwrap()
});

// The path to the runtime config in bootfs.
pub const DEFAULT_CONFIG_PATH: &str = "config/component_manager";

// The name of the ELF runner.
pub const ELF_RUNNER_NAME: &str = "elf";
// The name of the RealmBuilder runner.
pub const REALM_BUILDER_RUNNER_NAME: &str = "realm_builder";

#[derive(Deserialize, Serialize)]
pub struct DynamicComponent {
    pub url: AbsoluteComponentUrl,
    pub environment: Option<String>,
}

#[derive(Deserialize, Serialize)]
pub struct ComponentTreeConfig {
    pub dynamic_components: HashMap<NodePath, DynamicComponent>,
}

pub struct V2ComponentModelDataCollector {}

impl V2ComponentModelDataCollector {
    pub fn new() -> Self {
        Self {}
    }

    fn get_decls(
        &self,
        model: &Arc<DataModel>,
    ) -> Result<HashMap<Url, (ComponentDecl, Option<ConfigFields>)>> {
        let mut decls = HashMap::new();
        let mut urls = HashMap::new();

        let components =
            model.get::<Components>().context("Unable to retrieve components from the model")?;
        for component in components.entries.iter().filter(|x| x.version == 2) {
            urls.insert(component.id, component.url.clone());
        }

        for manifest in model
            .get::<Manifests>()
            .context("Unable to retrieve manifests from the model")?
            .entries
            .iter()
        {
            if let ManifestData::Version2 { cm_base64, cvf_bytes } = &manifest.manifest {
                match urls.remove(&manifest.component_id) {
                    Some(url) => {
                        let result: Result<fdecl::Component, fidl::Error> = decode_persistent(
                            &base64::decode(&cm_base64)
                                .context("Unable to decode base64 v2 manifest")?,
                        );
                        match result {
                            Ok(decl) => {
                                let decl = decl.fidl_into_native();
                                let config = if let Some(schema) = &decl.config {
                                    let cvf_bytes = cvf_bytes
                                        .as_ref()
                                        .context("getting config values to match schema")?;
                                    let values_data =
                                        decode_persistent::<fconfig::ValuesData>(cvf_bytes)
                                            .context("decoding config values")?
                                            .fidl_into_native();
                                    let resolved = ConfigFields::resolve(schema, values_data)
                                        .context("resolving configuration")?;
                                    Some(resolved)
                                } else {
                                    None
                                };

                                decls.insert(url, (decl, config));
                            }
                            Err(err) => {
                                error!(
                                    %err,
                                    %url,
                                    "Manifest for component is corrupted"
                                );
                            }
                        }
                    }
                    None => {
                        return Err(anyhow!(
                            "No component URL found for v2 component with id {}",
                            manifest.component_id
                        ));
                    }
                }
            }
        }
        Ok(decls)
    }

    fn get_runtime_config(&self, config_path: &str, zbi: &Zbi) -> Result<RuntimeConfig> {
        match zbi.bootfs.get(config_path) {
            Some(config_data) => Ok(RuntimeConfig::try_from(
                decode_persistent::<component_internal::Config>(&config_data)
                    .context("Unable to decode runtime config")?,
            )
            .context("Unable to parse runtime config")?),
            None => Err(anyhow!("file {} not found in bootfs", config_path.to_string())),
        }
    }

    fn get_component_id_index(
        &self,
        index_path: Option<&str>,
        zbi: &Zbi,
    ) -> Result<ComponentIdIndex> {
        match index_path {
            Some(path) => {
                let split: Vec<&str> = path.split_inclusive("/").collect();
                if split.as_slice()[..2] == ["/", "boot/"] {
                    let remainder = split[2..].join("");
                    match zbi.bootfs.get(&remainder) {
                        Some(index_data) => {
                            let fidl_index = decode_persistent::<
                                component_internal::ComponentIdIndex,
                            >(index_data)
                            .context("Unable to decode component ID index from persistent FIDL")?;
                            let index = component_id_index::Index::from_fidl(fidl_index).context(
                                "Unable to create internal index for component ID index from FIDL",
                            )?;
                            Ok(ComponentIdIndex::new_from_index(index).context(
                                "Unable to create component ID index from internal index",
                            )?)
                        }
                        None => Err(anyhow!("file {} not found in bootfs", remainder)),
                    }
                } else {
                    Err(anyhow!("Unable to parse component ID index file path {}", path))
                }
            }
            None => Ok(ComponentIdIndex::default()),
        }
    }

    fn make_builtin_runner_registry(&self, runtime_config: &RuntimeConfig) -> RunnerRegistry {
        let mut runners = Vec::new();
        // Always register the ELF runner.
        runners.push(RunnerRegistration {
            source_name: ELF_RUNNER_NAME.into(),
            target_name: ELF_RUNNER_NAME.into(),
            source: RegistrationSource::Self_,
        });
        // Register the RealmBuilder runner if needed.
        if runtime_config.realm_builder_resolver_and_runner
            == component_internal::RealmBuilderResolverAndRunner::Namespace
        {
            runners.push(RunnerRegistration {
                source_name: REALM_BUILDER_RUNNER_NAME.into(),
                target_name: REALM_BUILDER_RUNNER_NAME.into(),
                source: RegistrationSource::Self_,
            });
        }
        RunnerRegistry::from_decl(&runners)
    }

    fn load_dynamic_components(
        component_tree_config_path: &Option<PathBuf>,
    ) -> Result<HashMap<NodePath, (AbsoluteComponentUrl, Option<String>)>> {
        if component_tree_config_path.is_none() {
            return Ok(HashMap::new());
        }
        let component_tree_config_path = component_tree_config_path.as_ref().unwrap();

        let mut component_tree_config_file = File::open(component_tree_config_path)
            .context("Failed to open component tree configuration file")?;
        let component_tree_config: ComponentTreeConfig =
            from_reader(&mut component_tree_config_file)
                .context("Failed to parse component tree configuration file")?;

        let mut dynamic_components = HashMap::new();
        for (node_path, dynamic_component) in component_tree_config.dynamic_components.into_iter() {
            dynamic_components
                .insert(node_path, (dynamic_component.url, dynamic_component.environment));
        }
        Ok(dynamic_components)
    }
}

impl DataCollector for V2ComponentModelDataCollector {
    fn collect(&self, model: Arc<DataModel>) -> Result<()> {
        let builder = ModelBuilderForAnalyzer::new(DEFAULT_ROOT_URL.clone());

        let decls_by_url = self.get_decls(&model)?;

        let zbi = &model.get::<Zbi>().context("Unable to find the zbi model.")?;
        let runtime_config = self.get_runtime_config(DEFAULT_CONFIG_PATH, &zbi).context(
            format!("Unable to get the runtime config at path {:?}", DEFAULT_CONFIG_PATH),
        )?;
        let component_id_index =
            self.get_component_id_index(runtime_config.component_id_index_path.as_deref(), &zbi)?;

        info!(
            total = decls_by_url.len(),
            "V2ComponentModelDataCollector: Found v2 component declarations",
        );

        let dynamic_components =
            Self::load_dynamic_components(&model.config().component_tree_config_path)?;
        let runner_registry = self.make_builtin_runner_registry(&runtime_config);
        let build_result = builder.build_with_dynamic_components(
            dynamic_components,
            decls_by_url,
            Arc::new(runtime_config),
            Arc::new(component_id_index),
            runner_registry,
        );

        for err in build_result.errors.iter() {
            warn!(%err, "V2ComponentModelDataCollector");
        }

        match build_result.model {
            Some(component_model) => {
                info!(
                    total_instances = component_model.len(),
                    "V2ComponentModelDataCollector: Built v2 component model"
                );
                let core_deps_collection: Arc<CoreDataDeps> = model.get().map_err(|err| {
                    anyhow!(
                        "Failed to read core data deps for v2 component model data: {}",
                        err.to_string()
                    )
                })?;
                let deps = core_deps_collection.deps.clone();
                model
                    .set(V2ComponentModel::new(deps, component_model, build_result.errors))
                    .map_err(|err| {
                        anyhow!(
                            "Failed to store v2 component model in data model: {}",
                            err.to_string()
                        )
                    })?;
                Ok(())
            }
            None => Err(anyhow!("Failed to build v2 component model")),
        }
    }
}
