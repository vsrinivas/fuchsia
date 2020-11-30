// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{self, Context},
    fidl::endpoints::{ClientEnd, Proxy},
    fidl_fuchsia_io::{self as fio, DirectoryProxy},
    fidl_fuchsia_sys2::{self as fsys, ComponentResolverRequest, ComponentResolverRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
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
    let pkgfs_dir = io_util::open_directory_in_namespace(
        "/pkgfs",
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
    )
    .context("failed to open /pkgfs")?;
    while let Some(ComponentResolverRequest::Resolve { component_url, responder }) =
        stream.try_next().await.context("failed to read request from FIDL stream")?
    {
        match resolve_component(&component_url, &pkgfs_dir).await {
            Ok(result) => responder.send(Status::OK.into_raw(), result),
            Err(err) => {
                error!("failed to resolve component URL {}: {}", &component_url, &err);
                responder.send(err.as_zx_status(), fsys::Component::empty())
            }
        }
        .context("failed sending response")?;
    }
    Ok(())
}

async fn resolve_component(
    component_url: &str,
    pkgfs_dir: &DirectoryProxy,
) -> Result<fsys::Component, ResolverError> {
    let package_url = PkgUrl::parse(component_url)?;
    let cm_path = package_url
        .resource()
        .ok_or_else(|| ResolverError::InvalidUrl(PkgUrlParseError::InvalidResourcePath))?;
    let package_dir = resolve_package(&package_url, pkgfs_dir).await?;

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
            ..fsys::Package::empty()
        }),
        ..fsys::Component::empty()
    })
}

async fn resolve_package(
    package_url: &PkgUrl,
    pkgfs_dir: &DirectoryProxy,
) -> Result<DirectoryProxy, ResolverError> {
    let root_url = package_url.root_url();
    if root_url.host() != "fuchsia.com" {
        return Err(ResolverError::UnsupportedRepo);
    }
    let package_name = io_util::canonicalize_path(root_url.path());
    // Package contents are available at `packages/$PACKAGE_NAME/0`.
    let dir = io_util::directory::open_directory(
        pkgfs_dir,
        &format!("packages/{}/0", package_name),
        fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
    )
    .await
    .map_err(ResolverError::PackageNotFound)?;
    Ok(dir)
}

