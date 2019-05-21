// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![cfg(test)]

use {
    failure::Error,
    fidl_fuchsia_amber::{ControlMarker as AmberMarker, ControlProxy as AmberProxy},
    fidl_fuchsia_pkg::{RepositoryManagerMarker, RepositoryManagerProxy},
    fidl_fuchsia_pkg_ext::{
        MirrorConfigBuilder, RepositoryConfig, RepositoryConfigBuilder, RepositoryKey,
    },
    fidl_fuchsia_pkg_rewrite::{
        EngineMarker as RewriteEngineMarker, EngineProxy as RewriteEngineProxy,
    },
    fidl_fuchsia_sys::TerminationReason,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder, Stdio},
        server::{NestedEnvironment, ServiceFs},
    },
    fuchsia_uri::pkg_uri::RepoUri,
    fuchsia_uri_rewrite::Rule,
    futures::prelude::*,
    std::{convert::TryInto, fs::File},
};

mod types;
use types::SourceConfigBuilder;

const ROOT_KEY_1: &str = "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307";
const ROOT_KEY_2: &str = "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100";

fn amberctl() -> AppBuilder {
    AppBuilder::new("fuchsia-pkg://fuchsia.com/amberctl-tests#meta/amberctl.cmx".to_owned())
}

struct Mounts {
    misc: tempfile::TempDir,
    data_amber: tempfile::TempDir,
}

impl Mounts {
    fn new() -> Self {
        Self {
            misc: tempfile::tempdir().expect("/tmp to exist"),
            data_amber: tempfile::tempdir().expect("/tmp to exist"),
        }
    }
}

struct Proxies {
    amber: AmberProxy,
    repo_manager: RepositoryManagerProxy,
    rewrite_engine: RewriteEngineProxy,
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
        .expect("/misc to mount")
        .add_dir_to_namespace(
            "/data/amber".to_owned(),
            File::open(mounts.data_amber.path()).expect("/data/amber temp dir to open"),
        )
        .expect("/data/amber to mount");

