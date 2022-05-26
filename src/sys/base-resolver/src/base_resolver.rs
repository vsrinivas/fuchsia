// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Context},
    fidl::endpoints::{ClientEnd, Proxy},
    fidl_fuchsia_component_decl as fdecl,
    fidl_fuchsia_component_resolution::{
        self as fresolution, ResolverRequest, ResolverRequestStream,
    },
    fidl_fuchsia_io as fio,
    fuchsia_component::server::ServiceFs,
    fuchsia_url::{
        errors::{ParseError as PkgUrlParseError, ResourcePathError},
        pkg_url::PkgUrl,
    },
    futures::prelude::*,
    log::*,
};

pub(crate) async fn main() -> anyhow::Result<()> {
    info!("started");

    let mut service_fs = ServiceFs::new_local();
    service_fs.dir("svc").add_fidl_service(Services::BaseResolver);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;
    let () = service_fs
        .for_each_concurrent(None, |request| async {
            match request {
                Services::BaseResolver(stream) => {
                    serve(stream)
                        .unwrap_or_else(|e| {
                            error!("failed to serve base resolver request: {:#}", e)
                        })
                        .await
                }
            }
        })
        .await;

    Ok(())
}

enum Services {
    BaseResolver(ResolverRequestStream),
}

async fn serve(mut stream: ResolverRequestStream) -> anyhow::Result<()> {
    let packages_dir = io_util::open_directory_in_namespace(
        "/pkgfs/packages",
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .context("failed to open /pkgfs")?;
    while let Some(request) =
        stream.try_next().await.context("failed to read request from FIDL stream")?
    {
        match request {
            ResolverRequest::Resolve { component_url, responder } => {
                let mut result =
                    resolve_component(&component_url, &packages_dir).await.map_err(|err| {
                        let fidl_err = (&err).into();
                        error!(
                            "failed to resolve component URL {}: {:#}",
                            &component_url,
                            anyhow::anyhow!(err)
                        );
                        fidl_err
                    });
                responder.send(&mut result).context("failed sending response")?;
            }
            ResolverRequest::ResolveWithContext { component_url: _, context: _, responder } => {
                // To be implemented in a future commit
                responder
                    .send(&mut Err(fresolution::ResolverError::Internal))
                    .expect("failed to send resolve response");
            }
        };
    }
    Ok(())
}

async fn resolve_component(
    component_url: &str,
    packages_dir: &fio::DirectoryProxy,
) -> Result<fresolution::Component, crate::ResolverError> {
    let package_url = PkgUrl::parse(component_url)?;
    let cm_path = package_url.resource().ok_or_else(|| {
        crate::ResolverError::InvalidUrl(PkgUrlParseError::InvalidResourcePath(
            ResourcePathError::PathIsEmpty,
        ))
    })?;
    let package_dir = resolve_package(&package_url, packages_dir).await?;

    let data = mem_util::open_file_data(&package_dir, cm_path)
        .await
        .map_err(crate::ResolverError::ComponentNotFound)?;
    let raw_bytes = mem_util::bytes_from_data(&data).map_err(crate::ResolverError::ReadManifest)?;
    let decl: fdecl::Component = fidl::encoding::decode_persistent(&raw_bytes[..])
        .map_err(crate::ResolverError::ParsingManifest)?;

    let config_values = if let Some(config_decl) = decl.config.as_ref() {
        // if we have a config declaration, we need to read the value file from the package dir
        let strategy =
            config_decl.value_source.as_ref().ok_or(crate::ResolverError::InvalidConfigSource)?;
        let config_path = match strategy {
            fdecl::ConfigValueSource::PackagePath(path) => path,
            other => return Err(crate::ResolverError::UnsupportedConfigSource(other.to_owned())),
        };
        Some(
            mem_util::open_file_data(&package_dir, &config_path)
                .await
                .map_err(crate::ResolverError::ConfigValuesNotFound)?,
        )
    } else {
        None
    };

    let package_dir = ClientEnd::new(
        package_dir.into_channel().expect("could not convert proxy to channel").into_zx_channel(),
    );
    Ok(fresolution::Component {
        url: Some(component_url.into()),
        decl: Some(data),
        package: Some(fresolution::Package {
            url: Some(package_url.root_url().to_string()),
            directory: Some(package_dir),
            ..fresolution::Package::EMPTY
        }),
        config_values,
        ..fresolution::Component::EMPTY
    })
}

async fn resolve_package(
    package_url: &PkgUrl,
    packages_dir: &fio::DirectoryProxy,
) -> Result<fio::DirectoryProxy, crate::ResolverError> {
    let root_url = package_url.root_url();
    if root_url.host() != "fuchsia.com" {
        return Err(crate::ResolverError::UnsupportedRepo);
    }
    if root_url.package_hash().is_some() {
        return Err(crate::ResolverError::PackageHashNotSupported);
    }
    let package_name = io_util::canonicalize_path(root_url.path());
    // Package contents are available at `packages/$PACKAGE_NAME/0`.
    let dir = io_util::directory::open_directory(
        packages_dir,
        &format!("{}/0", package_name),
        fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
    )
    .await
    .map_err(crate::ResolverError::PackageNotFound)?;
    Ok(dir)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl::encoding::encode_persistent_with_context,
        fidl::endpoints::{create_proxy, ServerEnd},
        fidl::prelude::*,
        fidl_fuchsia_component_config as fconfig, fidl_fuchsia_component_decl as fdecl,
        fidl_fuchsia_mem as fmem,
        fuchsia_zircon::Status,
        std::sync::Arc,
    };

    /// A DirectoryEntry implementation that checks whether an expected set of flags
    /// are set in the Open request.
    struct FlagVerifier(fio::OpenFlags);

    impl vfs::directory::entry::DirectoryEntry for FlagVerifier {
        fn open(
            self: Arc<Self>,
            _scope: vfs::execution_scope::ExecutionScope,
            flags: fio::OpenFlags,
            _mode: u32,
            _path: vfs::path::Path,
            server_end: ServerEnd<fio::NodeMarker>,
        ) {
            let status = if flags & self.0 != self.0 { Status::INVALID_ARGS } else { Status::OK };
            let stream = server_end.into_stream().expect("failed to create stream");
            let control_handle = stream.control_handle();
            control_handle
                .send_on_open_(
                    status.into_raw(),
                    Some(&mut fio::NodeInfo::Directory(fio::DirectoryObject {})),
                )
                .expect("failed to send OnOpen event");
            control_handle.shutdown_with_epitaph(status);
        }

        fn entry_info(&self) -> vfs::directory::entry::EntryInfo {
            vfs::directory::entry::EntryInfo::new(fio::INO_UNKNOWN, fio::DirentType::Directory)
        }
    }

    fn serve_pkgfs(
        pseudo_dir: Arc<dyn vfs::directory::entry::DirectoryEntry>,
    ) -> fio::DirectoryProxy {
        let (proxy, server_end) = create_proxy::<fio::DirectoryMarker>()
            .expect("failed to create DirectoryProxy/Server pair");
        let () = pseudo_dir.open(
            vfs::execution_scope::ExecutionScope::new(),
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
            0,
            vfs::path::Path::dot(),
            ServerEnd::new(server_end.into_channel()),
        );
        proxy
    }

    #[fuchsia::test]
    async fn resolves_package_with_executable_rights() {
        let pkg_url = PkgUrl::new_package("fuchsia.com".into(), "/test-package".into(), None)
            .expect("failed to create test PkgUrl");
        let flag_verifier = Arc::new(FlagVerifier(
            fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_EXECUTABLE,
        ));
        let pkgfs_dir = serve_pkgfs(vfs::pseudo_directory! {
            "test-package" => vfs::pseudo_directory! {
                "0" => flag_verifier,
            }
        });

        let package =
            resolve_package(&pkg_url, &pkgfs_dir).await.expect("failed to resolve package");
        let event_stream = package.take_event_stream().map_ok(|_| ());
        assert_matches!(
            event_stream.try_collect::<()>().await,
            Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. })
        );
    }

    #[fuchsia::test]
    async fn fails_to_resolve_package_unsupported_repo() {
        let pkg_url = PkgUrl::new_package("fuchsia.ca".into(), "/test-package".into(), None)
            .expect("failed to create test PkgUrl");
        let pkgfs_dir = serve_pkgfs(vfs::pseudo_directory! {});

        assert_matches!(
            resolve_package(&pkg_url, &pkgfs_dir).await,
            Err(crate::ResolverError::UnsupportedRepo)
        );
    }

    #[fuchsia::test]
    async fn fails_to_resolve_component_invalid_url() {
        let pkgfs_dir = serve_pkgfs(vfs::pseudo_directory! {});
        assert_matches!(
            resolve_component("fuchsia://fuchsia.com/foo#meta/bar.cm", &pkgfs_dir).await,
            Err(crate::ResolverError::InvalidUrl(_))
        );
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/foo", &pkgfs_dir).await,
            Err(crate::ResolverError::InvalidUrl(_))
        );
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/#meta/bar.cm", &pkgfs_dir).await,
            Err(crate::ResolverError::InvalidUrl(_))
        );
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.ca/foo#meta/bar.cm", &pkgfs_dir).await,
            Err(crate::ResolverError::UnsupportedRepo)
        );

        let url_with_hash = concat!(
            "fuchsia-pkg://fuchsia.com/test-package",
            "?hash=f241b31d5913b66c90a44d44537d6bec62672e1f05dbc4c4f22b863b01c68749",
            "#meta/test.cm"
        );
        assert_matches!(
            resolve_component(url_with_hash, &pkgfs_dir).await,
            Err(crate::ResolverError::PackageHashNotSupported)
        );
    }

    #[fuchsia::test]
    async fn fails_to_resolve_component_package_not_found() {
        let pkgfs_dir = serve_pkgfs(build_fake_pkgfs());
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/missing-package#meta/foo.cm", &pkgfs_dir)
                .await,
            Err(crate::ResolverError::PackageNotFound(_))
        );
    }

    #[fuchsia::test]
    async fn fails_to_resolve_component_missing_manifest() {
        let pkgfs_dir = serve_pkgfs(build_fake_pkgfs());
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/test-package#meta/bar.cm", &pkgfs_dir)
                .await,
            Err(crate::ResolverError::ComponentNotFound(_))
        );
    }

    #[fuchsia::test]
    async fn resolves_component_vmo_manifest() {
        let pkgfs_dir = serve_pkgfs(build_fake_pkgfs());
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/test-package#meta/vmo.cm", &pkgfs_dir)
                .await,
            Ok(fresolution::Component { decl: Some(fmem::Data::Buffer(_)), .. })
        );
    }

    #[fuchsia::test]
    async fn resolves_component_file_manifest() {
        let pkgfs_dir = serve_pkgfs(build_fake_pkgfs());
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/test-package#meta/foo.cm", &pkgfs_dir)
                .await,
            Ok(fresolution::Component {
                decl: Some(fidl_fuchsia_mem::Data::Buffer(fidl_fuchsia_mem::Buffer { .. })),
                ..
            })
        );
    }

    #[fuchsia::test]
    async fn resolves_component_with_config() {
        let pkgfs_dir = serve_pkgfs(build_fake_pkgfs());
        let component = resolve_component(
            "fuchsia-pkg://fuchsia.com/test-package#meta/foo-with-config.cm",
            &pkgfs_dir,
        )
        .await
        .unwrap();
        assert_matches!(
            component,
            fresolution::Component { decl: Some(..), config_values: Some(..), .. }
        );
    }

    #[fuchsia::test]
    async fn fails_to_resolve_component_missing_config_values() {
        let pkgfs_dir = serve_pkgfs(build_fake_pkgfs());
        let error = resolve_component(
            "fuchsia-pkg://fuchsia.com/test-package#meta/foo-without-config.cm",
            &pkgfs_dir,
        )
        .await
        .unwrap_err();
        assert_matches!(error, crate::ResolverError::ConfigValuesNotFound(..));
    }

    #[fuchsia::test]
    async fn fails_to_resolve_component_bad_config_source() {
        let pkgfs_dir = serve_pkgfs(build_fake_pkgfs());
        let error = resolve_component(
            "fuchsia-pkg://fuchsia.com/test-package#meta/foo-with-bad-config.cm",
            &pkgfs_dir,
        )
        .await
        .unwrap_err();
        assert_matches!(error, crate::ResolverError::InvalidConfigSource);
    }

    fn build_fake_pkgfs() -> Arc<dyn vfs::directory::entry::DirectoryEntry> {
        let cm_bytes = encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut fdecl::Component::EMPTY.clone(),
        )
        .expect("failed to encode ComponentDecl FIDL");

        vfs::pseudo_directory! {
            "test-package" => vfs::pseudo_directory! {
                "0" => vfs::pseudo_directory! {
                    "meta" => vfs::pseudo_directory! {
                        "foo.cm" => vfs::file::vmo::asynchronous::read_only_const(&cm_bytes),
                        "foo-with-config.cm" => vfs::file::vmo::asynchronous::read_only_const(
                            &encode_persistent_with_context(
                                &fidl::encoding::Context {
                                    wire_format_version: fidl::encoding::WireFormatVersion::V2
                                },
                                &mut fdecl::Component {
                                    config: Some(fdecl::ConfigSchema {
                                        value_source: Some(
                                            fdecl::ConfigValueSource::PackagePath(
                                                "meta/foo-with-config.cvf".to_string(),
                                            ),
                                        ),
                                        ..fdecl::ConfigSchema::EMPTY
                                    }),
                                    ..fdecl::Component::EMPTY
                                }
                            ).unwrap()
                        ),
                        "foo-with-config.cvf" => vfs::file::vmo::asynchronous::read_only_const(
                            &encode_persistent_with_context(
                                &fidl::encoding::Context {
                                    wire_format_version: fidl::encoding::WireFormatVersion::V2
                                },
                                &mut fconfig::ValuesData {
                                    ..fconfig::ValuesData::EMPTY
                                }
                            ).unwrap()
                        ),
                        "foo-with-bad-config.cm" => vfs::file::vmo::asynchronous::read_only_const(
                            &encode_persistent_with_context(
                                &fidl::encoding::Context {
                                    wire_format_version: fidl::encoding::WireFormatVersion::V2
                                },
                                &mut fdecl::Component {
                                    config: Some(fdecl::ConfigSchema {
                                        ..fdecl::ConfigSchema::EMPTY
                                    }),
                                    ..fdecl::Component::EMPTY
                                }
                            ).unwrap()
                        ),
                        "foo-without-config.cm" => vfs::file::vmo::asynchronous::read_only_const(
                            &encode_persistent_with_context(
                                &fidl::encoding::Context {
                                    wire_format_version: fidl::encoding::WireFormatVersion::V2
                                },
                                &mut fdecl::Component {
                                    config: Some(fdecl::ConfigSchema {
                                        value_source: Some(
                                            fdecl::ConfigValueSource::PackagePath(
                                                "doesnt-exist.cvf".to_string(),
                                            ),
                                        ),
                                        ..fdecl::ConfigSchema::EMPTY
                                    }),
                                    ..fdecl::Component::EMPTY
                                }
                            ).unwrap()
                        ),
                        "vmo.cm" => vfs::file::vmo::asynchronous::read_only_const(&cm_bytes),
                    }
                }
            }
        }
    }
}
