// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod collection;
mod collector;
mod controller;

use {
    crate::verify::{
        collector::component_tree::V2ComponentTreeDataCollector,
        controller::build::VerifyBuildController,
        controller::capability_routing::TreeMappingController,
    },
    scrutiny::prelude::*,
    std::sync::Arc,
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
        }
    ),
    vec![PluginDescriptor::new("CorePlugin")]
);

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::core::collection::{Component, Components, Manifest, ManifestData, Manifests},
        anyhow::Result,
        cm_rust::{ChildDecl, ComponentDecl, NativeIntoFidl},
        fidl::encoding::encode_persistent,
        fidl_fuchsia_sys2 as fsys2,
        serde_json::json,
        tempfile::tempdir,
    };

    fn data_model() -> Arc<DataModel> {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        Arc::new(DataModel::connect(uri).unwrap())
    }

    fn new_child_decl(name: String, url: String) -> ChildDecl {
        ChildDecl { name, url, startup: fsys2::StartupMode::Lazy, environment: None }
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

    fn make_v2_component(id: i32, url: String) -> Component {
        Component { id, url, version: 2, inferred: false }
    }

    fn make_v2_manifest(component_id: i32, decl: ComponentDecl) -> Result<Manifest> {
        let mut decl_fidl: fsys2::ComponentDecl = decl.native_into_fidl();
        let decl_base64 = base64::encode(&encode_persistent(&mut decl_fidl)?);
        Ok(Manifest { component_id, manifest: ManifestData::Version2(decl_base64), uses: vec![] })
    }

    fn single_v2_component_model() -> Result<Arc<DataModel>> {
        let model = data_model();
        let root_id = 0;
        let root_component =
            make_v2_component(root_id, "fuchsia-boot:///#meta/root.cm".to_string());
        let root_manifest = make_v2_manifest(root_id, new_component_decl(vec![]))?;
        model.set(Components::new(vec![root_component]))?;
        model.set(Manifests::new(vec![root_manifest]))?;
        Ok(model)
    }

    fn multi_v2_component_model() -> Result<Arc<DataModel>> {
        let model = data_model();
        let root_id = 0;
        let foo_id = 1;
        let bar_id = 2;
        let baz_id = 3;

        let root_url = "fuchsia-boot:///#meta/root.cm".to_string();
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

        model.set(Components::new(vec![
            root_component,
            foo_component,
            bar_component,
            baz_component,
        ]))?;

        model.set(Manifests::new(vec![root_manifest, foo_manifest, bar_manifest, baz_manifest]))?;
        Ok(model)
    }

    #[test]
    fn test_map_tree_single_node() -> Result<()> {
        let model = single_v2_component_model()?;
        V2ComponentTreeDataCollector::new().collect(model.clone())?;

        let controller = TreeMappingController::default();
        let response = controller.query(model.clone(), json!("{}"))?;
        assert_eq!(
            response,
            json!({"route": [{"node": "/", "url": "fuchsia-boot:///#meta/root.cm"}]})
        );

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
                == json!({"route": [{"node": "/", "url": "fuchsia-boot:///#meta/root.cm"},
                                 {"node": "/foo","url": "fuchsia-boot:///#meta/foo.cm"},
                                 {"node": "/bar", "url": "fuchsia-boot:///#meta/bar.cm"},
                                 {"node": "/foo/baz", "url": "fuchsia-boot:///#meta/baz.cm"}]}))
                | (response
                    == json!({"route": [{"node": "/", "url": "fuchsia-boot:///#meta/root.cm"},
                                 {"node": "/bar","url": "fuchsia-boot:///#meta/bar.cm"},
                                 {"node": "/foo", "url": "fuchsia-boot:///#meta/foo.cm"},
                                 {"node": "/foo/baz", "url": "fuchsia-boot:///#meta/baz.cm"}]}))
        );

        Ok(())
    }
}
