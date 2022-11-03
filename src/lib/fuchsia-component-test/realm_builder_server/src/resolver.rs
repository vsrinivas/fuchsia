// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::ConfigOverridePolicy,
    anyhow::{Context, Error},
    cm_fidl_validator,
    cm_rust::NativeIntoFidl,
    fidl::endpoints::{create_endpoints, ServerEnd},
    fidl::Vmo,
    fidl_fuchsia_component_config as fconfig, fidl_fuchsia_component_decl as fcdecl,
    fidl_fuchsia_component_resolution as fresolution, fidl_fuchsia_io as fio,
    fidl_fuchsia_mem as fmem, fuchsia_async as fasync,
    futures::{
        lock::{Mutex, MutexGuard},
        TryStreamExt,
    },
    std::{collections::HashMap, path::Path, sync::Arc},
    tracing::*,
    url::Url,
};

const RESOLVER_SCHEME: &'static str = "realm-builder";

#[derive(Clone)]
struct ResolveableComponent {
    /// The component's declaration/manifest.
    decl: fcdecl::Component,

    /// The component's package directory, if available.
    package_dir: Option<fio::DirectoryProxy>,

    /// Whether to rely on the `value_source` field in the component manifest or to only use
    /// values provided as replacements.
    config_override_policy: ConfigOverridePolicy,

    /// Any configuration values provided by the caller. If `config_override_policy` is
    /// `RequireAllValuesFromBuilder`, must provide a value for every field in the component's schema.
    config_value_replacements: HashMap<usize, cm_rust::ValueSpec>,
}

#[derive(Debug)]
pub struct Registry {
    next_unique_component_id: Mutex<u64>,
    component_decls: Mutex<HashMap<Url, ResolveableComponent>>,
}

impl Registry {
    pub fn new() -> Arc<Self> {
        Arc::new(Self {
            next_unique_component_id: Mutex::new(0),
            component_decls: Mutex::new(HashMap::new()),
        })
    }

    // Only used for unit tests
    #[cfg(test)]
    pub async fn get_decl_for_url(self: &Arc<Self>, url: &str) -> Option<cm_rust::ComponentDecl> {
        use cm_rust::FidlIntoNative;
        let url = Url::parse(url).expect("Failed to parse component URL.");
        let component = self.component_decls.lock().await.get(&url).cloned();
        component.map(|c| c.decl.fidl_into_native())
    }

    // Validates the given decl, and returns a URL at which it can be resolved
    pub async fn validate_and_register(
        self: &Arc<Self>,
        decl: &fcdecl::Component,
        name: String,
        package_dir: Option<fio::DirectoryProxy>,
        config_value_replacements: HashMap<usize, cm_rust::ValueSpec>,
        config_override_policy: ConfigOverridePolicy,
    ) -> Result<String, cm_fidl_validator::error::ErrorList> {
        cm_fidl_validator::validate(decl)?;

        let mut next_unique_component_id_guard = self.next_unique_component_id.lock().await;
        let mut component_decls_guard = self.component_decls.lock().await;

        let url = format!("{}://{}/{}", RESOLVER_SCHEME, *next_unique_component_id_guard, name);
        *next_unique_component_id_guard += 1;
        component_decls_guard.insert(
            Url::parse(&url).expect("Generated invalid component URL."),
            ResolveableComponent {
                decl: decl.clone(),
                package_dir,
                config_value_replacements,
                config_override_policy,
            },
        );
        Ok(url)
    }

    pub fn run_resolver_service(self: &Arc<Self>, stream: fresolution::ResolverRequestStream) {
        let self_ref = self.clone();
        fasync::Task::local(async move {
            if let Err(err) = self_ref.handle_resolver_request_stream(stream).await {
                warn!(%err, "`Resolver` server unexpectedly failed.", );
            }
        })
        .detach();
    }

    async fn resolve_structured_config(
        decl: &fcdecl::Component,
        package_dir: Option<&fio::DirectoryProxy>,
        config_value_replacements: &HashMap<usize, cm_rust::ValueSpec>,
    ) -> Result<Option<fmem::Data>, Error> {
        if let Some(schema) = &decl.config {
            let existing_values = match (&schema.value_source, package_dir) {
                (Some(fcdecl::ConfigValueSource::PackagePath(path)), Some(dir)) => {
                    Some(mem_util::open_file_data(dir, path).await?)
                }
                // fall back to using any overrides we got
                _ => None,
            };

            Ok(Some(
                Self::verify_and_replace_config_values(
                    schema,
                    existing_values,
                    config_value_replacements,
                )
                .await?,
            ))
        } else {
            Ok(None)
        }
    }

