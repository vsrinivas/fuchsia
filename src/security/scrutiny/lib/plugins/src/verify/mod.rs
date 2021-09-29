// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod collection;
mod collector;
mod controller;

use {
    crate::verify::{
        collector::component_tree::V2ComponentTreeDataCollector,
        controller::{
            build::VerifyBuildController,
            capability_routing::{CapabilityRouteController, TreeMappingController},
            route_sources::RouteSourcesController,
        },
    },
    cm_fidl_analyzer::{
        capability_routing::{error::CapabilityRouteError, route::RouteSegment},
        component_tree::NodePath,
        serde_ext::ErrorWithMessage,
    },
    cm_rust::{CapabilityName, CapabilityTypeName},
    scrutiny::prelude::*,
    serde::{Deserialize, Serialize},
    std::{collections::HashSet, sync::Arc},
};

pub use controller::route_sources::{
    RouteSourceError, Source, VerifyRouteSourcesResult, VerifyRouteSourcesResults,
};

plugin!(
    VerifyPlugin,
    PluginHooks::new(
        collectors! {
            "V2ComponentTreeDataCollector" => V2ComponentTreeDataCollector::new(),
        },
        controllers! {
            "/verify/build" => VerifyBuildController::default(),
            "/verify/map_tree" => TreeMappingController::default(),
            "/verify/capability_routes" => CapabilityRouteController::default(),
            "/verify/route_sources" => RouteSourcesController::default(),
        }
    ),
    vec![PluginDescriptor::new("CorePlugin")]
);

/// Top-level result type for `CapabilityRouteController` query result.
#[derive(Deserialize, Serialize)]
pub struct CapabilityRouteResults {
    pub deps: HashSet<String>,
    pub results: Vec<ResultsForCapabilityType>,
}

/// `CapabilityRouteController` query results grouped by severity.
#[derive(Deserialize, Serialize)]
pub struct ResultsForCapabilityType {
    pub capability_type: CapabilityTypeName,
    pub results: ResultsBySeverity,
}

/// Results from `CapabilityRouteController` grouped by severity (error,
/// warning, ok).
#[derive(Default, Deserialize, Serialize)]
pub struct ResultsBySeverity {
    #[serde(skip_serializing_if = "Vec::is_empty", default)]
    pub errors: Vec<ErrorResult>,
    #[serde(skip_serializing_if = "Vec::is_empty", default)]
    pub warnings: Vec<WarningResult>,
    #[serde(skip_serializing_if = "Vec::is_empty", default)]
    pub ok: Vec<OkResult>,
}

/// Error-severity results from `CapabilityRouteController`.
#[derive(Clone, Deserialize, PartialEq, Serialize)]
pub struct ErrorResult {
    pub using_node: NodePath,
    pub capability: CapabilityName,
    pub error: ErrorWithMessage<CapabilityRouteError>,
}

/// Warning-severity results from `CapabilityRouteController`.
#[derive(Deserialize, Serialize)]
pub struct WarningResult {
    pub using_node: NodePath,
    pub capability: CapabilityName,
    pub warning: ErrorWithMessage<CapabilityRouteError>,
}

