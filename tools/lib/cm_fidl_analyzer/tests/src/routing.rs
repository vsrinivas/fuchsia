// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    async_trait::async_trait,
    cm_fidl_analyzer::{
        component_model::{
            ComponentInstanceForAnalyzer, ComponentModelForAnalyzer, ModelBuilderForAnalyzer,
        },
        component_tree::{ComponentTreeBuilder, NodePath},
    },
    cm_rust::{
        CapabilityDecl, CapabilityPath, ComponentDecl, DependencyType, ExposeDecl,
        ExposeDeclCommon, ExposeServiceDecl, ExposeSource, ExposeTarget, OfferDecl,
        OfferServiceDecl, OfferSource, OfferTarget, ServiceDecl, UseDecl, UseServiceDecl,
        UseSource,
    },
    cm_rust_testing::ComponentDeclBuilder,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_sys2 as fsys, fuchsia_zircon_status as zx_status,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ChildMonikerBase},
    routing::{
        component_instance::ComponentInstanceInterface,
        config::{AllowlistEntry, CapabilityAllowlistKey, RuntimeConfig, SecurityPolicy},
    },
    routing_test_helpers::{CheckUse, ExpectedResult, RoutingTestModel, RoutingTestModelBuilder},
    std::{
        collections::{HashMap, HashSet},
        convert::{TryFrom, TryInto},
        iter::FromIterator,
        path::Path,
        sync::Arc,
    },
    thiserror::Error,
};

const TEST_URL_PREFIX: &str = "test:///";

pub struct RoutingTestForAnalyzer {
    model: Arc<ComponentModelForAnalyzer>,
}

pub struct RoutingTestBuilderForAnalyzer {
    root_url: String,
    decls_by_url: HashMap<String, ComponentDecl>,
    namespace_capabilities: Vec<CapabilityDecl>,
    builtin_capabilities: Vec<CapabilityDecl>,
    capability_policy: HashMap<CapabilityAllowlistKey, HashSet<AllowlistEntry>>,
    debug_capability_policy: HashMap<CapabilityAllowlistKey, HashSet<(AbsoluteMoniker, String)>>,
    component_id_index_path: Option<String>,
}

#[async_trait]
impl RoutingTestModelBuilder for RoutingTestBuilderForAnalyzer {
    type Model = RoutingTestForAnalyzer;

