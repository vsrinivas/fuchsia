// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::*,
    failure::{bail, format_err},
    fidl_fuchsia_pkg_ext::RepositoryConfigBuilder,
    fuchsia_async as fasync,
    fuchsia_inspect::{
        assert_inspect_tree,
        reader::Property,
        testing::{AnyProperty, PropertyAssertion},
    },
    fuchsia_pkg_testing::RepositoryBuilder,
    matches::assert_matches,
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
                tuf_config_name: OptionDebugStringProperty::<String>::None,
                channel_name: OptionDebugStringProperty::<String>::None,
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
                tuf_config_name: OptionDebugStringProperty::<String>::None,
                channel_name: OptionDebugStringProperty::<String>::None,
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
                    merkles_successfully_resolved_count: 1u64,
                    last_updated_time: OptionDebugStringProperty::Some(AnyProperty),
                    last_used_time: OptionDebugStringProperty::Some(AnyProperty),
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

enum OptionDebugStringProperty<I> {
    Some(I),
    None,
}

impl<I> PropertyAssertion for OptionDebugStringProperty<I>
where
    I: PropertyAssertion,
{
    fn run(&self, actual: &Property) -> Result<(), failure::Error> {
        match actual {
            Property::String(name, value) => match self {
                OptionDebugStringProperty::Some(inner) => {
                    const PREFIX: &str = "Some(";
                    const SUFFIX: &str = ")";
                    if !value.starts_with(PREFIX) {
                        bail!(r#"expected property to be "Some(...", actual {:?}"#, actual);
                    }
                    if !value.ends_with(SUFFIX) {
                        bail!(r#"expected property to be "...)", actual {:?}"#, actual);
                    }
                    let inner_value = &value[PREFIX.len()..(value.len() - SUFFIX.len())];
                    inner.run(&Property::String(name.clone(), inner_value.to_owned()))
                }
                OptionDebugStringProperty::None => {
                    if value != "None" {
                        bail!(r#"expected property string to be "None", got {:?}"#, actual);
                    }
                    Ok(())
                }
            },
            _wrong_type => Err(format_err!("expected string property, got {:?}", actual)),
        }
    }
}

#[test]
fn test_option_debug_string_property() {
    fn make_string_property(value: &str) -> Property {
        Property::String("name".to_owned(), value.to_owned())
    }

    fn dbg<D: std::fmt::Debug>(d: D) -> String {
        format!("{:?}", d)
    }

    // trivial ok
    assert_matches!(
        OptionDebugStringProperty::Some(AnyProperty).run(&make_string_property("Some()")),
        Ok(())
    );
    assert_matches!(
        OptionDebugStringProperty::<AnyProperty>::None.run(&make_string_property("None")),
        Ok(())
    );

    // trivial err
    assert_matches!(
        OptionDebugStringProperty::Some(AnyProperty).run(&make_string_property("None")),
        Err(_)
    );
    assert_matches!(
        OptionDebugStringProperty::Some(AnyProperty).run(&make_string_property("Some(foo")),
        Err(_)
    );
    assert_matches!(
        OptionDebugStringProperty::<AnyProperty>::None.run(&make_string_property("Some()")),
        Err(_)
    );

    // non-empty inner ok
    assert_matches!(
        OptionDebugStringProperty::Some(AnyProperty).run(&make_string_property("Some(value)")),
        Ok(())
    );
    assert_matches!(
        OptionDebugStringProperty::Some(dbg("string"))
            .run(&make_string_property("Some(\"string\")")),
        Ok(())
    );

    // non-empty inner err
    assert_matches!(
        OptionDebugStringProperty::Some(dbg("a")).run(&make_string_property("Some(\"b\")")),
        Err(_)
    );
}
