// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests pkg-resolver's resolve keeps working when
/// MinFs is broken.
use {
    super::*,
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_io::{
        DirectoryControlHandle, DirectoryRequest, DirectoryRequestStream, NodeMarker,
    },
    fidl_fuchsia_pkg_ext::RepositoryConfig,
    fidl_fuchsia_pkg_rewrite_ext::Rule,
    fuchsia_pkg_testing::{serve::ServedRepository, RepositoryBuilder},
    futures::future::BoxFuture,
    std::sync::{
        atomic::{AtomicBool, AtomicU64},
        Arc,
    },
};

trait OpenRequestHandler {
    fn handle_open_request(
        &self,
        flags: u32,
        mode: u32,
        path: String,
        object: ServerEnd<NodeMarker>,
        control_handle: DirectoryControlHandle,
    );
}

struct DirectoryStreamHandler<O> {
    open_handler: Arc<O>,
}

impl<O> DirectoryStreamHandler<O>
where
    O: OpenRequestHandler + Send + Sync + 'static,
{
    fn new(open_handler: Arc<O>) -> Self {
        Self { open_handler }
    }

    fn handle_stream(
        self: Arc<Self>,
        mut stream: DirectoryRequestStream,
    ) -> BoxFuture<'static, ()> {
        async move {
            while let Some(req) = stream.next().await {
                match req.unwrap() {
                    DirectoryRequest::Clone { flags, object, control_handle: _ } => {
                        let stream = DirectoryRequestStream::from_channel(
                            fasync::Channel::from_channel(object.into_channel()).unwrap(),
                        );
                        crate::mock_filesystem::describe_dir(flags, &stream);
                        fasync::spawn(Arc::clone(&self).handle_stream(stream));
                    }
                    DirectoryRequest::Open { flags, mode, path, object, control_handle } => self
                        .open_handler
                        .handle_open_request(flags, mode, path, object, control_handle),
                    other => panic!("unhandled request type: {:?}", other),
                }
            }
        }
        .boxed()
    }
}

struct OpenFailOrTempFs {
    should_fail: AtomicBool,
    fail_count: AtomicU64,
    tempdir: tempfile::TempDir,
}

impl OpenFailOrTempFs {
    fn new_failing() -> Arc<Self> {
        Arc::new(Self {
            should_fail: AtomicBool::new(true),
            fail_count: AtomicU64::new(0),
            tempdir: tempfile::tempdir().expect("/tmp to exist"),
        })
    }

    fn get_fail_count(&self) -> u64 {
        self.fail_count.load(std::sync::atomic::Ordering::SeqCst)
    }

    fn make_succeed(&self) {
        self.should_fail.store(false, std::sync::atomic::Ordering::SeqCst);
    }

    fn should_fail(&self) -> bool {
        self.should_fail.load(std::sync::atomic::Ordering::SeqCst)
    }
}

impl OpenRequestHandler for OpenFailOrTempFs {
    fn handle_open_request(
        &self,
        flags: u32,
        mode: u32,
        path: String,
        object: ServerEnd<NodeMarker>,
        control_handle: DirectoryControlHandle,
    ) {
        if self.should_fail() {
            self.fail_count.fetch_add(1, std::sync::atomic::Ordering::SeqCst);
            control_handle.send_on_open_(Status::NO_MEMORY.into_raw(), None).expect("send on open");
        } else {
            let (tempdir_proxy, server_end) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
            fdio::service_connect(self.tempdir.path().to_str().unwrap(), server_end.into_channel())
                .unwrap();
            tempdir_proxy.open(flags, mode, &path, object).unwrap();
        }
    }
}

async fn create_testenv_serves_repo(
    open_handler: Arc<OpenFailOrTempFs>,
) -> (TestEnv, RepositoryConfig, Package, ServedRepository) {
    // Create testenv with failing isolated-persistent-storage
    let directory_handler = Arc::new(DirectoryStreamHandler::new(open_handler));
    let (proxy, stream) =
        fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_io::DirectoryMarker>().unwrap();
    fasync::spawn(directory_handler.handle_stream(stream));
    let mounts = Mounts {
        pkg_resolver_data: DirOrProxy::Proxy(proxy),
        pkg_resolver_config_data: DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist")),
    };
    let env = TestEnvBuilder::new().mounts(mounts).build();

    // Serve repo with package
    let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
    let repo = Arc::new(
        RepositoryBuilder::from_template_dir(EMPTY_REPO_PATH)
            .add_package(&pkg)
            .build()
            .await
            .unwrap(),
    );
    let served_repository = repo.server().start().unwrap();
    let repo_url = "fuchsia-pkg://example.com".parse().unwrap();
    let config = served_repository.make_repo_config(repo_url);

    (env, config, pkg, served_repository)
}