    fn new(root_component: &str, components: Vec<(&'static str, ComponentDecl)>) -> Self {
        let root_url = format!("{}{}", TEST_URL_PREFIX, root_component);
        let decls_by_url = HashMap::from_iter(
            components
                .into_iter()
                .map(|(name, decl)| (format!("{}{}", TEST_URL_PREFIX, name), decl)),
        );
        Self {
            root_url,
            decls_by_url,
            namespace_capabilities: Vec::new(),
            builtin_capabilities: Vec::new(),
            capability_policy: HashMap::new(),
            debug_capability_policy: HashMap::new(),
            component_id_index_path: None,
        }
    }

    fn set_namespace_capabilities(&mut self, caps: Vec<CapabilityDecl>) {
        self.namespace_capabilities = caps;
    }

    fn set_builtin_capabilities(&mut self, caps: Vec<CapabilityDecl>) {
        self.builtin_capabilities = caps;
    }

    /// Add a custom capability security policy to restrict routing of certain caps.
    fn add_capability_policy(
        &mut self,
        key: CapabilityAllowlistKey,
        allowlist: HashSet<AllowlistEntry>,
    ) {
        self.capability_policy.insert(key, allowlist);
    }

    /// Add a custom debug capability security policy to restrict routing of certain caps.
    fn add_debug_capability_policy(
        &mut self,
        key: CapabilityAllowlistKey,
        allowlist: HashSet<(AbsoluteMoniker, String)>,
    ) {
        self.debug_capability_policy.insert(key, allowlist);
    }

    fn set_component_id_index_path(&mut self, index_path: String) {
        self.component_id_index_path = Some(index_path);
    }

    async fn build(self) -> RoutingTestForAnalyzer {
        let mut config = RuntimeConfig::default();
        config.namespace_capabilities = self.namespace_capabilities;
        config.builtin_capabilities = self.builtin_capabilities;

        let mut security_policy = SecurityPolicy::default();
        security_policy.capability_policy = self.capability_policy;
        security_policy.debug_capability_policy = self.debug_capability_policy;
        config.security_policy = security_policy;

        config.component_id_index_path = self.component_id_index_path;

        let tree = ComponentTreeBuilder::new(self.decls_by_url)
            .build(self.root_url)
            .tree
            .expect("failed to build ComponentTree");

        let model = ModelBuilderForAnalyzer::new()
            .build(tree, Arc::new(config))
            .await
            .expect("failed to build ComponentModelForAnalyzer");
        RoutingTestForAnalyzer { model }
    }
}

#[derive(Debug, Error)]
pub enum TestModelError {
    #[error("matching use decl not found")]
    UseDeclNotFound,
    #[error("matching expose decl not found")]
    ExposeDeclNotFound,
    #[error("found use decl for Event capability, but mode does not match request")]
    EventModeMismatch,
}

impl TestModelError {
    pub fn as_zx_status(&self) -> zx_status::Status {
        match self {
            Self::UseDeclNotFound | Self::ExposeDeclNotFound => zx_status::Status::NOT_FOUND,
            Self::EventModeMismatch => zx_status::Status::UNAVAILABLE,
        }
    }
}

impl RoutingTestForAnalyzer {
    fn find_matching_use(
        &self,
        check: CheckUse,
        decl: &ComponentDecl,
    ) -> (Result<UseDecl, TestModelError>, ExpectedResult) {
        match check {
            CheckUse::Directory { path, expected_res, .. } => (
                decl.uses
                    .iter()
                    .find_map(|u| match u {
                        UseDecl::Directory(d) if d.target_path == path => Some(u.clone()),
                        _ => None,
                    })
                    .ok_or(TestModelError::UseDeclNotFound),
                expected_res,
            ),
            CheckUse::Event { request, expected_res, .. } => {
                let find_decl = decl.uses.iter().find_map(|u| match u {
                    UseDecl::Event(d) if (d.target_name == request.event_name) => Some(d.clone()),
                    _ => None,
                });
                let decl_result = match find_decl {
                    Some(d) if d.mode == request.mode => Ok(UseDecl::Event(d)),
                    Some(_) => Err(TestModelError::EventModeMismatch),
                    None => Err(TestModelError::UseDeclNotFound),
                };
                (decl_result, expected_res)
            }
            CheckUse::Protocol { path, expected_res, .. } => (
                decl.uses
                    .iter()
                    .find_map(|u| match u {
                        UseDecl::Protocol(d) if d.target_path == path => Some(u.clone()),
                        _ => None,
                    })
                    .ok_or(TestModelError::UseDeclNotFound),
                expected_res,
            ),
            CheckUse::Service { path, expected_res, .. } => (
                decl.uses
                    .iter()
                    .find_map(|u| match u {
                        UseDecl::Service(d) if d.target_path == path => Some(u.clone()),
                        _ => None,
                    })
                    .ok_or(TestModelError::UseDeclNotFound),
                expected_res,
            ),
            CheckUse::Storage { path, expected_res, .. } => (
                decl.uses
                    .iter()
                    .find_map(|u| match u {
                        UseDecl::Storage(d) if d.target_path == path => Some(u.clone()),
                        _ => None,
                    })
                    .ok_or(TestModelError::UseDeclNotFound),
                expected_res,
            ),
            CheckUse::StorageAdmin { expected_res, .. } => (
                decl.uses
                    .iter()
                    .find_map(|u| match u {
                        UseDecl::Protocol(d)
                            if d.source_name.to_string() == fsys::StorageAdminMarker::NAME =>
                        {
                            Some(u.clone())
                        }
                        _ => None,
                    })
                    .ok_or(TestModelError::UseDeclNotFound),
                expected_res,
            ),
        }
    }

    fn find_matching_expose(
        &self,
        check: CheckUse,
        decl: &ComponentDecl,
    ) -> (Result<ExposeDecl, TestModelError>, ExpectedResult) {
        match check {
            CheckUse::Directory { path, expected_res, .. }
            | CheckUse::Protocol { path, expected_res, .. }
            | CheckUse::Service { path, expected_res, .. } => (
                decl.exposes
                    .iter()
                    .find(|&e| e.target_name().to_string() == path.basename)
                    .cloned()
                    .ok_or(TestModelError::ExposeDeclNotFound),
                expected_res,
            ),
            CheckUse::Event { .. } | CheckUse::Storage { .. } | CheckUse::StorageAdmin { .. } => {
                panic!("attempted to use from expose for unsupported capability type")
            }
        }
    }
}

#[async_trait]
impl RoutingTestModel for RoutingTestForAnalyzer {
    type C = ComponentInstanceForAnalyzer;