    async fn verify_and_replace_config_values(
        schema: &fcdecl::ConfigSchema,
        values_data: Option<fmem::Data>,
        config_value_replacements: &HashMap<usize, cm_rust::ValueSpec>,
    ) -> Result<fmem::Data, Error> {
        let mut values_data: fconfig::ValuesData = if let Some(v) = values_data {
            let bytes = mem_util::bytes_from_data(&v)?;
            let values_data = fidl::encoding::decode_persistent(&bytes)?;
            cm_fidl_validator::validate_values_data(&values_data)?;
            values_data
        } else {
            // make a boilerplate ValuesData that our replacements can fill
            let num_expected_values = schema
                .fields
                .as_ref()
                .expect("schema must have fields TODO real error handling")
                .len();
            let blank_values = (0..num_expected_values)
                .map(|_| fconfig::ValueSpec { value: None, ..fconfig::ValueSpec::EMPTY })
                .collect::<Vec<_>>();
            fconfig::ValuesData {
                checksum: schema.checksum.clone(),
                values: Some(blank_values),
                ..fconfig::ValuesData::EMPTY
            }
        };

        for (index, replacement) in config_value_replacements {
            let value = values_data
                .values
                .as_mut()
                .expect("either validated or constructed above")
                .get_mut(*index)
                .expect("Config Value File and Schema should have the same number of fields!");
            *value = replacement.clone().native_into_fidl();
        }

        cm_fidl_validator::validate_values_data(&values_data)
            .context("ensuring all values are populated")?;

        let data = fidl::encoding::encode_persistent(&mut values_data)?;
        let data = fmem::Data::Bytes(data);
        return Ok(data);
    }

