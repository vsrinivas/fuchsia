// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl::endpoints::{Proxy, ServerEnd},
    fidl_fuchsia_io as fio,
    std::sync::Arc,
};

/// Represents the sandboxed package resolver.
pub struct Resolver {
    _pkg_resolver_proxy: fidl_fuchsia_pkg::PackageResolverProxy,
    svc_dir_proxy: fidl_fuchsia_io::DirectoryProxy,
}

impl Resolver {
    pub fn new_with_proxy(
        pkg_resolver_proxy: fidl_fuchsia_pkg::PackageResolverProxy,
        directory_proxy_with_access_to_pkg_resolver: fidl_fuchsia_io::DirectoryProxy,
    ) -> Result<Self, Error> {
        Ok(Self {
            _pkg_resolver_proxy: pkg_resolver_proxy,
            svc_dir_proxy: directory_proxy_with_access_to_pkg_resolver,
        })
    }

    pub fn new() -> Result<Self, Error> {
        let svc_dir_proxy = fuchsia_fs::directory::open_in_namespace(
            "/svc",
            fidl_fuchsia_io::OpenFlags::RIGHT_READABLE | fidl_fuchsia_io::OpenFlags::RIGHT_WRITABLE,
        )
        .context("error opening svc directory")?;

        Ok(Self {
            _pkg_resolver_proxy: fuchsia_component::client::connect_to_protocol::<
                fidl_fuchsia_pkg::PackageResolverMarker,
            >()?,
            svc_dir_proxy,
        })
    }

    /// TODO(fxbug.dev/104919): delete once CFv2 migration is done
    fn clone_resolver_proxy(
        resolver_proxy: &fidl_fuchsia_io::DirectoryProxy,
    ) -> Result<fidl_fuchsia_io::DirectoryProxy, Error> {
        let (resolver_clone, remote) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()?;
        resolver_proxy.clone(
            fidl_fuchsia_io::OpenFlags::CLONE_SAME_RIGHTS,
            ServerEnd::from(remote.into_channel()),
        )?;
        Ok(resolver_clone.into_proxy()?)
    }

    #[cfg(test)]
    pub fn package_resolver_proxy(&self) -> fidl_fuchsia_pkg::PackageResolverProxy {
        self._pkg_resolver_proxy.clone()
    }

    pub fn directory_request(
        &self,
    ) -> Result<Arc<fidl::endpoints::ClientEnd<fio::DirectoryMarker>>, Error> {
        // TODO(https://fxbug.dev/108786): Use Proxy::into_client_end when available.
        Ok(std::sync::Arc::new(fidl::endpoints::ClientEnd::new(
            Self::clone_resolver_proxy(&self.svc_dir_proxy)?
                .into_channel()
                .expect("proxy into channel")
                .into_zx_channel(),
        )))
    }
}

#[cfg(test)]
pub(crate) mod for_tests {
    use {
        super::*,
        crate::cache::for_tests::CacheForTest,
        anyhow::anyhow,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_boot::{ArgumentsRequest, ArgumentsRequestStream},
        fidl_fuchsia_pkg_ext as pkg,
        fidl_fuchsia_pkg_ext::RepositoryConfigs,
        fuchsia_component_test::{
            Capability, ChildOptions, DirectoryContents, RealmBuilder, RealmInstance, Ref, Route,
        },
        fuchsia_pkg_testing::{serve::ServedRepository, Repository},
        fuchsia_url::RepositoryUrl,
        futures::StreamExt,
        futures::TryStreamExt,
    };

    const SSL_TEST_CERTS_PATH: &str = "/pkg/data/ssl/cert.pem";
    const SSL_CERT_FILE_NAME: &str = "cert.pem";
    pub const EMPTY_REPO_PATH: &str = "/pkg/empty-repo";

    /// This wraps the `Resolver` in order to reduce test boilerplate.
    pub struct ResolverForTest {
        pub cache: CacheForTest,
        pub resolver: Arc<Resolver>,
        _served_repo: ServedRepository,
        _realm_instance: RealmInstance,
    }