    async fn check_use(&self, moniker: AbsoluteMoniker, check: CheckUse) {
        let target_id = NodePath::new(
            moniker.path().into_iter().map(|child_moniker| child_moniker.to_partial()).collect(),
        );
        let target = self.model.get_instance(&target_id).expect("target instance not found");
        let target_decl = target.decl().await.expect("target ComponentDecl not found");

        let (find_decl, expected) = self.find_matching_use(check, &target_decl);

        // If `find_decl` is not OK, check that `expected` has a matching error.
        // Otherwise, route the capability and compare the result to `expected`.
        match &find_decl {
            Err(err) => {
                match expected {
                    ExpectedResult::Ok => panic!("expected UseDecl was not found: {}", err),
                    ExpectedResult::Err(status) => {
                        assert_eq!(err.as_zx_status(), status);
                    }
                    ExpectedResult::ErrWithNoEpitaph => {}
                };
                return;
            }
            Ok(use_decl) => match self.model.check_use_capability(use_decl, &target).await {
                Err(ref err) => match expected {
                    ExpectedResult::Ok => panic!("routing failed, expected success: {:?}", err),
                    ExpectedResult::Err(status) => {
                        assert_eq!(err.as_zx_status(), status);
                    }
                    ExpectedResult::ErrWithNoEpitaph => {}
                },
                Ok(()) => match expected {
                    ExpectedResult::Ok => {}
                    _ => panic!("capability use succeeded, expected failure"),
                },
            },
        }
    }

    async fn check_use_exposed_dir(&self, moniker: AbsoluteMoniker, check: CheckUse) {
        let target =
            self.model.get_instance(&NodePath::from(moniker)).expect("target instance not found");
        let target_decl = target.decl().await.expect("target ComponentDecl not found");

        let (find_decl, expected) = self.find_matching_expose(check, &target_decl);

        // If `find_decl` is not OK, check that `expected` has a matching error.
        // Otherwise, route the capability and compare the result to `expected`.
        match &find_decl {
            Err(err) => {
                match expected {
                    ExpectedResult::Ok => panic!("expected ExposeDecl was not found: {}", err),
                    ExpectedResult::Err(status) => {
                        assert_eq!(err.as_zx_status(), status);
                    }
                    _ => unimplemented![],
                };
                return;
            }
            Ok(expose_decl) => {
                match self.model.check_use_exposed_capability(expose_decl, &target).await {
                    Err(err) => match expected {
                        ExpectedResult::Ok => panic!("routing failed, expected success"),
                        ExpectedResult::Err(status) => {
                            assert_eq!(err.as_zx_status(), status);
                        }
                        _ => unimplemented![],
                    },
                    Ok(()) => match expected {
                        ExpectedResult::Ok => {}
                        _ => panic!("capability use succeeded, expected failure"),
                    },
                }
            }
        }
    }

    async fn look_up_instance(
        &self,
        moniker: &AbsoluteMoniker,
    ) -> Result<Arc<ComponentInstanceForAnalyzer>, anyhow::Error> {
        self.model.get_instance(&NodePath::from(moniker.clone())).map_err(|err| anyhow!(err))
    }

    // File and directory operations
    //
    // All file and directory operations are no-ops for the static model.
    #[allow(unused_variables)]
    async fn check_open_file(&self, moniker: AbsoluteMoniker, path: CapabilityPath) {}

    #[allow(unused_variables)]
    async fn create_static_file(&self, path: &Path, contents: &str) -> Result<(), anyhow::Error> {
        Ok(())
    }

    #[allow(unused_variables)]
    fn install_namespace_directory(&self, path: &str) {}

    #[allow(unused_variables)]
    fn add_subdir_to_data_directory(&self, subdir: &str) {}

    #[allow(unused_variables)]
    async fn check_test_subdir_contents(&self, path: &str, expected: Vec<String>) {}

    #[allow(unused_variables)]
    async fn check_namespace_subdir_contents(&self, path: &str, expected: Vec<String>) {}

    #[allow(unused_variables)]
    async fn check_test_subdir_contains(&self, path: &str, expected: String) {}

    #[allow(unused_variables)]
    async fn check_test_dir_tree_contains(&self, expected: String) {}
}

#[cfg(test)]
mod tests {
    use {super::*, routing_test_helpers::instantiate_common_routing_tests};

    instantiate_common_routing_tests! { RoutingTestBuilderForAnalyzer }

