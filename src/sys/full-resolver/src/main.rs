// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context},
    fidl::endpoints::{create_proxy, ClientEnd, Proxy},
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_component_resolution as fresolution,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxy, ResolutionContext},
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    fuchsia_pkg::{
        transitional::{context_bytes_from_subpackages_map, subpackages_map_from_context_bytes},
        PackageDirectory,
    },
    fuchsia_url::{ComponentUrl, Hash, PackageUrl, RelativePackageUrl, RepositoryUrl},
    full_resolver_config::Config,
    futures::prelude::*,
    std::collections::HashMap,
    thiserror::Error,
    tracing::*,
};

enum IncomingService {
    Resolver(fresolution::ResolverRequestStream),
}

#[fuchsia::main]
async fn main() -> anyhow::Result<()> {
    info!("started");

    // Record configuration to inspect
    let config = Config::take_from_startup_handle();
    let inspector = fuchsia_inspect::component::inspector();
    inspector.root().record_child("config", |config_node| config.record_inspect(config_node));

    let mut service_fs = ServiceFs::new_local();
    service_fs.dir("svc").add_fidl_service(IncomingService::Resolver);

    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;

    service_fs
        .for_each_concurrent(None, |request| async {
            if let Err(err) = match request {
                IncomingService::Resolver(stream) => serve(stream, &config).await,
            } {
                error!("failed to serve resolve request: {:#}", err);
            }
        })
        .await;

    Ok(())
}

async fn serve(
    mut stream: fresolution::ResolverRequestStream,
    config: &Config,
) -> anyhow::Result<()> {
    let package_resolver = connect_to_protocol::<PackageResolverMarker>()
        .context("failed to connect to PackageResolver service")?;
    while let Some(request) =
        stream.try_next().await.context("failed to read request from FIDL stream")?
    {
        match request {
            fresolution::ResolverRequest::Resolve { component_url, responder } => {
                let mut result =
                    resolve_component_without_context(&component_url, &package_resolver)
                        .await
                        .map_err(|err| {
                            let fidl_err = (&err).into();
                            warn!(
                                "failed to resolve component URL {}: {:#}",
                                component_url,
                                anyhow!(err)
                            );
                            fidl_err
                        });
                responder.send(&mut result).context("failed sending response")?;
            }
            fresolution::ResolverRequest::ResolveWithContext {
                component_url,
                context,
                responder,
            } => {
                if config.enable_subpackages {
                    let mut result =
                        resolve_component_with_context(&component_url, &context, &package_resolver)
                            .await
                            .map_err(|err| {
                                let fidl_err = (&err).into();
                                warn!(
                                    "failed to resolve component URL {} with context {:?}: {:#}",
                                    component_url,
                                    context,
                                    anyhow!(err)
                                );
                                fidl_err
                            });
                    responder.send(&mut result).context("failed sending response")?;
                } else {
                    error!(
                        "full-resolver ResolveWithContext is disabled. Config value `enable_subpackages` is false. Cannot resolve component URL {:?} with context {:?}",
                        component_url,
                        context
                    );
                    responder
                        .send(&mut Err(fresolution::ResolverError::Internal))
                        .context("failed sending response")?;
                }
            }
        }
    }
    Ok(())
}

async fn resolve_component_without_context(
    component_url: &str,
    package_resolver: &PackageResolverProxy,
) -> Result<fresolution::Component, ResolverError> {
    resolve_component(component_url, None, package_resolver).await
}

async fn resolve_component_with_context(
    component_url: &str,
    context: &fresolution::Context,
    package_resolver: &PackageResolverProxy,
) -> Result<fresolution::Component, ResolverError> {
    resolve_component(component_url, Some(context), package_resolver).await
}

