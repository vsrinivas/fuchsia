// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]

use {
    anyhow::Error,
    fidl_fuchsia_amber::{ControlMarker as AmberMarker, ControlProxy as AmberProxy},
    fidl_fuchsia_amber_ext::{self as types, SourceConfigBuilder},
    fidl_fuchsia_pkg::{RepositoryManagerMarker, RepositoryManagerProxy},
    fidl_fuchsia_pkg_ext::{
        MirrorConfigBuilder, RepositoryConfig, RepositoryConfigBuilder, RepositoryKey,
    },
    fidl_fuchsia_pkg_rewrite::{
        EngineMarker as RewriteEngineMarker, EngineProxy as RewriteEngineProxy,
    },
    fidl_fuchsia_pkg_rewrite_ext::Rule,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_url::pkg_url::RepoUrl,
    futures::prelude::*,
    parking_lot::Mutex,
    std::sync::Arc,
    std::{convert::TryInto, fs::File},
};

const ROOT_KEY_1: &str = "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307";
const ROOT_KEY_2: &str = "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100";

fn amberctl() -> AppBuilder {
    AppBuilder::new("fuchsia-pkg://fuchsia.com/amberctl-tests#meta/amberctl.cmx".to_owned())
}

struct Mounts {
    misc: tempfile::TempDir,
    pkgfs: tempfile::TempDir,
}

impl Mounts {
    fn new() -> Self {
        let misc = tempfile::tempdir().expect("/tmp to exist");
        let pkgfs = tempfile::tempdir().expect("/tmp to exist");
        std::fs::create_dir(pkgfs.path().join("install")).expect("mkdir pkgfs/install");
        std::fs::create_dir(pkgfs.path().join("needs")).expect("mkdir pkgfs/needs");
        Self { misc, pkgfs }
    }
}

struct Proxies {
    amber: AmberProxy,
    repo_manager: RepositoryManagerProxy,
    rewrite_engine: RewriteEngineProxy,
}

struct MockUpdateManager {
    called: Mutex<u32>,
}
impl MockUpdateManager {
    fn new() -> Self {
        Self { called: Mutex::new(0) }
    }

    async fn run(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_update::ManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await? {
            match event {
                fidl_fuchsia_update::ManagerRequest::CheckNow { options, monitor, responder } => {
                    eprintln!("TEST: Got update check request with options {:?}", options);
                    assert_eq!(
                        options,
                        fidl_fuchsia_update::Options {
                            initiator: Some(fidl_fuchsia_update::Initiator::User)
                        }
                    );
                    assert_eq!(monitor, None);
                    *self.called.lock() += 1;
                    responder.send(fidl_fuchsia_update::CheckStartedResult::Started)?;
                }
                _ => panic!("unhandled method {:?}", event),
            }
        }

        Ok(())
    }
}

struct MockSpaceManager {
    called: Mutex<u32>,
}
impl MockSpaceManager {
    fn new() -> Self {
        Self { called: Mutex::new(0) }
    }

    async fn run(
        self: Arc<Self>,
        mut stream: fidl_fuchsia_space::ManagerRequestStream,
    ) -> Result<(), Error> {
        while let Some(event) = stream.try_next().await? {
            *self.called.lock() += 1;
            let fidl_fuchsia_space::ManagerRequest::Gc { responder } = event;
            responder.send(&mut Ok(()))?;
        }
        Ok(())
    }
}

struct TestEnv {
    _amber: App,
    _pkg_resolver: App,
    _mounts: Mounts,
    env: NestedEnvironment,
    proxies: Proxies,
}

impl TestEnv {
    fn new() -> Self {
        Self::new_with_mounts(Mounts::new())
    }

    fn new_with_mounts(mounts: Mounts) -> Self {
        let mut amber = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/amberctl-tests#meta/amber_with_isolated_storage.cmx"
                .to_owned(),
        )
        .add_dir_to_namespace(
            "/misc".to_owned(),
            File::open(mounts.misc.path()).expect("/misc temp dir to open"),
        )
        .expect("/misc to mount");