    // Routing of service capabilities is tested as part of the common routing tests
    // generated by `instantiate_common_routing_tests`.
    //
    // In order to test additional validation beyond routing, we need much more setup for component
    // manager's routing model than we do for the static analyzer's model. The following tests
    // suffice to test the static analyzer's `check_use_capability()` method for service capabilities.

    ///   a
    ///  /
    /// b
    ///
    /// a: offer to b from self
    /// b: use from parent
    #[fuchsia::test]
    async fn check_use_service_from_parent() {
        let use_decl = UseServiceDecl {
            dependency_type: DependencyType::Strong,
            source: UseSource::Parent,
            source_name: "foo".into(),
            target_path: CapabilityPath::try_from("/foo").unwrap(),
        };
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Service(OfferServiceDecl {
                        source: OfferSource::Self_,
                        source_name: "foo".into(),

                        target_name: "foo".into(),
                        target: OfferTarget::Child("b".to_string()),
                    }))
                    .service(ServiceDecl {
                        name: "foo".into(),
                        source_path: Some("/svc/foo".try_into().unwrap()),
                    })
                    .add_lazy_child("b")
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_decl.clone().into()).build()),
        ];
        let model = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Service {
                    path: CapabilityPath::try_from("/foo").unwrap(),
                    instance: "".into(),
                    member: "".into(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await
    }

    ///   a
    ///  /
    /// b
    ///
    /// a: offer to b from self
    /// b: use from parent, but parent component is not executable
    #[fuchsia::test]
    async fn check_service_source_is_executable() {
        let use_decl = UseServiceDecl {
            dependency_type: DependencyType::Strong,
            source: UseSource::Parent,
            source_name: "foo".into(),
            target_path: CapabilityPath::try_from("/foo").unwrap(),
        };
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new_empty_component()
                    .offer(OfferDecl::Service(OfferServiceDecl {
                        source: OfferSource::Self_,
                        source_name: "foo".into(),

                        target_name: "foo".into(),
                        target: OfferTarget::Child("b".to_string()),
                    }))
                    .service(ServiceDecl {
                        name: "foo".into(),
                        source_path: Some("/svc/foo".try_into().unwrap()),
                    })
                    .add_lazy_child("b")
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_decl.clone().into()).build()),
        ];
        let model = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Service {
                    path: CapabilityPath::try_from("/foo").unwrap(),
                    instance: "".into(),
                    member: "".into(),
                    expected_res: ExpectedResult::Err(zx_status::Status::UNAVAILABLE),
                },
            )
            .await
    }

    ///   a
    ///  /
    /// b
    ///
    /// a: use from b
    /// b: expose to parent from self
    #[fuchsia::test]
    async fn check_use_service_from_child() {
        let use_decl = UseServiceDecl {
            dependency_type: DependencyType::Strong,
            source: UseSource::Child("b".to_string()),
            source_name: "foo".into(),
            target_path: CapabilityPath::try_from("/foo").unwrap(),
        };
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .use_(use_decl.clone().into())
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .service(ServiceDecl {
                        name: "foo".into(),
                        source_path: Some("/svc/foo".try_into().unwrap()),
                    })
                    .expose(ExposeDecl::Service(ExposeServiceDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo".into(),
                        target_name: "foo".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
        ];
        let model = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        model
            .check_use(
                vec![].into(),
                CheckUse::Service {
                    path: CapabilityPath::try_from("/foo").unwrap(),
                    instance: "".into(),
                    member: "".into(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await
    }

    ///   a
    ///  / \
    /// b   c
    ///
    /// a: offer to b from child c
    /// b: use from parent
    /// c: expose from self
    #[fuchsia::test]
    async fn check_use_service_from_sibling() {
        let use_decl = UseServiceDecl {
            dependency_type: DependencyType::Strong,
            source: UseSource::Parent,
            source_name: "foo".into(),
            target_path: CapabilityPath::try_from("/foo").unwrap(),
        };
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Service(OfferServiceDecl {
                        source: OfferSource::Child("c".into()),
                        source_name: "foo".into(),
                        target_name: "foo".into(),
                        target: OfferTarget::Child("b".to_string()),
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_decl.clone().into()).build()),
            (
                "c",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::Service(ExposeServiceDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo".into(),
                        target_name: "foo".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .service(ServiceDecl {
                        name: "foo".into(),
                        source_path: Some("/svc/foo".try_into().unwrap()),
                    })
                    .build(),
            ),
        ];
        let model = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Service {
                    path: CapabilityPath::try_from("/foo").unwrap(),
                    instance: "".into(),
                    member: "".into(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await
    }
}