#[derive(Error, Debug)]
enum ResolverError {
    #[error("invalid component URL: {}", .0)]
    InvalidUrl(#[from] PkgUrlParseError),
    #[error("the hostname refers to an unsupported repo")]
    UnsupportedRepo,
    #[error("invalid component manifest: {}", .0)]
    InvalidManifest(#[source] anyhow::Error),
    #[error("component not found: {}", .0)]
    ComponentNotFound(#[source] io_util::node::OpenError),
    #[error("package not found: {}", .0)]
    PackageNotFound(#[source] io_util::node::OpenError),
}

impl ResolverError {
    fn as_zx_status(&self) -> i32 {
        match self {
            Self::InvalidUrl(_) => Status::INVALID_ARGS.into_raw(),
            Self::UnsupportedRepo => Status::NOT_SUPPORTED.into_raw(),
            Self::InvalidManifest(_) => Status::IO_INVALID.into_raw(),
            Self::ComponentNotFound(_) => Status::NOT_FOUND.into_raw(),
            Self::PackageNotFound(_) => Status::NOT_FOUND.into_raw(),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fake_pkgfs::{Entry, MockDir, MockFile},
        fidl::encoding::encode_persistent,
        fidl::endpoints::{create_proxy, RequestStream, ServerEnd},
        fidl_fuchsia_io::{DirectoryMarker, DirectoryObject, NodeInfo, NodeMarker},
        matches::assert_matches,
        std::sync::Arc,
    };

    /// A DirectoryEntry implementation that checks whether an expected set of flags
    /// are set in the Open request.
    struct FlagVerifier(u32);

    impl Entry for FlagVerifier {
        fn open(
            self: Arc<Self>,
            flags: u32,
            _mode: u32,
            _path: &str,
            server_end: ServerEnd<NodeMarker>,
        ) {
            let status = if flags & self.0 != self.0 { Status::INVALID_ARGS } else { Status::OK };
            let stream = server_end.into_stream().expect("failed to create stream");
            let control_handle = stream.control_handle();
            control_handle
                .send_on_open_(
                    status.into_raw(),
                    Some(&mut NodeInfo::Directory(DirectoryObject {})),
                )
                .expect("failed to send OnOpen event");
            control_handle.shutdown_with_epitaph(status);
        }
    }

    fn serve_pkgfs(pseudo_dir: Arc<dyn Entry>) -> Result<DirectoryProxy, anyhow::Error> {
        let (proxy, server_end) = create_proxy::<DirectoryMarker>()
            .context("failed to create DirectoryProxy/Server pair")?;
        pseudo_dir.open(
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
            fio::MODE_TYPE_DIRECTORY,
            ".",
            ServerEnd::new(server_end.into_channel()),
        );
        Ok(proxy)
    }

    #[fasync::run_singlethreaded(test)]
    async fn resolves_package_with_executable_rights() {
        let pkg_url = PkgUrl::new_package("fuchsia.com".into(), "/test-package".into(), None)
            .expect("failed to create test PkgUrl");
        let flag_verifier =
            Arc::new(FlagVerifier(fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE));
        let pkgfs_dir =
            serve_pkgfs(Arc::new(MockDir::new().add_entry(
                "packages",
                Arc::new(MockDir::new().add_entry(
                    "test-package",
                    Arc::new(MockDir::new().add_entry("0", flag_verifier)),
                )),
            )))
            .expect("failed to serve pkgfs");

        let package =
            resolve_package(&pkg_url, &pkgfs_dir).await.expect("failed to resolve package");
        let event_stream = package.take_event_stream().map_ok(|_| ());
        assert_matches!(
            event_stream.try_collect::<()>().await,
            Err(fidl::Error::ClientChannelClosed { status: Status::OK, .. })
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn fails_to_resolve_package_unsupported_repo() {
        let pkg_url = PkgUrl::new_package("fuchsia.ca".into(), "/test-package".into(), None)
            .expect("failed to create test PkgUrl");
        let pkgfs_dir = serve_pkgfs(Arc::new(MockDir::new())).expect("failed to serve pkgfs");
        assert_matches!(
            resolve_package(&pkg_url, &pkgfs_dir).await,
            Err(ResolverError::UnsupportedRepo)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn fails_to_resolve_component_invalid_url() {
        let pkgfs_dir = serve_pkgfs(Arc::new(MockDir::new())).expect("failed to serve pkgfs");
        assert_matches!(
            resolve_component("fuchsia://fuchsia.com/foo#meta/bar.cm", &pkgfs_dir).await,
            Err(ResolverError::InvalidUrl(_))
        );
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/foo", &pkgfs_dir).await,
            Err(ResolverError::InvalidUrl(_))
        );
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/#meta/bar.cm", &pkgfs_dir).await,
            Err(ResolverError::InvalidUrl(_))
        );
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.ca/foo#meta/bar.cm", &pkgfs_dir).await,
            Err(ResolverError::UnsupportedRepo)
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn fails_to_resolve_component_package_not_found() {
        let pkgfs_dir = serve_pkgfs(build_fake_pkgfs()).expect("failed to serve pkgfs");
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/missing-package#meta/foo.cm", &pkgfs_dir)
                .await,
            Err(ResolverError::PackageNotFound(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn fails_to_resolve_component_missing_manifest() {
        let pkgfs_dir = serve_pkgfs(build_fake_pkgfs()).expect("failed to serve pkgfs");
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/test-package#meta/bar.cm", &pkgfs_dir)
                .await,
            Err(ResolverError::ComponentNotFound(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn fails_to_resolve_component_invalid_manifest() {
        let pkgfs_dir = serve_pkgfs(build_fake_pkgfs()).expect("failed to serve pkgfs");
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/test-package#meta/bad.cm", &pkgfs_dir)
                .await,
            Err(ResolverError::InvalidManifest(_))
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn resolves_component_successfully() {
        fuchsia_syslog::init().unwrap();
        fuchsia_syslog::set_severity(fuchsia_syslog::levels::INFO);
        let pkgfs_dir = serve_pkgfs(build_fake_pkgfs()).expect("failed to serve pkgfs");
        assert_matches!(
            resolve_component("fuchsia-pkg://fuchsia.com/test-package#meta/foo.cm", &pkgfs_dir)
                .await,
            Ok(_)
        );
    }

    fn build_fake_pkgfs() -> Arc<MockDir> {
        let cm_bytes = encode_persistent(&mut fsys::ComponentDecl::empty())
            .expect("failed to encode ComponentDecl FIDL");
        Arc::new(
            MockDir::new().add_entry(
                "packages",
                Arc::new(
                    MockDir::new().add_entry(
                        "test-package",
                        Arc::new(
                            MockDir::new().add_entry(
                                "0",
                                Arc::new(
                                    MockDir::new().add_entry(
                                        "meta",
                                        Arc::new(
                                            MockDir::new()
                                                .add_entry(
                                                    "foo.cm",
                                                    Arc::new(MockFile::new(cm_bytes)),
                                                )
                                                .add_entry(
                                                    "bad.cm",
                                                    Arc::new(MockFile::new(b"foo".to_vec())),
                                                ),
                                        ),
                                    ),
                                ),
                            ),
                        ),
                    ),
                ),
            ),
        )
    }
}