async fn resolve_component(
    component_url_str: &str,
    some_incoming_context: Option<&fresolution::Context>,
    package_resolver: &PackageResolverProxy,
) -> Result<fresolution::Component, ResolverError> {
    let component_url = ComponentUrl::parse(component_url_str)?;
    let package = resolve_package_async(
        component_url.package_url(),
        some_incoming_context
            .map(|component_context| ResolutionContext { bytes: component_context.bytes.clone() })
            .as_ref(),
        package_resolver,
    )
    .await?;

    // Read the component manifest (.cm file) from the package directory.
    let data = mem_util::open_file_data(&package.dir, component_url.resource())
        .await
        .map_err(ResolverError::ManifestNotFound)?;
    let raw_bytes = mem_util::bytes_from_data(&data).map_err(ResolverError::ReadingManifest)?;
    let decl: fdecl::Component = fidl::encoding::decode_persistent(&raw_bytes[..])
        .map_err(ResolverError::ParsingManifest)?;

    let config_values = if let Some(config_decl) = decl.config.as_ref() {
        // if we have a config declaration, we need to read the value file from the package dir
        let strategy =
            config_decl.value_source.as_ref().ok_or(ResolverError::MissingConfigSource)?;
        let config_path = match strategy {
            fdecl::ConfigValueSource::PackagePath(path) => path,
            other => return Err(ResolverError::UnsupportedConfigStrategy(other.to_owned())),
        };
        Some(
            mem_util::open_file_data(&package.dir, &config_path)
                .await
                .map_err(ResolverError::ConfigValuesNotFound)?,
        )
    } else {
        None
    };

    let package_dir = ClientEnd::new(
        package.dir.into_channel().expect("could not convert proxy to channel").into_zx_channel(),
    );
    Ok(fresolution::Component {
        url: Some(component_url_str.to_string()),
        resolution_context: Some(fresolution::Context { bytes: package.context.bytes }),
        decl: Some(data),
        package: Some(fresolution::Package {
            url: Some(component_url.package_url().to_string()),
            directory: Some(package_dir),
            ..fresolution::Package::EMPTY
        }),
        config_values,
        ..fresolution::Component::EMPTY
    })
}

#[derive(Debug)]
struct ResolvedPackage {
    dir: fio::DirectoryProxy,
    context: ResolutionContext,
}

async fn resolve_package_async(
    package_url: &PackageUrl,
    some_incoming_context: Option<&ResolutionContext>,
    package_resolver: &PackageResolverProxy,
) -> Result<ResolvedPackage, ResolverError> {
    let (proxy, server_end) =
        create_proxy::<fio::DirectoryMarker>().expect("failed to create channel pair");
    let package_dir = PackageDirectory::from_proxy(proxy);

    let package_context = match package_url {
        PackageUrl::Relative(relative) => {
            let context = some_incoming_context
                .ok_or_else(|| ResolverError::RelativeUrlMissingContext(package_url.to_string()))?;
            // TODO(fxbug.dev/100060): Replace with `package_resolver.resolve_with_context` when
            // available.
            transitional::resolve_with_context(
                package_resolver,
                &package_dir,
                relative,
                context,
                server_end,
            )
            .await
        }
        PackageUrl::Absolute(absolute) => {
            // TODO(fxbug.dev/100060): Replace with `package_resolver.resolve` when available.
            transitional::resolve(package_resolver, &package_dir, absolute, server_end).await
        }
    }
    .map_err(ResolverError::IoError)?
    .map_err(|err| match err {
        fidl_fuchsia_pkg::ResolveError::PackageNotFound => ResolverError::PackageNotFound,
        fidl_fuchsia_pkg::ResolveError::RepoNotFound
        | fidl_fuchsia_pkg::ResolveError::UnavailableBlob
        | fidl_fuchsia_pkg::ResolveError::UnavailableRepoMetadata => ResolverError::Unavailable,
        fidl_fuchsia_pkg::ResolveError::NoSpace => ResolverError::NoSpace,
        _ => ResolverError::Internal,
    })?;
    Ok(ResolvedPackage { dir: package_dir.into_proxy(), context: package_context })
}

/// Implements the expected behavior of future Rust bindings for the upcoming
/// iteration of the FIDL API for `fuchsia.pkg.PackageResolver`, once updated to
/// support subpackages, by wrapping the existing API. Once the new version is
/// implemented, this module will be removed.
mod transitional {
    use {super::*, fidl::endpoints::ServerEnd, fuchsia_url::AbsolutePackageUrl};

    pub async fn resolve(
        package_resolver: &PackageResolverProxy,
        package_dir: &PackageDirectory,
        package_url: &AbsolutePackageUrl,
        dir_server_end: ServerEnd<fio::DirectoryMarker>,
    ) -> Result<Result<ResolutionContext, fidl_fuchsia_pkg::ResolveError>, fidl::Error> {
        let result = package_resolver.resolve(&package_url.to_string(), dir_server_end).await?;
        if let Err(err) = result {
            // The proxy call returned (outer result Ok), but it returned an Err (inner result)
            return Ok(Err(err));
        }
        // TODO(fxbug.dev/100060): When package resolver implements
        // ResolveWithContext, remove the following call to `fabricate...`
        // and use the above `result` (and its context) instead.
        let result = fabricate_package_context(package_url.repository(), package_dir)
            .await
            .map_err(|err: anyhow::Error| {
                error!("failed to fabricate package context: {:#}", err);
                fidl_fuchsia_pkg::ResolveError::Internal
            });
        Ok(result)
    }

