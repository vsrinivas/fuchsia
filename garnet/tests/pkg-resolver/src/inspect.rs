// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*, fidl_fuchsia_pkg_ext::RepositoryConfigBuilder, fuchsia_async as fasync,
    fuchsia_inspect::assert_inspect_tree, fuchsia_pkg_testing::RepositoryBuilder,
};

#[fasync::run_singlethreaded(test)]
async fn test_initial_inspect_state() {
    let env = TestEnv::new();
    // Wait for inspect to be created
    env.proxies
        .rewrite_engine
        .test_apply("fuchsia-pkg://test")
        .await
        .expect("fidl call succeeds")
        .expect("test apply result is ok");

    // Obtain inspect hierarchy
    let hierarchy = env.pkg_resolver_inspect_hierarchy().await;

    assert_inspect_tree!(
        hierarchy,
        root: {
            rewrite_manager: {
                dynamic_rules: {},
                dynamic_rules_path: format!("{:?}", Some(std::path::Path::new("/data/rewrites.json"))),
                static_rules: {},
                generation: 0u64,
            },
            main: {
              channel: {
                tuf_config_name: format!("{:?}", Option::<String>::None),
                channel_name: format!("{:?}", Option::<String>::None),
              }
            },
            experiments: {
            },
            repository_manager: {
                dynamic_configs_path: format!("{:?}", Some(std::path::Path::new("/data/repositories.json"))),
                dynamic_configs: {},
                static_configs: {},
                amber_conns: {},
                repos: {},
                stats: {
                    mirrors: {}
                }
            }
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_adding_repo_updates_inspect_state() {
    let env = TestEnv::new();
    let config = RepositoryConfigBuilder::new("fuchsia-pkg://example.com".parse().unwrap()).build();
    Status::ok(
        env.proxies.repo_manager.add(config.clone().into()).await.expect("fidl call succeeds"),
    )
    .expect("repo successfully added");

    // Obtain inspect service and convert into a node hierarchy.
    let hierarchy = env.pkg_resolver_inspect_hierarchy().await;

    assert_inspect_tree!(
        hierarchy,
        root: {
            rewrite_manager: {
                dynamic_rules: {},
                dynamic_rules_path: format!("{:?}", Some(std::path::Path::new("/data/rewrites.json"))),
                static_rules: {},
                generation: 0u64,
            },
            main: {
              channel: {
                tuf_config_name: format!("{:?}", Option::<String>::None),
                channel_name: format!("{:?}", Option::<String>::None),
              }
            },
            experiments: {
            },
            repository_manager: {
                dynamic_configs_path: format!("{:?}", Some(std::path::Path::new("/data/repositories.json"))),
                dynamic_configs: {
                    "example.com": {
                        root_keys: {},
                        mirrors: {},
                        update_package_url: format!("{:?}", config.update_package_url()),
                    }
                },
                static_configs: {},
                amber_conns: {},
                repos: {},
                stats: {
                    mirrors: {}
                }
            }
        }
    );
    env.stop().await;
}

#[fasync::run_singlethreaded(test)]
async fn test_resolving_package_updates_inspect_state() {
    let env = TestEnv::new();
    env.set_experiment_state(Experiment::RustTuf, true).await;

    let pkg = PackageBuilder::new("just_meta_far").build().await.expect("created pkg");
    let repo = RepositoryBuilder::new().add_package(&pkg).build().await.unwrap();
    let served_repository = repo.serve(env.launcher()).await.unwrap();
    let repo_url = "fuchsia-pkg://example.com".parse().unwrap();
    let config = served_repository.make_repo_config(repo_url);
    env.proxies.repo_manager.add(config.clone().into()).await.unwrap();

    env.resolve_package("fuchsia-pkg://example.com/just_meta_far")
        .await
        .expect("package to resolve");

    assert_inspect_tree!(
        env.pkg_resolver_inspect_hierarchy().await,
        root:   {
            rewrite_manager: {
                dynamic_rules: {},
                dynamic_rules_path: format!("{:?}", Some(std::path::Path::new("/data/rewrites.json"))),
                static_rules: {},
                generation: 0u64,
            },
            main: {
              channel: {
                tuf_config_name: format!("{:?}", Option::<String>::None),
                channel_name: format!("{:?}", Option::<String>::None),
              }
            },
            experiments: {
              "RustTuf": 1i64
            },
            repository_manager: {
                dynamic_configs_path: format!("{:?}", Some(std::path::Path::new("/data/repositories.json"))),
                dynamic_configs: {
                    "example.com": {
                        root_keys: {
                          "0": format!("{:?}", config.root_keys()[0])
                        },
                        mirrors: {
                          "0": {
                            blob_key: format!("{:?}", config.mirrors()[0].blob_key()),
                            mirror_url: format!("{:?}", config.mirrors()[0].mirror_url()),
                            subscribe: format!("{:?}", config.mirrors()[0].subscribe()),
                            blob_mirror_url: format!("{:?}", config.mirrors()[0].blob_mirror_url())
                          }
                        },
                        update_package_url: format!("{:?}", config.update_package_url()),
                    }
                },
                static_configs: {},
                amber_conns: {},
                repos: {
                  "example.com": contains {
                    num_packages_fetched: 1u64,
                    last_updated_time: fuchsia_inspect::testing::AnyProperty,
                  },
                },
                stats: {
                    mirrors: {
                        format!("{}/blobs", served_repository.local_url()) => {
                            network_blips: 0u64,
                            network_rate_limits: 0u64,
                        },
                    },
                },
            }
        }
    );
    env.stop().await;
}