    impl ResolverForTest {
        async fn serve_boot_args(mut stream: ArgumentsRequestStream, channel: Option<String>) {
            while let Some(req) = stream.try_next().await.unwrap() {
                match req {
                    ArgumentsRequest::GetString { key, responder } => {
                        if key == "tuf_repo_config" {
                            responder.send(channel.as_deref()).unwrap();
                        } else {
                            eprintln!("Unexpected arguments GetString: {}, closing channel.", key);
                        }
                    }
                    _ => eprintln!("Unexpected arguments request, closing channel."),
                }
            }
        }

        // TODO(fxbug.dev/104919): Delete these mocks when all of isolated-swd is migrated to v2,
        // as the components will have the same behavior as their non-isolated counterparts.
        async fn run_mocks(
            handles: fuchsia_component_test::LocalComponentHandles,
            channel: Option<String>,
        ) -> Result<(), Error> {
            let mut fs = fuchsia_component::server::ServiceFs::new();
            fs.dir("svc")
                .add_fidl_service(move |stream| Self::serve_boot_args(stream, channel.clone()));
            fs.serve_connection(handles.outgoing_dir)?;
            let () = fs.for_each_concurrent(None, |req| async { req.await }).await;
            Ok(())
        }

        #[cfg(test)]
        pub async fn new(
            repo: Arc<Repository>,
            repo_url: RepositoryUrl,
            channel: Option<String>,
            realm_builder: RealmBuilder,
        ) -> Result<Self, Error> {
            let blobfs = blobfs_ramdisk::BlobfsRamdisk::start().context("starting blobfs").unwrap();
            let cache_ref =
                CacheForTest::realm_setup(&realm_builder, &blobfs).await.expect("setting up cache");

            let served_repo = Arc::clone(&repo).server().start()?;
            let repo_config =
                RepositoryConfigs::Version1(vec![served_repo.make_repo_config(repo_url)]);

            let cert_bytes = std::fs::read(std::path::Path::new(SSL_TEST_CERTS_PATH)).unwrap();

            let pkg_resolver = realm_builder
                .add_child("pkg-resolver", "#meta/pkg-resolver.cm", ChildOptions::new())
                .await
                .unwrap();

            realm_builder
                .read_only_directory(
                    "config-data",
                    vec![&pkg_resolver],
                    DirectoryContents::new().add_file(
                        "repositories/test.json",
                        serde_json::to_string(&repo_config).unwrap(),
                    ),
                )
                .await
                .unwrap();

            realm_builder
                .read_only_directory(
                    "root-ssl-certificates",
                    vec![&pkg_resolver],
                    DirectoryContents::new().add_file(SSL_CERT_FILE_NAME, cert_bytes),
                )
                .await
                .unwrap();

            let local_mocks = realm_builder
                .add_local_child(
                    "boot_arguments",
                    move |handles| Box::pin(Self::run_mocks(handles, channel.clone())),
                    ChildOptions::new(),
                )
                .await
                .unwrap();

            realm_builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                        .from(Ref::parent())
                        .to(&pkg_resolver),
                )
                .await
                .unwrap();