        let mut pkg_resolver = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/amberctl-tests#meta/pkg_resolver_for_integration_test.cmx"
                .to_owned(),
        )
        .add_dir_to_namespace(
            "/pkgfs".to_owned(),
            File::open(mounts.pkgfs.path()).expect("/pkgfs temp dir to open"),
        )
        .expect("/pkgfs to mount");

        let mut fs = ServiceFs::new();
        fs.add_proxy_service_to::<AmberMarker, _>(amber.directory_request().unwrap().clone())
            .add_proxy_service_to::<RepositoryManagerMarker, _>(
                pkg_resolver.directory_request().unwrap().clone(),
            )
            .add_proxy_service_to::<RewriteEngineMarker, _>(
                pkg_resolver.directory_request().unwrap().clone(),
            );

        let env = fs
            .create_salted_nested_environment("amberctl_env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        let amber = amber.spawn(env.launcher()).expect("amber to launch");
        let pkg_resolver = pkg_resolver.spawn(env.launcher()).expect("amber to launch");

        let amber_proxy = env.connect_to_service::<AmberMarker>().expect("connect to amber");
        let repo_manager_proxy = env
            .connect_to_service::<RepositoryManagerMarker>()
            .expect("connect to repository manager");
        let rewrite_engine_proxy =
            env.connect_to_service::<RewriteEngineMarker>().expect("connect to rewrite engine");

        Self {
            _amber: amber,
            _pkg_resolver: pkg_resolver,
            _mounts: mounts,
            env,
            proxies: Proxies {
                amber: amber_proxy,
                repo_manager: repo_manager_proxy,
                rewrite_engine: rewrite_engine_proxy,
            },
        }
    }

    async fn _run_amberctl(&self, builder: AppBuilder) -> String {
        let fut = builder.output(self.env.launcher()).expect("amberctl to launch");
        let output = fut.await.expect("amberctl to run");
        output.ok().expect("amberctl to succeed");
        String::from_utf8(output.stdout).unwrap()
    }

    async fn run_amberctl<'a>(&'a self, args: &'a [impl std::fmt::Debug + AsRef<str>]) -> String {
        self._run_amberctl(amberctl().args(args.into_iter().map(|s| s.as_ref()))).await
    }

    // Runs "amberctl list_srcs" and returns a vec of fuchsia-pkg URIs from the output
    async fn run_amberctl_list_srcs(&self) -> Vec<String> {
        let mut res = vec![];
        let output = self.run_amberctl(&["list_srcs"]).await;
        for (pos, _) in output.match_indices("\"fuchsia-pkg") {
            let (_, suffix) = output.split_at(pos + 1);
            let url = suffix.split('"').next().unwrap();
            res.push(url.to_owned());
        }
        res
    }

    async fn run_amberctl_add_static_src(&self, name: &'static str) {
        self._run_amberctl(
            amberctl()
                .add_dir_to_namespace(
                    "/configs".to_string(),
                    File::open("/pkg/data/sources").expect("/pkg/data/sources to exist"),
                )
                .expect("static /configs to mount")
                .arg("add_src")
                .arg(format!("-f=/configs/{}", name)),
        )
        .await;
    }

    async fn run_amberctl_add_src(&self, source: types::SourceConfig) {
        let config_dir = tempfile::tempdir().expect("temp config dir to create");
        let file_path = config_dir.path().join("test.json");
        let mut config_file = File::create(file_path).expect("temp config file to create");
        serde_json::to_writer(&mut config_file, &source).expect("source config to serialize");
        drop(config_file);

        self._run_amberctl(
            amberctl()
                .add_dir_to_namespace(
                    "/configs".to_string(),
                    File::open(config_dir.path()).expect("temp config dir to exist"),
                )
                .expect("static /configs to mount")
                .arg("add_src")
                .arg("-f=/configs/test.json"),
        )
        .await;
    }

    async fn run_amberctl_add_repo_config(&self, source: types::SourceConfig) {
        let config_dir = tempfile::tempdir().expect("temp config dir to create");
        let file_path = config_dir.path().join("test.json");
        let mut config_file = File::create(file_path).expect("temp config file to create");
        serde_json::to_writer(&mut config_file, &source).expect("source config to serialize");
        drop(config_file);

        self._run_amberctl(
            amberctl()
                .add_dir_to_namespace(
                    "/configs".to_string(),
                    File::open(config_dir.path()).expect("temp config dir to exist"),
                )
                .expect("static /configs to mount")
                .arg("add_repo_cfg")
                .arg("-f=/configs/test.json"),
        )
        .await;
    }

    async fn resolver_list_repos(&self) -> Vec<RepositoryConfig> {
        let (iterator, iterator_server_end) = fidl::endpoints::create_proxy().unwrap();
        self.proxies.repo_manager.list(iterator_server_end).unwrap();
        collect_iterator(|| iterator.next()).await.unwrap()
    }

    async fn rewrite_engine_list_rules(&self) -> Vec<Rule> {
        let (iterator, iterator_server_end) = fidl::endpoints::create_proxy().unwrap();
        self.proxies.rewrite_engine.list(iterator_server_end).unwrap();
        collect_iterator(|| iterator.next()).await.unwrap()
    }
}

