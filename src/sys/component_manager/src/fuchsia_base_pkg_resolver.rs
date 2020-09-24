// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{
        model::Model,
        realm::BindReason,
        resolver::{Resolver, ResolverError, ResolverFut},
        routing,
    },
    anyhow::format_err,
    cm_fidl_validator,
    fidl::endpoints::{create_proxy, ClientEnd},
    fidl_fuchsia_io::{self as fio, DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_sys2 as fsys,
    fuchsia_url::pkg_url::PkgUrl,
    futures::{channel::oneshot, lock::Mutex},
    std::{
        convert::TryInto,
        ops::DerefMut,
        path::{Path, PathBuf},
        sync::Weak,
    },
};

pub static SCHEME: &str = "fuchsia-pkg";

/// Resolves component URLs with the "fuchsia-pkg" scheme.
///
/// This is a 'pure' CFv2 fuchsia-pkg resolver, in that it does not rely on an existing package
/// resolver implementation (like fuchsia_pkg_resolver::FuchsiaPkgResolver does), and instead
/// directly implements a resolver on top of a pkgfs directory connection (i.e. through
/// /pkgfs/packages). However, because of this it is currently limited to loading packages from the
/// "base" package set".
///
/// The root component must expose a "/pkgfs" directory capability for this to work.
///
/// See the fuchsia_pkg_url crate for URL syntax.
///
/// TODO(fxbug.dev/46491): Replace this with one or more v2 resolver capabilities implemented and exposed
/// by the package system, and simply used appropriately in the component topology.
pub struct FuchsiaPkgResolver {
    model: Mutex<oneshot::Receiver<Weak<Model>>>,
    pkgfs_proxy: Mutex<Option<DirectoryProxy>>,
}

impl FuchsiaPkgResolver {
    pub fn new(model: oneshot::Receiver<Weak<Model>>) -> FuchsiaPkgResolver {
        FuchsiaPkgResolver { model: Mutex::new(model), pkgfs_proxy: Mutex::new(None) }
    }

    // Open a new directory connection to pkgfs, which (for this resolver to work) must be exposed
    // as a directory capability named '/pkgfs' from the root component to the runtime:
    // - perform capability routing
    // - bind to the appropriate component
    // - open the pkgfs directory capability
    async fn open_pkgfs(&self) -> Result<DirectoryProxy, anyhow::Error> {
        // try_recv is used instead of await so that we don't block forever if the model is never
        // provided through the channel. This shouldn't happen in practice but this lets us test the
        // behavior in this case deterministically rather than using a timeout.
        let mut model_chan = self.model.lock().await;
        let model = model_chan.deref_mut().try_recv().expect("model channel dropped");
        let model = model.and_then(|m| m.upgrade()).ok_or(ResolverError::model_not_available())?;

        // TODO(56604): Switch to name-based by finding "pkgfs" instead and updating CMLs.
        let (capability_path, realm) = routing::find_exposed_root_directory_capability(
            &model.root_realm,
            "/pkgfs".try_into().unwrap(),
        )
        .await
        .map_err(|e| format_err!("failed to route pkgfs handle: {}", e))?;
        let (pkgfs_proxy, pkgfs_server) = create_proxy::<DirectoryMarker>()?;
        let mut pkgfs_server = pkgfs_server.into_channel();
        realm
            .bind(&BindReason::BasePkgResolver)
            .await
            .map_err(|e| format_err!("failed to bind to pkgfs provider: {}", e))?
            .open_outgoing(
                fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
                fio::MODE_TYPE_DIRECTORY,
                capability_path.to_path_buf(),
                &mut pkgfs_server,
            )
            .await
            .map_err(|e| {
                format_err!("failed to open outgoing directory of pkgfs provider: {}", e)
            })?;
        Ok(pkgfs_proxy)
    }

