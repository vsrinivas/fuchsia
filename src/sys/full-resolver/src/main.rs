// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context},
    fidl::endpoints::{create_proxy, ClientEnd, Proxy},
    fidl_fuchsia_component_decl as fdecl, fidl_fuchsia_component_resolution as fresolution,
    fidl_fuchsia_io as fio, fidl_fuchsia_pkg as fpkg,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    full_resolver_config::Config,
    futures::stream::{StreamExt as _, TryStreamExt as _},
    tracing::{error, info, warn},
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
    inspector.root().record_child("config", |node| config.record_inspect(node));

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
    let package_resolver = connect_to_protocol::<fpkg::PackageResolverMarker>()
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
    package_resolver: &fpkg::PackageResolverProxy,
) -> Result<fresolution::Component, ResolverError> {
    let component_url = fuchsia_url::ComponentUrl::parse(component_url)?;
    let (dir, dir_server_end) =
        create_proxy::<fio::DirectoryMarker>().map_err(ResolverError::IoError)?;
    let outgoing_context = package_resolver
        .resolve(&component_url.package_url().to_string(), dir_server_end)
        .await
        .map_err(ResolverError::IoError)?
        .map_err(ResolverError::PackageResolve)?;
    resolve_component(&component_url, dir, outgoing_context).await
}

async fn resolve_component_with_context(
    component_url: &str,
    incoming_context: &fresolution::Context,
    package_resolver: &fpkg::PackageResolverProxy,
) -> Result<fresolution::Component, ResolverError> {
    let component_url = fuchsia_url::ComponentUrl::parse(component_url)?;
    let (dir, dir_server_end) =
        create_proxy::<fio::DirectoryMarker>().map_err(ResolverError::IoError)?;
    let outgoing_context = package_resolver
        .resolve_with_context(
            &component_url.package_url().to_string(),
            &mut fpkg::ResolutionContext { bytes: incoming_context.bytes.clone() },
            dir_server_end,
        )
        .await
        .map_err(ResolverError::IoError)?
        .map_err(ResolverError::PackageResolve)?;
    resolve_component(&component_url, dir, outgoing_context).await
}

async fn resolve_component(
    component_url: &fuchsia_url::ComponentUrl,
    dir: fio::DirectoryProxy,
    outgoing_context: fpkg::ResolutionContext,
) -> Result<fresolution::Component, ResolverError> {
    // Read the component manifest (.cm file) from the package directory.
    let manifest_data = mem_util::open_file_data(&dir, component_url.resource())
        .await
        .map_err(ResolverError::ManifestNotFound)?;
    let manifest_bytes =
        mem_util::bytes_from_data(&manifest_data).map_err(ResolverError::ReadingManifest)?;
    let decl: fdecl::Component = fidl::encoding::decode_persistent(&manifest_bytes[..])
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
            mem_util::open_file_data(&dir, &config_path)
                .await
                .map_err(ResolverError::ConfigValuesNotFound)?,
        )
    } else {
        None
    };

    let dir = ClientEnd::new(
        dir.into_channel().map_err(|_| ResolverError::DirectoryProxyIntoChannel)?.into_zx_channel(),
    );
    Ok(fresolution::Component {
        url: Some(component_url.to_string()),
        resolution_context: Some(fresolution::Context { bytes: outgoing_context.bytes }),
        decl: Some(manifest_data),
        package: Some(fresolution::Package {
            url: Some(component_url.package_url().to_string()),
            directory: Some(dir),
            ..fresolution::Package::EMPTY
        }),
        config_values,
        ..fresolution::Component::EMPTY
    })
}