    pub async fn resolve_with_context(
        package_resolver: &PackageResolverProxy,
        package_dir: &PackageDirectory,
        package_url: &RelativePackageUrl,
        context: &ResolutionContext,
        dir_server_end: ServerEnd<fio::DirectoryMarker>,
    ) -> Result<Result<ResolutionContext, fidl_fuchsia_pkg::ResolveError>, fidl::Error> {
        let (repo, hash) = match get_subpackage_repo_and_hash(package_url, context) {
            Ok(v) => v,
            Err(err) => {
                error!("failed to parse package context: {:?}", err);
                return Ok(Err(fidl_fuchsia_pkg::ResolveError::Internal));
            }
        };

        // TODO(fxbug.dev/100060): When package resolver implements
        // ResolveWithContext, remove the `pinned_subpackage_url` (an absolute
        // package URL) and pass the relative subpackage URL (without repo,
        // and without hash) to ResolveWithContext, along with the given
        // context.
        //
        // Until then, PackageResolver::Resolve() requires an absolute URL, and
        // the path must be the actual package name. The actual package name
        // is not known, so the only way this workaround works is by ensuring
        // the subpackage name equals the original package name (by not renaming
        // it in the `subpackages` declaration in the build files).
        let pinned_subpackage_url = format!("{}/{}?hash={}", repo, package_url.as_ref(), hash);

        if let Err(err) = package_resolver.resolve(&pinned_subpackage_url, dir_server_end).await {
            return Err(err);
        }
        match fabricate_package_context(&repo, package_dir).await {
            Ok(package_context) => Ok(Ok(package_context)),
            Err(err) => {
                error!(
                    %package_url,
                    "failed to fabricate package context: {:#}",
                    err
                );
                Ok(Err(fidl_fuchsia_pkg::ResolveError::Internal))
            }
        }
    }

    fn get_subpackage_repo_and_hash(
        subpackage: &RelativePackageUrl,
        context: &ResolutionContext,
    ) -> anyhow::Result<(RepositoryUrl, Hash)> {
        let info = PackageResolutionInfo::from_package_context(context)?;
        Ok((
            info.repo,
            info.subpackage_hashes
                .get(&subpackage)
                .ok_or_else(|| anyhow::format_err!("subpackage {} not found", subpackage))?
                .clone(),
        ))
    }

