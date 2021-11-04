// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod component_instance;
pub mod component_model;
pub mod environment;
pub mod node_path;
pub mod route;
pub mod serde_ext;

use {
    crate::{
        component_instance::ComponentInstanceForAnalyzer,
        component_model::ComponentModelForAnalyzer,
    },
    routing::component_instance::ComponentInstanceInterface,
    std::{collections::VecDeque, sync::Arc},
};

/// The `ComponentInstanceVisitor` trait defines a common entry point for analyzers
/// that operate on a single component instance.
///
/// Errors may offer suggestions on improvements or better idioms, as well as detecting
/// invalid manifests. This is distinct from the `cm_validator` library which is
/// concerned with direct validation of the manifest.
pub trait ComponentInstanceVisitor {
    fn visit_instance(
        &mut self,
        instance: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Result<(), anyhow::Error>;
}

/// The `ComponentModelWalker` trait defines an interface for iteratively operating on
/// component instances in a `ComponentModelForAnalyzer`, given a type implementing a
/// per-instance operation via the `ComponentInstanceVisitor` trait.
pub trait ComponentModelWalker {
    // Walks the component graph, doing the operation implemented by `visitor` at
    // each instance.
    fn walk<V: ComponentInstanceVisitor>(
        &mut self,
        model: &Arc<ComponentModelForAnalyzer>,
        visitor: &mut V,
    ) -> Result<(), anyhow::Error> {
        self.initialize(model)?;
        let mut instance = model.get_root_instance()?;
        loop {
            visitor.visit_instance(&instance)?;
            match self.get_next_instance()? {
                Some(next) => instance = next,
                None => {
                    return Ok(());
                }
            }
        }
    }

    // Sets up any initial state before beginning the walk.
    fn initialize(&mut self, model: &Arc<ComponentModelForAnalyzer>) -> Result<(), anyhow::Error>;

    // Gets the next component instance to visit.
    fn get_next_instance(
        &mut self,
    ) -> Result<Option<Arc<ComponentInstanceForAnalyzer>>, anyhow::Error>;
}

/// A walker implementing breadth-first traversal of a full `ComponentModelForAnalyzer`, starting at
/// the root instance.
#[derive(Default)]
pub struct BreadthFirstModelWalker {
    discovered: VecDeque<Arc<ComponentInstanceForAnalyzer>>,
}

impl BreadthFirstModelWalker {
    pub fn new() -> Self {
        Self { discovered: VecDeque::new() }
    }

    fn discover_children(&mut self, instance: &Arc<ComponentInstanceForAnalyzer>) {
        let children = instance.get_children();
        self.discovered.reserve(children.len());
        for child in children.into_iter() {
            self.discovered.push_back(child);
        }
    }
}

impl ComponentModelWalker for BreadthFirstModelWalker {
    fn initialize(&mut self, model: &Arc<ComponentModelForAnalyzer>) -> Result<(), anyhow::Error> {
        self.discover_children(&model.get_root_instance()?);
        Ok(())
    }

    fn get_next_instance(
        &mut self,
    ) -> Result<Option<Arc<ComponentInstanceForAnalyzer>>, anyhow::Error> {
        match self.discovered.pop_front() {
            Some(next) => {
                self.discover_children(&next);
                Ok(Some(next))
            }
            None => Ok(None),
        }
    }
}

/// A ComponentInstanceVisitor which just records an identifier and the url of each component instance visited.
#[derive(Default)]
pub struct ModelMappingVisitor {
    // A vector of (instance id, url) pairs.
    visited: Vec<(String, String)>,
}

impl ModelMappingVisitor {
    pub fn new() -> Self {
        Self { visited: Vec::new() }
    }

    pub fn map(&self) -> &Vec<(String, String)> {
        &self.visited
    }
}

impl ComponentInstanceVisitor for ModelMappingVisitor {
    fn visit_instance(
        &mut self,
        instance: &Arc<ComponentInstanceForAnalyzer>,
    ) -> Result<(), anyhow::Error> {
        self.visited.push((instance.node_path().to_string(), instance.url().to_string()));
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::component_model::ModelBuilderForAnalyzer,
        cm_rust::ComponentDecl,
        cm_rust_testing::ComponentDeclBuilder,
        routing::{
            component_id_index::ComponentIdIndex, config::RuntimeConfig,
            environment::RunnerRegistry,
        },
        std::{collections::HashMap, iter::FromIterator},
    };

    const TEST_URL_PREFIX: &str = "test:///";

    fn make_test_url(component_name: &str) -> String {
        format!("{}{}", TEST_URL_PREFIX, component_name)
    }

    fn make_decl_map(
        components: Vec<(&'static str, ComponentDecl)>,
    ) -> HashMap<String, ComponentDecl> {
        HashMap::from_iter(components.into_iter().map(|(name, decl)| (make_test_url(name), decl)))
    }

    #[test]
    fn breadth_first_walker() -> Result<(), anyhow::Error> {
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").add_lazy_child("c").build()),
            ("b", ComponentDeclBuilder::new().build()),
            ("c", ComponentDeclBuilder::new().add_lazy_child("d").build()),
            ("d", ComponentDeclBuilder::new().build()),
        ];
        let a_url = make_test_url("a");
        let b_url = make_test_url("b");
        let c_url = make_test_url("c");
        let d_url = make_test_url("d");

        let config = Arc::new(RuntimeConfig::default());
        let build_model_result = ModelBuilderForAnalyzer::new(
            cm_types::Url::new(a_url.clone()).expect("failed to parse root component url"),
        )
        .build(
            make_decl_map(components),
            config,
            Arc::new(ComponentIdIndex::default()),
            RunnerRegistry::default(),
        );
        assert_eq!(build_model_result.errors.len(), 0);
        assert!(build_model_result.model.is_some());
        let model = build_model_result.model.unwrap();
        assert_eq!(model.len(), 4);

        let mut visitor = ModelMappingVisitor::new();
        BreadthFirstModelWalker::new().walk(&model, &mut visitor)?;
        let map = visitor.map();

        // The visitor should visit both "b" and "c" before "d", but may visit "b" and "c" in either order.
        assert!(
            (map == &vec![
                ("/".to_string(), a_url.clone()),
                ("/b".to_string(), b_url.clone()),
                ("/c".to_string(), c_url.clone()),
                ("/c/d".to_string(), d_url.clone())
            ]) || (map
                == &vec![
                    ("/".to_string(), a_url.clone()),
                    ("/c".to_string(), c_url.clone()),
                    ("/b".to_string(), b_url.clone()),
                    ("/c/d".to_string(), d_url.clone())
                ])
        );

        Ok(())
    }
}
