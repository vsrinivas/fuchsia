// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::{Components, ManifestData, Manifests},
    crate::verify::collection::V2ComponentTree,
    anyhow::{anyhow, Result},
    cm_fidl_analyzer::component_tree::ComponentTreeBuilder,
    cm_rust::{ComponentDecl, FidlIntoNative},
    fidl::encoding::decode_persistent,
    fidl_fuchsia_sys2 as fsys2,
    log::{info, warn},
    scrutiny::model::{collector::DataCollector, model::DataModel},
    std::{collections::HashMap, sync::Arc},
};

pub struct V2ComponentTreeDataCollector {}

impl V2ComponentTreeDataCollector {
    pub fn new() -> Self {
        Self {}
    }

    fn get_decls(&self, model: &Arc<DataModel>) -> Result<HashMap<String, ComponentDecl>> {
        let mut decls = HashMap::<String, ComponentDecl>::new();
        let mut urls = HashMap::<i32, String>::new();

        let components = model.get::<Components>()?;
        for component in components.entries.iter().filter(|x| x.version == 2) {
            urls.insert(component.id, component.url.clone());
        }

        for manifest in model.get::<Manifests>()?.entries.iter() {
            if let ManifestData::Version2(decl_base64) = &manifest.manifest {
                match urls.remove(&manifest.component_id) {
                    Some(url) => {
                        let decl: fsys2::ComponentDecl =
                            decode_persistent(&base64::decode(&decl_base64)?)?;
                        decls.insert(url, decl.fidl_into_native());
                    }
                    None => {
                        return Err(anyhow!(
                            "No component URL found for v2 component with id {}",
                            manifest.component_id
                        ));
                    }
                }
            }
        }
        Ok(decls)
    }
}

impl DataCollector for V2ComponentTreeDataCollector {
    fn collect(&self, model: Arc<DataModel>) -> Result<()> {
        let decls_by_url = self.get_decls(&model)?;

        info!(
            "V2ComponentTreeDataCollector: Found {} v2 component declarations",
            decls_by_url.len()
        );

        let build_result = ComponentTreeBuilder::new(decls_by_url)
            .build("fuchsia-boot:///#meta/root.cm".to_string());

        for error in build_result.errors.iter() {
            warn!("V2ComponentTreeDataCollector: {}", error);
        }

        match build_result.tree {
            Some(tree) => {
                info!(
                    "V2ComponentTreeDataCollector: Built v2 component tree with {} nodes",
                    tree.len()
                );
                model.set(V2ComponentTree::new(tree, build_result.errors))?;

                Ok(())
            }
            None => Err(anyhow!("Failed to build v2 component tree")),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        crate::core::collection::{Component, Components, Manifest, ManifestData, Manifests},
        anyhow::Result,
        cm_rust::{ChildDecl, NativeIntoFidl},
        fidl::encoding::encode_persistent,
        fidl_fuchsia_sys2::StartupMode,
        tempfile::tempdir,
    };

    fn data_model() -> Arc<DataModel> {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        Arc::new(DataModel::connect(uri).unwrap())
    }

    fn new_child_decl(name: String, url: String) -> ChildDecl {
        ChildDecl { name, url, startup: StartupMode::Lazy, environment: None }
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

    fn make_v1_component(id: i32) -> Component {
        Component { id, url: "".to_string(), version: 1, inferred: false }
    }

    fn make_v1_manifest(component_id: i32) -> Manifest {
        Manifest { component_id, manifest: ManifestData::Version1("".to_string()), uses: vec![] }
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

    fn v1_and_v2_component_model() -> Result<Arc<DataModel>> {
        let model = data_model();
        let root_id = 0;
        let root_component =
            make_v2_component(root_id, "fuchsia-boot:///#meta/root.cm".to_string());
        let root_manifest = make_v2_manifest(root_id, new_component_decl(vec![]))?;

        let v1_id = 1;
        let v1_component = make_v1_component(v1_id);
        let v1_manifest = make_v1_manifest(v1_id);

        model.set(Components::new(vec![root_component, v1_component]))?;
        model.set(Manifests::new(vec![root_manifest, v1_manifest]))?;
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
    fn single_v2_component() -> Result<()> {
        let model = single_v2_component_model()?;
        V2ComponentTreeDataCollector::new().collect(model.clone())?;
        let tree = &model.get::<V2ComponentTree>()?.tree;
        assert_eq!(tree.len(), 1);
        Ok(())
    }

    #[test]
    fn v1_and_v2_component() -> Result<()> {
        let model = v1_and_v2_component_model()?;
        V2ComponentTreeDataCollector::new().collect(model.clone())?;
        let tree = &model.get::<V2ComponentTree>()?.tree;
        assert_eq!(tree.len(), 1);
        Ok(())
    }

    #[test]
    fn multi_v2_component() -> Result<()> {
        let model = multi_v2_component_model()?;
        V2ComponentTreeDataCollector::new().collect(model.clone())?;
        let tree = &model.get::<V2ComponentTree>()?.tree;
        assert_eq!(tree.len(), 4);
        Ok(())
    }
}
