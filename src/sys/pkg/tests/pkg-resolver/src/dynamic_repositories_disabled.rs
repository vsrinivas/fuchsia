// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// This module tests pkg_resolver's RepositoryManager when
/// dynamic repository configs.
use {
    super::*,
    crate::mock_filesystem::spawn_directory_handler,
    fidl_fuchsia_pkg::RepositoryManagerProxy,
    fidl_fuchsia_pkg_ext::{RepositoryConfig, RepositoryConfigBuilder, RepositoryConfigs},
    fuchsia_async as fasync,
    std::convert::TryInto,
};

impl Mounts {
    fn add_dynamic_repositories(&self, repo_configs: &RepositoryConfigs) {
        if let DirOrProxy::Dir(ref d) = self.pkg_resolver_data {
            let f = File::create(d.path().join("repositories.json")).unwrap();
            serde_json::to_writer(BufWriter::new(f), repo_configs).unwrap();
        } else {
            panic!("not supported");
        }
    }
}

fn make_repo_config(repo: &RepositoryConfig) -> RepositoryConfigs {
    RepositoryConfigs::Version1(vec![repo.clone()])
}

fn make_repo() -> RepositoryConfig {
    RepositoryConfigBuilder::new("fuchsia-pkg://example.com".parse().unwrap()).build()
}

pub async fn get_repos(repository_manager: &RepositoryManagerProxy) -> Vec<RepositoryConfig> {
    let (repo_iterator, repo_iterator_server) =
        fidl::endpoints::create_proxy().expect("create repo iterator proxy");
    repository_manager.list(repo_iterator_server).expect("list repos");
    let mut ret = vec![];
    loop {
        let repos = repo_iterator.next().await.expect("advance repo iterator");
        if repos.is_empty() {
            return ret;
        }
        ret.extend(repos.into_iter().map(|r| r.try_into().unwrap()))
    }
}

#[fasync::run_singlethreaded(test)]
async fn load_dynamic_repos() {
    let mounts = Mounts::new();
    let repo = make_repo();
    mounts.add_dynamic_repositories(&make_repo_config(&repo));
    mounts.add_config(&Config { disable_dynamic_configuration: false });
    let env = TestEnvBuilder::new().mounts(mounts).build();

    assert_eq!(get_repos(&env.proxies.repo_manager).await, vec![repo]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn no_load_dynamic_repos_if_disabled() {
    let mounts = Mounts::new();
    mounts.add_dynamic_repositories(&make_repo_config(&make_repo()));
    mounts.add_config(&Config { disable_dynamic_configuration: true });
    let env = TestEnvBuilder::new().mounts(mounts).build();

    assert_eq!(get_repos(&env.proxies.repo_manager).await, vec![]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn add_succeeds() {
    let mounts = Mounts::new();
    mounts.add_config(&Config { disable_dynamic_configuration: false });
    let env = TestEnvBuilder::new().mounts(mounts).build();
    let repo = make_repo();

    Status::ok(env.proxies.repo_manager.add(repo.clone().into()).await.unwrap()).unwrap();
    assert_eq!(get_repos(&env.proxies.repo_manager).await, vec![repo]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn add_fails_if_disabled() {
    let mounts = Mounts::new();
    mounts.add_config(&Config { disable_dynamic_configuration: true });
    let env = TestEnvBuilder::new().mounts(mounts).build();
    let repo = make_repo();

    assert_eq!(
        env.proxies.repo_manager.add(repo.clone().into()).await.unwrap(),
        Status::ACCESS_DENIED.into_raw()
    );
    assert_eq!(get_repos(&env.proxies.repo_manager).await, vec![]);

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn remove_fails_with_not_found() {
    let mounts = Mounts::new();
    mounts.add_config(&Config { disable_dynamic_configuration: false });
    let env = TestEnvBuilder::new().mounts(mounts).build();

    assert_eq!(
        env.proxies.repo_manager.remove("fuchsia-pkg://example.com").await.unwrap(),
        Status::NOT_FOUND.into_raw()
    );

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn remove_fails_with_access_denied_if_disabled() {
    let mounts = Mounts::new();
    mounts.add_config(&Config { disable_dynamic_configuration: true });
    let env = TestEnvBuilder::new().mounts(mounts).build();

    assert_eq!(
        env.proxies.repo_manager.remove("fuchsia-pkg://example.com").await.unwrap(),
        Status::ACCESS_DENIED.into_raw()
    );

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn attempt_to_open_persisted_dynamic_repos() {
    let (proxy, open_counts) = spawn_directory_handler();
    let mounts = Mounts {
        pkg_resolver_data: DirOrProxy::Proxy(proxy),
        pkg_resolver_config_data: DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist")),
    };
    mounts.add_config(&Config { disable_dynamic_configuration: false });
    let env = TestEnvBuilder::new().mounts(mounts).build();

    // Waits for pkg_resolver to be initialized
    get_repos(&env.proxies.repo_manager).await;

    assert_eq!(open_counts.lock().get("repositories.json"), Some(&1));

    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn no_attempt_to_open_persisted_dynamic_repos_if_disabled() {
    let (proxy, open_counts) = spawn_directory_handler();
    let mounts = Mounts {
        pkg_resolver_data: DirOrProxy::Proxy(proxy),
        pkg_resolver_config_data: DirOrProxy::Dir(tempfile::tempdir().expect("/tmp to exist")),
    };
    mounts.add_config(&Config { disable_dynamic_configuration: true });
    let env = TestEnvBuilder::new().mounts(mounts).build();

    // Waits for pkg_resolver to be initialized
    get_repos(&env.proxies.repo_manager).await;

    assert_eq!(open_counts.lock().get("repositories.json"), None);

    env.stop().await;
}
