// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::{Components, ManifestData, Manifests, Sysmgr},
    anyhow,
    scrutiny::{
        collectors, controllers,
        engine::{
            hook::PluginHooks,
            plugin::{Plugin, PluginDescriptor},
        },
        model::collector::DataCollector,
        model::controller::DataController,
        model::model::DataModel,
        plugin,
    },
    serde::{Deserialize, Serialize},
    serde_json::{self, value::Value},
    std::{
        collections::{HashMap, HashSet},
        sync::Arc,
    },
};

#[derive(Default)]
pub struct FindSysRealmComponents {}

#[derive(Debug, Clone, Deserialize, Serialize, PartialEq, Eq, Hash)]
struct ComponentManifest {
    pub url: String,
    pub manifest: String,
    pub features: ManifestContent,
}

#[derive(Debug, Deserialize, Clone, Serialize, PartialEq, Eq, Hash)]
struct ManifestContent {
    features: Option<Vec<String>>,
}

impl DataController for FindSysRealmComponents {
    fn query(&self, model: Arc<DataModel>, _query: Value) -> Result<Value, anyhow::Error> {
        // 1. Identify the URLs that provide services in the sys realm
        // 2. Find the data model's IDs for the components
        // 3. Using the component IDs, find the manifest for the providing components
        let mut component_urls = HashSet::<String>::new();
        let sysmgr = model.get::<Sysmgr>()?;
        for svc in sysmgr.services.keys() {
            component_urls.insert(sysmgr.services.get(svc).unwrap().clone());
        }

        for app in &sysmgr.apps {
            component_urls.insert(app.clone());
        }

        let mut component_ids = HashMap::<i32, String>::new();
        let components = model.get::<Components>()?;
        for c in &components.entries {
            if component_urls.contains(&c.url) {
                component_ids.insert(c.id, c.url.clone());
            }
        }

        let mut component_manifests = HashMap::<i32, ComponentManifest>::new();
        let manifests = model.get::<Manifests>()?;
        for manifest in &manifests.entries {
            component_ids.get(&manifest.component_id).map(|url| {
                let manifest_str = match &manifest.manifest {
                    ManifestData::Version1(content) => content.clone(),
                    ManifestData::Version2(content) => content.clone(),
                };
                let features = serde_json::from_str::<ManifestContent>(&manifest_str).ok()?;
                component_manifests.insert(
                    manifest.component_id,
                    ComponentManifest {
                        url: url.clone(),
                        manifest: manifest_str,
                        features: features,
                    },
                );
                Some(url)
            });
        }

        let mut feature_index = HashMap::<String, Vec<ComponentManifest>>::new();
        for manifest in component_manifests.values() {
            match &manifest.features.features {
                Some(features) => {
                    for feature in features {
                        if let Some(f) = feature_index.get_mut(feature) {
                            f.push(manifest.clone());
                        } else {
                            feature_index.insert(feature.clone(), vec![manifest.clone()]);
                        }
                    }
                }
                None => {
                    if let Some(f) = feature_index.get_mut(&"".to_string()) {
                        f.push(manifest.clone());
                    } else {
                        feature_index.insert("".to_string(), vec![manifest.clone()]);
                    }
                }
            }
        }
        Ok(serde_json::to_value(
            component_manifests.values().into_iter().collect::<Vec<&ComponentManifest>>(),
        )?)
    }

    fn description(&self) -> String {
        String::from("Finds the components that live in the v1 sys realm")
    }
}

plugin!(
    SysRealmPlugin,
    PluginHooks::new(
        collectors! {},
        controllers! {
           "/sys/realm" => FindSysRealmComponents::default(),
        }
    ),
    vec![PluginDescriptor::new("CorePlugin")]
);

#[cfg(test)]
mod tests {
    use {
        super::{ComponentManifest, FindSysRealmComponents, ManifestContent},
        crate::core::{
            package::{
                collector::PackageDataCollector,
                getter::PackageGetter,
                test_utils::{self, MockPackageGetter, MockPackageReader},
            },
            util::{
                jsons::{Custom, FarPackageDefinition, Signed, TargetsJson},
                types::{ComponentV1Manifest, PackageDefinition},
            },
        },
        scrutiny::model::controller::DataController,
        scrutiny_testing::fake::fake_model_config,
        serde_json,
        std::{
            collections::{HashMap, HashSet},
            sync::Arc,
        },
    };

