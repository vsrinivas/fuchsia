// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    cm_fidl_validator,
    fidl::endpoints::{create_endpoints, ServerEnd},
    fidl_fuchsia_component_decl as fcdecl, fidl_fuchsia_io as fio, fidl_fuchsia_mem as fmem,
    fidl_fuchsia_sys2 as fsys, fuchsia_async as fasync,
    futures::{
        lock::{Mutex, MutexGuard},
        TryStreamExt,
    },
    std::{collections::HashMap, path::Path, sync::Arc},
    tracing::*,
    url::Url,
};

#[cfg(test)]
use cm_rust::{self, FidlIntoNative};

const RESOLVER_SCHEME: &'static str = "realm-builder";

#[derive(Clone)]
struct ResolveableComponent {
    decl: fcdecl::Component,
    package_dir: Option<fio::DirectoryProxy>,
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
        decl: fcdecl::Component,
        name: String,
        package_dir: Option<fio::DirectoryProxy>,
    ) -> Result<String, cm_fidl_validator::error::ErrorList> {
        cm_fidl_validator::validate(&decl)?;

        let mut next_unique_component_id_guard = self.next_unique_component_id.lock().await;
        let mut component_decls_guard = self.component_decls.lock().await;

        let url = format!("{}://{}-{}", RESOLVER_SCHEME, *next_unique_component_id_guard, name);
        *next_unique_component_id_guard += 1;
        component_decls_guard.insert(
            Url::parse(&url).expect("generated invalid URL"),
            ResolveableComponent { decl, package_dir },
        );
        Ok(url)
    }

    pub fn run_resolver_service(self: &Arc<Self>, stream: fsys::ComponentResolverRequestStream) {
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
    ) -> Result<Option<fmem::Data>, Error> {
        if let Some(fcdecl::ConfigSchema { value_source, .. }) = &decl.config {
            if let Some(fcdecl::ConfigValueSource::PackagePath(path)) = value_source {
                if let Some(p) = package_dir {
                    Ok(Some(mem_util::open_file_data(p, path).await?))
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

    async fn handle_resolver_request_stream(
        self: &Arc<Self>,
        mut stream: fsys::ComponentResolverRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            match req {
                fsys::ComponentResolverRequest::Resolve { component_url, responder } => {
                    let parsed_url = match Url::parse(&component_url) {
                        Ok(url) => url,
                        Err(_) => {
                            responder.send(&mut Err(fsys::ResolverError::InvalidArgs))?;
                            continue;
                        }
                    };
                    let component_decls_guard = self.component_decls.lock().await;
                    if let Some(ResolveableComponent { decl, package_dir }) =
                        component_decls_guard.get(&parsed_url).cloned()
                    {
                        let package = if let Some(p) = &package_dir {
                            let (client_end, server_end) =
                                create_endpoints::<fio::DirectoryMarker>()?;
                            p.clone(
                                fio::CLONE_FLAG_SAME_RIGHTS,
                                ServerEnd::new(server_end.into_channel()),
                            )?;
                            Some(fsys::Package {
                                package_url: Some(component_url.clone()),
                                package_dir: Some(client_end),
                                ..fsys::Package::EMPTY
                            })
                        } else {
                            None
                        };

                        let config_values = Self::get_config_data(&decl, &package_dir).await?;

                        responder.send(&mut Ok(fsys::Component {
                            resolved_url: Some(component_url),
                            decl: Some(encode(decl)?),
                            package,
                            config_values,
                            ..fsys::Component::EMPTY
                        }))?;
                    } else {
                        let mut res =
                            Self::load_relative_url(parsed_url, component_decls_guard).await;
                        responder.send(&mut res)?;
                    }
                }
            }
        }
        Ok(())
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
    ) -> Result<fsys::Component, fsys::ResolverError> {
        let component_url = parsed_url.as_str().to_string();
        let fragment = match parsed_url.fragment() {
            Some(fragment) => fragment.to_string(),
            None => return Err(fsys::ResolverError::ManifestNotFound),
        };

        parsed_url.set_fragment(None);
        let package_dir = component_decls_guard
            .get(&parsed_url)
            .ok_or(fsys::ResolverError::ManifestNotFound)?
            .package_dir
            .clone();
        let package_dir = package_dir.ok_or(fsys::ResolverError::PackageNotFound)?;
        let manifest_file =
            io_util::open_file(&package_dir, Path::new(&fragment), fio::OPEN_RIGHT_READABLE)
                .map_err(|_| fsys::ResolverError::ManifestNotFound)?;
        let component_decl: fcdecl::Component = io_util::read_file_fidl(&manifest_file)
            .await
            .map_err(|_| fsys::ResolverError::ManifestNotFound)?;
        cm_fidl_validator::validate(&component_decl)
            .map_err(|_| fsys::ResolverError::ManifestNotFound)?;
        let (client_end, server_end) = create_endpoints::<fio::DirectoryMarker>()
            .map_err(|_| fsys::ResolverError::Internal)?;
        package_dir
            .clone(fio::CLONE_FLAG_SAME_RIGHTS, ServerEnd::new(server_end.into_channel()))
            .map_err(|_| fsys::ResolverError::Io)?;
        let config_values = Self::get_config_data(&component_decl, &Some(package_dir))
            .await
            .map_err(|_| fsys::ResolverError::ConfigValuesNotFound)?;
        Ok(fsys::Component {
            resolved_url: Some(component_url.clone()),
            decl: Some(encode(component_decl).map_err(|_| fsys::ResolverError::Internal)?),
            package: Some(fsys::Package {
                package_url: Some(component_url),
                package_dir: Some(client_end),
                ..fsys::Package::EMPTY
            }),
            config_values,
            ..fsys::Component::EMPTY
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
