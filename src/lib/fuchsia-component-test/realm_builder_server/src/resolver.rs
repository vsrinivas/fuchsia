// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    cm_fidl_validator,
    cm_rust::{FidlIntoNative, NativeIntoFidl},
    fidl::endpoints::{create_endpoints, ServerEnd},
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
    decl: fcdecl::Component,
    package_dir: Option<fio::DirectoryProxy>,
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
        let url = Url::parse(url).expect("failed to parse URL");
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
    ) -> Result<String, cm_fidl_validator::error::ErrorList> {
        cm_fidl_validator::validate(decl)?;

        let mut next_unique_component_id_guard = self.next_unique_component_id.lock().await;
        let mut component_decls_guard = self.component_decls.lock().await;

        let url = format!("{}://{}/{}", RESOLVER_SCHEME, *next_unique_component_id_guard, name);
        *next_unique_component_id_guard += 1;
        component_decls_guard.insert(
            Url::parse(&url).expect("generated invalid URL"),
            ResolveableComponent { decl: decl.clone(), package_dir, config_value_replacements },
        );
        Ok(url)
    }

    pub fn run_resolver_service(self: &Arc<Self>, stream: fresolution::ResolverRequestStream) {
        let self_ref = self.clone();
        fasync::Task::local(async move {
            if let Err(e) = self_ref.handle_resolver_request_stream(stream).await {
                warn!("error encountered while running resolver service: {:?}", e);
            }
        })
        .detach();
    }

    async fn get_config_data(
        decl: &fcdecl::Component,
        package_dir: &Option<fio::DirectoryProxy>,
        config_value_replacements: &HashMap<usize, cm_rust::ValueSpec>,
    ) -> Result<Option<fmem::Data>, Error> {
        if let Some(fcdecl::ConfigSchema { value_source, .. }) = &decl.config {
            if let Some(fcdecl::ConfigValueSource::PackagePath(path)) = value_source {
                if let Some(p) = package_dir {
                    let data = mem_util::open_file_data(p, path).await?;
                    let data =
                        Self::verify_and_replace_config_values(data, config_value_replacements)
                            .await?;
                    Ok(Some(data))
                } else {
                    return Err(anyhow!(
                        "Expected package directory for opening config values at {:?}, but none was provided",
                        path
                    ));
                }
            } else {
                return Err(anyhow!(
                    "Expected ConfigValueSource::PackagePath, got {:?}",
                    value_source
                ));
            }
        } else {
            Ok(None)
        }
    }

    async fn verify_and_replace_config_values(
        values_data: fmem::Data,
        config_value_replacements: &HashMap<usize, cm_rust::ValueSpec>,
    ) -> Result<fmem::Data, Error> {
        let bytes = mem_util::bytes_from_data(&values_data)?;
        let values_data = fidl::encoding::decode_persistent(&bytes)?;
        cm_fidl_validator::validate_values_data(&values_data)?;
        let mut values_data: cm_rust::ValuesData = values_data.fidl_into_native();

        for (index, replacement) in config_value_replacements {
            let value = values_data
                .values
                .get_mut(*index)
                .expect("Config Value File and Schema should have the same number of fields!");
            *value = replacement.clone();
        }

        let mut values_data: fconfig::ValuesData = values_data.native_into_fidl();
        cm_fidl_validator::validate_values_data(&values_data)?;

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
                    responder.send(&mut self.resolve(&component_url).await)?;
                }
                fresolution::ResolverRequest::ResolveWithContext {
                    component_url: _,
                    context: _,
                    responder,
                } => {
                    warn!("The RealmBuilder resolver does not resolve relative path component URLs with a context");
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
        let ResolveableComponent { decl, package_dir, config_value_replacements } =
            resolveable_component;
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

        let config_values =
            Self::get_config_data(&decl, &package_dir, &config_value_replacements).await?;
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
        let manifest_file =
            io_util::open_file(&package_dir, Path::new(&fragment), fio::OpenFlags::RIGHT_READABLE)
                .map_err(|_| fresolution::ResolverError::ManifestNotFound)?;
        let component_decl: fcdecl::Component = io_util::read_file_fidl(&manifest_file)
            .await
            .map_err(|_| fresolution::ResolverError::ManifestNotFound)?;
        cm_fidl_validator::validate(&component_decl)
            .map_err(|_| fresolution::ResolverError::ManifestNotFound)?;
        let (client_end, server_end) = create_endpoints::<fio::DirectoryMarker>()
            .map_err(|_| fresolution::ResolverError::Internal)?;
        package_dir
            .clone(fio::OpenFlags::CLONE_SAME_RIGHTS, ServerEnd::new(server_end.into_channel()))
            .map_err(|_| fresolution::ResolverError::Io)?;
        let config_values = Self::get_config_data(
            &component_decl,
            &Some(package_dir),
            &component.config_value_replacements,
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
    Ok(fmem::Data::Bytes(
        fidl::encoding::encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut component_decl,
        )
        .context("failed to encode ComponentDecl")?,
    ))
}
