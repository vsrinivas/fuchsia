// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Context},
    fidl::endpoints::{create_proxy, ClientEnd, Proxy},
    fidl_fuchsia_io::{self as fio, DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_pkg::{PackageResolverMarker, PackageResolverProxy, UpdatePolicy},
    fidl_fuchsia_sys2::{self as fsys, ComponentResolverRequest, ComponentResolverRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_service, server::ServiceFs},
    fuchsia_url::{errors::ParseError as PkgUrlParseError, pkg_url::PkgUrl},
    fuchsia_zircon::Status,
    futures::prelude::*,
    log::*,
    thiserror::Error,
};

/// Wraps all hosted protocols into a single type that can be matched against
/// and dispatched.
enum IncomingRequest {
    ComponentResolver(ComponentResolverRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    fuchsia_syslog::init().expect("failed to initialize logging");
    let mut service_fs = ServiceFs::new_local();
    service_fs.dir("svc").add_fidl_service(IncomingRequest::ComponentResolver);
    service_fs.take_and_serve_directory_handle().context("failed to serve outgoing namespace")?;
    service_fs
        .for_each_concurrent(None, |IncomingRequest::ComponentResolver(stream)| async move {
            match serve(stream).await {
                Ok(()) => {}
                Err(err) => error!("failed to serve resolve request: {:?}", err),
            }
        })
        .await;

    Ok(())
}

async fn serve(mut stream: ComponentResolverRequestStream) -> Result<(), anyhow::Error> {
    let package_resolver = connect_to_service::<PackageResolverMarker>()
        .context("failed to connect to PackageResolver service")?;
    while let Some(ComponentResolverRequest::Resolve { component_url, responder }) =
        stream.try_next().await.context("failed to read request from FIDL stream")?
    {
        match resolve_component(&component_url, &package_resolver).await {
            Ok(result) => responder.send(Status::OK.into_raw(), result),
            Err(err) => {
                error!("failed to resolve component URL {}: {}", &component_url, &err);
                responder.send(err.as_zx_status(), fsys::Component::EMPTY)
            }
        }
        .context("failed sending response")?;
    }
    Ok(())
}

async fn resolve_component(
    component_url: &str,
    package_resolver: &PackageResolverProxy,
) -> Result<fsys::Component, ResolverError> {
    let package_url = PkgUrl::parse(component_url)?;
    let cm_path = package_url
        .resource()
        .ok_or_else(|| ResolverError::InvalidUrl(PkgUrlParseError::InvalidResourcePath))?;
    let package_dir = resolve_package(&package_url, package_resolver).await?;

    // Read the component manifest (.cm file) from the package directory.
    let cm_file = io_util::directory::open_file(&package_dir, cm_path, fio::OPEN_RIGHT_READABLE)
        .await
        .map_err(ResolverError::ComponentNotFound)?;
    let component_decl =
        io_util::file::read_fidl(&cm_file).await.map_err(ResolverError::InvalidManifest)?;

    let package_dir = ClientEnd::new(
        package_dir.into_channel().expect("could not convert proxy to channel").into_zx_channel(),
    );
    Ok(fsys::Component {
        resolved_url: Some(component_url.into()),
        decl: Some(component_decl),
        package: Some(fsys::Package {
            package_url: Some(package_url.root_url().to_string()),
            package_dir: Some(package_dir),
            ..fsys::Package::EMPTY
        }),
        ..fsys::Component::EMPTY
    })
}

async fn resolve_package(
    package_url: &PkgUrl,
    package_resolver: &PackageResolverProxy,
) -> Result<DirectoryProxy, ResolverError> {
    let package_url = package_url.root_url();
    let selectors = Vec::new();
    let mut update_policy = UpdatePolicy { fetch_if_absent: true, allow_old_versions: false };
    let (proxy, server_end) =
        create_proxy::<DirectoryMarker>().expect("failed to create channel pair");
    package_resolver
        .resolve(
            &package_url.to_string(),
            &mut selectors.into_iter(),
            &mut update_policy,
            server_end,
        )
        .await
        .map_err(ResolverError::PackageResolverFidlError)?
        .map_err(Status::from_raw)
        .map_err(ResolverError::PackageResolverError)?;
    Ok(proxy)
}

#[derive(Error, Debug)]
enum ResolverError {
    #[error("invalid component URL: {}", .0)]
    InvalidUrl(#[from] PkgUrlParseError),
    #[error("invalid component manifest: {}", .0)]
    InvalidManifest(#[source] anyhow::Error),
    #[error("component not found: {}", .0)]
    ComponentNotFound(#[source] io_util::node::OpenError),
    #[error("failed to communicate with package resolver: {}", .0)]
    PackageResolverFidlError(#[source] fidl::Error),
    #[error("package resolver returned an error: {}", .0)]
    PackageResolverError(#[source] Status),
}

impl ResolverError {
    fn as_zx_status(&self) -> i32 {
        match self {
            Self::InvalidUrl(_) => Status::INVALID_ARGS.into_raw(),
            Self::InvalidManifest(_) => Status::IO_INVALID.into_raw(),
            Self::ComponentNotFound(_) => Status::NOT_FOUND.into_raw(),
            Self::PackageResolverFidlError(_) => Status::INTERNAL.into_raw(),
            Self::PackageResolverError(s) => s.into_raw(),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::{encoding::encode_persistent, endpoints::ServerEnd},
        fidl_fuchsia_pkg::PackageResolverRequest,
        futures::join,
        matches::assert_matches,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::pcb::asynchronous::read_only_static, path::Path, pseudo_directory,
        },
    };

    #[fasync::run_singlethreaded(test)]
    async fn resolve_package_succeeds() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            let fs = pseudo_directory! {
                "test_file" => read_only_static(b"foo"),
            };
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve {
                        package_url,
                        selectors,
                        update_policy,
                        dir,
                        responder,
                    } => {
                        assert_eq!(
                            package_url, "fuchsia-pkg://fuchsia.com/test",
                            "unexpected package URL"
                        );
                        assert_matches!(
                            update_policy,
                            UpdatePolicy { fetch_if_absent: true, allow_old_versions: false }
                        );
                        assert!(
                            selectors.is_empty(),
                            "Call to Resolve should not contain any selectors"
                        );
                        fs.clone().open(
                            ExecutionScope::new(),
                            fio::OPEN_RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::empty(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            let result = resolve_package(
                &PkgUrl::new_package("fuchsia.com".into(), "/test".into(), None).unwrap(),
                &proxy,
            )
            .await;
            let directory = result.expect("package resolver failed unexpectedly");
            let file =
                io_util::directory::open_file(&directory, "test_file", fio::OPEN_RIGHT_READABLE)
                    .await
                    .expect("failed to open 'test_file' from package resolver directory");
            let contents = io_util::file::read(&file)
                .await
                .expect("failed to read 'test_file' contents from package resolver directory");
            assert_eq!(&contents, b"foo");
        };
        join!(server, client);
    }

    #[fasync::run_singlethreaded(test)]
    async fn resolve_component_succeeds() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            let cm_bytes = encode_persistent(&mut fsys::ComponentDecl::EMPTY.clone())
                .expect("failed to encode ComponentDecl FIDL");
            let fs = pseudo_directory! {
                "meta" => pseudo_directory!{
                    "test.cm" => read_only_static(cm_bytes),
                },
            };
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve {
                        package_url,
                        selectors,
                        update_policy,
                        dir,
                        responder,
                    } => {
                        assert_eq!(
                            package_url, "fuchsia-pkg://fuchsia.com/test",
                            "unexpected package URL"
                        );
                        assert_matches!(
                            update_policy,
                            UpdatePolicy { fetch_if_absent: true, allow_old_versions: false }
                        );
                        assert!(
                            selectors.is_empty(),
                            "Call to Resolve should not contain any selectors"
                        );
                        fs.clone().open(
                            ExecutionScope::new(),
                            fio::OPEN_RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::empty(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            assert_matches!(
                resolve_component("fuchsia-pkg://fuchsia.com/test#meta/test.cm", &proxy).await,
                Ok(_)
            );
        };
        join!(server, client);
    }

    #[fasync::run_singlethreaded(test)]
    async fn resolve_component_succeeds_with_hash() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            let cm_bytes = encode_persistent(&mut fsys::ComponentDecl::EMPTY.clone())
                .expect("failed to encode ComponentDecl FIDL");
            let fs = pseudo_directory! {
                "meta" => pseudo_directory!{
                    "test.cm" => read_only_static(cm_bytes),
                },
            };
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve {
                        package_url,
                        selectors,
                        update_policy,
                        dir,
                        responder,
                    } => {
                        assert_eq!(package_url, "fuchsia-pkg://fuchsia.com/test?hash=9e3a3f63c018e2a4db0ef93903a87714f036e3e8ff982a7a2020eca86cc4677c", "unexpected package URL");
                        assert_matches!(
                            update_policy,
                            UpdatePolicy { fetch_if_absent: true, allow_old_versions: false }
                        );
                        assert!(
                            selectors.is_empty(),
                            "Call to Resolve should not contain any selectors"
                        );
                        fs.clone().open(
                            ExecutionScope::new(),
                            fio::OPEN_RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::empty(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            assert_matches!(resolve_component("fuchsia-pkg://fuchsia.com/test?hash=9e3a3f63c018e2a4db0ef93903a87714f036e3e8ff982a7a2020eca86cc4677c#meta/test.cm", &proxy).await, Ok(_));
        };
        join!(server, client);
    }

    #[fasync::run_singlethreaded(test)]
    async fn resolve_component_fails_with_bad_manifest() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            let fs = pseudo_directory! {
                "meta" => pseudo_directory!{
                    "test.cm" => read_only_static(b"foo"),
                },
            };
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve { dir, responder, .. } => {
                        fs.clone().open(
                            ExecutionScope::new(),
                            fio::OPEN_RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::empty(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            assert_matches!(
                resolve_component("fuchsia-pkg://fuchsia.com/test#meta/test.cm", &proxy).await,
                Err(ResolverError::InvalidManifest(_))
            );
        };
        join!(server, client);
    }

    #[fasync::run_singlethreaded(test)]
    async fn resolve_component_fails_bad_connection() {
        let (proxy, server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        drop(server);
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/test#meta/test.cm", &proxy).await,
            Err(ResolverError::PackageResolverFidlError(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn resolve_component_fails_with_package_resolver_failure() {
        let (proxy, mut server) =
            fidl::endpoints::create_proxy_and_stream::<PackageResolverMarker>().unwrap();
        let server = async move {
            while let Some(request) = server.try_next().await.unwrap() {
                match request {
                    PackageResolverRequest::Resolve { responder, .. } => {
                        responder.send(&mut Err(Status::NO_SPACE.into_raw())).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            assert_matches!(
                resolve_component("fuchsia-pkg://fuchsia.com/test#meta/test.cm", &proxy).await,
                Err(ResolverError::PackageResolverError(Status::NO_SPACE))
            );
        };
        join!(server, client);
    }

    #[fasync::run_singlethreaded(test)]
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
                            fio::OPEN_RIGHT_READABLE,
                            fio::MODE_TYPE_DIRECTORY,
                            Path::empty(),
                            ServerEnd::new(dir.into_channel()),
                        );
                        responder.send(&mut Ok(())).unwrap();
                    }
                    _ => panic!("unexpected API call"),
                }
            }
        };
        let client = async move {
            assert_matches!(
                resolve_component("fuchsia-pkg://fuchsia.com/test#meta/test.cm", &proxy).await,
                Err(ResolverError::ComponentNotFound(_))
            );
        };
        join!(server, client);
    }
}