    async fn handle_resolver_request_stream(
        self: &Arc<Self>,
        mut stream: fresolution::ResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                fresolution::ResolverRequest::Resolve { component_url, responder } => {
                    let mut resolve_result = self.resolve(&component_url).await;
                    responder.send(&mut resolve_result).map_err(|err| {
                        warn!("FIDL error {err:?} responding to resolve request for component URL '{component_url} with ':\n{resolve_result:#?}");
                        err
                    })?;
                }
                fresolution::ResolverRequest::ResolveWithContext {
                    component_url,
                    context,
                    responder,
                } => {
                    warn!("The RealmBuilder resolver does not resolve relative path component URLs with a context. Cannot resolve {} with context {:?}.", component_url, context);
                    responder.send(&mut Err(fresolution::ResolverError::InvalidArgs))?;
                }
            }
        }
        Ok(())
    }

    async fn resolve(
        self: &Arc<Self>,
        component_url: &str,
    ) -> Result<fresolution::Component, fresolution::ResolverError> {
        let parsed_url =
            Url::parse(&component_url).map_err(|_| fresolution::ResolverError::Internal)?;
        let component_decls_guard = self.component_decls.lock().await;
        if let Some(resolvable_component) = component_decls_guard.get(&parsed_url).cloned() {
            Self::load_absolute_url(component_url, resolvable_component)
                .await
                .map_err(|_| fresolution::ResolverError::Internal)
        } else {
            Self::load_relative_url(parsed_url, component_decls_guard).await
        }
    }

    async fn load_absolute_url(
        component_url: &str,
        resolveable_component: ResolveableComponent,
    ) -> Result<fresolution::Component, Error> {
        let ResolveableComponent {
            decl,
            package_dir,
            config_value_replacements,
            config_override_policy,
        } = resolveable_component;
        let package = if let Some(p) = &package_dir {
            let (client_end, server_end) = create_endpoints::<fio::DirectoryMarker>()?;
            p.clone(fio::OpenFlags::CLONE_SAME_RIGHTS, ServerEnd::new(server_end.into_channel()))?;
            Some(fresolution::Package {
                url: Some(component_url.to_string()),
                directory: Some(client_end),
                ..fresolution::Package::EMPTY
            })
        } else {
            None
        };

        let package_dir_for_config = match config_override_policy {
            ConfigOverridePolicy::DisallowValuesFromBuilder => {
                assert!(
                    config_value_replacements.is_empty(),
                    "cannot have received overrides if disallowed"
                );
                package_dir.as_ref()
            }
            ConfigOverridePolicy::LoadPackagedValuesFirst => package_dir.as_ref(),
            ConfigOverridePolicy::RequireAllValuesFromBuilder => None,
        };
        let config_values = Self::resolve_structured_config(
            &decl,
            package_dir_for_config,
            &config_value_replacements,
        )
        .await?;
        Ok(fresolution::Component {
            url: Some(component_url.to_string()),
            decl: Some(encode(decl)?),
            package,
            config_values,
            resolution_context: None,
            ..fresolution::Component::EMPTY
        })
    }

    // Realm builder never generates URLs with fragments. If we're asked to resolve a URL with
    // one, then this must be from component manager attempting to make sense of a relative URL
    // where the parent of the component is an absolute realm builder URL. We rewrite relative
    // URLs to be absolute realm builder URLs at build time, but if a component is added at
    // runtime (as in, to a collection) then realm builder doesn't have a chance to do this
    // rewrite.
    //
    // To handle this: let's use the fragment to look for a component in the test package.
    async fn load_relative_url<'a>(
        mut parsed_url: Url,
        component_decls_guard: MutexGuard<'a, HashMap<Url, ResolveableComponent>>,
    ) -> Result<fresolution::Component, fresolution::ResolverError> {
        let component_url = parsed_url.as_str().to_string();
        let fragment = match parsed_url.fragment() {
            Some(fragment) => fragment.to_string(),
            None => return Err(fresolution::ResolverError::ManifestNotFound),
        };

        parsed_url.set_fragment(None);
        let component = component_decls_guard
            .get(&parsed_url)
            .ok_or(fresolution::ResolverError::ManifestNotFound)?;
        let package_dir =
            component.package_dir.clone().ok_or(fresolution::ResolverError::PackageNotFound)?;
        let manifest_file = fuchsia_fs::open_file(
            &package_dir,
            Path::new(&fragment),
            fio::OpenFlags::RIGHT_READABLE,
        )
        .map_err(|_| fresolution::ResolverError::ManifestNotFound)?;
        let component_decl: fcdecl::Component = fuchsia_fs::read_file_fidl(&manifest_file)
            .await
            .map_err(|_| fresolution::ResolverError::ManifestNotFound)?;
        cm_fidl_validator::validate(&component_decl)
            .map_err(|_| fresolution::ResolverError::ManifestNotFound)?;
        let (client_end, server_end) = create_endpoints::<fio::DirectoryMarker>()
            .map_err(|_| fresolution::ResolverError::Internal)?;
        package_dir
            .clone(fio::OpenFlags::CLONE_SAME_RIGHTS, ServerEnd::new(server_end.into_channel()))
            .map_err(|_| fresolution::ResolverError::Io)?;
        let package_dir_for_config = match component.config_override_policy {
            ConfigOverridePolicy::DisallowValuesFromBuilder => {
                assert!(
                    component.config_value_replacements.is_empty(),
                    "cannot have received config overrides if disallowed"
                );
                Some(&package_dir)
            }
            ConfigOverridePolicy::LoadPackagedValuesFirst => Some(&package_dir),
            ConfigOverridePolicy::RequireAllValuesFromBuilder => None,
        };
        let config_values = Self::resolve_structured_config(
            &component_decl,
            package_dir_for_config,
            // Since realm builder never generates relative URLs, the component we are resolving
            // here will never be one with config value replacements set, so don't provide any.
            &HashMap::new(),
        )
        .await
        .map_err(|_| fresolution::ResolverError::ConfigValuesNotFound)?;
        Ok(fresolution::Component {
            url: Some(component_url.clone()),
            resolution_context: None,
            decl: Some(encode(component_decl).map_err(|_| fresolution::ResolverError::Internal)?),
            package: Some(fresolution::Package {
                url: Some(component_url),
                directory: Some(client_end),
                ..fresolution::Package::EMPTY
            }),
            config_values,
            ..fresolution::Component::EMPTY
        })
    }
}

fn encode(mut component_decl: fcdecl::Component) -> Result<fmem::Data, Error> {
    let encoded = fidl::encoding::encode_persistent_with_context(
        &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
        &mut component_decl,
    )
    .context("failed to encode ComponentDecl")?;
    let encoded_size = encoded.len() as u64;
    let vmo = Vmo::create(encoded_size)?;
    vmo.write(&encoded, 0)?;
    Ok(fmem::Data::Buffer(fmem::Buffer { vmo, size: encoded_size }))
}