async fn collect_iterator<F, E, I, O>(mut next: impl FnMut() -> F) -> Result<Vec<O>, Error>
where
    F: Future<Output = Result<Vec<I>, fidl::Error>>,
    I: TryInto<O, Error = E>,
    Error: From<E>,
{
    let mut res = Vec::new();
    loop {
        let more = next().await?;
        if more.is_empty() {
            break;
        }
        res.extend(more.into_iter().map(|cfg| cfg.try_into()).collect::<Result<Vec<_>, _>>()?);
    }
    Ok(res)
}

struct SourceConfigGenerator {
    id_prefix: String,
    n: usize,
}

impl SourceConfigGenerator {
    fn new(id_prefix: impl Into<String>) -> Self {
        Self { id_prefix: id_prefix.into(), n: 0 }
    }
}

impl Iterator for SourceConfigGenerator {
    type Item = (types::SourceConfigBuilder, RepositoryConfigBuilder);

    fn next(&mut self) -> Option<Self::Item> {
        let id = format!("{}{:02}", &self.id_prefix, self.n);
        let repo_url = format!("fuchsia-pkg://{}", &id);
        let mirror_url = format!("http://example.com/{}", &id);
        self.n += 1;

        Some((
            SourceConfigBuilder::new(id)
                .repo_url(mirror_url.clone())
                .add_root_key(ROOT_KEY_1)
                .auto(true),
            RepositoryConfigBuilder::new(RepoUrl::parse(&repo_url).unwrap())
                .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_1).unwrap()))
                .add_mirror(MirrorConfigBuilder::new(mirror_url).subscribe(true)),
        ))
    }
}

fn make_test_repo_config() -> RepositoryConfig {
    RepositoryConfigBuilder::new("fuchsia-pkg://test".parse().unwrap())
        .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_1).unwrap()))
        .add_mirror(MirrorConfigBuilder::new("http://example.com").subscribe(true))
        .build()
}