    struct SysRealmProvider {
        pkg_name: String,
        config_path: String,
        config_content: String,
        service_provider: ServiceProvider,
        app: Option<SysApp>,
    }

    struct ServiceProvider {
        sandbox: ComponentV1Manifest,
        serialized_sandbox: String,
        manifest_path: String,
        service_name: String,
    }

    struct SysApp {
        sandbox: ComponentV1Manifest,
        serialized_sandbox: String,
        manifest_path: String,
    }

    impl SysRealmProvider {
        pub fn new(
            pkg_name: String,
            manifest_path: String,
            service_name: String,
            config_path: String,
            config_content: String,
            sandbox: ComponentV1Manifest,
        ) -> Self {
            Self {
                pkg_name,
                config_path,
                config_content,
                service_provider: ServiceProvider {
                    serialized_sandbox: serde_json::to_string::<ComponentV1Manifest>(&sandbox)
                        .unwrap(),
                    sandbox,
                    manifest_path,
                    service_name,
                },
                app: None,
            }
        }
    }

    impl From<&SysRealmProvider> for PackageDefinition {
        fn from(src: &SysRealmProvider) -> PackageDefinition {
            let mut manifests = vec![(
                src.service_provider.manifest_path.clone(),
                src.service_provider.sandbox.clone(),
            )];
            if let Some(app) = &src.app {
                manifests.push((app.manifest_path.clone(), app.sandbox.clone()));
            }

            let src_manifests = test_utils::create_test_cmx_map(manifests);

            test_utils::create_test_package_with_cms(src.pkg_name.clone(), src_manifests)
        }
    }

    #[test]
    fn test_regular_sys_realm() {
        let mut foo_provider = SysRealmProvider::new(
            String::from("fuchsia-pkg://fuchsia.com/foo"),
            String::from("meta/foo-server.cmx"),
            String::from("fuchsia.test.service.foo"),
            String::from("data/sysmgr/foo.config"),
            // This string itself doesn't seem to be used anywhere. It
            // seems like the meta data comes directly from the service package
            // definitions pushed into the mock package reader. We'll supply it
            // anyway for the sake of consistency.
            String::from(
                r#"{
                "services": {"fuchsia.test.service.foo": "fuchsia-pkg://fuchsia.com/foo#meta/foo-server.cmx"},
                "apps": ["fuchsia-pkg://fuchsia.com/foo#meta/foo-app.cmx"]
            }"#,
            ),
            ComponentV1Manifest {
                dev: None,
                services: None,
                system: None,
                pkgfs: None,
                features: Some(vec![String::from("isolated-temp")]),
            },
        );
        foo_provider.app = {
            let sandbox = ComponentV1Manifest {
                dev: None,
                services: None,
                system: None,
                pkgfs: None,
                features: Some(vec![String::from("config-data")]),
            };
            Some(SysApp {
                manifest_path: String::from("meta/foo-app.cmx"),
                serialized_sandbox: serde_json::to_string::<ComponentV1Manifest>(&sandbox).unwrap(),
                sandbox,
            })
        };