    async fn resolve_package(
        &self,
        component_package_url: &PkgUrl,
    ) -> Result<DirectoryProxy, ResolverError> {
        // Grab the proxy_proxy lock and lazy-initialize if not already done.
        let mut pkgfs_proxy = self.pkgfs_proxy.lock().await;
        if pkgfs_proxy.is_none() {
            *pkgfs_proxy = Some(self.open_pkgfs().await.map_err(|e| {
                ResolverError::component_not_available(component_package_url.to_string(), e)
            })?);
        }

        // Package contents are available at `packages/$PACKAGE_NAME/0`.
        let root_url = component_package_url.root_url();
        let package_name = io_util::canonicalize_path(root_url.path());
        let path = PathBuf::from("packages").join(package_name).join("0");
        let dir = io_util::open_directory(
            pkgfs_proxy.as_ref().unwrap(),
            &path,
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_EXECUTABLE,
        )
        .map_err(|e| {
            ResolverError::component_not_available(
                component_package_url.to_string(),
                e.context("failed to open package directory"),
            )
        })?;
        Ok(dir)
    }

    async fn resolve_async<'a>(
        &'a self,
        component_url: &'a str,
    ) -> Result<fsys::Component, ResolverError> {
        // Parse URL.
        let component_package_url = PkgUrl::parse(component_url)
            .map_err(|e| ResolverError::url_parse_error(component_url, e))?;
        let cm_path = Path::new(
            component_package_url
                .resource()
                .ok_or(ResolverError::url_missing_resource_error(component_url))?,
        );

        // Resolve package.
        let dir = self.resolve_package(&component_package_url).await?;

        // Read component manifest from package.
        let file = io_util::open_file(&dir, cm_path, fio::OPEN_RIGHT_READABLE)
            .map_err(|e| ResolverError::manifest_not_available(component_url, e))?;
        let component_decl = io_util::read_file_fidl(&file).await.map_err(|e| {
            match e.downcast_ref::<io_util::file::ReadError>() {
                Some(_) => ResolverError::manifest_not_available(component_url, e),
                None => ResolverError::manifest_invalid(component_url, e),
            }
        })?;
        // Validate the component manifest
        cm_fidl_validator::validate(&component_decl)
            .map_err(|e| ResolverError::manifest_invalid(component_url, e))?;

        let package_dir = ClientEnd::new(
            dir.into_channel().expect("could not convert proxy to channel").into_zx_channel(),
        );
        let package = fsys::Package {
            package_url: Some(component_package_url.root_url().to_string()),
            package_dir: Some(package_dir),
        };
        Ok(fsys::Component {
            resolved_url: Some(component_url.to_string()),
            decl: Some(component_decl),
            package: Some(package),
        })
    }
}