#[fasync::run_singlethreaded(test)]
async fn test_services_start_with_no_config() {
    let env = TestEnv::new();

    assert_eq!(env.run_amberctl_list_srcs().await, Vec::<String>::new());
    assert_eq!(env.proxies.amber.do_test(42).await.unwrap(), "Your number was 42\n");
    assert_eq!(env.resolver_list_repos().await, vec![]);
    assert_eq!(env.rewrite_engine_list_rules().await, vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn test_add_src() {
    let env = TestEnv::new();

    env.run_amberctl_add_static_src("test.json").await;

    assert_eq!(env.run_amberctl_list_srcs().await, vec!["fuchsia-pkg://test"]);
    assert_eq!(env.resolver_list_repos().await, vec![make_test_repo_config()]);
    assert_eq!(
        env.rewrite_engine_list_rules().await,
        vec![Rule::new("fuchsia.com", "test", "/", "/").unwrap()]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_open_repo_merkle_for() {
    let env = TestEnv::new();
    let (repo_proxy, repo_server_end) = fidl::endpoints::create_proxy().expect("create proxy");
    env.proxies
        .amber
        .open_repository(make_test_repo_config().into(), repo_server_end)
        .await
        .expect("open repo");

    let (status_raw, message, _merkle, _size) =
        repo_proxy.merkle_for("example_pkg", None).await.expect("merkle for");

    assert_eq!(fuchsia_zircon::Status::from_raw(status_raw), fuchsia_zircon::Status::INTERNAL);
    assert_eq!(
        message,
        "error finding merkle for package example_pkg/: tuf_source: error reading TUF targets: tuf: no root keys found in local meta store"
    )
}

#[fasync::run_singlethreaded(test)]
async fn test_add_repo() {
    let env = TestEnv::new();

    let source = SourceConfigBuilder::new("localhost")
        .repo_url("http://127.0.0.1:8083")
        .add_root_key(ROOT_KEY_1)
        .build();

    let repo = RepositoryConfigBuilder::new("fuchsia-pkg://localhost".parse().unwrap())
        .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_1).unwrap()))
        .add_mirror(MirrorConfigBuilder::new("http://127.0.0.1:8083"))
        .build();

    env.run_amberctl_add_repo_config(source).await;

    assert_eq!(env.resolver_list_repos().await, vec![repo]);
    assert_eq!(env.rewrite_engine_list_rules().await, vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn test_add_src_with_ipv4_id() {
    let env = TestEnv::new();

    let source = SourceConfigBuilder::new("http://10.0.0.1:8083")
        .repo_url("http://10.0.0.1:8083")
        .add_root_key(ROOT_KEY_1)
        .build();

    let repo = RepositoryConfigBuilder::new("fuchsia-pkg://http___10_0_0_1_8083".parse().unwrap())
        .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_1).unwrap()))
        .add_mirror(MirrorConfigBuilder::new("http://10.0.0.1:8083"))
        .build();

    env.run_amberctl_add_src(source).await;

    assert_eq!(env.resolver_list_repos().await, vec![repo]);
    assert_eq!(
        env.rewrite_engine_list_rules().await,
        vec![Rule::new("fuchsia.com", "http___10_0_0_1_8083", "/", "/").unwrap()]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_add_src_with_ipv6_id() {
    let env = TestEnv::new();

    let source = SourceConfigBuilder::new("http://[fe80::1122:3344]:8083")
        .repo_url("http://[fe80::1122:3344]:8083")
        .add_root_key(ROOT_KEY_1)
        .build();

    let repo = RepositoryConfigBuilder::new(
        "fuchsia-pkg://http____fe80__1122_3344__8083".parse().unwrap(),
    )
    .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_1).unwrap()))
    .add_mirror(MirrorConfigBuilder::new("http://[fe80::1122:3344]:8083"))
    .build();

    env.run_amberctl_add_src(source).await;

    assert_eq!(env.resolver_list_repos().await, vec![repo]);
    assert_eq!(
        env.rewrite_engine_list_rules().await,
        vec![Rule::new("fuchsia.com", "http____fe80__1122_3344__8083", "/", "/").unwrap()]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_add_src_disables_other_sources() {
    let env = TestEnv::new();

    let configs = SourceConfigGenerator::new("testgen").take(3).collect::<Vec<_>>();

    for (config, _) in &configs {
        env.run_amberctl_add_src(config.clone().build().into()).await;
    }

    env.run_amberctl_add_static_src("test.json").await;

    let mut repo_configs = vec![make_test_repo_config()];
    for (_, repo_config) in configs {
        repo_configs.push(repo_config.build());
    }

    assert_eq!(env.resolver_list_repos().await, repo_configs);
    assert_eq!(
        env.rewrite_engine_list_rules().await,
        vec![Rule::new("fuchsia.com", "test", "/", "/").unwrap()]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_add_repo_retains_existing_state() {
    let env = TestEnv::new();

    // start with an existing source.
    env.run_amberctl_add_static_src("test.json").await;

    // add a repo.
    let source = SourceConfigBuilder::new("devhost")
        .repo_url("http://10.0.0.1:8083")
        .add_root_key(ROOT_KEY_1)
        .build();
    let repo = RepositoryConfigBuilder::new("fuchsia-pkg://devhost".parse().unwrap())
        .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_1).unwrap()))
        .add_mirror(MirrorConfigBuilder::new("http://10.0.0.1:8083"))
        .build();
    env.run_amberctl_add_repo_config(source).await;

    // ensure adding the repo didn't remove state configured when adding the source.
    assert_eq!(env.resolver_list_repos().await, vec![repo, make_test_repo_config()]);
    assert_eq!(
        env.rewrite_engine_list_rules().await,
        vec![Rule::new("fuchsia.com", "test", "/", "/").unwrap()]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_rm_src() {
    let env = TestEnv::new();

    let cfg_a = SourceConfigBuilder::new("http://[fe80::1122:3344]:8083")
        .repo_url("http://example.com/a")
        .rate_period(60)
        .add_root_key(ROOT_KEY_1)
        .build();

    let cfg_b = SourceConfigBuilder::new("b")
        .repo_url("http://example.com/b")
        .rate_period(60)
        .add_root_key(ROOT_KEY_2)
        .build();

    env.run_amberctl_add_src(cfg_a.into()).await;
    env.run_amberctl_add_src(cfg_b.into()).await;

    env.run_amberctl(&["rm_src", "-n", "http://[fe80::1122:3344]:8083"]).await;
    assert_eq!(
        env.resolver_list_repos().await,
        vec![RepositoryConfigBuilder::new("fuchsia-pkg://b".parse().unwrap())
            .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_2).unwrap()))
            .add_mirror(MirrorConfigBuilder::new("http://example.com/b"))
            .build()]
    );
    // rm_src removes all rules, so no source remains enabled.
    assert_eq!(env.rewrite_engine_list_rules().await, vec![]);

    env.run_amberctl(&["rm_src", "-n", "b"]).await;
    assert_eq!(env.resolver_list_repos().await, vec![]);
    assert_eq!(env.rewrite_engine_list_rules().await, vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn test_enable_src() {
    let env = TestEnv::new();

    let source = SourceConfigBuilder::new("test")
        .repo_url("http://example.com")
        .enabled(false)
        .add_root_key(ROOT_KEY_1)
        .build();

    let repo = RepositoryConfigBuilder::new("fuchsia-pkg://test".parse().unwrap())
        .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_1).unwrap()))
        .add_mirror(MirrorConfigBuilder::new("http://example.com"))
        .build();

    env.run_amberctl_add_src(source.into()).await;

    assert_eq!(env.resolver_list_repos().await, vec![repo.clone()]);
    // Adding a disabled source does not add a rewrite rule for it.
    assert_eq!(env.rewrite_engine_list_rules().await, vec![]);

    env.run_amberctl(&["enable_src", "-n", "test"]).await;

    assert_eq!(env.resolver_list_repos().await, vec![repo]);
    assert_eq!(
        env.rewrite_engine_list_rules().await,
        vec![Rule::new("fuchsia.com", "test", "/", "/").unwrap()]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_enable_src_disables_other_sources() {
    let env = TestEnv::new();

    // add some enabled sources
    let mut gen = SourceConfigGenerator::new("test");
    let configs = gen.by_ref().take(3).collect::<Vec<_>>();
    for (config, _) in &configs {
        env.run_amberctl_add_src(config.clone().build().into()).await;
    }

    // add an initially disabled source
    let (config, repo) = gen.next().unwrap();
    let c = config.enabled(false).build();
    let id = c.id().to_owned();
    env.run_amberctl_add_src(c.into()).await;

    // verify the previously added source is still the enabled one
    assert_eq!(
        env.rewrite_engine_list_rules().await,
        vec![Rule::new("fuchsia.com", "test02", "/", "/").unwrap()]
    );

    // enable the new source source and verify the repos and rules
    let args = ["enable_src", "-n", &id];
    env.run_amberctl(&args).await;

    let mut repo_configs = vec![];
    for (_, repo_config) in configs {
        repo_configs.push(repo_config.build());
    }
    repo_configs.push(repo.build());
    assert_eq!(env.resolver_list_repos().await, repo_configs);
    assert_eq!(
        env.rewrite_engine_list_rules().await,
        vec![Rule::new("fuchsia.com", id, "/", "/").unwrap()]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_disable_src_disables_all_sources() {
    let env = TestEnv::new();

    env.run_amberctl_add_src(
        SourceConfigBuilder::new("a")
            .repo_url("http://example.com/a")
            .rate_period(60)
            .add_root_key(ROOT_KEY_1)
            .build()
            .into(),
    )
    .await;
    env.run_amberctl_add_src(
        SourceConfigBuilder::new("b")
            .repo_url("http://example.com/b")
            .rate_period(60)
            .add_root_key(ROOT_KEY_2)
            .build()
            .into(),
    )
    .await;

    env.run_amberctl(&["disable_src"]).await;

    assert_eq!(
        env.resolver_list_repos().await,
        vec![
            RepositoryConfigBuilder::new("fuchsia-pkg://a".parse().unwrap())
                .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_1).unwrap()))
                .add_mirror(MirrorConfigBuilder::new("http://example.com/a"))
                .build(),
            RepositoryConfigBuilder::new("fuchsia-pkg://b".parse().unwrap())
                .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_2).unwrap()))
                .add_mirror(MirrorConfigBuilder::new("http://example.com/b"))
                .build(),
        ]
    );
    // disabling any source clears all rewrite rules.
    assert_eq!(env.rewrite_engine_list_rules().await, vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn test_system_update() {
    // skip using TestEnv because we don't need to start pkg_resolver or amber here.
    let mut fs = ServiceFs::new();

    let update_manager = Arc::new(MockUpdateManager::new());
    let update_manager_clone = update_manager.clone();
    fs.add_fidl_service(move |stream| {
        let update_manager_clone = update_manager_clone.clone();
        fasync::spawn(
            update_manager_clone
                .run(stream)
                .unwrap_or_else(|e| panic!("error running mock update manager: {:?}", e)),
        )
    });

    let env = fs
        .create_salted_nested_environment("amberctl_env")
        .expect("nested environment to create successfully");
    fasync::spawn(fs.collect());

    amberctl()
        .arg("system_update")
        .output(env.launcher())
        .expect("amberctl to launch")
        .await
        .expect("amberctl to run")
        .ok()
        .expect("amberctl to succeed");

    assert_eq!(*update_manager.called.lock(), 1);
}

#[fasync::run_singlethreaded(test)]
async fn test_gc() {
    // skip using TestEnv because we don't need to start pkg_resolver or amber here.
    let mut fs = ServiceFs::new();

    let space_manager = Arc::new(MockSpaceManager::new());
    let space_manager_clone = Arc::clone(&space_manager);
    fs.add_fidl_service(move |stream| {
        let space_manager_clone = Arc::clone(&space_manager_clone);
        fasync::spawn(
            space_manager_clone
                .run(stream)
                .unwrap_or_else(|e| panic!("error running mock space manager: {:?}", e)),
        )
    });

    let env = fs
        .create_salted_nested_environment("amberctl_env")
        .expect("nested environment to create successfully");
    fasync::spawn(fs.collect());

    amberctl()
        .arg("gc")
        .output(env.launcher())
        .expect("amberctl to launch")
        .await
        .expect("amberctl to run")
        .ok()
        .expect("amberctl to succeed");

    assert_eq!(*space_manager.called.lock(), 1);
}