            realm_builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name("fuchsia.net.name.Lookup"))
                        .capability(Capability::protocol_by_name("fuchsia.posix.socket.Provider"))
                        .from(Ref::parent())
                        .to(&pkg_resolver),
                )
                .await
                .unwrap();

            realm_builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name("fuchsia.pkg.PackageResolver"))
                        .from(&pkg_resolver)
                        .to(Ref::parent()),
                )
                .await
                .unwrap();

            realm_builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name("fuchsia.pkg.PackageCache"))
                        .capability(Capability::protocol_by_name("fuchsia.space.Manager"))
                        .from(&cache_ref)
                        .to(&pkg_resolver),
                )
                .await
                .unwrap();

            realm_builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name("fuchsia.boot.Arguments"))
                        .from(&local_mocks)
                        .to(&pkg_resolver),
                )
                .await
                .unwrap();

            let realm_instance = realm_builder.build().await.unwrap();

            let cache = CacheForTest::new(&realm_instance, blobfs).await.unwrap();

            let resolver_proxy = realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<fidl_fuchsia_pkg::PackageResolverMarker>()
                .expect("connect to pkg resolver");

            let (resolver_clone, remote) =
                fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()?;
            realm_instance.root.get_exposed_dir().clone(
                fidl_fuchsia_io::OpenFlags::CLONE_SAME_RIGHTS,
                ServerEnd::from(remote.into_channel()),
            )?;

            let resolver =
                Resolver::new_with_proxy(resolver_proxy, resolver_clone.into_proxy()?).unwrap();

            let cache_proxy = cache.cache.package_cache_proxy().unwrap();

            assert_eq!(cache_proxy.sync().await.unwrap(), Ok(()));

            Ok(ResolverForTest {
                _realm_instance: realm_instance,
                cache,
                resolver: Arc::new(resolver),
                _served_repo: served_repo,
            })
        }

        /// Resolve a package using the resolver, returning the root directory of the package,
        /// and the context for resolving relative package URLs.
        pub async fn resolve_package(
            &self,
            url: &str,
        ) -> Result<(fio::DirectoryProxy, pkg::ResolutionContext), Error> {
            let proxy = self.resolver.package_resolver_proxy();
            let (package, package_remote) =
                fidl::endpoints::create_proxy().context("creating package directory endpoints")?;
            let resolved_context = proxy
                .resolve(url, package_remote)
                .await
                .unwrap()
                .map_err(|e| anyhow!("Package resolver error: {:?}", e))?;
            Ok((package, (&resolved_context).try_into().expect("resolver returns valid context")))
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::for_tests::{ResolverForTest, EMPTY_REPO_PATH},
        super::*,
        fuchsia_async as fasync,
        fuchsia_component_test::RealmBuilder,
        fuchsia_pkg_testing::{PackageBuilder, RepositoryBuilder},
    };

    const TEST_REPO_URL: &str = "fuchsia-pkg://test";

    #[fasync::run_singlethreaded(test)]
    pub async fn test_resolver() -> Result<(), Error> {
        let name = "test-resolver";
        let package = PackageBuilder::new(name)
            .add_resource_at("data/file1", "hello".as_bytes())
            .add_resource_at("data/file2", "hello two".as_bytes())
            .build()
            .await
            .unwrap();
        let repo = Arc::new(
            RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
                .add_package(&package)
                .build()
                .await
                .context("Building repo")
                .unwrap(),
        );

        let realm_builder = RealmBuilder::new().await.unwrap();
        let resolver =
            ResolverForTest::new(repo, TEST_REPO_URL.parse().unwrap(), None, realm_builder)
                .await
                .context("launching resolver")?;
        let (root_dir, _resolved_context) =
            resolver.resolve_package(&format!("{}/{}", TEST_REPO_URL, name)).await.unwrap();

        package.verify_contents(&root_dir).await.unwrap();
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_resolver_with_channel() -> Result<(), Error> {
        let name = "test-resolver-channel";
        let package = PackageBuilder::new(name)
            .add_resource_at("data/file1", "hello".as_bytes())
            .add_resource_at("data/file2", "hello two".as_bytes())
            .build()
            .await
            .unwrap();
        let repo = Arc::new(
            RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
                .add_package(&package)
                .build()
                .await
                .context("Building repo")
                .unwrap(),
        );

        let realm_builder = RealmBuilder::new().await.unwrap();
        let resolver = ResolverForTest::new(
            repo,
            "fuchsia-pkg://x64.resolver-test-channel.fuchsia.com".parse().unwrap(),
            Some("resolver-test-channel".to_owned()),
            realm_builder,
        )
        .await
        .context("launching resolver")?;
        let (root_dir, _resolved_context) =
            resolver.resolve_package(&format!("fuchsia-pkg://fuchsia.com/{}", name)).await.unwrap();

        package.verify_contents(&root_dir).await.unwrap();
        Ok(())
    }
}