impl Resolver for FuchsiaPkgResolver {
    fn resolve<'a>(&'a self, component_url: &'a str) -> ResolverFut {
        Box::pin(self.resolve_async(component_url))
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::model::{
            moniker::AbsoluteMoniker,
            testing::{routing_test_helpers::*, test_helpers::*},
        },
        cm_rust::*,
        directory_broker::DirectoryBroker,
        fidl::encoding::encode_persistent,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_data as fdata,
        fidl_fuchsia_io::{DirectoryRequest, NodeMarker},
        fidl_fuchsia_io2 as fio2, fuchsia_async as fasync,
        futures::prelude::*,
        std::{path::Path, sync::Arc},
        vfs::{
            self, directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::pcb::asynchronous::read_only_static, pseudo_directory,
        },
    };

    // Simulate a fake pkgfs Directory service that only contains a single package ("hello_world"),
    // using our own package directory (hosted by the real pkgfs) as the contents. In other words,
    // connect the path "packages/hello_world/0/" to "/pkg" from our namespace.
    // TODO(fxbug.dev/37534): This is implemented by manually handling the Directory.Open and forwarding
    // to the test's real package directory because Rust vfs does not yet support
    // OPEN_RIGHT_EXECUTABLE. Simplify in the future.
    struct FakePkgfs;

    impl FakePkgfs {
        pub fn new() -> DirectoryProxy {
            let (proxy, server_end) = create_proxy::<DirectoryMarker>().unwrap();
            Self::host_dir(server_end);
            proxy
        }

        fn host_dir(server_end: ServerEnd<DirectoryMarker>) {
            let mut stream = server_end.into_stream().expect("failed to create stream");
            fasync::Task::local(async move {
                while let Some(request) = stream.try_next().await.unwrap() {
                    match request {
                        DirectoryRequest::Open {
                            flags,
                            mode: _,
                            path,
                            object,
                            control_handle: _,
                        } => Self::handle_open(&path, flags, object),
                        _ => panic!("Fake doesn't support request: {:?}", request),
                    }
                }
            })
            .detach();
        }

        fn handle_open(path_str: &str, flags: u32, server_end: ServerEnd<NodeMarker>) {
            // Support opening a new connection to the fake
            if path_str == "." {
                Self::host_dir(ServerEnd::new(server_end.into_channel()));
                return;
            }

            let path = Path::new(path_str);
            let mut path_iter = path.iter();

            // "packages/" should always be the first path component used by the resolver.
            assert_eq!("packages", path_iter.next().unwrap().to_str().unwrap());

            match path_iter.next().unwrap().to_str().unwrap() {
                "hello-world" => {
                    // The next item is 0, as per pkgfs semantics. Check it and skip it.
                    assert_eq!("0", path_iter.next().unwrap().to_str().unwrap());

                    // Connect the server_end by forwarding to our real package directory, which can handle
                    // OPEN_RIGHT_EXECUTABLE. Also, pass through the input flags here to ensure that we
                    // don't artificially pass the test (i.e. the resolver needs to ask for the appropriate
                    // rights).
                    let mut open_path = PathBuf::from("/pkg");
                    open_path.extend(path_iter);
                    io_util::connect_in_namespace(
                        open_path.to_str().unwrap(),
                        server_end.into_channel(),
                        flags,
                    )
                    .expect("failed to open path in namespace");
                }
                "invalid-cm" => {
                    // Provide a cm that will fail due to multiple runners being configured.
                    let sub_dir = pseudo_directory! {
                        "meta" => pseudo_directory! {
                            "invalid.cm" => read_only_static(
                                encode_persistent(&mut fsys::ComponentDecl {
                                    program: None,
                                    uses: Some(vec![
                                        fsys::UseDecl::Runner(
                                            fsys::UseRunnerDecl {
                                                source_name: Some("elf".to_string()),
                                            }
                                        ),
                                        fsys::UseDecl::Runner (
                                            fsys::UseRunnerDecl {
                                                source_name: Some("web".to_string())
                                            }
                                        )
                                    ]),
                                    exposes: None,
                                    offers: None,
                                    capabilities: None,
                                    children: None,
                                    collections: None,
                                    environments: None,
                                    facets: None
                                }).unwrap()
                            ),
                        }
                    };
                    sub_dir.open(
                        ExecutionScope::new(),
                        fio::OPEN_RIGHT_READABLE,
                        fio::MODE_TYPE_DIRECTORY,
                        vfs::path::Path::empty(),
                        server_end,
                    );
                }
                _ => return,
            }
        }
    }

    async fn setup_fake_pkgfs_test() -> RoutingTest {
        let components = vec![(
            "pkgfs",
            ComponentDeclBuilder::new()
                .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                    source_path: "/pkgfs".try_into().unwrap(),
                    source: ExposeSource::Self_,
                    target_path: "/pkgfs".try_into().unwrap(),
                    target: ExposeTarget::Parent,
                    rights: Some(fio2::Operations::Connect),
                    subdir: None,
                }))
                .build(),
        )];
        let pkgfs_dir = DirectoryBroker::from_directory_proxy(FakePkgfs::new());
        let routing_test = RoutingTestBuilder::new("pkgfs", components)
            .add_outgoing_path("pkgfs", "/pkgfs".try_into().unwrap(), pkgfs_dir)
            .build()
            .await;
        routing_test.bind_instance(&AbsoluteMoniker::root()).await.expect("bind failed");
        routing_test
    }

    fn assert_model_not_available<T>(result: Result<T, ResolverError>) {
        let err = result.err().expect("Unexpected success");
        match &err {
            ResolverError::ComponentNotAvailable { .. } => {}
            _ => panic!("Unexpected error before model available: {}", err),
        }

        // This is nth(2) because of the anyhow::Error layer here + the ClonableError layer.
        let err = anyhow::Error::new(err);
        match err.chain().nth(2).and_then(|e| e.downcast_ref::<ResolverError>()) {
            Some(ResolverError::ModelAccessError) => {}
            Some(cause) => panic!("Unexpected error cause before model available: {}", cause),
            None => panic!("Missing error cause before model available"),
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolve_test() {
        let (sender, receiver) = oneshot::channel();
        let resolver = FuchsiaPkgResolver::new(receiver);
        let url = "fuchsia-pkg://fuchsia.com/hello-world#meta/hello-world.cm";

        // Resolve before model is sent through channel should fail.
        assert_model_not_available(resolver.resolve_async(url).await);

        let routing_test = setup_fake_pkgfs_test().await;
        sender
            .send(Arc::downgrade(&routing_test.model))
            .map_err(|_| "failed to send model")
            .unwrap();
        let component = resolver.resolve_async(url).await.expect("resolve failed");

        // Check that both the returned component manifest and the component manifest in
        // the returned package dir match the expected value. This also tests that
        // the resolver returned the right package dir.
        let fsys::Component { resolved_url, decl, package } = component;
        assert_eq!(resolved_url.unwrap(), url);
        let expected_decl = fsys::ComponentDecl {
            program: Some(fdata::Dictionary {
                entries: Some(vec![fdata::DictionaryEntry {
                    key: "binary".to_string(),
                    value: Some(Box::new(fdata::DictionaryValue::Str(
                        "bin/hello_world".to_string(),
                    ))),
                }]),
            }),
            uses: Some(vec![
                fsys::UseDecl::Runner(fsys::UseRunnerDecl { source_name: Some("elf".to_string()) }),
                fsys::UseDecl::Protocol(fsys::UseProtocolDecl {
                    source: Some(fsys::Ref::Parent(fsys::ParentRef {})),
                    source_path: Some("/svc/fuchsia.logger.LogSink".to_string()),
                    target_path: Some("/svc/fuchsia.logger.LogSink".to_string()),
                }),
            ]),
            exposes: None,
            offers: None,
            facets: None,
            capabilities: None,
            children: None,
            collections: None,
            environments: None,
        };
        assert_eq!(decl.unwrap(), expected_decl);

        let fsys::Package { package_url, package_dir } = package.unwrap();
        assert_eq!(package_url.unwrap(), "fuchsia-pkg://fuchsia.com/hello-world");

        let dir_proxy = package_dir.unwrap().into_proxy().unwrap();
        let path = Path::new("meta/hello-world.cm");
        let file_proxy = io_util::open_file(&dir_proxy, path, fio::OPEN_RIGHT_READABLE)
            .expect("could not open cm");
        assert_eq!(
            io_util::read_file_fidl::<fsys::ComponentDecl>(&file_proxy)
                .await
                .expect("could not read cm"),
            expected_decl
        );

        // Try to load an executable file, like a binary, reusing the library_loader helper that
        // opens with OPEN_RIGHT_EXECUTABLE and gets a VMO with VMO_FLAG_EXEC.
        library_loader::load_vmo(&dir_proxy, "bin/hello_world")
            .await
            .expect("failed to open executable file");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolve_model_unavailable() {
        let (sender, receiver) = oneshot::channel();
        let resolver = FuchsiaPkgResolver::new(receiver);
        let url = "fuchsia-pkg://fuchsia.com/hello-world#meta/hello-world.cm";
        sender.send(Weak::new()).map_err(|_| "failed to send").unwrap();
        assert_model_not_available(resolver.resolve_async(url).await);
    }

    macro_rules! test_resolve_error {
        ($resolver:ident, $url:expr, $resolver_error_expected:ident) => {
            let url = $url;
            let res = $resolver.resolve_async(url).await;
            match res.err().expect("unexpected success") {
                ResolverError::$resolver_error_expected { url: u, .. } => {
                    assert_eq!(u, url);
                }
                e => panic!("unexpected error {:?}", e),
            }
        };
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn resolve_errors_test() {
        // Create a FuchsiaPkgResolver with the proxy directly initialized (skip the model setup)
        let (_, receiver) = oneshot::channel();
        let resolver = FuchsiaPkgResolver {
            model: Mutex::new(receiver),
            pkgfs_proxy: Mutex::new(Some(FakePkgfs::new())),
        };
        test_resolve_error!(
            resolver,
            "fuchsia-pkg:///hello-world#meta/hello-world.cm",
            UrlParseError
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/hello-world",
            UrlMissingResourceError
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/goodbye-world#meta/hello-world.cm",
            ManifestNotAvailable
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/hello-world#meta/does_not_exist.cm",
            ManifestNotAvailable
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/hello-world#meta/component_manager_tests_invalid.cm",
            ManifestInvalid
        );
        test_resolve_error!(
            resolver,
            "fuchsia-pkg://fuchsia.com/invalid-cm#meta/invalid.cm",
            ManifestInvalid
        );
    }
}
