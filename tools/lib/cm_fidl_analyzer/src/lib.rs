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
    fuchsia_merkle::Hash,
    fuchsia_url::{
        AbsoluteComponentUrl, AbsolutePackageUrl, PackageName, PackageVariant, RepositoryUrl,
    },
    routing::component_instance::ComponentInstanceInterface,
    std::{collections::VecDeque, sync::Arc},
};

/// Output for package URL matching functions.
#[derive(Debug, Eq, PartialEq)]
pub enum PkgUrlMatch {
    /// URLs match in all particulars they specify. For example, if one URL A specifies a package
    /// hash and is strongly matched against URL B, then URL B must also specify the same package
    /// hash.
    StrongMatch,
    /// URLs match in all aspects that both URLs specify. For example, if URL B specifies a variant
    /// and URL B does not, these URLs may weakly match if other all other aspects that they both
    /// specify match.
    WeakMatch,
    /// URLs are mismatched in at least one aspect, such as scheme, host, package name, etc..
    NoMatch,
}

impl From<(PkgUrlMatch, PkgUrlMatch)> for PkgUrlMatch {
    fn from(matches: (PkgUrlMatch, PkgUrlMatch)) -> Self {
        match matches {
            (PkgUrlMatch::NoMatch, _) | (_, PkgUrlMatch::NoMatch) => PkgUrlMatch::NoMatch,
            (PkgUrlMatch::WeakMatch, _) | (_, PkgUrlMatch::WeakMatch) => PkgUrlMatch::WeakMatch,
            (_, _) => PkgUrlMatch::StrongMatch,
        }
    }
}

pub fn match_absolute_component_urls(
    a: &AbsoluteComponentUrl,
    b: &AbsoluteComponentUrl,
) -> PkgUrlMatch {
    (
        match_absolute_pkg_urls(a.package_url(), b.package_url()),
        match_component_url_resource(a.resource(), b.resource()),
    )
        .into()
}

fn match_component_url_resource(a: &str, b: &str) -> PkgUrlMatch {
    match a == b {
        true => PkgUrlMatch::StrongMatch,
        false => PkgUrlMatch::NoMatch,
    }
}

pub fn match_absolute_pkg_urls(a: &AbsolutePackageUrl, b: &AbsolutePackageUrl) -> PkgUrlMatch {
    (
        (
            (
                match_pkg_url_repository(a.repository(), b.repository()),
                match_pkg_url_name(a.name(), b.name()),
            )
                .into(),
            match_pkg_url_variant(a.variant(), b.variant()),
        )
            .into(),
        match_pkg_url_hash(a.hash(), b.hash()),
    )
        .into()
}

fn match_pkg_url_repository(a: &RepositoryUrl, b: &RepositoryUrl) -> PkgUrlMatch {
    match a == b {
        true => PkgUrlMatch::StrongMatch,
        false => PkgUrlMatch::NoMatch,
    }
}

fn match_pkg_url_name(a: &PackageName, b: &PackageName) -> PkgUrlMatch {
    match a == b {
        true => PkgUrlMatch::StrongMatch,
        false => PkgUrlMatch::NoMatch,
    }
}

fn match_pkg_url_variant(a: Option<&PackageVariant>, b: Option<&PackageVariant>) -> PkgUrlMatch {
    match (a, b) {
        (None, None) => PkgUrlMatch::StrongMatch,
        (Some(av), Some(bv)) => match av == bv {
            true => PkgUrlMatch::StrongMatch,
            false => PkgUrlMatch::NoMatch,
        },
        (Some(_), None) | (None, Some(_)) => PkgUrlMatch::WeakMatch,
    }
}