#[derive(thiserror::Error, Debug)]
enum ResolverError {
    #[error("invalid component URL")]
    InvalidUrl(#[from] fuchsia_url::errors::ParseError),

    #[error("manifest not found")]
    ManifestNotFound(#[source] mem_util::FileError),

    #[error("config values not found")]
    ConfigValuesNotFound(#[source] mem_util::FileError),

    #[error("IO error")]
    IoError(#[source] fidl::Error),

    #[error("failed to deal with fuchsia.mem.Data")]
    ReadingManifest(#[source] mem_util::DataError),

    #[error("failed to parse compiled manifest to check for config")]
    ParsingManifest(#[source] fidl::Error),

    #[error("component has config fields but does not have a config value lookup strategy")]
    MissingConfigSource,

    #[error("unsupported config value resolution strategy {_0:?}")]
    UnsupportedConfigStrategy(fdecl::ConfigValueSource),

    #[error("resolving the package {0:?}")]
    PackageResolve(fpkg::ResolveError),

    #[error("converting package directory proxy into an async channel")]
    DirectoryProxyIntoChannel,
}

impl From<&ResolverError> for fresolution::ResolverError {
    fn from(err: &ResolverError) -> Self {
        use {fresolution::ResolverError as ferr, ResolverError::*};
        match err {
            DirectoryProxyIntoChannel => ferr::Internal,
            InvalidUrl(_) => ferr::InvalidArgs,
            ManifestNotFound { .. } => ferr::ManifestNotFound,
            ConfigValuesNotFound { .. } => ferr::ConfigValuesNotFound,
            ReadingManifest(_) | IoError(_) => ferr::Io,
            ParsingManifest(..) | MissingConfigSource | UnsupportedConfigStrategy(..) => {
                ferr::InvalidManifest
            }
            PackageResolve(e) => {
                use fidl_fuchsia_pkg::ResolveError as PkgErr;
                match e {
                    PkgErr::PackageNotFound | PkgErr::BlobNotFound => ferr::PackageNotFound,
                    PkgErr::RepoNotFound
                    | PkgErr::UnavailableBlob
                    | PkgErr::UnavailableRepoMetadata => ferr::ResourceUnavailable,
                    PkgErr::NoSpace => ferr::NoSpace,
                    PkgErr::AccessDenied | PkgErr::Internal => ferr::Internal,
                    PkgErr::Io => ferr::Io,
                    PkgErr::InvalidUrl | PkgErr::InvalidContext => ferr::InvalidArgs,
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::Error,
        assert_matches::assert_matches,
        fidl_fuchsia_component_config as fconfig, fidl_fuchsia_component_decl as fdecl,
        fidl_fuchsia_io as fio, fuchsia_async as fasync,
        fuchsia_component::server as fserver,
        fuchsia_component_test::{
            Capability, ChildOptions, LocalComponentHandles, RealmBuilder, Ref, Route,
        },
        futures::{channel::mpsc, lock::Mutex, SinkExt as _},
        std::{boxed::Box, sync::Arc},
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::vmo::asynchronous::read_only_static, path::Path, pseudo_directory,
        },
    };

    async fn mock_pkg_resolver(
        trigger: Arc<Mutex<Option<mpsc::Sender<Result<(), Error>>>>>,
        handles: LocalComponentHandles,
    ) -> Result<(), Error> {
        let mut fs = fserver::ServiceFs::new();
        fs.dir("svc").add_fidl_service(
            move |mut req_stream: fpkg::PackageResolverRequestStream| {
                let tx = trigger.clone();
                fasync::Task::local(async move {
                    while let Some(fpkg::PackageResolverRequest::Resolve { responder, .. }) =
                        req_stream.try_next().await.expect("Serving package resolver stream failed")
                    {
                        responder
                            .send(&mut Err(fpkg::ResolveError::PackageNotFound))
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

    async fn component_requester(
        trigger: Arc<Mutex<Option<mpsc::Sender<Result<(), Error>>>>>,
        url: String,
        handles: LocalComponentHandles,
    ) -> Result<(), Error> {
        let resolver_proxy = handles.connect_to_protocol::<fresolution::ResolverMarker>()?;
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
    async fn fidl_wiring_and_serving() {
        let (sender, mut receiver) = mpsc::channel(2);
        let sender = Arc::new(Mutex::new(Some(sender)));
        let builder = RealmBuilder::new().await.expect("Failed to create test realm builder");
        let full_resolver = builder
            .add_child("full-resolver", "#meta/full-resolver.cm", ChildOptions::new())
            .await
            .expect("Failed add full-resolver to test topology");
        let fake_pkg_resolver = builder
            .add_local_child(
                "fake-pkg-resolver",
                {
                    let sender = sender.clone();
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
                    let sender = sender.clone();
                    move |handles: LocalComponentHandles| {
                        Box::pin(component_requester(
                            sender.clone(),
                            "fuchsia-pkg://fuchsia.com/test-pkg-request#meta/test-component.cm"
                                .to_owned(),
                            handles,
                        ))
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
    async fn resolve_component_without_context_forwards_to_pkg_resolver_and_returns_context() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<fpkg::PackageResolverMarker>().unwrap();
        let server = async move {
            let cm_bytes =
                fidl::encoding::encode_persistent(&mut fdecl::Component::EMPTY.clone()).unwrap();
            let fs = pseudo_directory! {
                "meta" => pseudo_directory! {
                    "test.cm" => read_only_static(cm_bytes),
                },
            };
            match server.try_next().await.unwrap().expect("client makes one request") {
                fpkg::PackageResolverRequest::Resolve { package_url, dir, responder } => {
                    assert_eq!(package_url, "fuchsia-pkg://fuchsia.example/test");
                    fs.clone().open(
                        ExecutionScope::new(),
                        fio::OpenFlags::RIGHT_READABLE,
                        fio::MODE_TYPE_DIRECTORY,
                        Path::dot(),
                        dir.into_channel().into(),
                    );
                    responder
                        .send(&mut Ok(fpkg::ResolutionContext {
                            bytes: b"context-contents".to_vec(),
                        }))
                        .unwrap();
                }
                _ => panic!("unexpected API call"),
            }
            assert_matches!(server.try_next().await, Ok(None));
        };
        let client = async move {
            assert_matches!(
                resolve_component_without_context(
                    "fuchsia-pkg://fuchsia.example/test#meta/test.cm",
                    &proxy
                )
                .await,
                Ok(fresolution::Component {
                    decl: Some(fidl_fuchsia_mem::Data::Buffer(fidl_fuchsia_mem::Buffer { .. })),
                    resolution_context: Some(fresolution::Context { bytes }),
                    ..
                })
                    if bytes == b"context-contents".to_vec()
            );
        };
        let ((), ()) = futures::join!(server, client);
    }

    #[fuchsia::test]
    async fn resolve_component_with_context_forwards_to_pkg_resolver_and_returns_context() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<fpkg::PackageResolverMarker>().unwrap();
        let server = async move {
            let cm_bytes =
                fidl::encoding::encode_persistent(&mut fdecl::Component::EMPTY.clone()).unwrap();
            let fs = pseudo_directory! {
                "meta" => pseudo_directory! {
                    "test.cm" => read_only_static(cm_bytes),
                },
            };
            match server.try_next().await.unwrap().expect("client makes one request") {
                fpkg::PackageResolverRequest::ResolveWithContext {
                    package_url,
                    context,
                    dir,
                    responder,
                } => {
                    assert_eq!(package_url, "fuchsia-pkg://fuchsia.example/test");
                    assert_eq!(
                        context,
                        fpkg::ResolutionContext { bytes: b"incoming-context".to_vec() }
                    );
                    fs.clone().open(
                        ExecutionScope::new(),
                        fio::OpenFlags::RIGHT_READABLE,
                        fio::MODE_TYPE_DIRECTORY,
                        Path::dot(),
                        dir.into_channel().into(),
                    );
                    responder
                        .send(&mut Ok(fpkg::ResolutionContext {
                            bytes: b"outgoing-context".to_vec(),
                        }))
                        .unwrap();
                }
                _ => panic!("unexpected API call"),
            }
            assert_matches!(server.try_next().await, Ok(None));
        };
        let client = async move {
            assert_matches!(
                resolve_component_with_context(
                    "fuchsia-pkg://fuchsia.example/test#meta/test.cm",
                    &fresolution::Context{ bytes: b"incoming-context".to_vec()},
                    &proxy
                )
                .await,
                Ok(fresolution::Component {
                    decl: Some(fidl_fuchsia_mem::Data::Buffer(fidl_fuchsia_mem::Buffer { .. })),
                    resolution_context: Some(fresolution::Context { bytes }),
                    ..
                })
                    if bytes == b"outgoing-context".to_vec()
            );
        };
        let ((), ()) = futures::join!(server, client);
    }

    #[fuchsia::test]
    async fn resolve_component_without_context_fails_bad_connection() {
        let (proxy, _) =
            fidl::endpoints::create_proxy_and_stream::<fpkg::PackageResolverMarker>().unwrap();
        assert_matches!(
            resolve_component_without_context(
                "fuchsia-pkg://fuchsia.example/test#meta/test.cm",
                &proxy
            )
            .await,
            Err(ResolverError::IoError(_))
        );
    }

    #[fuchsia::test]
    async fn resolve_component_with_context_fails_bad_connection() {
        let (proxy, _) =
            fidl::endpoints::create_proxy_and_stream::<fpkg::PackageResolverMarker>().unwrap();
        assert_matches!(
            resolve_component_with_context(
                "fuchsia-pkg://fuchsia.example/test#meta/test.cm",
                &fresolution::Context { bytes: vec![] },
                &proxy
            )
            .await,
            Err(ResolverError::IoError(_))
        );
    }

    #[fuchsia::test]
    async fn resolve_component_without_context_fails_with_package_resolver_failure() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<fpkg::PackageResolverMarker>().unwrap();
        let server = async move {
            match server.try_next().await.unwrap().expect("client makes one request") {
                fpkg::PackageResolverRequest::Resolve { responder, .. } => {
                    responder.send(&mut Err(fpkg::ResolveError::NoSpace)).unwrap();
                }
                _ => panic!("unexpected API call"),
            }
            assert_matches!(server.try_next().await, Ok(None));
        };
        let client = async move {
            assert_matches!(
                resolve_component_without_context(
                    "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
                    &proxy
                )
                .await,
                Err(ResolverError::PackageResolve(fpkg::ResolveError::NoSpace))
            );
        };
        let ((), ()) = futures::join!(server, client);
    }

    #[fuchsia::test]
    async fn resolve_component_with_context_fails_with_package_resolver_failure() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<fpkg::PackageResolverMarker>().unwrap();
        let server = async move {
            match server.try_next().await.unwrap().expect("client makes one request") {
                fpkg::PackageResolverRequest::ResolveWithContext { responder, .. } => {
                    responder.send(&mut Err(fpkg::ResolveError::NoSpace)).unwrap();
                }
                _ => panic!("unexpected API call"),
            }
            assert_matches!(server.try_next().await, Ok(None));
        };
        let client = async move {
            assert_matches!(
                resolve_component_with_context(
                    "fuchsia-pkg://fuchsia.com/test#meta/test.cm",
                    &fresolution::Context { bytes: vec![] },
                    &proxy
                )
                .await,
                Err(ResolverError::PackageResolve(fpkg::ResolveError::NoSpace))
            );
        };
        let ((), ()) = futures::join!(server, client);
    }

    #[fuchsia::test]
    async fn resolve_component_fails_with_component_not_found() {
        let (dir, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        pseudo_directory! {}.clone().open(
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            dir_server.into_channel().into(),
        );

        assert_matches!(
            resolve_component(
                &"fuchsia-pkg://fuchsia.com/test#meta/test.cm".parse().unwrap(),
                dir,
                fpkg::ResolutionContext { bytes: vec![] }
            )
            .await,
            Err(ResolverError::ManifestNotFound(..))
        );
    }

    #[fuchsia::test]
    async fn resolve_component_succeeds_with_config() {
        let cm_bytes = fidl::encoding::encode_persistent(&mut fdecl::Component {
            config: Some(fdecl::ConfigSchema {
                value_source: Some(fdecl::ConfigValueSource::PackagePath(
                    "meta/test_with_config.cvf".to_owned(),
                )),
                ..fdecl::ConfigSchema::EMPTY
            }),
            ..fdecl::Component::EMPTY
        })
        .unwrap();
        let expected_config = fconfig::ValuesData {
            values: Some(vec![fidl_fuchsia_component_config::ValueSpec {
                value: Some(fidl_fuchsia_component_config::Value::Single(
                    fidl_fuchsia_component_config::SingleValue::Uint8(3),
                )),
                ..fidl_fuchsia_component_config::ValueSpec::EMPTY
            }]),
            ..fconfig::ValuesData::EMPTY
        };
        let cvf_bytes = fidl::encoding::encode_persistent(&mut expected_config.clone()).unwrap();
        let (dir, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        pseudo_directory! {
            "meta" => pseudo_directory! {
                "test_with_config.cm" => read_only_static(cm_bytes),
                "test_with_config.cvf" => read_only_static(cvf_bytes),
            },
        }
        .clone()
        .open(
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            dir_server.into_channel().into(),
        );

        assert_matches!(
            resolve_component(
                &"fuchsia-pkg://fuchsia.example/test#meta/test_with_config.cm".parse().unwrap(),
                dir,
                fpkg::ResolutionContext{ bytes: vec![]}
            )
            .await
            .unwrap(),
            fresolution::Component {
                decl: Some(fidl_fuchsia_mem::Data::Buffer(fidl_fuchsia_mem::Buffer { .. })),
                config_values: Some(data),
                ..
            }
                if {
                    let raw_bytes = mem_util::bytes_from_data(&data).unwrap();
                    let actual_config: fconfig::ValuesData = fidl::encoding::decode_persistent(&raw_bytes[..]).unwrap();
                    assert_eq!(actual_config, expected_config);
                    true
                }
        );
    }

    #[fuchsia::test]
    async fn resolve_component_fails_missing_config_value_file() {
        let cm_bytes = fidl::encoding::encode_persistent(&mut fdecl::Component {
            config: Some(fdecl::ConfigSchema {
                value_source: Some(fdecl::ConfigValueSource::PackagePath(
                    "meta/test_with_config.cvf".to_string(),
                )),
                ..fdecl::ConfigSchema::EMPTY
            }),
            ..fdecl::Component::EMPTY
        })
        .unwrap();
        let (dir, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        pseudo_directory! {
            "meta" => pseudo_directory! {
                "test_with_config.cm" => read_only_static(cm_bytes),
            },
        }
        .clone()
        .open(
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            dir_server.into_channel().into(),
        );

        assert_matches!(
            resolve_component(
                &"fuchsia-pkg://fuchsia.example/test#meta/test_with_config.cm".parse().unwrap(),
                dir,
                fpkg::ResolutionContext { bytes: vec![] }
            )
            .await,
            Err(ResolverError::ConfigValuesNotFound(_))
        );
    }

    #[fuchsia::test]
    async fn resolve_component_fails_bad_config_strategy() {
        let cm_bytes = fidl::encoding::encode_persistent(&mut fdecl::Component {
            config: Some(fdecl::ConfigSchema::EMPTY.clone()),
            ..fdecl::Component::EMPTY
        })
        .unwrap();
        let cvf_bytes =
            fidl::encoding::encode_persistent(&mut fconfig::ValuesData::EMPTY.clone()).unwrap();
        let (dir, dir_server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
        pseudo_directory! {
            "meta" => pseudo_directory! {
                "test_with_config.cm" => read_only_static(cm_bytes),
                "test_with_config.cvf" => read_only_static(cvf_bytes),
            },
        }
        .clone()
        .open(
            ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            dir_server.into_channel().into(),
        );

        assert_matches!(
            resolve_component(
                &"fuchsia-pkg://fuchsia.com/test#meta/test_with_config.cm".parse().unwrap(),
                dir,
                fpkg::ResolutionContext { bytes: vec![] }
            )
            .await,
            Err(ResolverError::MissingConfigSource)
        );
    }
}
