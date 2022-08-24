// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod collection;
mod collector;
mod controller;

use {
    crate::verify::{
        collector::component_model::V2ComponentModelDataCollector,
        controller::{
            build::VerifyBuildController,
            capability_routing::{CapabilityRouteController, V2ComponentModelMappingController},
            component_resolvers::ComponentResolversController,
            route_sources::RouteSourcesController,
            structured_config::ExtractStructuredConfigController,
            structured_config::VerifyStructuredConfigController,
        },
    },
    cm_fidl_analyzer::{
        node_path::NodePath,
        route::{CapabilityRouteError, RouteSegment},
        serde_ext::ErrorWithMessage,
    },
    cm_rust::{CapabilityName, CapabilityTypeName},
    scrutiny::prelude::*,
    serde::{Deserialize, Serialize},
    std::{collections::HashSet, path::PathBuf, sync::Arc},
};

pub use controller::{
    route_sources::{
        RouteSourceError, Source, VerifyRouteSourcesResult, VerifyRouteSourcesResults,
    },
    structured_config::ExtractStructuredConfigResponse,
    structured_config::VerifyStructuredConfigResponse,
};

plugin!(
    VerifyPlugin,
    PluginHooks::new(
        collectors! {
            "V2ComponentModelDataCollector" => V2ComponentModelDataCollector::new(),
        },
        controllers! {
            "/verify/build" => VerifyBuildController::default(),
            "/verify/v2_component_model" => V2ComponentModelMappingController::default(),
            "/verify/capability_routes" => CapabilityRouteController::default(),
            "/verify/route_sources" => RouteSourcesController::default(),
            "/verify/component_resolvers" => ComponentResolversController::default(),
            "/verify/structured_config" => VerifyStructuredConfigController::default(),

            // This doesn't actually verify anything, but we need the verify model to get
            // V2ComponentModel, and depending on this plugin in `toolkit` doesn't work.
            "/verify/structured_config/extract" => ExtractStructuredConfigController::default(),
        }
    ),
    vec![PluginDescriptor::new("CorePlugin")]
);

/// Top-level result type for `CapabilityRouteController` query result.
#[derive(Deserialize, Serialize)]
pub struct CapabilityRouteResults {
    pub deps: HashSet<PathBuf>,
    pub results: Vec<ResultsForCapabilityType>,
}

/// `CapabilityRouteController` query results grouped by severity.
#[derive(Clone, Deserialize, Serialize)]
pub struct ResultsForCapabilityType {
    pub capability_type: CapabilityTypeName,
    pub results: ResultsBySeverity,
}

/// Results from `CapabilityRouteController` grouped by severity (error,
/// warning, ok).
#[derive(Clone, Default, Deserialize, Serialize)]
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
#[derive(Clone, Deserialize, Serialize)]
pub struct WarningResult {
    pub using_node: NodePath,
    pub capability: CapabilityName,
    pub warning: ErrorWithMessage<CapabilityRouteError>,
}