fn match_pkg_url_hash(a: Option<Hash>, b: Option<Hash>) -> PkgUrlMatch {
    match (a, b) {
        (None, None) => PkgUrlMatch::StrongMatch,
        (Some(av), Some(bv)) => match av == bv {
            true => PkgUrlMatch::StrongMatch,
            false => PkgUrlMatch::NoMatch,
        },
        (Some(_), None) | (None, Some(_)) => PkgUrlMatch::WeakMatch,
    }
}

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
        super::{
            match_absolute_component_urls, BreadthFirstModelWalker, ComponentModelWalker,
            ModelMappingVisitor, PkgUrlMatch,
        },
        crate::component_model::ModelBuilderForAnalyzer,
        cm_rust::ComponentDecl,
        cm_rust_testing::ComponentDeclBuilder,
        fuchsia_url::AbsoluteComponentUrl,
        routing::{
            component_id_index::ComponentIdIndex, config::RuntimeConfig,
            environment::RunnerRegistry,
        },
        std::{collections::HashMap, iter::FromIterator, sync::Arc},
        url::Url,
    };

    const TEST_URL_PREFIX: &str = "test:///";

    fn make_test_url(component_name: &str) -> Url {
        Url::parse(&format!("{}{}", TEST_URL_PREFIX, component_name)).unwrap()
    }

    fn make_decl_map(
        components: Vec<(&'static str, ComponentDecl)>,
    ) -> HashMap<Url, (ComponentDecl, Option<config_encoder::ConfigFields>)> {
        HashMap::from_iter(
            components.into_iter().map(|(name, decl)| (make_test_url(name), (decl, None))),
        )
    }

    #[fuchsia::test]
    fn match_absolute_component_urls_strong() {
        let url_strs = vec![
            "fuchsia-pkg://test.fuchsia.com/alpha#meta/alpha.cm",
            "fuchsia-pkg://test.fuchsia.com/alpha/0#meta/alpha.cm",
            "fuchsia-pkg://test.fuchsia.com/alpha?hash=0000000000000000000000000000000000000000000000000000000000000000#meta/alpha.cm",
            "fuchsia-pkg://test.fuchsia.com/alpha/0?hash=0000000000000000000000000000000000000000000000000000000000000000#meta/alpha.cm",
        ];

        for url_str in url_strs.into_iter() {
            assert_eq!(
                PkgUrlMatch::StrongMatch,
                match_absolute_component_urls(
                    &AbsoluteComponentUrl::parse(url_str).unwrap(),
                    &AbsoluteComponentUrl::parse(url_str).unwrap(),
                )
            );
        }
    }

    #[fuchsia::test]
    fn match_absolute_component_urls_weak() {
        let url_strs = vec![
            "fuchsia-pkg://test.fuchsia.com/alpha#meta/alpha.cm",
            "fuchsia-pkg://test.fuchsia.com/alpha/0#meta/alpha.cm",
            "fuchsia-pkg://test.fuchsia.com/alpha?hash=0000000000000000000000000000000000000000000000000000000000000000#meta/alpha.cm",
            "fuchsia-pkg://test.fuchsia.com/alpha/0?hash=0000000000000000000000000000000000000000000000000000000000000000#meta/alpha.cm",
        ];

        for i in 0..url_strs.len() {
            for j in 0..url_strs.len() {
                if i == j {
                    continue;
                }
                assert_eq!(
                    PkgUrlMatch::WeakMatch,
                    match_absolute_component_urls(
                        &AbsoluteComponentUrl::parse(url_strs[i]).unwrap(),
                        &AbsoluteComponentUrl::parse(url_strs[j]).unwrap(),
                    )
                );
            }
        }
    }

    #[fuchsia::test]
    fn match_absolute_component_urls_no_match() {
        assert_eq!(
            PkgUrlMatch::NoMatch,
            match_absolute_component_urls(
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.com/alpha#meta/alpha.cm")
                    .unwrap(),
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.org/alpha#meta/alpha.cm")
                    .unwrap(),
            )
        );
        assert_eq!(
            PkgUrlMatch::NoMatch,
            match_absolute_component_urls(
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.com/alpha#meta/alpha.cm")
                    .unwrap(),
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.com/beta#meta/alpha.cm")
                    .unwrap(),
            )
        );
        assert_eq!(
            PkgUrlMatch::NoMatch,
            match_absolute_component_urls(
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.com/alpha#meta/alpha.cm")
                    .unwrap(),
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.com/alpha#meta/beta.cm")
                    .unwrap(),
            )
        );
        assert_eq!(
            PkgUrlMatch::NoMatch,
            match_absolute_component_urls(
                &AbsoluteComponentUrl::parse(
                    "fuchsia-pkg://test.fuchsia.com/alpha/0#meta/alpha.cm"
                )
                .unwrap(),
                &AbsoluteComponentUrl::parse(
                    "fuchsia-pkg://test.fuchsia.com/alpha/1#meta/alpha.cm"
                )
                .unwrap(),
            )
        );
        assert_eq!(
            PkgUrlMatch::NoMatch,
            match_absolute_component_urls(
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.com/alpha/0?hash=0000000000000000000000000000000000000000000000000000000000000000#meta/alpha.cm").unwrap(),
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.com/alpha/1?hash=0000000000000000000000000000000000000000000000000000000000000000#meta/alpha.cm").unwrap(),
            )
        );
        assert_eq!(
            PkgUrlMatch::NoMatch,
            match_absolute_component_urls(
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.com/alpha?hash=0000000000000000000000000000000000000000000000000000000000000000#meta/alpha.cm").unwrap(),
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.com/alpha?hash=1111111111111111111111111111111111111111111111111111111111111111#meta/alpha.cm").unwrap(),
            )
        );
        assert_eq!(
            PkgUrlMatch::NoMatch,
            match_absolute_component_urls(
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.com/alpha/0?hash=0000000000000000000000000000000000000000000000000000000000000000#meta/alpha.cm").unwrap(),
                &AbsoluteComponentUrl::parse("fuchsia-pkg://test.fuchsia.com/alpha/0?hash=1111111111111111111111111111111111111111111111111111111111111111#meta/alpha.cm").unwrap(),
            )
        );
    }

    #[fuchsia::test]
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
        let build_model_result = ModelBuilderForAnalyzer::new(a_url.clone()).build(
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
                ("/".to_string(), a_url.to_string()),
                ("/b".to_string(), b_url.to_string()),
                ("/c".to_string(), c_url.to_string()),
                ("/c/d".to_string(), d_url.to_string())
            ]) || (map
                == &vec![
                    ("/".to_string(), a_url.to_string()),
                    ("/c".to_string(), c_url.to_string()),
                    ("/b".to_string(), b_url.to_string()),
                    ("/c/d".to_string(), d_url.to_string())
                ])
        );

        Ok(())
    }
}
