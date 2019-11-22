// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    fidl_fuchsia_pkg_ext::RepositoryConfigBuilder,
    fuchsia_async as fasync,
    fuchsia_inspect::{assert_inspect_tree, reader::PartialNodeHierarchy},
    std::convert::TryFrom,
};

#[fasync::run_singlethreaded(test)]
async fn test_initial_inspect_state() {
    let env = TestEnv::new();
    // Wait for inspect VMO to be created
    env.proxies
        .rewrite_engine
        .test_apply("fuchsia-pkg://test")
        .await
        .expect("fidl call succeeds")
        .expect("test apply result is ok");

    // Obtain VMO and convert into node heirarchy
    let vmo = env.pkg_resolver_inspect_vmo();
    let partial = PartialNodeHierarchy::try_from(&vmo).unwrap();

    assert_inspect_tree!(
       partial,
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
                conns: {},
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

    // Obtain VMO and convert into node heirarchy
    let vmo = env.pkg_resolver_inspect_vmo();
    let partial = PartialNodeHierarchy::try_from(&vmo).unwrap();

    assert_inspect_tree!(
      partial,
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
                conns: {},
                stats: {
                    mirrors: {}
                }
            }
        }
    );
    env.stop().await;
}