/// Ok-severity results from `CapabilityRouteController`.
#[derive(Clone, Deserialize, Serialize)]
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
                collection::V2ComponentModel,
                collector::component_model::{DEFAULT_CONFIG_PATH, DEFAULT_ROOT_URL},
            },
        },
        anyhow::Result,
        cm_fidl_analyzer::component_model::ModelBuilderForAnalyzer,
        cm_rust::{
            Availability, CapabilityDecl, CapabilityName, CapabilityPath, ChildDecl, ComponentDecl,
            DependencyType, DirectoryDecl, FidlIntoNative, NativeIntoFidl, OfferDecl,
            OfferDirectoryDecl, OfferProtocolDecl, OfferSource, OfferTarget, ProgramDecl, UseDecl,
            UseDirectoryDecl, UseProtocolDecl, UseSource,
        },
        fidl::encoding::encode_persistent_with_context,
        fidl_fuchsia_component_decl as fdecl,
        fidl_fuchsia_component_internal as component_internal, fidl_fuchsia_io as fio,
        maplit::hashset,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
        routing::{
            component_id_index::ComponentIdIndex, component_instance::ComponentInstanceInterface,
            config::RuntimeConfig, environment::RunnerRegistry,
        },
        scrutiny_testing::fake::*,
        serde_json::json,
        std::{collections::HashMap, convert::TryFrom},
        url::Url,
    };

    static CORE_DEP_STR: &str = "core_dep";

    fn data_model() -> Arc<DataModel> {
        fake_data_model()
    }

    fn new_child_decl(name: String, url: String) -> ChildDecl {
        ChildDecl {
            name,
            url,
            startup: fdecl::StartupMode::Lazy,
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
            config: None,
        }
    }

    fn new_component_with_capabilities(
        uses: Vec<UseDecl>,
        offers: Vec<OfferDecl>,
        capabilities: Vec<CapabilityDecl>,
        children: Vec<ChildDecl>,
    ) -> ComponentDecl {
        let mut program = ProgramDecl::default();
        program.runner = Some("elf".into());
        ComponentDecl {
            program: Some(program),
            uses,
            exposes: vec![],
            offers,
            capabilities,
            children,
            collections: vec![],
            facets: None,
            environments: vec![],
            config: None,
        }
    }

    fn new_use_directory_decl(
        source: UseSource,
        source_name: CapabilityName,
        rights: fio::Operations,
    ) -> UseDirectoryDecl {
        UseDirectoryDecl {
            source,
            source_name,
            target_path: CapabilityPath { dirname: "".to_string(), basename: "".to_string() },
            rights,
            subdir: None,
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        }
    }

    fn new_offer_directory_decl(
        source: OfferSource,
        source_name: CapabilityName,
        target: OfferTarget,
        target_name: CapabilityName,
        rights: Option<fio::Operations>,
    ) -> OfferDirectoryDecl {
        OfferDirectoryDecl {
            source,
            source_name,
            target,
            target_name,
            rights,
            subdir: None,
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
        }
    }

    fn new_directory_decl(name: CapabilityName, rights: fio::Operations) -> DirectoryDecl {
        DirectoryDecl { name, source_path: None, rights }
    }

    fn new_use_protocol_decl(source: UseSource, source_name: CapabilityName) -> UseProtocolDecl {
        UseProtocolDecl {
            source,
            source_name,
            target_path: CapabilityPath {
                dirname: "/dir".to_string(),
                basename: "svc".to_string(),
            },
            dependency_type: DependencyType::Strong,
            availability: Availability::Required,
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
            availability: Availability::Required,
        }
    }

    fn make_v2_component(id: i32, url: String) -> Component {
        let url = Url::parse(&url).unwrap();
        Component { id, url, version: 2, source: fake_component_src_pkg() }
    }

    fn make_v2_manifest(component_id: i32, decl: ComponentDecl) -> Result<Manifest> {
        let mut decl_fidl: fdecl::Component = decl.native_into_fidl();
        let cm_base64 = base64::encode(&encode_persistent_with_context(
            &fidl::encoding::Context { wire_format_version: fidl::encoding::WireFormatVersion::V2 },
            &mut decl_fidl,
        )?);
        Ok(Manifest {
            component_id,
            manifest: ManifestData::Version2 { cm_base64, cvf_bytes: None },
            uses: vec![],
        })
    }

    // Creates a data model with a ZBI containing one component manifest and the provided component
    // id index.
    fn single_v2_component_model(
        root_component_url: Option<String>,
        component_id_index_path: Option<String>,
        component_id_index: component_id_index::Index,
    ) -> Result<Arc<DataModel>> {
        let model = data_model();
        let root_id = 0;
        let root_component = make_v2_component(
            root_id,
            root_component_url.clone().unwrap_or(DEFAULT_ROOT_URL.to_string()),
        );
        let root_manifest = make_v2_manifest(root_id, new_component_decl(vec![]))?;
        let deps = hashset! { CORE_DEP_STR.to_string().into() };
        model.set(Components::new(vec![root_component]))?;
        model.set(Manifests::new(vec![root_manifest]))?;
        model
            .set(Zbi { ..zbi(root_component_url, component_id_index_path, component_id_index) })?;
        model.set(CoreDataDeps { deps })?;
        Ok(model)
    }

    // Creates a data model with a ZBI containing 4 component manifests and a default component id index.
    // The structure of the component instance tree is:
    //
    //        root
    //       /    \
    //     foo    bar
    //     /
    //   baz
    //
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

        let root_component = make_v2_component(root_id, root_url);
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

        let deps = hashset! { CORE_DEP_STR.to_string().into() };

        model.set(Components::new(vec![
            root_component,
            foo_component,
            bar_component,
            baz_component,
        ]))?;

        model.set(Manifests::new(vec![root_manifest, foo_manifest, bar_manifest, baz_manifest]))?;
        model.set(Zbi { ..zbi(None, None, component_id_index::Index::default()) })?;
        model.set(CoreDataDeps { deps })?;
        Ok(model)
    }

    fn cmls_to_model(cmls: Vec<(&'static str, serde_json::Value)>) -> Result<Arc<DataModel>> {
        let model = data_model();

        let mut id = 0;
        let mut components = vec![];
        let mut manifests = vec![];
        for (uri, json) in cmls {
            let decl = cml::compile(&serde_json::from_value(json)?, None)?.fidl_into_native();
            let manifest = make_v2_manifest(id, decl)?;
            let component = make_v2_component(id, uri.to_owned());

            components.push(component);
            manifests.push(manifest);

            id += 1;
        }

        model.set(Components::new(components))?;
        model.set(Zbi { ..zbi(None, None, component_id_index::Index::default()) })?;
        model.set(Manifests::new(manifests))?;
        model.set(CoreDataDeps { deps: hashset! { CORE_DEP_STR.to_string().into() } })?;

        V2ComponentModelDataCollector::new().collect(model.clone())?;

        Ok(model)
    }

    fn two_instance_component_model() -> Result<Arc<DataModel>> {
        let model = data_model();

        let root_url = &*DEFAULT_ROOT_URL;
        let child_url = Url::parse("fuchsia-boot:///#meta/child.cm").unwrap();

        let child_name = "child".to_string();
        let missing_child_name = "missing_child".to_string();

        let good_dir_name = CapabilityName("good_dir".to_string());
        let bad_dir_name = CapabilityName("bad_dir".to_string());
        let offer_rights = fio::Operations::CONNECT;

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

        let mut decls = HashMap::new();
        decls.insert(
            root_url.clone(),
            (
                new_component_with_capabilities(
                    vec![],
                    vec![
                        OfferDecl::Directory(root_offer_good_dir),
                        OfferDecl::Protocol(root_offer_protocol),
                    ],
                    vec![CapabilityDecl::Directory(root_good_dir_decl)],
                    vec![new_child_decl(child_name, child_url.to_string())],
                ),
                None,
            ),
        );
        decls.insert(
            child_url,
            (
                new_component_with_capabilities(
                    vec![
                        UseDecl::Directory(child_use_good_dir),
                        UseDecl::Directory(child_use_bad_dir),
                        UseDecl::Protocol(child_use_protocol),
                    ],
                    vec![],
                    vec![],
                    vec![],
                ),
                None,
            ),
        );

        let build_model_result = ModelBuilderForAnalyzer::new(root_url.clone()).build(
            decls,
            Arc::new(RuntimeConfig::default()),
            Arc::new(ComponentIdIndex::default()),
            RunnerRegistry::default(),
        );
        assert!(build_model_result.errors.is_empty());
        assert!(build_model_result.model.is_some());
        let component_model = build_model_result.model.unwrap();
        assert_eq!(component_model.len(), 2);
        let deps = hashset! { "v2_component_tree_dep".to_string().into() };

        model.set(V2ComponentModel::new(deps, component_model, build_model_result.errors))?;
        Ok(model)
    }

    fn zbi(
        root_component_url: Option<String>,
        component_id_index_path: Option<String>,
        component_id_index: component_id_index::Index,
    ) -> Zbi {
        let mut bootfs: HashMap<String, Vec<u8>> = HashMap::default();
        let mut runtime_config = component_internal::Config::EMPTY;
        runtime_config.root_component_url = root_component_url;
        runtime_config.component_id_index_path = component_id_index_path.clone();

        if let Some(path) = component_id_index_path {
            let split_index_path: Vec<&str> = path.split_inclusive("/").collect();
            if split_index_path.as_slice()[..2] == ["/", "boot/"] {
                bootfs.insert(
                    split_index_path[2..].join(""),
                    fidl::encoding::encode_persistent_with_context(
                        &fidl::encoding::Context {
                            wire_format_version: fidl::encoding::WireFormatVersion::V2,
                        },
                        &mut component_internal::ComponentIdIndex::try_from(component_id_index)
                            .expect("failed to convert component id index to fidl"),
                    )
                    .expect("failed to encode component id index as persistent fidl"),
                );
            }
        }

        bootfs.insert(
            DEFAULT_CONFIG_PATH.to_string(),
            fidl::encoding::encode_persistent_with_context(
                &fidl::encoding::Context {
                    wire_format_version: fidl::encoding::WireFormatVersion::V2,
                },
                &mut runtime_config,
            )
            .unwrap(),
        );
        return Zbi { sections: Vec::default(), bootfs, cmdline: "".to_string() };
    }

    // Prepares a ZBI with a nonempty component ID index, collects a `V2ComponentModel` with one
    // component instance, and checks that the component ID index provided by that component instance
    // contains the expected entry.
    #[test]
    fn collect_component_model_with_id_index() -> Result<()> {
        let iid = "0".repeat(64);
        let model = single_v2_component_model(
            None,
            Some("/boot/index_path".to_string()),
            component_id_index::Index {
                instances: vec![component_id_index::InstanceIdEntry {
                    instance_id: Some(iid.clone()),
                    appmgr_moniker: None,
                    moniker: Some(AbsoluteMoniker::parse_str("/a/b/c").unwrap()),
                }],
                ..component_id_index::Index::default()
            },
        )?;
        V2ComponentModelDataCollector::new().collect(model.clone())?;

        let collection =
            &model.get::<V2ComponentModel>().expect("failed to find the v2 component model");
        assert!(collection.errors.is_empty());

        let root_instance = collection.component_model.get_root_instance()?;

        assert_eq!(
            Some(&iid),
            root_instance
                .try_get_component_id_index()?
                .look_up_moniker(&AbsoluteMoniker::parse_str("/a/b/c").unwrap())
        );
        Ok(())
    }

    #[test]
    fn test_component_resolvers_child_source() -> Result<()> {
        let model = cmls_to_model(vec![
            (
                "fuchsia-boot:///#meta/root.cm",
                json!({
                    "capabilities": [
                        {
                            "protocol": "protocol",
                            "path": "/protocol",
                        },
                    ],
                    "offer": [
                        {
                            "protocol": [
                                "protocol",
                            ],
                            "from" : "#self",
                            "to": "#my-resolver"
                        },
                    ],
                    "children": [
                        {
                            "name": "logger",
                            "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                            "environment" : "#myenv",
                        },
                        {
                            "name" : "my-resolver",
                            "url" : "fuchsia-pkg://fuchsia.com/resolver#meta/resolver.cm",

                        },
                    ],
                    "environments" : [
                        {
                            "name": "myenv",
                            "extends": "realm",
                            "resolvers" : [ {
                                "resolver" : "my-resolver",
                                "from" : "#my-resolver",
                                "scheme": "fuchsia-pkg",
                            },
                            ],
                        },
                    ]
                }),
            ),
            (
                "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                json!({
                    "program" : {
                        "runner" : "elf",
                        "binary" : "bin/logger",
                    },
                }),
            ),
            (
                "fuchsia-pkg://fuchsia.com/resolver#meta/resolver.cm",
                json!({
                    "program": {
                        "runner": "elf",
                        "binary": "bin/full_resolver",
                    },
                    "capabilities": [
                        {
                            "resolver": "my-resolver",
                            "path": "/svc/fuchsia.component.resolution.Resolver",
                        },
                        { "protocol": "fuchsia.component.resolution.Resolver" },
                    ],
                    "use": [
                        {
                            "protocol": [
                                "protocol",
                            ],
                        },
                    ],
                    "expose": [
                        {
                            "resolver": "my-resolver",
                            "from": "self",
                        },
                        {
                            "protocol": "fuchsia.component.resolution.Resolver",
                            "from": "self",
                        },
                    ],
                }),
            ),
        ])?;

        let controller = ComponentResolversController::default();

        let response = controller.query(
            model.clone(),
            json!({ "scheme": "fuchsia-pkg", "moniker": "/my-resolver", "protocol": "protocol"}),
        )?;
        assert_eq!(
            response,
            json!({
              "deps": ["core_dep"],
              "monikers": ["/logger"],
            })
        );
        Ok(())
    }

    #[test]
    fn test_component_resolvers_self_source() -> Result<()> {
        let model = cmls_to_model(vec![
            (
                "fuchsia-boot:///#meta/root.cm",
                json!({
                    "capabilities": [
                        {
                            "protocol": "protocol",
                            "path": "/protocol",
                        },
                        {
                            "resolver": "my-resolver",
                            "path": "/svc/fuchsia.component.resolution.Resolver",
                        },
                    ],
                    "use": [
                        {
                            "protocol": [
                                "protocol",
                            ],
                        },
                    ],
                    "children": [
                        {
                            "name": "logger",
                            "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                            "environment": "#myenv"
                        },
                    ],
                    "environments" : [
                        {
                            "name": "myenv",
                            "extends": "realm",
                            "resolvers" : [
                                {
                                    "resolver" : "my-resolver",
                                    "scheme": "fuchsia-pkg",
                                    "from": "self",
                                }
                            ]
                        }
                    ]

                }),
            ),
            (
                "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                json!({
                    "program" : {
                        "runner" : "elf",
                        "binary" : "bin/logger",
                    },
                }),
            ),
        ])?;
        let controller = ComponentResolversController::default();

        let response = controller.query(
            model.clone(),
            json!({ "scheme": "fuchsia-pkg", "moniker": "/", "protocol": "protocol"}),
        )?;
        assert_eq!(
            response,
            json!({
              "deps": ["core_dep"],
              "monikers": ["/logger"],
            })
        );
        Ok(())
    }

    #[test]
    fn test_component_resolvers_parent_source() -> Result<()> {
        let model = cmls_to_model(vec![
            (
                "fuchsia-boot:///#meta/root.cm",
                json!({
                    "capabilities": [
                        {
                            "protocol": "protocol",
                            "path": "/protocol",
                        },
                        {
                            "resolver": "my-resolver",
                            "path": "/svc/fuchsia.component.resolution.Resolver",
                        },
                    ],
                    "offer": [
                        {
                            "resolver": "my-resolver",
                            "from" : "self",
                            "to": "#logger"
                        },
                    ],
                    "use": [
                        {
                            "protocol": [
                                "protocol",
                            ],
                        },
                    ],
                    "children": [
                        {
                            "name": "logger",
                            "url": "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                        },
                    ],

                }),
            ),
            (
                "fuchsia-pkg://fuchsia.com/logger#meta/logger.cm",
                json!({
                    "children" : [
                        {
                            "name": "log-child",
                            "url": "fuchsia-pkg://fuchsia.com/log-child#meta/log-child.cm",
                            "environment" : "#env",
                        },
                    ],
                    "environments" : [
                        {
                            "name": "env",
                            "extends": "none",
                            "resolvers" : [ {
                                "resolver" : "my-resolver",
                                "from" : "parent",
                                "scheme": "fuchsia-pkg",
                            },
                            ],
                        },
                    ],
                }),
            ),
            (
                "fuchsia-pkg://fuchsia.com/log-child#meta/log-child.cm",
                json!({
                    "program" : {
                        "runner" : "elf",
                        "binary" : "bin/logger",
                    },
                }),
            ),
        ])?;

        let controller = ComponentResolversController::default();

        let response = controller.query(
            model.clone(),
            json!({ "scheme": "fuchsia-pkg", "moniker": "/", "protocol": "protocol"}),
        )?;
        assert_eq!(
            response,
            json!({
              "deps": ["core_dep"],
              "monikers": ["/logger/log-child"],
            })
        );
        Ok(())
    }

    #[test]
    fn test_component_resolvers_ignores_invalid_resolver_registration() -> Result<()> {
        let model = cmls_to_model(vec![
            (
                "fuchsia-boot:///#meta/root.cm",
                json!({
                    "children": [
                        {
                            "name": "bootstrap",
                            "url": "fuchsia-boot:///#meta/bootstrap.cm",
                            "startup": "eager",
                        },
                        {
                            "name": "core",
                            "url": "fuchsia-pkg://fuchsia.com/core#meta/core.cm",
                            "environment": "#core-env",
                        },
                    ],
                    "offer": [
                        {
                            "protocol": [
                                "fuchsia.component.resolution.Resolver",
                            ],
                            "from" : "#bootstrap",
                            "to": "#core"
                        },
                    ],
                    "environments" : [
                        {
                            "name": "core-env",
                            "extends": "realm",
                            "resolvers" : [ {
                                "resolver" : "base_resolver",
                                "from" : "#bootstrap",
                                "scheme": "fuchsia-pkg",
                            },
                            ],
                        },
                    ]
                }),
            ),
            (
                "fuchsia-boot:///#meta/bootstrap.cm",
                json!({
                    "children": [
                        {
                            "name": "base_resolver",
                            // A declared but undefined child component, so its capabilities cannot
                            // be analyzed.
                            "url": "fuchsia-boot:///#meta/base-resolver.cm",
                        },
                    ],
                    "expose": [
                        {
                            "protocol": "fuchsia.component.resolution.Resolver",
                            "from": "#base_resolver",
                        },
                        {
                            "resolver": "base_resolver",
                            "from": "#base_resolver",
                        },
                    ],
                }),
            ),
            (
                "fuchsia-pkg://fuchsia.com/core#meta/core.cm",
                json!({
                    "children": [
                        {
                            "name": "resolved-from-realm",
                            "url": "fuchsia-pkg://fuchsia.com/resolve-me#meta/resolve-me.cm",
                        },
                        {
                            "name": "resolved-from-custom",
                            "url": "fuchsia-pkg://fuchsia.com/resolve-me#meta/resolve-me.cm",
                            "environment": "#custom-resolver-env",
                        },
                        {
                            "name": "custom-resolver",
                            "url": "fuchsia-pkg://fuchsia.com/custom-resolver#meta/custom-resolver.cm",
                        },

                    ],
                    "expose": [
                        {
                            "protocol": "fuchsia.component.resolution.Resolver",
                            "from": "#base_resolver",
                        },
                        {
                            "resolver": "base_resolver",
                            "from": "#base_resolver",
                        },
                    ],
                    "capabilities": [
                        {
                            "protocol": "fuchsia.test.SpecialProtocol",
                            "path": "/fake-for-test",
                        },
                    ],
                    "offer": [
                        {
                            "protocol": [
                                "fuchsia.test.SpecialProtocol",
                            ],
                            "from" : "#self",
                            "to": "#custom-resolver"
                        },
                    ],
                    "environments" : [
                        {
                            "name": "custom-resolver-env",
                            "extends": "realm",
                            "resolvers" : [ {
                                "resolver" : "custom-resolver",
                                "from" : "#custom-resolver",
                                "scheme": "fuchsia-pkg",
                            },
                            ],
                        },
                    ]
                }),
            ),
            // Don't provide a definition of the base-resolver component
            // to ensure the walker ignores invalid resolver configurations
            /*
            (
                "fuchsia-boot:///#meta/base-resolver.cm",
                json!({
                    "program" : {
                        "runner" : "elf",
                        "binary" : "bin/foo",
                    },
                }),
            ),
            */
            (
                "fuchsia-pkg://fuchsia.com/resolve-me#meta/resolve-me.cm",
                json!({
                    "program" : {
                        "runner" : "elf",
                        "binary" : "bin/app",
                    },
                }),
            ),
            (
                "fuchsia-pkg://fuchsia.com/custom-resolver#meta/custom-resolver.cm",
                json!({
                    "program": {
                        "runner": "elf",
                        "binary": "bin/custom_resolver",
                    },
                    "capabilities": [
                        {
                            "resolver": "custom-resolver",
                            "path": "/svc/fuchsia.component.resolution.Resolver",
                        },
                        { "protocol": "fuchsia.component.resolution.Resolver" },
                    ],
                    "use": [
                        {
                            "protocol": [
                                "fuchsia.test.SpecialProtocol",
                            ],
                        },
                    ],
                    "expose": [
                        {
                            "resolver": "custom-resolver",
                            "from": "self",
                        },
                        {
                            "protocol": "fuchsia.component.resolution.Resolver",
                            "from": "self",
                        },
                    ],
                }),
            ),
        ])?;

        let controller = ComponentResolversController::default();

        // Even with an invalid component resolver, ensure queries for other component resolvers
        // find the expected instances.
        let response = controller.query(
            model.clone(),
            json!({ "scheme": "fuchsia-pkg", "moniker": "/core/custom-resolver", "protocol": "fuchsia.test.SpecialProtocol"}),
        )?;
        assert_eq!(
            response,
            json!({
              "deps": ["core_dep"],
              "monikers": ["/core/resolved-from-custom"],
            })
        );
        Ok(())
    }

    #[test]
    fn test_map_tree_single_node_default_url() -> Result<()> {
        let model = single_v2_component_model(None, None, component_id_index::Index::default())?;
        V2ComponentModelDataCollector::new().collect(model.clone())?;

        let controller = V2ComponentModelMappingController::default();
        let response = controller.query(model.clone(), json!("{}"))?;
        assert_eq!(
            response,
            json!({"instances":  [{ "instance": "/", "url": DEFAULT_ROOT_URL.to_string() }]})
        );
        Ok(())
    }

    #[test]
    fn test_map_tree_single_node_custom_url() -> Result<()> {
        let root_url = "fuchsia-boot:///#meta/foo.cm".to_string();
        let model = single_v2_component_model(
            Some(root_url.clone()),
            None,
            component_id_index::Index::default(),
        )?;
        V2ComponentModelDataCollector::new().collect(model.clone())?;

        let controller = V2ComponentModelMappingController::default();
        let response = controller.query(model.clone(), json!("{}"))?;
        assert_eq!(response, json!({"instances": [ {"instance": "/", "url": root_url }]}));

        Ok(())
    }

    #[test]
    fn test_map_component_model_multi_instance() -> Result<()> {
        let model = multi_v2_component_model()?;
        V2ComponentModelDataCollector::new().collect(model.clone())?;

        let controller = V2ComponentModelMappingController::default();
        let response = controller.query(model.clone(), json!("{}"))?;
        assert!(
            (response
                == json!({"instances": [{"instance": "/", "url": DEFAULT_ROOT_URL.to_string()},
                                 {"instance": "/foo","url": "fuchsia-boot:///#meta/foo.cm"},
                                 {"instance": "/bar", "url": "fuchsia-boot:///#meta/bar.cm"},
                                 {"instance": "/foo/baz", "url": "fuchsia-boot:///#meta/baz.cm"}]}))
                | (response
                    == json!({"instances": [{"instance": "/", "url": DEFAULT_ROOT_URL.to_string()},
                                 {"instance": "/bar","url": "fuchsia-boot:///#meta/bar.cm"},
                                 {"instance": "/foo", "url": "fuchsia-boot:///#meta/foo.cm"},
                                 {"instance": "/foo/baz", "url": "fuchsia-boot:///#meta/baz.cm"}]}))
        );

        Ok(())
    }

    #[test]
    fn test_capability_routing_all_results() -> Result<()> {
        let model = two_instance_component_model()?;

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
                                        "analyzer_model_error": {
                                            "routing_error": {
                                                "use_from_parent_not_found": {
                                                    "capability_id": "bad_dir",
                                                    "moniker": "/child",
                                                }
                                            }
                                        }
                                    },
                                    "message": "`/child` tried to use `bad_dir` from its parent, but the parent does not offer that capability. Note, use clauses in CML default to using from parent."
                                },
                                "using_node": "/child",
                            },
                        ],
                        "ok": [
                            {
                                "capability": "good_dir",
                                "using_node": "/child",
                            },
                        ],
                    }
                },
                {
                    "capability_type": "protocol",
                    "results": {
                        "warnings": [
                            {
                                "capability": "protocol",
                                "warning": {
                                    "error": {
                                        "analyzer_model_error": {
                                            "routing_error": {
                                                "offer_from_child_instance_not_found": {
                                                    "capability_id": "protocol",
                                                    "child_moniker": "missing_child",
                                                    "moniker": "/",
                                                }
                                            }
                                        }
                                    },
                                    "message": "`/` tried to offer `protocol` from `#missing_child`, but no such child was found."
                                },
                                "using_node": "/child",
                            },
                        ],
                    }
                }
            ]
        });

        assert_eq!(response, expected);

        Ok(())
    }

    #[test]
    fn test_capability_routing_verbose_results() -> Result<()> {
        let model = two_instance_component_model()?;

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
                                  "analyzer_model_error": {
                                      "routing_error": {
                                          "use_from_parent_not_found": {
                                              "capability_id": "bad_dir",
                                              "moniker": "/child",
                                          }
                                      }
                                  }
                              },
                              "message": "`/child` tried to use `bad_dir` from its parent, but the parent does not offer that capability. Note, use clauses in CML default to using from parent."
                          },
                          "using_node": "/child",
                      },
                  ],
                  "ok": [
                      {
                          "capability": "good_dir",
                          "route": [
                              {
                                  "action": "use_by",
                                  "capability": {
                                      "availability": "required",
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
                                      "availability": "required",
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
                        "warning": {
                            "error": {
                                "analyzer_model_error": {
                                    "routing_error": {
                                        "offer_from_child_instance_not_found": {
                                            "capability_id": "protocol",
                                            "child_moniker": "missing_child",
                                            "moniker": "/",
                                        }
                                    }
                                }
                            },
                            "message": "`/` tried to offer `protocol` from `#missing_child`, but no such child was found."
                        },
                        "using_node": "/child",
                    },
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
        let model = two_instance_component_model()?;

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
                                  "analyzer_model_error": {
                                      "routing_error": {
                                          "use_from_parent_not_found": {
                                              "capability_id": "bad_dir",
                                              "moniker": "/child",
                                          }
                                      }
                                  }
                              },
                              "message": "`/child` tried to use `bad_dir` from its parent, but the parent does not offer that capability. Note, use clauses in CML default to using from parent."
                          },
                          "using_node": "/child",
                      },
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
                                      "analyzer_model_error": {
                                          "routing_error": {
                                              "offer_from_child_instance_not_found": {
                                                  "capability_id": "protocol",
                                                  "child_moniker": "missing_child",
                                                  "moniker": "/",
                                              }
                                          }
                                      }
                                  },
                                  "message": "`/` tried to offer `protocol` from `#missing_child`, but no such child was found."
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
        let model = two_instance_component_model()?;

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
                                  "analyzer_model_error": {
                                      "routing_error": {
                                          "use_from_parent_not_found": {
                                              "capability_id": "bad_dir",
                                              "moniker": "/child",
                                          }
                                      }
                                  }
                              },
                              "message": "`/child` tried to use `bad_dir` from its parent, but the parent does not offer that capability. Note, use clauses in CML default to using from parent."
                          },
                          "using_node": "/child",
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