    async fn fabricate_package_context(
        repo: &RepositoryUrl,
        package_dir: &PackageDirectory,
    ) -> anyhow::Result<ResolutionContext> {
        let meta = package_dir.meta_subpackages().await?;
        PackageResolutionInfo::new(repo.clone(), meta.into_subpackages()).into_package_context()
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct PackageResolutionInfo {
    pub repo: RepositoryUrl,
    pub subpackage_hashes: HashMap<RelativePackageUrl, Hash>,
}

impl PackageResolutionInfo {
    pub fn new(repo: RepositoryUrl, subpackage_hashes: HashMap<RelativePackageUrl, Hash>) -> Self {
        Self { repo, subpackage_hashes }
    }

    pub fn from_package_context(context: &ResolutionContext) -> Result<Self, anyhow::Error> {
        let mut parts = context.bytes.split(|&b| b == b'\0');
        let repo = RepositoryUrl::parse_host(
            String::from_utf8(
                parts
                    .next()
                    .ok_or_else(|| anyhow::format_err!("Empty resolution context bytes"))?
                    .to_vec(),
            )
            .with_context(|| {
                format!(
                    "Error extracting package URL host from resolution context bytes: {:?}",
                    context
                )
            })?,
        )?;

        let subpackage_hashes = parts
            .next()
            .map(|bytes| {
                subpackages_map_from_context_bytes(&bytes.to_vec()).with_context(|| {
                    format!(
                        "Error extracting subpackages JSON from resolution context bytes: {:?}",
                        bytes
                    )
                })
            })
            .transpose()?
            .unwrap_or_default();
        Ok(Self { repo, subpackage_hashes })
    }

    pub fn into_package_context(self) -> anyhow::Result<ResolutionContext> {
        let Self { repo, subpackage_hashes } = self;
        let mut bytes = repo.host().as_bytes().to_vec();
        if let Some(mut context_bytes) = context_bytes_from_subpackages_map(&subpackage_hashes)? {
            bytes.push(b'\0');
            bytes.append(&mut context_bytes);
        }
        Ok(ResolutionContext { bytes })
    }
}

#[derive(Error, Debug)]
enum ResolverError {
    #[error("an unexpected error occurred")]
    Internal,

    #[error("invalid component URL")]
    InvalidUrl(#[from] fuchsia_url::errors::ParseError),

    #[error("manifest not found")]
    ManifestNotFound(#[source] mem_util::FileError),

    #[error("config values not found")]
    ConfigValuesNotFound(#[source] mem_util::FileError),

    #[error("package not found")]
    PackageNotFound,

    #[error("IO error")]
    IoError(#[source] fidl::Error),

    #[error("failed to deal with fuchsia.mem.Data")]
    ReadingManifest(#[source] mem_util::DataError),

    #[error("failed to parse compiled manifest to check for config")]
    ParsingManifest(#[source] fidl::Error),

    #[error("insufficient space to store package")]
    NoSpace,

    #[error("the component's package is temporarily unavailable")]
    Unavailable,

    #[error("component has config fields but does not have a config value lookup strategy")]
    MissingConfigSource,

    #[error("unsupported config value resolution strategy {_0:?}")]
    UnsupportedConfigStrategy(fdecl::ConfigValueSource),

    #[error("failed to create the resolution context")]
    CreatingContext(#[from] anyhow::Error),

    #[error("a context is required to resolve relative url: {0}")]
    RelativeUrlMissingContext(String),
}

impl From<&ResolverError> for fresolution::ResolverError {
    fn from(err: &ResolverError) -> fresolution::ResolverError {
        use {fresolution::ResolverError as ferr, ResolverError::*};
        match err {
            Internal => ferr::Internal,
            InvalidUrl(_) => ferr::InvalidArgs,
            ManifestNotFound { .. } => ferr::ManifestNotFound,
            ConfigValuesNotFound { .. } => ferr::ConfigValuesNotFound,
            PackageNotFound => ferr::PackageNotFound,
            ReadingManifest(_) | IoError(_) => ferr::Io,
            NoSpace => ferr::NoSpace,
            Unavailable => ferr::ResourceUnavailable,
            ParsingManifest(..) | MissingConfigSource | UnsupportedConfigStrategy(..) => {
                ferr::InvalidManifest
            }
            CreatingContext(_) => ferr::Internal,
            RelativeUrlMissingContext(_) => ferr::Internal,
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Error,
        assert_matches::assert_matches,
        fidl::{encoding::encode_persistent_with_context, endpoints::ServerEnd},
        fidl_fuchsia_component_config as fconfig, fidl_fuchsia_component_decl as fdecl,
        fidl_fuchsia_component_resolution::ResolverMarker,
        fidl_fuchsia_io as fio, fidl_fuchsia_mem as fmem,
        fidl_fuchsia_pkg::{self as fpkg, PackageResolverRequest, PackageResolverRequestStream},
        fuchsia_async as fasync,
        fuchsia_component::server as fserver,
        fuchsia_component_test::{
            Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
        },
        fuchsia_hash::Hash,
        fuchsia_pkg::MetaSubpackages,
        fuchsia_zircon::Vmo,
        futures::{channel::mpsc, join, lock::Mutex},
        std::{boxed::Box, iter::FromIterator, str::FromStr, sync::Arc},
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::vmo::asynchronous::read_only_static, path::Path, pseudo_directory,
        },
    };

    async fn resolve_package(
        package_url: &PackageUrl,
        package_resolver: &PackageResolverProxy,
    ) -> Result<ResolvedPackage, ResolverError> {
        resolve_package_async(package_url, None, package_resolver).await
    }

    async fn mock_pkg_resolver(
        trigger: Arc<Mutex<Option<mpsc::Sender<Result<(), Error>>>>>,
        handles: LocalComponentHandles,
    ) -> Result<(), Error> {
        let mut fs = fserver::ServiceFs::new();
        fs.dir("svc").add_fidl_service(
            move |mut req_stream: fidl_fuchsia_pkg::PackageResolverRequestStream| {
                let tx = trigger.clone();
                fasync::Task::local(async move {
                    while let Some(fidl_fuchsia_pkg::PackageResolverRequest::Resolve {
                        responder,
                        ..
                    }) =
                        req_stream.try_next().await.expect("Serving package resolver stream failed")
                    {
                        responder
                            .send(&mut Err(fidl_fuchsia_pkg::ResolveError::PackageNotFound))
                            .expect("failed sending package resolver response to client");

                        {
                            let mut lock = tx.lock().await;
                            let mut c = lock.take().unwrap();
                            c.send(Ok(())).await.expect("failed sending oneshot to test");
                            lock.replace(c);
                        }
                    }
                })
                .detach();
            },
        );

        fs.serve_connection(handles.outgoing_dir)?;
        fs.collect::<()>().await;
        Ok(())
    }

    async fn package_requester(
        trigger: Arc<Mutex<Option<mpsc::Sender<Result<(), Error>>>>>,
        url: String,
        handles: LocalComponentHandles,
    ) -> Result<(), Error> {
        let resolver_proxy = handles.connect_to_protocol::<ResolverMarker>()?;
        let _ = resolver_proxy.resolve(&url).await?;
        fasync::Task::local(async move {
            let mut lock = trigger.lock().await;
            let mut c = lock.take().unwrap();
            c.send(Ok(())).await.expect("sending oneshot from package requester failed");
            lock.replace(c);
        })
        .detach();
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    // Test that the default configuration which forwards requests to
    // PackageResolver works properly.
    async fn test_using_pkg_resolver() {
        let (sender, mut receiver) = mpsc::channel(2);
        let tx = Arc::new(Mutex::new(Some(sender)));
        let resolver_url =
            "fuchsia-pkg://fuchsia.com/full-resolver-unittests#meta/full-resolver.cm".to_string();
        let requested_url =
            "fuchsia-pkg://fuchsia.com/test-pkg-request#meta/test-component.cm".to_string();
        let builder = RealmBuilder::new().await.expect("Failed to create test realm builder");
        let full_resolver = builder
            .add_child("full-resolver", resolver_url, ChildOptions::new())
            .await
            .expect("Failed add full-resolver to test topology");
        let fake_pkg_resolver = builder
            .add_local_child(
                "fake-pkg-resolver",
                {
                    let sender = tx.clone();
                    move |handles: LocalComponentHandles| {
                        Box::pin(mock_pkg_resolver(sender.clone(), handles))
                    }
                },
                ChildOptions::new(),
            )
            .await
            .expect("Failed adding base resolver mock");
        let requesting_component = builder
            .add_local_child(
                "requesting-component",
                {
                    let sender = tx.clone();
                    move |handles: LocalComponentHandles| {
                        Box::pin(package_requester(sender.clone(), requested_url.clone(), handles))
                    }
                },
                ChildOptions::new().eager(),
            )
            .await
            .expect("Failed adding mock request component");
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.pkg.PackageResolver"))
                    .from(&fake_pkg_resolver)
                    .to(&full_resolver),
            )
            .await
            .expect("Failed adding resolver route from fake-base-resolver to full-resolver");
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name(
                        "fuchsia.component.resolution.Resolver",
                    ))
                    .from(&full_resolver)
                    .to(&requesting_component),
            )
            .await
            .expect("Failed adding resolver route from full-resolver to requesting-component");
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                    .from(Ref::parent())
                    .to(&full_resolver)
                    .to(&fake_pkg_resolver)
                    .to(&requesting_component),
            )
            .await
            .expect("Failed adding LogSink route to test components");
        let _test_topo = builder.build().await.unwrap();

        receiver.next().await.expect("Unexpected error waiting for response").expect("error sent");
        receiver.next().await.expect("Unexpected error waiting for response").expect("error sent");
    }

    #[fuchsia::test]
    async fn resolve_package_succeeds() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            let fs = pseudo_directory! {
                "test_file" => read_only_static(b"foo"),
            };
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve { package_url, dir, responder } => {
                        assert_eq!(
                            package_url, "fuchsia-pkg://fuchsia.com/test",
                            "unexpected package URL"
                        );
                        fs.clone().open(
                            ExecutionScope::new(),
                            fio::OpenFlags::RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::dot(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] })).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            let result =
                resolve_package(&"fuchsia-pkg://fuchsia.com/test".parse().unwrap(), &proxy).await;
            let package = result.expect("package resolver failed unexpectedly");
            let file = fuchsia_fs::directory::open_file(
                &package.dir,
                "test_file",
                fio::OpenFlags::RIGHT_READABLE,
            )
            .await
            .expect("failed to open 'test_file' from package resolver directory");
            let contents = fuchsia_fs::file::read(&file)
                .await
                .expect("failed to read 'test_file' contents from package resolver directory");
            assert_eq!(&contents, b"foo");
        };
        join!(server, client);
    }

    #[fuchsia::test]
    async fn resolve_component_succeeds() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            let cm_bytes = encode_persistent_with_context(
                &fidl::encoding::Context {
                    wire_format_version: fidl::encoding::WireFormatVersion::V2,
                },
                &mut fdecl::Component::EMPTY.clone(),
            )
            .expect("failed to encode ComponentDecl FIDL");
            let fs = pseudo_directory! {
                "meta" => pseudo_directory!{
                    "test.cm" => read_only_static(cm_bytes),
                },
            };
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve { package_url, dir, responder } => {
                        assert_eq!(
                            package_url, "fuchsia-pkg://fuchsia.com/test",
                            "unexpected package URL"
                        );
                        fs.clone().open(
                            ExecutionScope::new(),
                            fio::OpenFlags::RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::dot(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] })).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            assert_matches!(
                resolve_component_without_context(
                    "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
                    &proxy
                )
                .await,
                Ok(fresolution::Component {
                    decl: Some(fidl_fuchsia_mem::Data::Buffer(fidl_fuchsia_mem::Buffer { .. })),
                    ..
                })
            );
        };
        join!(server, client);
    }

