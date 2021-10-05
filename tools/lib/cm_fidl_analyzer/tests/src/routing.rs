// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::anyhow,
    async_trait::async_trait,
    cm_fidl_analyzer::{
        capability_routing::route::RouteSegment,
        component_model::{
            AnalyzerModelError, ComponentInstanceForAnalyzer, ComponentModelForAnalyzer,
            ModelBuilderForAnalyzer, RouteMap,
        },
        component_tree::NodePath,
    },
    cm_rust::{
        CapabilityDecl, CapabilityName, CapabilityPath, ChildRef, ComponentDecl, DependencyType,
        ExposeDecl, ExposeDeclCommon, ExposeDirectoryDecl, ExposeProtocolDecl, ExposeServiceDecl,
        ExposeSource, ExposeTarget, OfferDecl, OfferDirectoryDecl, OfferEventDecl,
        OfferProtocolDecl, OfferServiceDecl, OfferSource, OfferStorageDecl, OfferTarget,
        ProtocolDecl, RegistrationSource, RunnerDecl, RunnerRegistration, ServiceDecl, StorageDecl,
        StorageDirectorySource, UseDecl, UseDirectoryDecl, UseEventDecl, UseProtocolDecl,
        UseServiceDecl, UseSource, UseStorageDecl,
    },
    cm_rust_testing::{
        ChildDeclBuilder, ComponentDeclBuilder, DirectoryDeclBuilder, EnvironmentDeclBuilder,
        ProtocolDeclBuilder,
    },
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_sys2 as fsys, fuchsia_zircon_status as zx_status,
    matches::assert_matches,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, PartialAbsoluteMoniker},
    routing::{
        component_id_index::ComponentIdIndex,
        component_instance::ComponentInstanceInterface,
        config::{AllowlistEntry, CapabilityAllowlistKey, RuntimeConfig, SecurityPolicy},
        environment::RunnerRegistry,
        error::RoutingError,
        rights::{READ_RIGHTS, WRITE_RIGHTS},
        RegistrationDecl,
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
    builtin_runner_registrations: Vec<RunnerRegistration>,
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
            builtin_runner_registrations: Vec::new(),
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

    fn register_mock_builtin_runner(&mut self, runner: &str) {
        let runner_name = CapabilityName(runner.into());
        self.builtin_runner_registrations.push(RunnerRegistration {
            source_name: runner_name.clone(),
            target_name: runner_name.clone(),
            source: RegistrationSource::Self_,
        });
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
        let component_id_index = match config.component_id_index_path {
            Some(ref index_path) => ComponentIdIndex::new(index_path)
                .await
                .expect(&format!("failed to create component ID index with path {}", index_path)),
            None => ComponentIdIndex::default(),
        };

        let build_model_result = ModelBuilderForAnalyzer::new(self.root_url)
            .build(
                self.decls_by_url,
                Arc::new(config),
                Arc::new(component_id_index),
                RunnerRegistry::from_decl(&self.builtin_runner_registrations),
            )
            .await;
        let model = build_model_result.model.expect("failed to build ComponentModelForAnalyzer");
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

    async fn check_use(&self, moniker: PartialAbsoluteMoniker, check: CheckUse) {
        let target_id = NodePath::new(moniker.path().clone());
        let target = self.model.get_instance(&target_id).expect("target instance not found");

        let (find_decl, expected) = self.find_matching_use(check, target.decl_for_testing());

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
                Ok(_) => match expected {
                    ExpectedResult::Ok => {}
                    _ => panic!("capability use succeeded, expected failure"),
                },
            },
        }
    }

    async fn check_use_exposed_dir(&self, moniker: PartialAbsoluteMoniker, check: CheckUse) {
        let target =
            self.model.get_instance(&NodePath::from(moniker)).expect("target instance not found");

        let (find_decl, expected) = self.find_matching_expose(check, target.decl_for_testing());

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
                    Ok(_) => match expected {
                        ExpectedResult::Ok => {}
                        _ => panic!("capability use succeeded, expected failure"),
                    },
                }
            }
        }
    }

    async fn look_up_instance(
        &self,
        moniker: &PartialAbsoluteMoniker,
    ) -> Result<Arc<ComponentInstanceForAnalyzer>, anyhow::Error> {
        self.model.get_instance(&NodePath::from(moniker.clone())).map_err(|err| anyhow!(err))
    }

    // File and directory operations
    //
    // All file and directory operations are no-ops for the static model.
    #[allow(unused_variables)]
    async fn check_open_file(&self, moniker: PartialAbsoluteMoniker, path: CapabilityPath) {}

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
                        target: OfferTarget::static_child("b".to_string()),
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
                vec!["b"].into(),
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
                        target: OfferTarget::static_child("b".to_string()),
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
                vec!["b"].into(),
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
                        source: OfferSource::static_child("c".into()),
                        source_name: "foo".into(),
                        target_name: "foo".into(),
                        target: OfferTarget::static_child("b".to_string()),
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
                vec!["b"].into(),
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
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: declares runner "elf" as service "/svc/runner" from self.
    /// a: registers runner "elf" from realm in environment as "hobbit".
    /// b: creates environment extending from realm.
    /// c: uses runner "hobbit" in its ProgramDecl.
    #[fuchsia::test]
    async fn check_program_runner_from_inherited_environment() {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("b").environment("env").build())
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fsys::EnvironmentExtends::Realm)
                            .add_runner(RunnerRegistration {
                                source_name: "elf".into(),
                                source: RegistrationSource::Self_,
                                target_name: "hobbit".into(),
                            })
                            .build(),
                    )
                    .runner(RunnerDecl {
                        name: "elf".into(),
                        source_path: Some(CapabilityPath::try_from("/svc/runner").unwrap()),
                    })
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("c").environment("env").build())
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fsys::EnvironmentExtends::Realm)
                            .build(),
                    )
                    .build(),
            ),
            ("c", ComponentDeclBuilder::new_empty_component().add_program("hobbit").build()),
        ];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let c_component = test.look_up_instance(&vec!["b", "c"].into()).await.expect("c instance");

        assert!(test
            .model
            .check_program_runner(
                c_component.decl_for_testing().program.as_ref().expect("missing program decl"),
                &c_component
            )
            .await
            .is_ok());
    }

    ///  a
    ///   \
    ///    b
    ///
    /// a: declares runner "elf" with service "/svc/runner" from "self".
    /// a: registers runner "elf" from self in environment as "hobbit".
    /// b: uses runner "hobbit" in its ProgramDecl. Fails because "hobbit" was not in environment.
    #[fuchsia::test]
    async fn check_program_runner_from_environment_not_found() {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("b").environment("env").build())
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fsys::EnvironmentExtends::Realm)
                            .add_runner(RunnerRegistration {
                                source_name: "elf".into(),
                                source: RegistrationSource::Self_,
                                target_name: "dwarf".into(),
                            })
                            .build(),
                    )
                    .runner(RunnerDecl {
                        name: "elf".into(),
                        source_path: Some(CapabilityPath::try_from("/svc/runner").unwrap()),
                    })
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new_empty_component().add_program("hobbit").build()),
        ];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let check_result = test
            .model
            .check_program_runner(
                &b_component.decl_for_testing().program.as_ref().expect("missing program decl"),
                &b_component,
            )
            .await;

        assert_matches!(
            check_result,
            Err(AnalyzerModelError::RoutingError(RoutingError::UseFromEnvironmentNotFound {
                    moniker,
                    capability_type,
                    capability_name,
            }))
                if moniker == b_component.abs_moniker().to_partial() &&
                capability_type == "runner" &&
                capability_name == CapabilityName("hobbit".to_string())
        );
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: offers protocol /svc/foo from self as /svc/bar
    /// b: uses protocol /svc/bar as /svc/hippo
    #[fuchsia::test]
    async fn map_route_use_from_parent() {
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "bar_svc".into(),
            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
            dependency_type: DependencyType::Strong,
        });
        let offer_decl = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Self_,
            source_name: "foo_svc".into(),
            target_name: "bar_svc".into(),
            target: OfferTarget::Child(ChildRef { name: "b".to_string(), collection: None }),
            dependency_type: DependencyType::Strong,
        });
        let protocol_decl = ProtocolDeclBuilder::new("foo_svc").build();
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .protocol(protocol_decl.clone())
                    .offer(offer_decl.clone())
                    .add_lazy_child("b")
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_decl.clone()).build()),
        ];
        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let route_map = test
            .model
            .check_use_capability(&use_decl, &b_component)
            .await
            .expect("expected OK route");

        assert_eq!(
            route_map,
            vec![RouteMap::from_segments(vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: use_decl
                },
                RouteSegment::OfferBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: offer_decl
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: CapabilityDecl::Protocol(protocol_decl)
                }
            ])]
        )
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: uses protocol /svc/bar from b as /svc/hippo
    /// b: exposes protocol /svc/foo from self as /svc/bar
    #[fuchsia::test]
    async fn map_route_use_from_child() {
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Child("b".to_string()),
            source_name: "bar_svc".into(),
            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
            dependency_type: DependencyType::Strong,
        });
        let expose_decl = ExposeDecl::Protocol(ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: "foo_svc".into(),
            target_name: "bar_svc".into(),
            target: ExposeTarget::Parent,
        });
        let protocol_decl = ProtocolDeclBuilder::new("foo_svc").build();
        let components = vec![
            ("a", ComponentDeclBuilder::new().use_(use_decl.clone()).add_lazy_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .protocol(protocol_decl.clone())
                    .expose(expose_decl.clone())
                    .build(),
            ),
        ];
        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let a_component = test.look_up_instance(&vec![].into()).await.expect("a instance");
        let route_map = test
            .model
            .check_use_capability(&use_decl, &a_component)
            .await
            .expect("expected OK route");

        assert_eq!(
            route_map,
            vec![RouteMap::from_segments(vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: use_decl
                },
                RouteSegment::ExposeBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: expose_decl
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: CapabilityDecl::Protocol(protocol_decl)
                }
            ])]
        )
    }

    /// a: uses protocol /svc/hippo from self
    #[fuchsia::test]
    async fn map_route_use_from_self() {
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Self_,
            source_name: "hippo".into(),
            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
            dependency_type: DependencyType::Strong,
        });
        let protocol_decl = ProtocolDeclBuilder::new("hippo").build();
        let components = vec![(
            "a",
            ComponentDeclBuilder::new()
                .protocol(protocol_decl.clone())
                .use_(use_decl.clone())
                .build(),
        )];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let a_component = test.look_up_instance(&vec![].into()).await.expect("a instance");
        let route_map = test
            .model
            .check_use_capability(&use_decl, &a_component)
            .await
            .expect("expected OK route");

        assert_eq!(
            route_map,
            vec![RouteMap::from_segments(vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: use_decl
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: CapabilityDecl::Protocol(protocol_decl)
                }
            ])]
        )
    }

    ///     a
    ///    / \
    ///   b   c
    ///  /
    /// d
    ///
    /// d: exposes directory /data/foo from self as /data/bar
    /// b: exposes directory /data/bar from d as /data/baz
    /// a: offers directory /data/baz from b as /data/foobar to c
    /// c: uses /data/foobar as /data/hippo
    #[fuchsia::test]
    async fn map_route_use_from_niece() {
        let use_decl = UseDecl::Directory(UseDirectoryDecl {
            source: UseSource::Parent,
            source_name: "foobar_data".into(),
            target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
            rights: *READ_RIGHTS,
            subdir: None,
            dependency_type: DependencyType::Strong,
        });
        let a_offer_decl = OfferDecl::Directory(OfferDirectoryDecl {
            source: OfferSource::static_child("b".to_string()),
            source_name: "baz_data".into(),
            target_name: "foobar_data".into(),
            target: OfferTarget::static_child("c".to_string()),
            rights: Some(*READ_RIGHTS),
            subdir: None,
            dependency_type: DependencyType::Strong,
        });
        let b_expose_decl = ExposeDecl::Directory(ExposeDirectoryDecl {
            source: ExposeSource::Child("d".to_string()),
            source_name: "bar_data".into(),
            target_name: "baz_data".into(),
            target: ExposeTarget::Parent,
            rights: Some(*READ_RIGHTS),
            subdir: None,
        });
        let d_expose_decl = ExposeDecl::Directory(ExposeDirectoryDecl {
            source: ExposeSource::Self_,
            source_name: "foo_data".into(),
            target_name: "bar_data".into(),
            target: ExposeTarget::Parent,
            rights: Some(*READ_RIGHTS),
            subdir: None,
        });
        let directory_decl = DirectoryDeclBuilder::new("foo_data").build();

        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(a_offer_decl.clone())
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .expose(b_expose_decl.clone())
                    .add_lazy_child("d")
                    .build(),
            ),
            ("c", ComponentDeclBuilder::new().use_(use_decl.clone()).build()),
            (
                "d",
                ComponentDeclBuilder::new()
                    .directory(directory_decl.clone())
                    .expose(d_expose_decl.clone())
                    .build(),
            ),
        ];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let c_component = test.look_up_instance(&vec!["c"].into()).await.expect("c instance");
        let route_map = test
            .model
            .check_use_capability(&use_decl, &c_component)
            .await
            .expect("expected OK route");

        assert_eq!(
            route_map,
            vec![RouteMap::from_segments(vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec!["c"]),
                    capability: use_decl
                },
                RouteSegment::OfferBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: a_offer_decl
                },
                RouteSegment::ExposeBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: b_expose_decl
                },
                RouteSegment::ExposeBy {
                    node_path: NodePath::absolute_from_vec(vec!["b", "d"]),
                    capability: d_expose_decl
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec!["b", "d"]),
                    capability: CapabilityDecl::Directory(directory_decl),
                }
            ])]
        )
    }

    ///  a
    ///   \
    ///    b
    ///
    /// a: declares runner "elf" with service "/svc/runner" from "self".
    /// a: registers runner "elf" from self in environment as "hobbit".
    /// b: refers to runner "hobbit" in its `ProgramDecl`.
    #[fuchsia::test]
    async fn map_route_for_program_runner() {
        let runner_reg = RunnerRegistration {
            source_name: "elf".into(),
            source: RegistrationSource::Self_,
            target_name: "hobbit".into(),
        };
        let runner_decl = RunnerDecl {
            name: "elf".into(),
            source_path: Some(CapabilityPath::try_from("/svc/runner").unwrap()),
        };
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("b").environment("env").build())
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env")
                            .extends(fsys::EnvironmentExtends::Realm)
                            .add_runner(runner_reg.clone())
                            .build(),
                    )
                    .runner(runner_decl.clone())
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new_empty_component().add_program("hobbit").build()),
        ];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let route_map = test
            .model
            .check_program_runner(
                &b_component
                    .decl_for_testing()
                    .program
                    .as_ref()
                    .expect("expected ProgramDecl for b"),
                &b_component,
            )
            .await
            .expect("expected OK route")
            .expect("expected program runner route");

        assert_eq!(
            route_map,
            RouteMap::from_segments(vec![
                RouteSegment::RegisterBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: RegistrationDecl::Runner(runner_reg)
                },
                RouteSegment::DeclareBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: CapabilityDecl::Runner(runner_decl)
                },
            ])
        )
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: has storage decl with name "cache" with a source of self at path /data
    /// a: offers cache storage to b from "mystorage"
    /// b: uses cache storage as /storage
    ///
    /// We expect 2 route maps: one for the storage capability and one for the backing
    /// directory.
    #[fuchsia::test]
    async fn map_route_storage_and_dir_from_parent() {
        let directory_decl = DirectoryDeclBuilder::new("data")
            .path("/data")
            .rights(*READ_RIGHTS | *WRITE_RIGHTS)
            .build();
        let storage_decl = StorageDecl {
            name: "cache".into(),
            backing_dir: "data".try_into().unwrap(),
            source: StorageDirectorySource::Self_,
            subdir: None,
            storage_id: fsys::StorageId::StaticInstanceIdOrMoniker,
        };
        let offer_storage_decl = OfferDecl::Storage(OfferStorageDecl {
            source: OfferSource::Self_,
            target: OfferTarget::static_child("b".to_string()),
            source_name: "cache".into(),
            target_name: "cache".into(),
        });
        let use_storage_decl = UseDecl::Storage(UseStorageDecl {
            source_name: "cache".into(),
            target_path: "/storage".try_into().unwrap(),
        });
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .storage(storage_decl.clone())
                    .directory(directory_decl.clone())
                    .offer(offer_storage_decl.clone())
                    .add_lazy_child("b")
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_storage_decl.clone()).build()),
        ];

        let test = RoutingTestBuilderForAnalyzer::new("a", components).build().await;
        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let route_map = test
            .model
            .check_use_capability(&use_storage_decl, &b_component)
            .await
            .expect("expected OK route");

        assert_eq!(
            route_map,
            vec![
                RouteMap::from_segments(vec![
                    RouteSegment::UseBy {
                        node_path: NodePath::absolute_from_vec(vec!["b"]),
                        capability: use_storage_decl
                    },
                    RouteSegment::OfferBy {
                        node_path: NodePath::absolute_from_vec(vec![]),
                        capability: offer_storage_decl
                    },
                    RouteSegment::DeclareBy {
                        node_path: NodePath::absolute_from_vec(vec![]),
                        capability: CapabilityDecl::Storage(storage_decl.clone())
                    }
                ]),
                RouteMap::from_segments(vec![
                    RouteSegment::RegisterBy {
                        node_path: NodePath::absolute_from_vec(vec![]),
                        capability: RegistrationDecl::Storage(storage_decl.into())
                    },
                    RouteSegment::DeclareBy {
                        node_path: NodePath::absolute_from_vec(vec![]),
                        capability: CapabilityDecl::Directory(directory_decl)
                    }
                ])
            ]
        )
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: offers framework event "started" to b as "started_on_a"
    /// a: offers built-in protocol "fuchsia.sys2.EventSource" to b
    /// b: uses "started_on_a" as "started"
    /// b: uses protocol "fuchsia.sys2.EventSource"
    #[fuchsia::test]
    async fn route_map_use_from_framework_and_builtin() {
        let offer_event_decl = OfferDecl::Event(OfferEventDecl {
            source: OfferSource::Framework,
            source_name: "started".into(),
            target_name: "started_on_a".into(),
            target: OfferTarget::static_child("b".to_string()),
            filter: None,
            mode: cm_rust::EventMode::Sync,
        });
        let use_event_decl = UseDecl::Event(UseEventDecl {
            source: UseSource::Parent,
            source_name: "started_on_a".into(),
            target_name: "started".into(),
            filter: None,
            mode: cm_rust::EventMode::Sync,
            dependency_type: DependencyType::Strong,
        });

        let offer_event_source_decl = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Parent,
            source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
            target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
            target: OfferTarget::static_child("b".to_string()),
            dependency_type: DependencyType::Strong,
        });
        let use_event_source_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
            target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
            dependency_type: DependencyType::Strong,
        });
        let event_source_decl = CapabilityDecl::Protocol(ProtocolDecl {
            name: "fuchsia.sys2.EventSource".into(),
            source_path: None,
        });

        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(offer_event_decl.clone())
                    .offer(offer_event_source_decl.clone())
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(use_event_source_decl.clone())
                    .use_(use_event_decl.clone())
                    .build(),
            ),
        ];

        let mut builder = RoutingTestBuilderForAnalyzer::new("a", components);
        builder.set_builtin_capabilities(vec![event_source_decl.clone()]);
        let test = builder.build().await;

        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let event_route_map = test
            .model
            .check_use_capability(&use_event_decl, &b_component)
            .await
            .expect("expected OK route");

        assert_eq!(
            event_route_map,
            vec![RouteMap::from_segments(vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: use_event_decl
                },
                RouteSegment::OfferBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: offer_event_decl
                },
                RouteSegment::ProvideFromFramework { capability: "started".into() }
            ])]
        );

        let event_source_route_map = test
            .model
            .check_use_capability(&use_event_source_decl, &b_component)
            .await
            .expect("expected OK route");

        assert_eq!(
            event_source_route_map,
            vec![RouteMap::from_segments(vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: use_event_source_decl
                },
                RouteSegment::OfferBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: offer_event_source_decl
                },
                RouteSegment::ProvideAsBuiltin { capability: event_source_decl }
            ])]
        )
    }

    ///  component manager's namespace
    ///   |
    ///   a
    ///    \
    ///     b
    ///
    /// a: offers protocol /offer_from_cm_namespace/svc/foo from component manager's
    ///    namespace as bar_svc
    /// b: uses protocol bar_svc as /svc/hippo
    #[fuchsia::test]
    async fn route_map_offer_from_component_manager_namespace() {
        let offer_decl = OfferDecl::Protocol(OfferProtocolDecl {
            source: OfferSource::Parent,
            source_name: "foo_svc".into(),
            target_name: "bar_svc".into(),
            target: OfferTarget::static_child("b".to_string()),
            dependency_type: DependencyType::Strong,
        });
        let use_decl = UseDecl::Protocol(UseProtocolDecl {
            source: UseSource::Parent,
            source_name: "bar_svc".into(),
            target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
            dependency_type: DependencyType::Strong,
        });
        let capability_decl = CapabilityDecl::Protocol(
            ProtocolDeclBuilder::new("foo_svc").path("/offer_from_cm_namespace/svc/foo").build(),
        );
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new().offer(offer_decl.clone()).add_lazy_child("b").build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_decl.clone()).build()),
        ];

        let mut builder = RoutingTestBuilderForAnalyzer::new("a", components);
        builder.set_namespace_capabilities(vec![capability_decl.clone()]);
        let test = builder.build().await;
        test.install_namespace_directory("/offer_from_cm_namespace");

        let b_component = test.look_up_instance(&vec!["b"].into()).await.expect("b instance");
        let route_map = test
            .model
            .check_use_capability(&use_decl, &b_component)
            .await
            .expect("expected OK route");

        assert_eq!(
            route_map,
            vec![RouteMap::from_segments(vec![
                RouteSegment::UseBy {
                    node_path: NodePath::absolute_from_vec(vec!["b"]),
                    capability: use_decl
                },
                RouteSegment::OfferBy {
                    node_path: NodePath::absolute_from_vec(vec![]),
                    capability: offer_decl
                },
                RouteSegment::ProvideFromNamespace { capability: capability_decl }
            ])]
        );
    }
}
