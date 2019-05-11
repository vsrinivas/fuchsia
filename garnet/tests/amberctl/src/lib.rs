// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]
#![cfg(test)]

use {
    fidl_fuchsia_amber::{ControlMarker as AmberMarker, ControlProxy as AmberProxy},
    fidl_fuchsia_sys::TerminationReason,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder, Stdio},
        server::{NestedEnvironment, ServiceFs},
    },
    futures::prelude::*,
    std::{convert::TryInto, fs::File},
};

mod types;
use types::SourceConfigBuilder;

const ROOT_KEY_1: &str = "be0b983f7396da675c40c6b93e47fced7c1e9ea8a32a1fe952ba8f519760b307";
const ROOT_KEY_2: &str = "00112233445566778899aabbccddeeffffeeddccbbaa99887766554433221100";

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
}

struct TestEnv {
    _amber: App,
    mounts: Mounts,
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

        let mut fs = ServiceFs::new();
        fs.add_proxy_service_to::<AmberMarker, _>(amber.directory_request().unwrap().clone());
        let env = fs
            .create_salted_nested_environment("amberctl_env")
            .expect("nested environment to create successfully");
        fasync::spawn(fs.collect());

        let amber = amber.spawn(env.launcher()).expect("amber to launch");

        let amber_proxy = env.connect_to_service::<AmberMarker>().expect("connect to amber");

        Self { _amber: amber, mounts, env, proxies: Proxies { amber: amber_proxy } }
    }

    /// Tear down the test environment, retaining the state directories.
    fn into_mounts(self) -> Mounts {
        self.mounts
    }

    /// Re-create the test environment, re-using the existing temporary state directories.
    fn restart(self) -> Self {
        Self::new_with_mounts(self.into_mounts())
    }

    async fn run_amberctl<'a>(&'a self, args: &'a [&'a str]) {
        let fut = AppBuilder::new(
            "fuchsia-pkg://fuchsia.com/amberctl-tests#meta/amberctl.cmx".to_owned(),
        )
        .args(args.into_iter().map(|s| *s))
        .add_dir_to_namespace(
            "/sources".to_string(),
            File::open("/pkg/data/sources").expect("/pkg/data/sources to exist"),
        )
        .expect("/sources to mount")
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
}

struct SourceConfigGenerator {
    builder: SourceConfigBuilder,
    root_id: String,
    root_url: String,
    n: usize,
}

impl SourceConfigGenerator {
    fn new(builder: SourceConfigBuilder) -> Self {
        let config = builder.clone().build();
        Self {
            root_id: config.id().to_owned(),
            root_url: config.repo_url().to_owned(),
            builder,
            n: 0,
        }
    }
}

impl Iterator for SourceConfigGenerator {
    type Item = types::SourceConfigBuilder;

    fn next(&mut self) -> Option<Self::Item> {
        let id = format!("{}{:02}", &self.root_id, self.n);
        let url = format!("{}/{:02}", &self.root_url, self.n);
        self.n += 1;

        Some(self.builder.clone().id(id).repo_url(url))
    }
}