    #[fuchsia::test]
    async fn resolve_component_succeeds_with_hash() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            let cm_bytes = encode_persistent_with_context(
                &fidl::encoding::Context {
                    wire_format_version: fidl::encoding::WireFormatVersion::V2,
                },
                &mut fdecl::Component::EMPTY.clone(),
            )
            .expect("failed to encode ComponentDecl FIDL");
            let fs = pseudo_directory! {
                "meta" => pseudo_directory!{
                    "test.cm" => read_only_static(cm_bytes),
                },
            };
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve { package_url, dir, responder } => {
                        assert_eq!(package_url, "fuchsia-pkg://fuchsia.com/test?hash=9e3a3f63c018e2a4db0ef93903a87714f036e3e8ff982a7a2020eca86cc4677c", "unexpected package URL");
                        fs.clone().open(
                            ExecutionScope::new(),
                            fio::OpenFlags::RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::dot(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] })).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            assert_matches!(resolve_component_without_context("fuchsia-pkg://fuchsia.com/test?hash=9e3a3f63c018e2a4db0ef93903a87714f036e3e8ff982a7a2020eca86cc4677c#meta/test.cm", &proxy).await, Ok(_));
        };
        join!(server, client);
    }

    #[fuchsia::test]
    async fn resolve_component_succeeds_with_vmo_manifest() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            let fs = pseudo_directory! {
                "meta" => pseudo_directory!{
                    "test.cm" => vfs::file::vmo::read_only(|| async move {
                        let cm_bytes = encode_persistent_with_context(&fidl::encoding::Context{wire_format_version: fidl::encoding::WireFormatVersion::V2},&mut fdecl::Component::EMPTY.clone())
                            .expect("failed to encode ComponentDecl FIDL");
                        let capacity = cm_bytes.len() as u64;
                        let vmo = Vmo::create(capacity)?;
                        vmo.write(&cm_bytes, 0).expect("failed to write manifest bytes to vmo");
                        Ok(vmo)
                    }),
                },
            };
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve { dir, responder, .. } => {
                        fs.clone().open(
                            ExecutionScope::new(),
                            fio::OpenFlags::RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::dot(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] })).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            assert_matches!(
                resolve_component_without_context(
                    "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
                    &proxy
                )
                .await,
                Ok(fresolution::Component { decl: Some(fmem::Data::Buffer(_)), .. })
            );
        };
        join!(server, client);
    }

    #[fuchsia::test]
    async fn resolves_component_in_subpackage_succeeds() {
        let parent_package_url = "fuchsia-pkg://fuchsia.com/toplevel-package";
        let parent_component_url = parent_package_url.to_owned() + "#meta/foo.cm";
        let subpackage_name = "my_subpackage";
        let subpackaged_component_fragment = "#meta/subfoo.cm";
        let subpackaged_component_relative_url =
            subpackage_name.to_owned() + subpackaged_component_fragment;
        let subpackage_hash = "facefacefacefacefacefacefacefacefacefacefacefacefacefacefaceface";
        let subpackage_hash_query_url =
            format!("fuchsia-pkg://fuchsia.com/my_subpackage?hash={}", subpackage_hash);
        let subpackages = MetaSubpackages::from_iter(vec![(
            RelativePackageUrl::parse(subpackage_name).unwrap(),
            Hash::from_str(subpackage_hash).unwrap(),
        )]);

        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            let cm_bytes = encode_persistent_with_context(
                &fidl::encoding::Context {
                    wire_format_version: fidl::encoding::WireFormatVersion::V2,
                },
                &mut fdecl::Component::EMPTY.clone(),
            )
            .expect("failed to encode ComponentDecl FIDL");
            let parent_package_fs = pseudo_directory! {
                "meta" => pseudo_directory!{
                    "foo.cm" => read_only_static(cm_bytes.clone()),
                    "fuchsia.pkg" => pseudo_directory!{
                        "subpackages" => vfs::file::vmo::asynchronous::read_only_const(&serde_json::to_vec(&subpackages).unwrap()),
                    },
                },
            };
            let subpackage_fs = pseudo_directory! {
                "meta" => pseudo_directory!{
                    "subfoo.cm" => read_only_static(cm_bytes.clone()),
                },
            };
            let mut package_urls_to_resolve =
                vec![parent_package_url.to_owned(), subpackage_hash_query_url.to_owned()];
            let mut packages = vec![parent_package_fs, subpackage_fs];
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve { package_url, dir, responder } => {
                        let expected = package_urls_to_resolve.remove(0);
                        assert_eq!(package_url, expected, "unexpected package URL");
                        let fs = packages.remove(0);
                        fs.open(
                            ExecutionScope::new(),
                            fio::OpenFlags::RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::dot(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] })).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            let parent_component = resolve_component_without_context(&parent_component_url, &proxy)
                .await
                .expect("failed to resolve parent_component");
            let resolution_context_repr =
                parent_component.resolution_context.as_ref().map(|context| {
                    let mut parts = context.bytes.split(|b| *b == b'\0');
                    let host = String::from_utf8(parts.next().unwrap_or(&[]).to_vec())
                        .unwrap_or_else(|e| format!("{:?}({:?})", e, context.bytes));
                    let subpackages = String::from_utf8(parts.next().unwrap_or(&[]).to_vec())
                        .unwrap_or_else(|e| format!("{:?}({:?})", e, context.bytes));
                    format!("host: {}, subpackages: {}", host, subpackages)
                });
            assert_matches!(parent_component.resolution_context, Some(..));
            assert_matches!(
                resolve_component_with_context(
                    &subpackaged_component_relative_url,
                    parent_component.resolution_context.as_ref().unwrap(),
                    &proxy
                )
                .await,
                Ok(fresolution::Component {
                    decl: Some(fidl_fuchsia_mem::Data::Buffer(fidl_fuchsia_mem::Buffer { .. })),
                    ..
                }),
                "Could not resolve subpackaged component '{}' from context '{:?}'",
                subpackaged_component_relative_url,
                resolution_context_repr
            );
        };
        join!(server, client);
    }

    #[fuchsia::test]
    async fn resolve_component_fails_bad_connection() {
        let (proxy, server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        drop(server);
        assert_matches!(
            resolve_component_without_context(
                "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
                &proxy
            )
            .await,
            Err(ResolverError::IoError(_))
        );
    }

    #[fuchsia::test]
    async fn resolve_component_fails_with_package_resolver_failure() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve { responder, .. } => {
                        responder.send(&mut Err(fidl_fuchsia_pkg::ResolveError::NoSpace)).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            assert_matches!(
                resolve_component_without_context(
                    "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
                    &proxy
                )
                .await,
                Err(ResolverError::NoSpace)
            );
        };
        join!(server, client);
    }

    #[fuchsia::test]
    async fn resolve_component_fails_with_component_not_found() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            let fs = pseudo_directory! {};
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve { dir, responder, .. } => {
                        fs.clone().open(
                            ExecutionScope::new(),
                            fio::OpenFlags::RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::dot(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] })).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            assert_matches!(
                resolve_component_without_context(
                    "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
                    &proxy
                )
                .await,
                Err(ResolverError::ManifestNotFound(..))
            );
        };
        join!(server, client);
    }

    fn spawn_pkg_resolver(
        fs: Arc<impl vfs::directory::entry::DirectoryEntry>,
        mut server: PackageResolverRequestStream,
    ) -> fasync::Task<()> {
        fasync::Task::spawn(async move {
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve { package_url, dir, responder } => {
                        assert_eq!(
                            package_url, "fuchsia-pkg://fuchsia.com/test",
                            "unexpected package URL"
                        );
                        fs.clone().open(
                            ExecutionScope::new(),
                            fio::OpenFlags::RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::dot(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(fpkg::ResolutionContext { bytes: vec![] })).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        })
    }

    #[fuchsia::test]
    async fn resolve_component_succeeds_with_config() {
        let (proxy, server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let cm_bytes = encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut fdecl::Component {
                config: Some(fdecl::ConfigSchema {
                    value_source: Some(fdecl::ConfigValueSource::PackagePath(
                        "meta/test_with_config.cvf".to_string(),
                    )),
                    ..fdecl::ConfigSchema::EMPTY
                }),
                ..fdecl::Component::EMPTY
            },
        )
        .expect("failed to encode ComponentDecl FIDL");
        let cvf_bytes = encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut fconfig::ValuesData { ..fconfig::ValuesData::EMPTY },
        )
        .expect("failed to encode ValuesData FIDL");
        let fs = pseudo_directory! {
            "meta" => pseudo_directory! {
                "test_with_config.cm" => read_only_static(cm_bytes),
                "test_with_config.cvf" => read_only_static(cvf_bytes),
            },
        };
        let _pkg_resolver = spawn_pkg_resolver(fs, server);
        assert_matches!(
            resolve_component_without_context(
                "fuchsia-pkg://fuchsia.com/test#meta/test_with_config.cm",
                &proxy
            )
            .await
            .unwrap(),
            fresolution::Component {
                decl: Some(fidl_fuchsia_mem::Data::Buffer(fidl_fuchsia_mem::Buffer { .. })),
                config_values: Some(fidl_fuchsia_mem::Data::Buffer(
                    fidl_fuchsia_mem::Buffer { .. }
                )),
                ..
            }
        );
    }

    #[fuchsia::test]
    async fn resolve_component_fails_missing_config_value_file() {
        let (proxy, server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let cm_bytes = encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut fdecl::Component {
                config: Some(fdecl::ConfigSchema {
                    value_source: Some(fdecl::ConfigValueSource::PackagePath(
                        "meta/test_with_config.cvf".to_string(),
                    )),
                    ..fdecl::ConfigSchema::EMPTY
                }),
                ..fdecl::Component::EMPTY
            },
        )
        .expect("failed to encode ComponentDecl FIDL");
        let fs = pseudo_directory! {
            "meta" => pseudo_directory! {
                "test_with_config.cm" => read_only_static(cm_bytes),
            },
        };
        let _pkg_resolver = spawn_pkg_resolver(fs, server);
        assert_matches!(
            resolve_component_without_context(
                "fuchsia-pkg://fuchsia.com/test#meta/test_with_config.cm",
                &proxy
            )
            .await
            .unwrap_err(),
            ResolverError::ConfigValuesNotFound(_)
        );
    }

    #[fuchsia::test]
    async fn resolve_component_fails_bad_config_strategy() {
        let (proxy, server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let cm_bytes = encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut fdecl::Component {
                config: Some(fdecl::ConfigSchema { ..fdecl::ConfigSchema::EMPTY }),
                ..fdecl::Component::EMPTY
            },
        )
        .expect("failed to encode ComponentDecl FIDL");
        let cvf_bytes = encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut fconfig::ValuesData { ..fconfig::ValuesData::EMPTY },
        )
        .expect("failed to encode ValuesData FIDL");
        let fs = pseudo_directory! {
            "meta" => pseudo_directory! {
                "test_with_config.cm" => read_only_static(cm_bytes),
                "test_with_config.cvf" => read_only_static(cvf_bytes),
            },
        };
        let _pkg_resolver = spawn_pkg_resolver(fs, server);
        assert_matches!(
            resolve_component_without_context(
                "fuchsia-pkg://fuchsia.com/test#meta/test_with_config.cm",
                &proxy
            )
            .await
            .unwrap_err(),
            ResolverError::MissingConfigSource
        );
    }

    #[test]
    fn test_package_resolution_info() {
        let mut subpackages_map = HashMap::default();
        subpackages_map.insert(
            RelativePackageUrl::parse("subpackage_name").unwrap(),
            Hash::from_str("facefacefacefacefacefacefacefacefacefacefacefacefacefacefaceface")
                .unwrap(),
        );
        let info = PackageResolutionInfo::new(
            RepositoryUrl::parse_host("fuchsia.com".to_string()).unwrap(),
            subpackages_map,
        );
        let package_context = info.clone().into_package_context().unwrap();
        let info2 = PackageResolutionInfo::from_package_context(&package_context).unwrap();
        assert_eq!(info, info2);
    }
}