        let bar_provider = SysRealmProvider::new(
            String::from("fuchsia-pkg://fuchsia.com/bar"),
            String::from("meta/bar-server.cmx"),
            String::from("fuchsia.test.service.bar"),
            String::from("data/sysmgr/bar.config"),
            String::from(
                r#"{
                "services": {"fuchsia.test.service.bar": "fuchsia-pkg://fuchsia.com/bar#meta/bar-server.cmx"}
            }"#,
            ),
            ComponentV1Manifest {
                dev: None,
                services: None,
                system: None,
                pkgfs: None,
                features: None,
            },
        );

        let buzz_provider = SysRealmProvider::new(
            String::from("fuchsia-pkg://fuchsia.com/buzz"),
            String::from("meta/buzzer.cmx"),
            String::from("fuchsia.test.service.buzzit"),
            String::from("data/sysmgr/buzz.config"),
            String::from(
                r#"{
                "services": {"fuchsia.test.service.buzzit": "fuchsia-pkg://fuchsia.com/buzz#meta/buzzer.cmx"}
            }"#,
            ),
            ComponentV1Manifest {
                dev: None,
                services: None,
                system: None,
                pkgfs: None,
                features: Some(vec![
                    String::from("isolated-temp"),
                    String::from("vulkan"),
                    String::from("isolated-cache"),
                ]),
            },
        );

        // Given these package descriptions, we expect our data controller to
        // produce output that looks like this.
        let mut expected = {
            let mut manifests = HashSet::<ComponentManifest>::new();
            manifests.insert(ComponentManifest {
                url: format!(
                    "{}#{}",
                    foo_provider.pkg_name.clone(),
                    foo_provider.service_provider.manifest_path.clone()
                ),
                features: ManifestContent { features: Some(vec![String::from("isolated-temp")]) },
                manifest: foo_provider.service_provider.serialized_sandbox.clone(),
            });

            let sys_app = foo_provider.app.as_ref().unwrap();
            manifests.insert(ComponentManifest {
                url: format!("{}#{}", foo_provider.pkg_name.clone(), sys_app.manifest_path.clone()),
                features: ManifestContent { features: Some(vec![String::from("config-data")]) },
                manifest: sys_app.serialized_sandbox.clone(),
            });

            manifests.insert(ComponentManifest {
                url: format!(
                    "{}#{}",
                    bar_provider.pkg_name.clone(),
                    bar_provider.service_provider.manifest_path.clone()
                ),
                features: ManifestContent { features: None },
                manifest: bar_provider.service_provider.serialized_sandbox.clone(),
            });

            manifests.insert(ComponentManifest {
                url: format!(
                    "{}#{}",
                    buzz_provider.pkg_name.clone(),
                    buzz_provider.service_provider.manifest_path.clone()
                ),
                features: ManifestContent {
                    features: Some(vec![
                        String::from("isolated-temp"),
                        String::from("vulkan"),
                        String::from("isolated-cache"),
                    ]),
                },
                manifest: buzz_provider.service_provider.serialized_sandbox.clone(),
            });
            manifests
        };

        let mock = MockPackageReader::new();

        // create the service package definitions
        let foo_services = vec![(
            foo_provider.service_provider.service_name.clone(),
            format!(
                "{}#{}",
                foo_provider.pkg_name.clone(),
                foo_provider.service_provider.manifest_path.clone()
            ),
        )];
        let foo_apps = vec![format!(
            "{}#{}",
            foo_provider.pkg_name.clone(),
            foo_provider.app.as_ref().unwrap().manifest_path.clone()
        )];
        mock.append_service_pkg_def(test_utils::create_svc_pkg_def(foo_services, foo_apps));

        let bar_services = vec![(
            bar_provider.service_provider.service_name.clone(),
            format!(
                "{}#{}",
                bar_provider.pkg_name.clone(),
                bar_provider.service_provider.manifest_path.clone()
            ),
        )];
        mock.append_service_pkg_def(test_utils::create_svc_pkg_def(bar_services, vec![]));

        let buzz_services = vec![(
            buzz_provider.service_provider.service_name.clone(),
            format!(
                "{}#{}",
                buzz_provider.pkg_name.clone(),
                buzz_provider.service_provider.manifest_path.clone()
            ),
        )];
        mock.append_service_pkg_def(test_utils::create_svc_pkg_def(buzz_services, vec![]));

        // Create a config-data package that has appropriate entries for the
        // sysmgr package
        let mut meta_map = HashMap::new();
        meta_map.insert(foo_provider.config_path.clone(), foo_provider.config_content.clone());
        meta_map.insert(bar_provider.config_path.clone(), bar_provider.config_content.clone());
        meta_map.insert(buzz_provider.config_path.clone(), buzz_provider.config_content.clone());
        let config_data = test_utils::create_test_package_with_meta(
            fake_model_config().config_data_package_url(),
            meta_map,
        );
        mock.append_pkg_def(config_data);

        // Create the package for the service providers
        mock.append_pkg_def((&foo_provider).into());
        mock.append_pkg_def((&bar_provider).into());
        mock.append_pkg_def((&buzz_provider).into());

        // Create targets for the "foo", "bar", and "config-data" packages
        {
            let mut targets = HashMap::new();
            targets.insert(
                fake_model_config().config_data_package_url(),
                FarPackageDefinition {
                    custom: Custom { merkle: fake_model_config().config_data_package_url() },
                },
            );
            targets.insert(
                foo_provider.pkg_name.clone(),
                FarPackageDefinition { custom: Custom { merkle: foo_provider.pkg_name.clone() } },
            );

            targets.insert(
                bar_provider.pkg_name.clone(),
                FarPackageDefinition { custom: Custom { merkle: bar_provider.pkg_name.clone() } },
            );

            targets.insert(
                buzz_provider.pkg_name.clone(),
                FarPackageDefinition { custom: Custom { merkle: buzz_provider.pkg_name.clone() } },
            );

            mock.append_target(TargetsJson { signed: Signed { targets: targets } });
        }

        // With all the data created, send it to the PackageDataCollector
        let (_unknown, model) = test_utils::create_model();
        let pkg_collector = PackageDataCollector::default();
        let pkg_getter: Box<dyn PackageGetter> = Box::new(MockPackageGetter::new());
        pkg_collector
            .collect_with_reader(
                fake_model_config(),
                Box::new(mock),
                pkg_getter,
                Arc::clone(&model),
            )
            .unwrap();

        // Now run the model through our data controller
        let sys_realm = FindSysRealmComponents {};
        let actual_sys_realm = serde_json::from_value::<Vec<ComponentManifest>>(
            sys_realm.query(model.clone(), "".into()).unwrap(),
        )
        .unwrap();
        assert_eq!(actual_sys_realm.len(), 4);

        // Remove everything in `actual_sys_realm` that appears in the expected
        // output while also removing from `expected`.
        let actual_sys_realm = actual_sys_realm
            .into_iter()
            .filter(|actual| !expected.remove(actual))
            .collect::<Vec<ComponentManifest>>();

        // It should be the case that everything we found in the sys realm was
        // in the `expected` map and therefore should have been filtered out.
        assert_eq!(actual_sys_realm, vec![]);
        assert_eq!(expected.into_iter().collect::<Vec<ComponentManifest>>(), vec![]);
    }

    #[test]
    fn test_empty_sys_realm() {
        let mock_reader = MockPackageReader::new();
        // Create some packages, but ones that aren't in the sys realm
        let manifests = test_utils::create_test_cmx_map(vec![(
            String::from("meta/cmp1.cmx"),
            ComponentV1Manifest {
                dev: None,
                services: None,
                system: None,
                pkgfs: None,
                features: Some(vec![String::from("config-data")]),
            },
        )]);
        let pkg_def = test_utils::create_test_package_with_cms(
            String::from("fuchsia-pkg://fuchsia.com/pkg1"),
            manifests,
        );
        mock_reader.append_pkg_def(pkg_def);

        let manifests = test_utils::create_test_cmx_map(vec![(
            String::from("meta/cmp2.cmx"),
            ComponentV1Manifest {
                dev: None,
                services: None,
                system: None,
                pkgfs: None,
                features: None,
            },
        )]);
        let pkg_def = test_utils::create_test_package_with_cms(
            String::from("fuchsia-pkg://fuchsia.com/pkg2"),
            manifests,
        );
        mock_reader.append_pkg_def(pkg_def);

        let mut targets = HashMap::new();
        targets.insert(
            String::from("fuchsia-pkg://fuchsia.com/pkg1"),
            FarPackageDefinition {
                custom: Custom { merkle: String::from("fuchsia-pkg://fuchsia.com/pkg1") },
            },
        );

        targets.insert(
            String::from("fuchsia-pkg://fuchsia.com/pkg2"),
            FarPackageDefinition {
                custom: Custom { merkle: String::from("fuchsia-pkg://fuchsia.com/pkg2") },
            },
        );
        mock_reader.append_target(TargetsJson { signed: Signed { targets } });

        let (_unused, model) = test_utils::create_model();
        let pkg_collector = PackageDataCollector::default();
        let pkg_getter: Box<dyn PackageGetter> = Box::new(MockPackageGetter::new());
        pkg_collector
            .collect_with_reader(
                fake_model_config(),
                Box::new(mock_reader),
                pkg_getter,
                Arc::clone(&model),
            )
            .unwrap();

        let sys_realm = FindSysRealmComponents {};
        let actual_sys_realm = serde_json::from_value::<Vec<ComponentManifest>>(
            sys_realm.query(model.clone(), "".into()).unwrap(),
        )
        .unwrap();

        assert!(actual_sys_realm.is_empty());
    }
}