#[fasync::run_singlethreaded(test)]
async fn test_amber_starts_with_no_sources() {
    let env = TestEnv::new();

    assert_eq!(await!(env.amber_list_sources()), vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn test_add_src() {
    let env = TestEnv::new();

    await!(env.run_amberctl(&["add_src", "-f", "/sources/test.json"]));

    let cfg_test = SourceConfigBuilder::new("test")
        .repo_url("http://example.com")
        .rate_period(60)
        .auto(true)
        .add_root_key(ROOT_KEY_1)
        .build();

    assert_eq!(await!(env.amber_list_sources()), vec![cfg_test.clone()]);

    // Ensure source configs persist across service restarts
    let env = env.restart();
    assert_eq!(await!(env.amber_list_sources()), vec![cfg_test]);
}

#[fasync::run_singlethreaded(test)]
async fn test_add_src_disables_other_sources() {
    let env = TestEnv::new();

    let configs = SourceConfigGenerator::new(
        SourceConfigBuilder::new("test")
            .repo_url("http://example.com")
            .rate_period(60)
            .auto(true)
            .add_root_key(ROOT_KEY_1),
    )
    .take(3)
    .collect::<Vec<_>>();

    for config in &configs {
        assert_eq!(
            await!(env.proxies.amber.add_src(&mut config.clone().build().into())).unwrap(),
            true
        );
    }

    await!(env.run_amberctl(&["add_src", "-f", "/sources/test.json"]));

    let mut configs =
        configs.into_iter().map(|builder| builder.enabled(false).build()).collect::<Vec<_>>();
    let test_config =
        serde_json::from_reader(File::open("/pkg/data/sources/test.json").unwrap()).unwrap();
    configs.push(test_config);
    configs.sort_unstable();

    assert_eq!(await!(env.amber_list_sources()), configs);
}

#[fasync::run_singlethreaded(test)]
async fn test_rm_src() {
    let env = TestEnv::new();

    let cfg_a = SourceConfigBuilder::new("a")
        .repo_url("http://example.com/a")
        .rate_period(60)
        .add_root_key(ROOT_KEY_1)
        .build();

    let cfg_b = SourceConfigBuilder::new("b")
        .repo_url("http://example.com/b")
        .rate_period(60)
        .add_root_key(ROOT_KEY_2)
        .build();

    assert_eq!(await!(env.proxies.amber.add_src(&mut cfg_a.clone().into())).unwrap(), true);
    assert_eq!(await!(env.proxies.amber.add_src(&mut cfg_b.clone().into())).unwrap(), true);

    await!(env.run_amberctl(&["rm_src", "-n", "b"]));
    assert_eq!(await!(env.amber_list_sources()), vec![cfg_a]);

    await!(env.run_amberctl(&["rm_src", "-n", "a"]));
    assert_eq!(await!(env.amber_list_sources()), vec![]);
}

#[fasync::run_singlethreaded(test)]
async fn test_enable_src() {
    let env = TestEnv::new();

    let cfg = SourceConfigBuilder::new("test")
        .repo_url("http://example.com")
        .enabled(false)
        .add_root_key(ROOT_KEY_1);

    assert_eq!(await!(env.proxies.amber.add_src(&mut cfg.clone().build().into())).unwrap(), true);

    await!(env.run_amberctl(&["enable_src", "-n", "test"]));

    assert_eq!(await!(env.amber_list_sources()), vec![cfg.enabled(true).build()]);
}

#[fasync::run_singlethreaded(test)]
async fn test_enable_src_disables_other_sources() {
    let env = TestEnv::new();

    // add some enabled sources
    let mut gen = SourceConfigGenerator::new(
        SourceConfigBuilder::new("test").repo_url("http://example.com").add_root_key(ROOT_KEY_1),
    );
    let configs = gen.by_ref().take(3).collect::<Vec<_>>();
    for config in &configs {
        assert_eq!(
            await!(env.proxies.amber.add_src(&mut config.clone().build().into())).unwrap(),
            true
        );
    }

    // add an initially disabled source.
    let config = gen.next().unwrap().enabled(false);
    let c = config.clone().build();
    let id = c.id().to_owned();
    assert_eq!(await!(env.proxies.amber.add_src(&mut c.into())).unwrap(), true);

    // enable that source
    let args = ["enable_src", "-n", &id];
    await!(env.run_amberctl(&args));

    // verify the enabled sources are now disabled and the disabled source is now enabled
    let mut configs =
        configs.into_iter().map(|builder| builder.enabled(false).build()).collect::<Vec<_>>();
    configs.push(config.enabled(true).build());
    configs.sort_unstable();
    assert_eq!(await!(env.amber_list_sources()), configs);
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

    assert_eq!(await!(env.proxies.amber.add_src(&mut cfg_a.clone().build().into())).unwrap(), true);
    assert_eq!(await!(env.proxies.amber.add_src(&mut cfg_b.clone().build().into())).unwrap(), true);

    await!(env.run_amberctl(&["disable_src", "-n", "a"]));

    assert_eq!(
        await!(env.amber_list_sources()),
        vec![cfg_a.enabled(false).build(), cfg_b.enabled(true).build().into(),]
    );
}