        let mut pkg_resolver = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/pkg_resolver#meta/pkg_resolver.cmx".to_owned(),
        );

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

    async fn _run_amberctl(&self, builder: AppBuilder) {
        let fut =
            builder.stderr(Stdio::Inherit).output(self.env.launcher()).expect("amberctl to launch");
        let output = await!(fut).expect("amberctl to run");

        assert_eq!(output.exit_status.reason(), TerminationReason::Exited);
        assert!(
            output.exit_status.success(),
            "amberctl exited with {}\nSTDOUT\n{}\nSTDOUT",
            output.exit_status.code(),
            String::from_utf8_lossy(&output.stdout),
        );
    }

    async fn run_amberctl<'a>(&'a self, args: &'a [impl std::fmt::Debug + AsRef<str>]) {
        let fut = amberctl()
            .args(args.into_iter().map(|s| s.as_ref()))
            .stderr(Stdio::Inherit)
            .output(self.env.launcher())
            .expect("amberctl to launch");
        let output = await!(fut).expect("amberctl to run");

        assert_eq!(output.exit_status.reason(), TerminationReason::Exited);
        assert!(
            output.exit_status.success(),
            "amberctl {:?} exited with {}\nSTDOUT\n{}\nSTDOUT",
            args,
            output.exit_status.code(),
            String::from_utf8_lossy(&output.stdout),
        );
    }

    async fn run_amberctl_add_static_src(&self, name: &'static str) {
        await!(self._run_amberctl(
            amberctl()
                .add_dir_to_namespace(
                    "/configs".to_string(),
                    File::open("/pkg/data/sources").expect("/pkg/data/sources to exist"),
                )
                .expect("static /configs to mount")
                .args(["add_src", "-f"].into_iter().cloned())
                .arg(format!("/configs/{}", name))
        ));
    }

    async fn run_amberctl_add_src(&self, source: types::SourceConfig) {
        let mut config_file = tempfile::tempfile().expect("temp config file to create");
        serde_json::to_writer(&mut config_file, &source).expect("source config to serialize");

        await!(self._run_amberctl(
            amberctl()
                .add_dir_to_namespace("/configs/test.json".to_string(), config_file)
                .expect("static /configs to mount")
                // Run amberctl in non-exclusive mode so it doesn't disable existing source configs
                .args(["add_src", "-x", "-f", "/configs/test.json"].iter().map(|s| *s))
        ));
    }

    async fn amber_list_sources(&self) -> Vec<types::SourceConfig> {
        let sources = await!(self.proxies.amber.list_srcs()).unwrap();

        let mut sources = sources
            .into_iter()
            .map(|source| source.try_into())
            .collect::<Result<Vec<types::SourceConfig>, _>>()
            .unwrap();

        sources.sort_unstable();
        sources
    }

    async fn resolver_list_repos(&self) -> Vec<RepositoryConfig> {
        let (iterator, iterator_server_end) = fidl::endpoints::create_proxy().unwrap();
        self.proxies.repo_manager.list(iterator_server_end).unwrap();
        await!(collect_iterator(|| iterator.next())).unwrap()
    }

    async fn rewrite_engine_list_rules(&self) -> Vec<Rule> {
        let (iterator, iterator_server_end) = fidl::endpoints::create_proxy().unwrap();
        self.proxies.rewrite_engine.list(iterator_server_end).unwrap();
        await!(collect_iterator(|| iterator.next())).unwrap()
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
        let more = await!(next())?;
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
            RepositoryConfigBuilder::new(RepoUri::parse(&repo_url).unwrap())
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

    assert_eq!(await!(env.amber_list_sources()), vec![]);
    assert_eq!(await!(env.resolver_list_repos()), vec![]);
    assert_eq!(await!(env.rewrite_engine_list_rules()), vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn test_add_src() {
    let env = TestEnv::new();

    await!(env.run_amberctl_add_static_src("test.json"));

    let cfg_test = SourceConfigBuilder::new("test")
        .repo_url("http://example.com")
        .rate_period(60)
        .auto(true)
        .add_root_key(ROOT_KEY_1)
        .build();

    assert_eq!(await!(env.amber_list_sources()), vec![cfg_test]);
    assert_eq!(await!(env.resolver_list_repos()), vec![make_test_repo_config()]);
    assert_eq!(
        await!(env.rewrite_engine_list_rules()),
        vec![Rule::new("fuchsia.com", "test", "/", "/").unwrap()]
    );
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

    await!(env.run_amberctl_add_src(source.clone()));

    assert_eq!(await!(env.amber_list_sources()), vec![source]);
    assert_eq!(await!(env.resolver_list_repos()), vec![repo]);
    assert_eq!(
        await!(env.rewrite_engine_list_rules()),
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

    await!(env.run_amberctl_add_src(source.clone()));

    assert_eq!(await!(env.amber_list_sources()), vec![source]);
    assert_eq!(await!(env.resolver_list_repos()), vec![repo]);
    assert_eq!(
        await!(env.rewrite_engine_list_rules()),
        vec![Rule::new("fuchsia.com", "http____fe80__1122_3344__8083", "/", "/").unwrap()]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_add_src_disables_other_sources() {
    let env = TestEnv::new();

    let configs = SourceConfigGenerator::new("testgen").take(3).collect::<Vec<_>>();

    for (config, _) in &configs {
        await!(env.run_amberctl_add_src(config.clone().build().into()));
    }

    await!(env.run_amberctl_add_static_src("test.json"));

    let mut source_configs = vec![];
    let mut repo_configs = vec![make_test_repo_config()];
    for (source_config, repo_config) in configs {
        source_configs.push(source_config.enabled(false).build());
        repo_configs.push(repo_config.build());
    }
    let test_config =
        serde_json::from_reader(File::open("/pkg/data/sources/test.json").unwrap()).unwrap();
    source_configs.push(test_config);
    source_configs.sort_unstable();

    assert_eq!(await!(env.amber_list_sources()), source_configs);
    assert_eq!(await!(env.resolver_list_repos()), repo_configs);
    assert_eq!(
        await!(env.rewrite_engine_list_rules()),
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

    await!(env.run_amberctl_add_src(cfg_a.clone().into()));
    await!(env.run_amberctl_add_src(cfg_b.clone().into()));

    await!(env.run_amberctl(&["rm_src", "-n", "http://[fe80::1122:3344]:8083"]));
    assert_eq!(await!(env.amber_list_sources()), vec![cfg_b]);
    assert_eq!(
        await!(env.resolver_list_repos()),
        vec![RepositoryConfigBuilder::new("fuchsia-pkg://b".parse().unwrap())
            .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_2).unwrap()))
            .add_mirror(MirrorConfigBuilder::new("http://example.com/b"))
            .build()]
    );
    // rm_src removes all rules, so no source remains enabled.
    assert_eq!(await!(env.rewrite_engine_list_rules()), vec![]);

    await!(env.run_amberctl(&["rm_src", "-n", "b"]));
    assert_eq!(await!(env.amber_list_sources()), vec![]);
    assert_eq!(await!(env.resolver_list_repos()), vec![]);
    assert_eq!(await!(env.rewrite_engine_list_rules()), vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn test_enable_src() {
    let env = TestEnv::new();

    let source = SourceConfigBuilder::new("test")
        .repo_url("http://example.com")
        .enabled(false)
        .add_root_key(ROOT_KEY_1);

    let repo = RepositoryConfigBuilder::new("fuchsia-pkg://test".parse().unwrap())
        .add_root_key(RepositoryKey::Ed25519(hex::decode(ROOT_KEY_1).unwrap()))
        .add_mirror(MirrorConfigBuilder::new("http://example.com"))
        .build();

    await!(env.run_amberctl_add_src(source.clone().build().into()));

    assert_eq!(await!(env.resolver_list_repos()), vec![repo.clone()]);
    // Adding a disabled source does not add a rewrite rule for it.
    assert_eq!(await!(env.rewrite_engine_list_rules()), vec![]);

    await!(env.run_amberctl(&["enable_src", "-n", "test"]));

    assert_eq!(await!(env.amber_list_sources()), vec![source.enabled(true).build()]);
    assert_eq!(await!(env.resolver_list_repos()), vec![repo]);
    assert_eq!(
        await!(env.rewrite_engine_list_rules()),
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
        await!(env.run_amberctl_add_src(config.clone().build().into()));
    }

    // add an initially disabled source
    let (config, repo) = gen.next().unwrap();
    let config = config.enabled(false);
    let c = config.clone().build();
    let id = c.id().to_owned();
    await!(env.run_amberctl_add_src(c.into()));

    // enable that source
    let args = ["enable_src", "-n", &id];
    await!(env.run_amberctl(&args));

    // verify the enabled sources are now disabled and the disabled source is now enabled
    let mut source_configs = vec![];
    let mut repo_configs = vec![];
    for (source_config, repo_config) in configs {
        source_configs.push(source_config.enabled(false).build());
        repo_configs.push(repo_config.build());
    }
    source_configs.push(config.enabled(true).build());
    repo_configs.push(repo.build());
    source_configs.sort_unstable();
    assert_eq!(await!(env.amber_list_sources()), source_configs);
    assert_eq!(await!(env.resolver_list_repos()), repo_configs);
    assert_eq!(
        await!(env.rewrite_engine_list_rules()),
        vec![Rule::new("fuchsia.com", id, "/", "/").unwrap()]
    );
}

#[fasync::run_singlethreaded(test)]
async fn test_disable_src() {
    let env = TestEnv::new();

    let cfg_a = SourceConfigBuilder::new("a")
        .repo_url("http://example.com/a")
        .rate_period(60)
        .add_root_key(ROOT_KEY_1);

    let cfg_b = SourceConfigBuilder::new("b")
        .repo_url("http://example.com/b")
        .rate_period(60)
        .add_root_key(ROOT_KEY_2);

    await!(env.run_amberctl_add_src(cfg_a.clone().build().into()));
    await!(env.run_amberctl_add_src(cfg_b.clone().build().into()));

    await!(env.run_amberctl(&["disable_src", "-n", "a"]));

    assert_eq!(
        await!(env.amber_list_sources()),
        vec![cfg_a.enabled(false).build(), cfg_b.enabled(true).build().into(),]
    );
    assert_eq!(
        await!(env.resolver_list_repos()),
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
    assert_eq!(await!(env.rewrite_engine_list_rules()), vec![]);
}
