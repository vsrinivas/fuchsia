// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::{
        collection::{Components, ManifestData, Manifests, Sysmgr},
        util::jsons::{deserialize_url, serialize_url},
    },
    anyhow::{Context, Error},
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
    url::Url,
};

#[derive(Default)]
pub struct FindSysRealmComponents {}

#[derive(Debug, Clone, Deserialize, Serialize, PartialEq, Eq, Hash)]
struct ComponentManifest {
    #[serde(serialize_with = "serialize_url", deserialize_with = "deserialize_url")]
    pub url: Url,
    pub manifest: String,
    pub features: ManifestContent,
}

#[derive(Debug, Deserialize, Clone, Serialize, PartialEq, Eq, Hash)]
struct ManifestContent {
    features: Option<Vec<String>>,
}

impl DataController for FindSysRealmComponents {
    fn query(&self, model: Arc<DataModel>, _query: Value) -> Result<Value, Error> {
        // 1. Identify the URLs that provide services in the sys realm
        // 2. Find the data model's IDs for the components
        // 3. Using the component IDs, find the manifest for the providing components
        let sysmgr = model.get::<Sysmgr>()?;
        let mut component_urls: HashSet<Url> = sysmgr.services.values().map(Url::clone).collect();

        for app in &sysmgr.apps {
            let app_url = Url::parse(&app.to_string()).with_context(|| {
                format!("Failed to convert package URL to generic URL: {}", app)
            })?;
            component_urls.insert(app_url);
        }

        let mut component_ids = HashMap::new();
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
                    ManifestData::Version2 { cm_base64, .. } => cm_base64.clone(),
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
            collection::{
                Component, ComponentSource, Components, Manifest, ManifestData, Manifests, Sysmgr,
            },
            package::test_utils::create_model,
        },
        fuchsia_merkle::{Hash, HASH_SIZE},
        fuchsia_url::AbsoluteComponentUrl,
        maplit::{hashmap, hashset},
        scrutiny::model::controller::DataController,
        serde_json,
        std::collections::HashSet,
        url::Url,
    };

    #[fuchsia::test]
    fn test_regular_sys_realm() {
        let (_uri_str, model) = create_model();

        let foo_server_url =
            Url::parse("fuchsia-pkg://test.fuchsia.com/foo#meta/foo-server.cmx").unwrap();
        let foo_server_id = 0;
        let foo_server_manifest = r#"{}"#;

        let bar_server_url =
            Url::parse("fuchsia-pkg://test.fuchsia.com/bar#meta/bar-server.cmx").unwrap();
        let bar_server_id = 1;
        let bar_server_manifest = r#"{}"#;

        let buzzer_url = Url::parse("fuchsia-pkg://test.fuchsia.com/buzz#meta/buzzer.cmx").unwrap();
        let buzzer_id = 2;
        let buzzer_manifest = r#"{}"#;

        let foo_app_url_str = "fuchsia-pkg://test.fuchsia.com/foo#meta/foo-app.cmx";
        let foo_app_url = Url::parse(foo_app_url_str).unwrap();
        let foo_app_pkg_url = AbsoluteComponentUrl::parse(foo_app_url_str).unwrap();
        let foo_app_id = 3;
        let foo_app_manifest = r#"{"features":["frobinator"]}"#;

        model
            .set(Sysmgr {
                services: hashmap! {
                    "fuchsia.test.service.foo".to_string() =>
                        foo_server_url.clone(),
                    "fuchsia.test.service.bar".to_string() =>
                        bar_server_url.clone(),
                    "fuchsia.test.service.buzzit".to_string() =>
                        buzzer_url.clone(),
                },
                apps: hashset! {
                    foo_app_pkg_url,
                },
            })
            .unwrap();
        model
            .set(Components {
                entries: vec![
                    Component {
                        id: foo_server_id,
                        version: 1,
                        url: foo_server_url.clone(),
                        source: ComponentSource::StaticPackage(Hash::from([0; HASH_SIZE])),
                    },
                    Component {
                        id: bar_server_id,
                        version: 1,
                        url: bar_server_url.clone(),
                        source: ComponentSource::StaticPackage(Hash::from([1; HASH_SIZE])),
                    },
                    Component {
                        id: buzzer_id,
                        version: 1,
                        url: buzzer_url.clone(),
                        source: ComponentSource::StaticPackage(Hash::from([2; HASH_SIZE])),
                    },
                    Component {
                        id: foo_app_id,
                        version: 1,
                        url: foo_app_url.clone(),
                        source: ComponentSource::StaticPackage(Hash::from([3; HASH_SIZE])),
                    },
                ],
            })
            .unwrap();
        model
            .set(Manifests {
                entries: vec![
                    Manifest {
                        component_id: foo_server_id,
                        manifest: ManifestData::Version1(foo_server_manifest.to_string()),
                        uses: vec![],
                    },
                    Manifest {
                        component_id: bar_server_id,
                        manifest: ManifestData::Version1(bar_server_manifest.to_string()),
                        uses: vec![],
                    },
                    Manifest {
                        component_id: buzzer_id,
                        manifest: ManifestData::Version1(buzzer_manifest.to_string()),
                        uses: vec![],
                    },
                    Manifest {
                        component_id: foo_app_id,
                        manifest: ManifestData::Version1(foo_app_manifest.to_string()),
                        uses: vec![],
                    },
                ],
            })
            .unwrap();

        // `HashSet` used because order of components is not what is being tested.
        let expected: HashSet<ComponentManifest> = hashset! {
            ComponentManifest {
                url: foo_server_url,
                manifest: foo_server_manifest.to_string(),
                features: ManifestContent {features: None},
            },
            ComponentManifest {
                url: bar_server_url,
                manifest: bar_server_manifest.to_string(),
                features: ManifestContent {features: None},
            },
            ComponentManifest {
                url: buzzer_url,
                manifest: buzzer_manifest.to_string(),
                features: ManifestContent {features: None},
            },
            ComponentManifest {
                url: foo_app_url,
                manifest: foo_app_manifest.to_string(),
                features: ManifestContent {features: Some(vec!["frobinator".to_string()])},
            },
        };

        let actual: HashSet<ComponentManifest> = serde_json::from_value::<Vec<ComponentManifest>>(
            FindSysRealmComponents {}.query(model.clone(), "".into()).unwrap(),
        )
        .unwrap()
        .into_iter()
        .collect();

        assert_eq!(expected, actual);
    }

    #[fuchsia::test]
    fn test_empty_sys_realm() {
        let (_uri_str, model) = create_model();

        let cmp1_url = Url::parse("fuchsia-pkg://test.fuchsia.com/pkg1#meta/cmp1.cmx").unwrap();
        let cmp1_id = 0;
        let cmp1_manifest = r#"{"features":["config-data"]}"#;

        let cmp2_url = Url::parse("fuchsia-pkg://test.fuchsia.com/pkg2#meta/cmp2.cmx").unwrap();
        let cmp2_id = 1;
        let cmp2_manifest = r#"{}"#;

        model.set(Sysmgr { services: hashmap! {}, apps: hashset! {} }).unwrap();
        model
            .set(Components {
                entries: vec![
                    Component {
                        id: cmp1_id,
                        version: 1,
                        url: cmp1_url.clone(),
                        source: ComponentSource::StaticPackage(Hash::from([0; HASH_SIZE])),
                    },
                    Component {
                        id: cmp2_id,
                        version: 1,
                        url: cmp2_url.clone(),
                        source: ComponentSource::StaticPackage(Hash::from([1; HASH_SIZE])),
                    },
                ],
            })
            .unwrap();
        model
            .set(Manifests {
                entries: vec![
                    Manifest {
                        component_id: cmp1_id,
                        manifest: ManifestData::Version1(cmp1_manifest.to_string()),
                        uses: vec![],
                    },
                    Manifest {
                        component_id: cmp2_id,
                        manifest: ManifestData::Version1(cmp2_manifest.to_string()),
                        uses: vec![],
                    },
                ],
            })
            .unwrap();

        let actual = serde_json::from_value::<Vec<ComponentManifest>>(
            FindSysRealmComponents {}.query(model.clone(), "".into()).unwrap(),
        )
        .unwrap();

        assert!(actual.is_empty());
    }
}