/// Ok-severity results from `CapabilityRouteController`.
#[derive(Deserialize, Serialize)]
pub struct OkResult {
    pub using_node: NodePath,
    pub capability: CapabilityName,
    #[serde(skip_serializing_if = "Vec::is_empty", default)]
    pub route: Vec<RouteSegment>,
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            core::collection::{
                testing::fake_component_src_pkg, Component, Components, CoreDataDeps, Manifest,
                ManifestData, Manifests, Zbi,
            },
            verify::{
                collection::V2ComponentTree,
                collector::component_tree::{DEFAULT_CONFIG_PATH, DEFAULT_ROOT_URL},
            },
        },
        anyhow::Result,
        cm_fidl_analyzer::capability_routing::testing::build_two_node_tree,
        cm_rust::{
            CapabilityDecl, CapabilityName, CapabilityPath, ChildDecl, ComponentDecl,
            DependencyType, DirectoryDecl, NativeIntoFidl, OfferDecl, OfferDirectoryDecl,
            OfferProtocolDecl, OfferSource, OfferTarget, UseDecl, UseDirectoryDecl,
            UseProtocolDecl, UseSource,
        },
        fidl::encoding::encode_persistent,
        fidl_fuchsia_component_internal as component_internal,
        fidl_fuchsia_io2::Operations,
        fidl_fuchsia_sys2 as fsys2,
        maplit::hashset,
        scrutiny_testing::fake::*,
        serde_json::json,
        std::collections::HashMap,
    };

    static CORE_DEP_STR: &str = "core_dep";

    fn data_model() -> Arc<DataModel> {
        fake_data_model()
    }

    fn new_child_decl(name: String, url: String) -> ChildDecl {
        ChildDecl {
            name,
            url,
            startup: fsys2::StartupMode::Lazy,
            environment: None,
            on_terminate: None,
        }
    }

    fn new_component_decl(children: Vec<ChildDecl>) -> ComponentDecl {
        ComponentDecl {
            program: None,
            uses: vec![],
            exposes: vec![],
            offers: vec![],
            capabilities: vec![],
            children,
            collections: vec![],
            facets: None,
            environments: vec![],
        }
    }

    fn new_use_directory_decl(
        source: UseSource,
        source_name: CapabilityName,
        rights: Operations,
    ) -> UseDirectoryDecl {
        UseDirectoryDecl {
            source,
            source_name,
            target_path: CapabilityPath { dirname: "".to_string(), basename: "".to_string() },
            rights,
            subdir: None,
            dependency_type: DependencyType::Strong,
        }
    }

    fn new_offer_directory_decl(
        source: OfferSource,
        source_name: CapabilityName,
        target: OfferTarget,
        target_name: CapabilityName,
        rights: Option<Operations>,
    ) -> OfferDirectoryDecl {
        OfferDirectoryDecl {
            source,
            source_name,
            target,
            target_name,
            rights,
            subdir: None,
            dependency_type: DependencyType::Strong,
        }
    }

    fn new_directory_decl(name: CapabilityName, rights: Operations) -> DirectoryDecl {
        DirectoryDecl { name, source_path: None, rights }
    }

    fn new_use_protocol_decl(source: UseSource, source_name: CapabilityName) -> UseProtocolDecl {
        UseProtocolDecl {
            source,
            source_name,
            target_path: CapabilityPath { dirname: "".to_string(), basename: "".to_string() },
            dependency_type: DependencyType::Strong,
        }
    }

    fn new_offer_protocol_decl(
        source: OfferSource,
        source_name: CapabilityName,
        target: OfferTarget,
        target_name: CapabilityName,
    ) -> OfferProtocolDecl {
        OfferProtocolDecl {
            source,
            source_name,
            target,
            target_name,
            dependency_type: DependencyType::Strong,
        }
    }

    fn make_v2_component(id: i32, url: String) -> Component {
        Component { id, url, version: 2, source: fake_component_src_pkg() }
    }

    fn make_v2_manifest(component_id: i32, decl: ComponentDecl) -> Result<Manifest> {
        let mut decl_fidl: fsys2::ComponentDecl = decl.native_into_fidl();
        let decl_base64 = base64::encode(&encode_persistent(&mut decl_fidl)?);
        Ok(Manifest { component_id, manifest: ManifestData::Version2(decl_base64), uses: vec![] })
    }

    fn single_v2_component_model(root_component_url: Option<String>) -> Result<Arc<DataModel>> {
        let model = data_model();
        let root_id = 0;
        let root_component = make_v2_component(
            root_id,
            root_component_url.clone().unwrap_or(DEFAULT_ROOT_URL.to_string()),
        );
        let root_manifest = make_v2_manifest(root_id, new_component_decl(vec![]))?;
        let deps = hashset! { CORE_DEP_STR.to_string() };
        model.set(Components::new(vec![root_component]))?;
        model.set(Manifests::new(vec![root_manifest]))?;
        model.set(Zbi { ..zbi(root_component_url) })?;
        model.set(CoreDataDeps { deps })?;
        Ok(model)
    }

    fn multi_v2_component_model() -> Result<Arc<DataModel>> {
        let model = data_model();
        let root_id = 0;
        let foo_id = 1;
        let bar_id = 2;
        let baz_id = 3;

        let root_url = DEFAULT_ROOT_URL.to_string();
        let foo_url = "fuchsia-boot:///#meta/foo.cm".to_string();
        let bar_url = "fuchsia-boot:///#meta/bar.cm".to_string();
        let baz_url = "fuchsia-boot:///#meta/baz.cm".to_string();

        let root_component = make_v2_component(root_id, root_url.clone());
        let foo_component = make_v2_component(foo_id, foo_url.clone());
        let bar_component = make_v2_component(bar_id, bar_url.clone());
        let baz_component = make_v2_component(baz_id, baz_url.clone());

        let root_decl = new_component_decl(vec![
            new_child_decl("foo".to_string(), foo_url),
            new_child_decl("bar".to_string(), bar_url),
        ]);
        let foo_decl = new_component_decl(vec![new_child_decl("baz".to_string(), baz_url)]);
        let bar_decl = new_component_decl(vec![]);
        let baz_decl = new_component_decl(vec![]);

        let root_manifest = make_v2_manifest(root_id, root_decl)?;
        let foo_manifest = make_v2_manifest(foo_id, foo_decl)?;
        let bar_manifest = make_v2_manifest(bar_id, bar_decl)?;
        let baz_manifest = make_v2_manifest(baz_id, baz_decl)?;

        let deps = hashset! { CORE_DEP_STR.to_string() };

        model.set(Components::new(vec![
            root_component,
            foo_component,
            bar_component,
            baz_component,
        ]))?;

        model.set(Manifests::new(vec![root_manifest, foo_manifest, bar_manifest, baz_manifest]))?;
        model.set(Zbi { ..zbi(None) })?;
        model.set(CoreDataDeps { deps })?;
        Ok(model)
    }

    fn two_node_tree_model() -> Result<Arc<DataModel>> {
        let model = data_model();
        let child_name = "child".to_string();
        let missing_child_name = "missing_child".to_string();

        let good_dir_name = CapabilityName("good_dir".to_string());
        let bad_dir_name = CapabilityName("bad_dir".to_string());
        let offer_rights = Operations::Connect;

        let protocol_name = CapabilityName("protocol".to_string());

        let root_offer_good_dir = new_offer_directory_decl(
            OfferSource::Self_,
            good_dir_name.clone(),
            OfferTarget::static_child(child_name.clone()),
            good_dir_name.clone(),
            Some(offer_rights),
        );
        let root_offer_protocol = new_offer_protocol_decl(
            OfferSource::static_child(missing_child_name.clone()),
            protocol_name.clone(),
            OfferTarget::static_child(child_name.clone()),
            protocol_name.clone(),
        );
        let root_good_dir_decl = new_directory_decl(good_dir_name.clone(), offer_rights);

        let child_use_good_dir =
            new_use_directory_decl(UseSource::Parent, good_dir_name.clone(), offer_rights);
        let child_use_bad_dir =
            new_use_directory_decl(UseSource::Parent, bad_dir_name.clone(), offer_rights);
        let child_use_protocol = new_use_protocol_decl(UseSource::Parent, protocol_name.clone());

        let build_tree_result = build_two_node_tree(
            vec![],
            vec![
                OfferDecl::Directory(root_offer_good_dir),
                OfferDecl::Protocol(root_offer_protocol),
            ],
            vec![CapabilityDecl::Directory(root_good_dir_decl)],
            vec![
                UseDecl::Directory(child_use_good_dir.clone()),
                UseDecl::Directory(child_use_bad_dir.clone()),
                UseDecl::Protocol(child_use_protocol.clone()),
            ],
            vec![],
            vec![],
        );
        assert!(build_tree_result.tree.is_some());
        let tree = build_tree_result.tree.unwrap();
        let deps = hashset! { "v2_component_tree_dep".to_string() };

        model.set(V2ComponentTree::new(deps, tree, build_tree_result.errors))?;
        Ok(model)
    }

    fn zbi(root_component_url: Option<String>) -> Zbi {
        let mut bootfs: HashMap<String, Vec<u8>> = HashMap::default();
        let mut runtime_config = component_internal::Config::EMPTY;
        runtime_config.root_component_url = root_component_url;
        bootfs.insert(
            DEFAULT_CONFIG_PATH.to_string(),
            fidl::encoding::encode_persistent(&mut runtime_config).unwrap(),
        );
        return Zbi { sections: Vec::default(), bootfs, cmdline: "".to_string() };
    }

    #[test]
    fn test_map_tree_single_node_default_url() -> Result<()> {
        let model = single_v2_component_model(None)?;
        V2ComponentTreeDataCollector::new().collect(model.clone())?;

        let controller = TreeMappingController::default();
        let response = controller.query(model.clone(), json!("{}"))?;
        assert_eq!(
            response,
            json!({"route": [{"node": "/", "url": DEFAULT_ROOT_URL.to_string()}]})
        );

        Ok(())
    }

    #[test]
    fn test_map_tree_single_node_custom_url() -> Result<()> {
        let root_url = "fuchsia-boot:///#meta/foo.cm".to_string();
        let model = single_v2_component_model(Some(root_url.clone()))?;
        V2ComponentTreeDataCollector::new().collect(model.clone())?;

        let controller = TreeMappingController::default();
        let response = controller.query(model.clone(), json!("{}"))?;
        assert_eq!(response, json!({"route": [ {"node": "/", "url": root_url }]}));

        Ok(())
    }

    #[test]
    fn test_map_tree_multi_node() -> Result<()> {
        let model = multi_v2_component_model()?;
        V2ComponentTreeDataCollector::new().collect(model.clone())?;

        let controller = TreeMappingController::default();
        let response = controller.query(model.clone(), json!("{}"))?;
        assert!(
            (response
                == json!({"route": [{"node": "/", "url": DEFAULT_ROOT_URL.to_string()},
                                 {"node": "/foo","url": "fuchsia-boot:///#meta/foo.cm"},
                                 {"node": "/bar", "url": "fuchsia-boot:///#meta/bar.cm"},
                                 {"node": "/foo/baz", "url": "fuchsia-boot:///#meta/baz.cm"}]}))
                | (response
                    == json!({"route": [{"node": "/", "url": DEFAULT_ROOT_URL.to_string()},
                                 {"node": "/bar","url": "fuchsia-boot:///#meta/bar.cm"},
                                 {"node": "/foo", "url": "fuchsia-boot:///#meta/foo.cm"},
                                 {"node": "/foo/baz", "url": "fuchsia-boot:///#meta/baz.cm"}]}))
        );

        Ok(())
    }

    #[test]
    fn test_capability_routing_all_results() -> Result<()> {
        let model = two_node_tree_model()?;

        let controller = CapabilityRouteController::default();
        let response = controller.query(
            model.clone(),
            json!({ "capability_types": "directory protocol",
                     "response_level": "all"}),
        )?;

        let expected = json!({
          "deps": ["v2_component_tree_dep"],
          "results": [
            {
              "capability_type": "directory",
              "results": {
                "errors": [
                  {
                    "capability": "bad_dir",
                    "error": {
                      "error": {
                        "offer_decl_not_found": [
                          "/",
                          "bad_dir"
                        ]
                      },
                      "message": "no offer declaration for `/` with name `bad_dir`"
                    },
                    "using_node": "/child"
                  }
                ],
                "ok": [
                  {
                    "capability": "good_dir",
                    "using_node": "/child"
                  }
                ]
              }
            },
            {
              "capability_type": "protocol",
              "results": {
                "warnings": [
                  {
                    "capability": "protocol",
                    "using_node": "/child",
                    "warning": {
                      "error": {
                        "component_not_found": {
                          "component_node_not_found": "/missing_child"
                        }
                      },
                      "message": "failed to find component: `no node found with path `/missing_child``"
                    }
                  }
                ]
              }
            }
          ]
        });

        assert_eq!(response, expected);

        Ok(())
    }

    #[test]
    fn test_capability_routing_verbose_results() -> Result<()> {
        let model = two_node_tree_model()?;

        let controller = CapabilityRouteController::default();
        let response = controller.query(
            model.clone(),
            json!({ "capability_types": "directory protocol",
                     "response_level": "verbose"}),
        )?;

        let expected = json!({
          "deps": ["v2_component_tree_dep"],
          "results": [
            {
              "capability_type": "directory",
              "results": {
                "errors": [
                  {
                    "capability": "bad_dir",
                    "error": {
                      "error": {
                        "offer_decl_not_found": [
                          "/",
                          "bad_dir"
                        ]
                      },
                      "message": "no offer declaration for `/` with name `bad_dir`"
                    },
                    "using_node": "/child"
                  }
                ],
                "ok": [
                  {
                    "capability": "good_dir",
                    "route": [
                      {
                        "action": "use_by",
                        "capability": {
                          "dependency_type": "strong",
                          "rights": 1,
                          "source": "parent",
                          "source_name": "good_dir",
                          "subdir": null,
                          "target_path": "/",
                          "type": "directory"
                        },
                        "node_path": "/child"
                      },
                      {
                        "action": "offer_by",
                        "capability": {
                          "dependency_type": "strong",
                          "rights": 1,
                          "source": "self_",
                          "source_name": "good_dir",
                          "subdir": null,
                          "target": {
                            "child": {
                              "name": "child",
                              "collection": null,
                            }
                          },
                          "target_name": "good_dir",
                          "type": "directory"
                        },
                        "node_path": "/"
                      },
                      {
                        "action": "declare_by",
                        "capability": {
                          "name": "good_dir",
                          "rights": 1,
                          "source_path": null,
                          "type": "directory"
                        },
                        "node_path": "/"
                      }
                    ],
                    "using_node": "/child"
                  }
                ]
              }
            },
            {
              "capability_type": "protocol",
              "results": {
                "warnings": [
                  {
                    "capability": "protocol",
                    "using_node": "/child",
                    "warning": {
                      "error": {
                        "component_not_found": {
                          "component_node_not_found": "/missing_child"
                        }
                      },
                      "message": "failed to find component: `no node found with path `/missing_child``"
                    }
                  }
                ]
              }
            }
          ]
        });

        assert_eq!(response, expected);

        Ok(())
    }

    #[test]
    fn test_capability_routing_warn() -> Result<()> {
        let model = two_node_tree_model()?;

        let controller = CapabilityRouteController::default();
        let response = controller.query(
            model.clone(),
            json!({ "capability_types": "directory protocol",
                     "response_level": "warn"}),
        )?;

        let expected = json!({
          "deps": ["v2_component_tree_dep"],
          "results": [
            {
              "capability_type": "directory",
              "results": {
                "errors": [
                  {
                    "capability": "bad_dir",
                    "error": {
                      "error": {
                        "offer_decl_not_found": [
                          "/",
                          "bad_dir"
                        ]
                      },
                      "message": "no offer declaration for `/` with name `bad_dir`"
                    },
                    "using_node": "/child"
                  }
                ]
              }
            },
            {
              "capability_type": "protocol",
              "results": {
                "warnings": [
                  {
                    "capability": "protocol",
                    "using_node": "/child",
                    "warning": {
                      "error": {
                        "component_not_found": {
                          "component_node_not_found": "/missing_child"
                        }
                      },
                      "message": "failed to find component: `no node found with path `/missing_child``"
                    }
                  }
                ]
              }
            }
          ]
        });

        assert_eq!(response, expected);

        Ok(())
    }

    #[test]
    fn test_capability_routing_errors_only() -> Result<()> {
        let model = two_node_tree_model()?;

        let controller = CapabilityRouteController::default();
        let response = controller.query(
            model.clone(),
            json!({ "capability_types": "directory protocol",
                     "response_level": "error"}),
        )?;

        let expected = json!({
          "deps": ["v2_component_tree_dep"],
          "results": [
            {
              "capability_type": "directory",
              "results": {
                "errors": [
                  {
                    "capability": "bad_dir",
                    "error": {
                      "error": {
                        "offer_decl_not_found": [
                          "/",
                          "bad_dir"
                        ]
                      },
                      "message": "no offer declaration for `/` with name `bad_dir`"
                    },
                    "using_node": "/child"
                  }
                ]
              }
            },
            {
              "capability_type": "protocol",
              "results": {}
            }
          ]
        });

        assert_eq!(response, expected);

        Ok(())
    }
}