#[fasync::run_singlethreaded(test)]
async fn minfs_fails_create_repo_configs() {
    let open_handler = OpenFailOrTempFs::new_failing();
    let (mut env, config, pkg, _served_repo) =
        create_testenv_serves_repo(Arc::clone(&open_handler)).await;

    // Verify we can resolve the package with a broken MinFs, and that repo configs do not persist
    env.proxies.repo_manager.add(config.clone().into()).await.unwrap();
    let package_dir = env.resolve_package("fuchsia-pkg://example.com/just_meta_far").await.unwrap();
    pkg.verify_contents(&package_dir).await.unwrap();
    assert_eq!(open_handler.get_fail_count(), 3);
    env.restart_pkg_resolver().await;
    assert_eq!(
        crate::dynamic_repositories_disabled::get_repos(&env.proxies.repo_manager).await,
        vec![]
    );
    assert_eq!(open_handler.get_fail_count(), 5);

    // Now let MinFs recover and show how repo configs are saved on restart
    open_handler.make_succeed();
    env.proxies.repo_manager.add(config.clone().into()).await.unwrap();
    let package_dir = env.resolve_package("fuchsia-pkg://example.com/just_meta_far").await.unwrap();
    pkg.verify_contents(&package_dir).await.unwrap();
    assert_eq!(open_handler.get_fail_count(), 5);
    env.restart_pkg_resolver().await;
    assert_eq!(
        crate::dynamic_repositories_disabled::get_repos(&env.proxies.repo_manager).await,
        vec![config.clone()]
    );

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn minfs_fails_create_rewrite_rules() {
    let open_handler = OpenFailOrTempFs::new_failing();
    let (mut env, config, pkg, _served_repo) =
        create_testenv_serves_repo(Arc::clone(&open_handler)).await;

    // Add repo config and rewrite rules
    env.proxies.repo_manager.add(config.clone().into()).await.unwrap();
    let (edit_transaction, edit_transaction_server) = fidl::endpoints::create_proxy().unwrap();
    env.proxies.rewrite_engine.start_edit_transaction(edit_transaction_server).unwrap();
    let rule = Rule::new("should_be_rewritten", "example.com", "/", "/").unwrap();
    edit_transaction.add(&mut rule.clone().into()).await.unwrap();
    edit_transaction.commit().await.unwrap();

    // Verify we can resolve the package with a broken MinFs, and that rewrite rules do not persist
    let package_dir =
        env.resolve_package("fuchsia-pkg://should_be_rewritten/just_meta_far").await.unwrap();
    pkg.verify_contents(&package_dir).await.unwrap();
    assert_eq!(open_handler.get_fail_count(), 4);
    env.restart_pkg_resolver().await;
    assert_eq!(
        crate::dynamic_rewrite_disabled::get_rules(&env.proxies.rewrite_engine).await,
        vec![]
    );
    assert_eq!(open_handler.get_fail_count(), 6);

    // Now let MinFs recover and show how rewrite rules are saved on restart
    open_handler.make_succeed();
    env.proxies.repo_manager.add(config.clone().into()).await.unwrap();
    let (edit_transaction, edit_transaction_server) = fidl::endpoints::create_proxy().unwrap();
    env.proxies.rewrite_engine.start_edit_transaction(edit_transaction_server).unwrap();
    edit_transaction.add(&mut rule.clone().into()).await.unwrap();
    edit_transaction.commit().await.unwrap();
    let package_dir =
        env.resolve_package("fuchsia-pkg://should_be_rewritten/just_meta_far").await.unwrap();
    pkg.verify_contents(&package_dir).await.unwrap();
    assert_eq!(open_handler.get_fail_count(), 6);
    env.restart_pkg_resolver().await;
    assert_eq!(
        crate::dynamic_rewrite_disabled::get_rules(&env.proxies.rewrite_engine).await,
        vec![rule.clone()]
    );

    env.stop().await;
}
