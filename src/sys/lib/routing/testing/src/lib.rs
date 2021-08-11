// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod component_id_index;
pub mod policy;
pub mod rights;
pub mod storage;
pub mod storage_admin;

use {
    async_trait::async_trait,
    cm_rust::{
        CapabilityDecl, CapabilityName, CapabilityPath, CapabilityTypeName, ComponentDecl,
        DependencyType, DictionaryValue, EventMode, ExposeDecl, ExposeDirectoryDecl,
        ExposeProtocolDecl, ExposeServiceDecl, ExposeSource, ExposeTarget, OfferDecl,
        OfferDirectoryDecl, OfferEventDecl, OfferProtocolDecl, OfferServiceDecl, OfferSource,
        OfferTarget, ProgramDecl, RegistrationSource, ServiceDecl, UseDecl, UseDirectoryDecl,
        UseEventDecl, UseEventStreamDecl, UseProtocolDecl, UseServiceDecl, UseSource,
    },
    cm_rust_testing::{
        ChildDeclBuilder, ComponentDeclBuilder, DirectoryDeclBuilder, EnvironmentDeclBuilder,
        ProtocolDeclBuilder, TEST_RUNNER_NAME,
    },
    fidl_fuchsia_data as fdata, fidl_fuchsia_sys2 as fsys, fuchsia_zircon_status as zx,
    maplit::hashmap,
    moniker::{
        AbsoluteMoniker, AbsoluteMonikerBase, ChildMonikerBase, ExtendedMoniker, RelativeMoniker,
        RelativeMonikerBase,
    },
    routing::{
        capability_source::{CapabilitySourceInterface, ComponentCapability},
        component_id_index::ComponentInstanceId,
        component_instance::ComponentInstanceInterface,
        config::{AllowlistEntry, CapabilityAllowlistKey, CapabilityAllowlistSource},
        event::EventSubscription,
        rights::READ_RIGHTS,
        route_capability, RouteRequest, RouteSource,
    },
    std::{
        collections::HashSet,
        convert::{TryFrom, TryInto},
        marker::PhantomData,
        path::{Path, PathBuf},
        sync::Arc,
    },
};

/// Construct a capability path for the hippo service.
pub fn default_service_capability() -> CapabilityPath {
    "/svc/hippo".try_into().unwrap()
}

/// Construct a capability path for the hippo directory.
pub fn default_directory_capability() -> CapabilityPath {
    "/data/hippo".try_into().unwrap()
}

/// Returns an empty component decl for an executable component.
pub fn default_component_decl() -> ComponentDecl {
    ComponentDecl { ..Default::default() }
}

/// Returns an empty component decl set up to have a non-empty program and to use the "test_runner"
/// runner.
pub fn component_decl_with_test_runner() -> ComponentDecl {
    ComponentDecl {
        program: Some(ProgramDecl {
            runner: Some(TEST_RUNNER_NAME.into()),
            info: fdata::Dictionary { entries: Some(vec![]), ..fdata::Dictionary::EMPTY },
        }),
        ..Default::default()
    }
}

#[derive(Debug, PartialEq, Clone)]
pub enum ExpectedResult {
    Ok,
    Err(zx::Status),
    ErrWithNoEpitaph,
}

pub enum CheckUse {
    Protocol {
        path: CapabilityPath,
        expected_res: ExpectedResult,
    },
    Service {
        path: CapabilityPath,
        instance: String,
        member: String,
        expected_res: ExpectedResult,
    },
    Directory {
        path: CapabilityPath,
        file: PathBuf,
        expected_res: ExpectedResult,
    },
    Storage {
        path: CapabilityPath,
        // The relative moniker from the storage declaration to the use declaration. Only
        // used if `expected_res` is Ok.
        storage_relation: Option<RelativeMoniker>,
        // The backing directory for this storage is in component manager's namespace, not the
        // test's isolated test directory.
        from_cm_namespace: bool,
        storage_subdir: Option<String>,
        expected_res: ExpectedResult,
    },
    StorageAdmin {
        // The relative moniker from the storage declaration to the use declaration.
        storage_relation: RelativeMoniker,
        // The backing directory for this storage is in component manager's namespace, not the
        // test's isolated test directory.
        from_cm_namespace: bool,

        storage_subdir: Option<String>,
        expected_res: ExpectedResult,
    },
    Event {
        request: EventSubscription,
        start_component_tree: bool,
        expected_res: ExpectedResult,
    },
}

impl CheckUse {
    pub fn default_directory(expected_res: ExpectedResult) -> Self {
        Self::Directory {
            path: default_directory_capability(),
            file: PathBuf::from("hippo"),
            expected_res,
        }
    }
}

// This function should reproduce the logic of `crate::storage::generate_storage_path`.
pub fn generate_storage_path(
    subdir: Option<String>,
    relative_moniker: &RelativeMoniker,
    instance_id: Option<&ComponentInstanceId>,
) -> PathBuf {
    if let Some(id) = instance_id {
        return [id].iter().collect();
    }
    assert!(relative_moniker.up_path().is_empty());
    let mut down_path = relative_moniker.down_path().iter();
    let mut dir_path = vec![];
    if let Some(subdir) = subdir {
        dir_path.push(subdir);
    }
    if let Some(p) = down_path.next() {
        dir_path.push(p.as_str().to_string());
    }
    while let Some(p) = down_path.next() {
        dir_path.push("children".to_string());
        dir_path.push(p.as_str().to_string());
    }

    // Storage capabilities used to have a hardcoded set of types, which would be appended
    // here. To maintain compatibility with the old paths (and thus not lose data when this was
    // migrated) we append "data" here. This works because this is the only type of storage
    // that was actually used in the wild.
    //
    // This is only temporary, until the storage instance id migration changes this layout.
    dir_path.push("data".to_string());
    dir_path.into_iter().collect()
}

/// A `RoutingTestModel` attempts to use capabilities from instances in a component model
/// and checks the result of the attempt against an expectation.
#[async_trait]
pub trait RoutingTestModel {
    type C: ComponentInstanceInterface + 'static;

    /// Checks a `use` declaration at `moniker` by trying to use `capability`.
    async fn check_use(&self, moniker: AbsoluteMoniker, check: CheckUse);

    /// Checks using a capability from a component's exposed directory.
    async fn check_use_exposed_dir(&self, moniker: AbsoluteMoniker, check: CheckUse);

    /// Looks up a component instance by its absolute moniker.
    async fn look_up_instance(
        &self,
        moniker: &AbsoluteMoniker,
    ) -> Result<Arc<Self::C>, anyhow::Error>;

    /// Checks that a use declaration of `path` at `moniker` can be opened with
    /// Fuchsia file operations.
    async fn check_open_file(&self, moniker: AbsoluteMoniker, path: CapabilityPath);

    /// Create a file with the given contents in the test dir, along with any subdirectories
    /// required.
    async fn create_static_file(&self, path: &Path, contents: &str) -> Result<(), anyhow::Error>;

    /// Installs a new directory at `path` in the test's namespace.
    fn install_namespace_directory(&self, path: &str);

    /// Creates a subdirectory in the outgoing dir's /data directory.
    fn add_subdir_to_data_directory(&self, subdir: &str);

    /// Asserts that the subdir given by `path` within the test directory contains exactly the
    /// filenames in `expected`.
    async fn check_test_subdir_contents(&self, path: &str, expected: Vec<String>);

    /// Asserts that the directory at absolute `path` contains exactly the filenames in `expected`.
    async fn check_namespace_subdir_contents(&self, path: &str, expected: Vec<String>);

    /// Asserts that the subdir given by `path` within the test directory contains a file named `expected`.
    async fn check_test_subdir_contains(&self, path: &str, expected: String);

    /// Asserts that the tree in the test directory under `path` contains a file named `expected`.
    async fn check_test_dir_tree_contains(&self, expected: String);
}

/// Builds an implementation of `RoutingTestModel` from a set of `ComponentDecl`s.
#[async_trait]
pub trait RoutingTestModelBuilder {
    type Model: RoutingTestModel;

    /// Create a new builder. Both string arguments refer to component names, not URLs,
    /// ex: "a", not "test:///a" or "test:///a_resolved".
    fn new(root_component: &str, components: Vec<(&'static str, ComponentDecl)>) -> Self;

    /// Set the capabilities that should be available from the top instance's namespace.
    fn set_namespace_capabilities(&mut self, caps: Vec<CapabilityDecl>);

    /// Add a custom capability security policy to restrict routing of certain caps.
    fn add_capability_policy(
        &mut self,
        key: CapabilityAllowlistKey,
        allowlist: HashSet<AllowlistEntry>,
    );

    /// Add a custom debug capability security policy to restrict routing of certain caps.
    fn add_debug_capability_policy(
        &mut self,
        key: CapabilityAllowlistKey,
        allowlist: HashSet<(AbsoluteMoniker, String)>,
    );

    /// Sets the path to the component ID index for the test model.
    fn set_component_id_index_path(&mut self, index_path: String);

    async fn build(self) -> Self::Model;
}

/// The CommonRoutingTests are run under multiple contexts, e.g. both on Fuchsia under
/// component_manager and on the build host under cm_fidl_analyzer. This macro helps ensure that all
/// tests are run in each context.
#[macro_export]
macro_rules! instantiate_common_routing_tests {
    ($builder_impl:path) => {
        // New CommonRoutingTest tests must be added to this list to run.
        instantiate_common_routing_tests! {
            $builder_impl,
            test_use_from_parent,
            test_use_from_child,
            test_use_from_self,
            test_use_from_grandchild,
            test_use_from_grandparent,
            test_use_from_sibling_no_root,
            test_use_from_sibling_root,
            test_use_from_niece,
            test_use_kitchen_sink,
            test_use_from_component_manager_namespace,
            test_offer_from_component_manager_namespace,
            test_use_not_offered,
            test_use_offer_source_not_exposed,
            test_use_offer_source_not_offered,
            test_use_from_expose,
            test_use_from_expose_to_framework,
            test_offer_from_non_executable,
            test_use_directory_with_subdir_from_grandparent,
            test_use_directory_with_subdir_from_sibling,
            test_expose_directory_with_subdir,
            test_expose_from_self_and_child,
            test_use_not_exposed,
            test_use_protocol_denied_by_capability_policy,
            test_use_directory_with_alias_denied_by_capability_policy,
            test_use_protocol_partial_chain_allowed_by_capability_policy,
            test_use_protocol_component_provided_capability_policy,
            test_use_from_component_manager_namespace_denied_by_policy,
            test_use_protocol_component_provided_debug_capability_policy_at_root_from_self,
            test_use_protocol_component_provided_debug_capability_policy_from_self,
            test_use_protocol_component_provided_debug_capability_policy_from_child,
            test_use_protocol_component_provided_debug_capability_policy_from_grandchild,
            test_use_event_from_framework,
            test_can_offer_capability_requested_event,
            test_use_event_from_parent,
            test_use_event_from_grandparent,
            test_event_filter_routing,
            test_event_mode_routing_failure,
            test_event_mode_routing_success,
            test_route_service_from_parent,
            test_route_service_from_child,
            test_route_service_from_sibling,
        }

        // TODO(fxbug.dev/77649): These tests exercise features not currently supported under
        // cm_fidl_analyzer
        #[cfg(target_os = "fuchsia")]
        instantiate_common_routing_tests! {
            $builder_impl,
            test_use_builtin_from_grandparent,
            test_invalid_use_from_component_manager,
            test_invalid_offer_from_component_manager,
        }
    };
    ($builder_impl:path, $test:ident, $($remaining:ident),+ $(,)?) => {
        instantiate_common_routing_tests! { $builder_impl, $test }
        instantiate_common_routing_tests! { $builder_impl, $($remaining),+ }
    };
    ($builder_impl:path, $test:ident) => {
        // TODO(fxbug.dev/77647): #[fuchsia::test] did not work inside a declarative macro, so this
        // falls back on fuchsia_async and manual logging initialization for now.
        #[fuchsia_async::run_singlethreaded(test)]
        async fn $test() {
            fuchsia::init_logging_for_component_with_executor(|| {}, &[])();

            $crate::CommonRoutingTest::<$builder_impl>::new().$test().await
        }
    };
}

pub struct CommonRoutingTest<T: RoutingTestModelBuilder> {
    builder: PhantomData<T>,
}

impl<T: RoutingTestModelBuilder> CommonRoutingTest<T> {
    pub fn new() -> Self {
        Self { builder: PhantomData }
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: offers directory /data/foo from self as /data/bar
    /// a: offers service /svc/foo from self as /svc/bar
    /// a: offers service /svc/file from self as /svc/device
    /// b: uses directory /data/bar as /data/hippo
    /// b: uses service /svc/bar as /svc/hippo
    /// b: uses service /svc/device
    ///
    /// The test related to `/svc/file` is used to verify that services that require
    /// extended flags, like `OPEN_FLAG_DESCRIBE`, work correctly. This often
    /// happens for fuchsia.hardware protocols that compose fuchsia.io protocols,
    /// and expect that `fdio_open` should operate correctly.
    pub async fn test_use_from_parent(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .protocol(ProtocolDeclBuilder::new("file").path("/svc/file").build())
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "bar_data".into(),
                        target: OfferTarget::Child("b".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "bar_svc".into(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Self_,
                        source_name: "file".into(),
                        target_name: "device".into(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "bar_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "bar_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "device".into(),
                        target_path: CapabilityPath::try_from("/svc/device").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model.check_use(vec!["b:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model.check_open_file(vec!["b:0"].into(), "/svc/device".try_into().unwrap()).await;
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: uses directory /data/bar from #b as /data/hippo
    /// a: uses service /svc/bar from #b as /svc/hippo
    /// b: exposes directory /data/foo from self as /data/bar
    /// b: exposes service /svc/foo from self as /svc/bar
    pub async fn test_use_from_child(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Child("b".to_string()),
                        source_name: "bar_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Child("b".to_string()),
                        source_name: "bar_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "bar_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "bar_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model.check_use(vec![].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
        model
            .check_use(
                vec![].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    /// a: uses protocol /svc/hippo from self
    pub async fn test_use_from_self(&self) {
        let components = vec![(
            "a",
            ComponentDeclBuilder::new()
                .protocol(ProtocolDeclBuilder::new("hippo").build())
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Self_,
                    source_name: "hippo".into(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    dependency_type: DependencyType::Strong,
                }))
                .build(),
        )];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec![].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: uses /data/baz from #b as /data/hippo
    /// a: uses /svc/baz from #b as /svc/hippo
    /// b: exposes directory /data/bar from #c as /data/baz
    /// b: exposes service /svc/bar from #c as /svc/baz
    /// c: exposes directory /data/foo from self as /data/bar
    /// c: exposes service /svc/foo from self as /svc/bar
    pub async fn test_use_from_grandchild(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Child("b".to_string()),
                        source_name: "baz_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Child("b".to_string()),
                        source_name: "baz_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Child("c".to_string()),
                        source_name: "bar_data".into(),
                        target_name: "baz_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Child("c".to_string()),
                        source_name: "bar_svc".into(),
                        target_name: "baz_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "bar_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "bar_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model.check_use(vec![].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
        model
            .check_use(
                vec![].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: offers directory /data/foo from self as /data/bar
    /// a: offers service /svc/foo from self as /svc/bar
    /// b: offers directory /data/bar from realm as /data/baz
    /// b: offers service /svc/bar from realm as /svc/baz
    /// c: uses /data/baz as /data/hippo
    /// c: uses /svc/baz as /svc/hippo
    pub async fn test_use_from_grandparent(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "bar_data".into(),
                        target: OfferTarget::Child("b".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "bar_svc".into(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Parent,
                        source_name: "bar_data".into(),
                        target_name: "baz_data".into(),
                        target: OfferTarget::Child("c".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "bar_svc".into(),
                        target_name: "baz_svc".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "baz_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "baz_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(vec!["b:0", "c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok))
            .await;
        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: offers service /svc/builtin.Echo from realm
    /// b: offers service /svc/builtin.Echo from realm
    /// c: uses /svc/builtin.Echo as /svc/hippo
    pub async fn test_use_builtin_from_grandparent(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "builtin.Echo".into(),
                        target_name: "builtin.Echo".into(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "builtin.Echo".into(),
                        target_name: "builtin.Echo".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "builtin.Echo".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///     a
    ///    /
    ///   b
    ///  / \
    /// d   c
    ///
    /// d: exposes directory /data/foo from self as /data/bar
    /// b: offers directory /data/bar from d as /data/foobar to c
    /// c: uses /data/foobar as /data/hippo
    pub async fn test_use_from_sibling_no_root(&self) {
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Child("d".to_string()),
                        source_name: "bar_data".into(),
                        target_name: "foobar_data".into(),
                        target: OfferTarget::Child("c".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child("d".to_string()),
                        source_name: "bar_svc".into(),
                        target_name: "foobar_svc".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("c")
                    .add_lazy_child("d")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "foobar_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "foobar_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "bar_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "bar_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(vec!["b:0", "c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok))
            .await;
        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///   a
    ///  / \
    /// b   c
    ///
    /// b: exposes directory /data/foo from self as /data/bar
    /// a: offers directory /data/bar from b as /data/baz to c
    /// c: uses /data/baz as /data/hippo
    pub async fn test_use_from_sibling_root(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Child("b".to_string()),
                        source_name: "bar_data".into(),
                        target_name: "baz_data".into(),
                        target: OfferTarget::Child("c".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child("b".to_string()),
                        source_name: "bar_svc".into(),
                        target_name: "baz_svc".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "bar_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "bar_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "baz_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "baz_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
        model
            .check_use(
                vec!["c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
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
    pub async fn test_use_from_niece(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Child("b".to_string()),
                        source_name: "baz_data".into(),
                        target_name: "foobar_data".into(),
                        target: OfferTarget::Child("c".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child("b".to_string()),
                        source_name: "baz_svc".into(),
                        target_name: "foobar_svc".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Child("d".to_string()),
                        source_name: "bar_data".into(),
                        target_name: "baz_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Child("d".to_string()),
                        source_name: "bar_svc".into(),
                        target_name: "baz_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .add_lazy_child("d")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "foobar_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "foobar_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "bar_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "bar_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model.check_use(vec!["c:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
        model
            .check_use(
                vec!["c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///      a
    ///     / \
    ///    /   \
    ///   b     c
    ///  / \   / \
    /// d   e f   g
    ///            \
    ///             h
    ///
    /// a,d,h: hosts /svc/foo and /data/foo
    /// e: uses /svc/foo as /svc/hippo from a, uses /data/foo as /data/hippo from d
    /// f: uses /data/foo from d as /data/hippo, uses /svc/foo from h as /svc/hippo
    pub async fn test_use_kitchen_sink(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "foo_from_a_svc".into(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Child("b".to_string()),
                        source_name: "foo_from_d_data".into(),
                        target_name: "foo_from_d_data".into(),
                        target: OfferTarget::Child("c".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new_empty_component()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Child("d".to_string()),
                        source_name: "foo_from_d_data".into(),
                        target_name: "foo_from_d_data".into(),
                        target: OfferTarget::Child("e".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "foo_from_a_svc".into(),
                        target_name: "foo_from_a_svc".into(),
                        target: OfferTarget::Child("e".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Child("d".to_string()),
                        source_name: "foo_from_d_data".into(),
                        target_name: "foo_from_d_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .add_lazy_child("d")
                    .add_lazy_child("e")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new_empty_component()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Parent,
                        source_name: "foo_from_d_data".into(),
                        target_name: "foo_from_d_data".into(),
                        target: OfferTarget::Child("f".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child("g".to_string()),
                        source_name: "foo_from_h_svc".into(),
                        target_name: "foo_from_h_svc".into(),
                        target: OfferTarget::Child("f".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("f")
                    .add_lazy_child("g")
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "foo_from_d_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "foo_from_d_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "foo_from_a_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
            (
                "f",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "foo_from_d_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "foo_from_h_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
            (
                "g",
                ComponentDeclBuilder::new_empty_component()
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Child("h".to_string()),
                        source_name: "foo_from_h_svc".into(),
                        target_name: "foo_from_h_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .add_lazy_child("h")
                    .build(),
            ),
            (
                "h",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "foo_from_h_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(vec!["b:0", "e:0"].into(), CheckUse::default_directory(ExpectedResult::Ok))
            .await;
        model
            .check_use(
                vec!["b:0", "e:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model
            .check_use(vec!["c:0", "f:0"].into(), CheckUse::default_directory(ExpectedResult::Ok))
            .await;
        model
            .check_use(
                vec!["c:0", "f:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///  component manager's namespace
    ///   |
    ///   a
    ///
    /// a: uses directory /use_from_cm_namespace/data/foo as foo_data
    /// a: uses service /use_from_cm_namespace/svc/foo as foo_svc
    pub async fn test_use_from_component_manager_namespace(&self) {
        let components = vec![(
            "a",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Directory(UseDirectoryDecl {
                    source: UseSource::Parent,
                    source_name: "foo_data".into(),
                    target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                    rights: *READ_RIGHTS,
                    subdir: None,
                    dependency_type: DependencyType::Strong,
                }))
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_name: "foo_svc".into(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    dependency_type: DependencyType::Strong,
                }))
                .build(),
        )];
        let namespace_capabilities = vec![
            CapabilityDecl::Directory(
                DirectoryDeclBuilder::new("foo_data")
                    .path("/use_from_cm_namespace/data/foo")
                    .build(),
            ),
            CapabilityDecl::Protocol(
                ProtocolDeclBuilder::new("foo_svc").path("/use_from_cm_namespace/svc/foo").build(),
            ),
        ];
        let mut builder = T::new("a", components);
        builder.set_namespace_capabilities(namespace_capabilities);
        let model = builder.build().await;

        model.install_namespace_directory("/use_from_cm_namespace");
        model.check_use(vec![].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
        model
            .check_use(
                vec![].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///  component manager's namespace
    ///   |
    ///   a
    ///    \
    ///     b
    ///
    /// a: offers directory /offer_from_cm_namespace/data/foo from realm as bar_data
    /// a: offers service /offer_from_cm_namespace/svc/foo from realm as bar_svc
    /// b: uses directory bar_data as /data/hippo
    /// b: uses service bar_svc as /svc/hippo
    pub async fn test_offer_from_component_manager_namespace(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Parent,
                        source_name: "foo_data".into(),
                        target_name: "bar_data".into(),
                        target: OfferTarget::Child("b".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "foo_svc".into(),
                        target_name: "bar_svc".into(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "bar_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "bar_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let namespace_capabilities = vec![
            CapabilityDecl::Directory(
                DirectoryDeclBuilder::new("foo_data")
                    .path("/offer_from_cm_namespace/data/foo")
                    .build(),
            ),
            CapabilityDecl::Protocol(
                ProtocolDeclBuilder::new("foo_svc")
                    .path("/offer_from_cm_namespace/svc/foo")
                    .build(),
            ),
        ];
        let mut builder = T::new("a", components);
        builder.set_namespace_capabilities(namespace_capabilities);
        let model = builder.build().await;

        model.install_namespace_directory("/offer_from_cm_namespace");
        model.check_use(vec!["b:0"].into(), CheckUse::default_directory(ExpectedResult::Ok)).await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///
    /// b: uses directory /data/hippo as /data/hippo, but it's not in its realm
    /// b: uses service /svc/hippo as /svc/hippo, but it's not in its realm
    pub async fn test_use_not_offered(&self) {
        let components = vec![
            ("a", ComponentDeclBuilder::new_empty_component().add_lazy_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "hippo_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::default_directory(ExpectedResult::Err(zx::Status::UNAVAILABLE)),
            )
            .await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
                },
            )
            .await;
    }

    ///   a
    ///  / \
    /// b   c
    ///
    /// a: offers directory /data/hippo from b as /data/hippo, but it's not exposed by b
    /// a: offers service /svc/hippo from b as /svc/hippo, but it's not exposed by b
    /// c: uses directory /data/hippo as /data/hippo
    /// c: uses service /svc/hippo as /svc/hippo
    pub async fn test_use_offer_source_not_exposed(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new_empty_component()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source_name: "hippo_data".into(),
                        source: OfferSource::Child("b".to_string()),
                        target_name: "hippo_data".into(),
                        target: OfferTarget::Child("c".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source_name: "hippo_svc".into(),
                        source: OfferSource::Child("b".to_string()),
                        target_name: "hippo_svc".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            ("b", component_decl_with_test_runner()),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "hippo_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["c:0"].into(),
                CheckUse::default_directory(ExpectedResult::Err(zx::Status::UNAVAILABLE)),
            )
            .await;
        model
            .check_use(
                vec!["c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// b: offers directory /data/hippo from its realm as /data/hippo, but it's not offered by a
    /// b: offers service /svc/hippo from its realm as /svc/hippo, but it's not offfered by a
    /// c: uses directory /data/hippo as /data/hippo
    /// c: uses service /svc/hippo as /svc/hippo
    pub async fn test_use_offer_source_not_offered(&self) {
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new_empty_component()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source_name: "hippo_data".into(),
                        source: OfferSource::Parent,
                        target_name: "hippo_data".into(),
                        target: OfferTarget::Child("c".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source_name: "hippo_svc".into(),
                        source: OfferSource::Parent,
                        target_name: "hippo_svc".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "hippo_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let test = T::new("a", components).build().await;
        test.check_use(
            vec!["b:0", "c:0"].into(),
            CheckUse::default_directory(ExpectedResult::Err(zx::Status::UNAVAILABLE)),
        )
        .await;
        test.check_use(
            vec!["b:0", "c:0"].into(),
            CheckUse::Protocol {
                path: default_service_capability(),
                expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
            },
        )
        .await;
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// b: uses directory /data/hippo as /data/hippo, but it's exposed to it, not offered
    /// b: uses service /svc/hippo as /svc/hippo, but it's exposed to it, not offered
    /// c: exposes /data/hippo
    /// c: exposes /svc/hippo
    pub async fn test_use_from_expose(&self) {
        let components = vec![
            ("a", ComponentDeclBuilder::new_empty_component().add_lazy_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "hippo_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("hippo_data").build())
                    .protocol(ProtocolDeclBuilder::new("hippo_svc").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source_name: "hippo_data".into(),
                        source: ExposeSource::Self_,
                        target_name: "hippo_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source_name: "hippo_svc".into(),
                        source: ExposeSource::Self_,
                        target_name: "hippo_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::default_directory(ExpectedResult::Err(zx::Status::UNAVAILABLE)),
            )
            .await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
                },
            )
            .await;
    }

    ///   a
    ///  / \
    /// b   c
    ///
    /// b: exposes directory /data/foo from self as /data/bar to framework (NOT realm)
    /// a: offers directory /data/bar from b as /data/baz to c, but it is not exposed via realm
    /// c: uses /data/baz as /data/hippo
    pub async fn test_use_from_expose_to_framework(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Child("b".to_string()),
                        source_name: "bar_data".into(),
                        target_name: "baz_data".into(),
                        target: OfferTarget::Child("c".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Child("b".to_string()),
                        source_name: "bar_svc".into(),
                        target_name: "baz_svc".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "bar_data".into(),
                        target: ExposeTarget::Framework,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "bar_svc".into(),
                        target: ExposeTarget::Framework,
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "baz_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "baz_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["c:0"].into(),
                CheckUse::default_directory(ExpectedResult::Err(zx::Status::UNAVAILABLE)),
            )
            .await;
        model
            .check_use(
                vec!["c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: offers directory /data/hippo to b, but a is not executable
    /// a: offers service /svc/hippo to b, but a is not executable
    /// b: uses directory /data/hippo as /data/hippo, but it's not in its realm
    /// b: uses service /svc/hippo as /svc/hippo, but it's not in its realm
    pub async fn test_offer_from_non_executable(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new_empty_component()
                    .directory(DirectoryDeclBuilder::new("hippo_data").build())
                    .protocol(ProtocolDeclBuilder::new("hippo_svc").build())
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source_name: "hippo_data".into(),
                        source: OfferSource::Self_,
                        target_name: "hippo_data".into(),
                        target: OfferTarget::Child("b".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source_name: "hippo_svc".into(),
                        source: OfferSource::Self_,
                        target_name: "hippo_svc".into(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "hippo_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::default_directory(ExpectedResult::Err(zx::Status::UNAVAILABLE)),
            )
            .await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: offers directory /data/foo from self with subdir 's1/s2'
    /// b: offers directory /data/foo from realm with subdir 's3'
    /// c: uses /data/foo as /data/hippo
    pub async fn test_use_directory_with_subdir_from_grandparent(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "foo_data".into(),
                        target: OfferTarget::Child("b".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: Some(PathBuf::from("s1/s2")),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Parent,
                        source_name: "foo_data".into(),
                        target_name: "foo_data".into(),
                        target: OfferTarget::Child("c".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: Some(PathBuf::from("s3")),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "foo_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: Some(PathBuf::from("s4")),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .create_static_file(Path::new("foo/s1/s2/s3/s4/inner"), "hello")
            .await
            .expect("failed to create file");
        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Directory {
                    path: default_directory_capability(),
                    file: PathBuf::from("inner"),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///   a
    ///  / \
    /// b   c
    ///
    ///
    /// b: exposes directory /data/foo from self with subdir 's1/s2'
    /// a: offers directory /data/foo from `b` to `c` with subdir 's3'
    /// c: uses /data/foo as /data/hippo
    pub async fn test_use_directory_with_subdir_from_sibling(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Child("b".to_string()),
                        source_name: "foo_data".into(),
                        target: OfferTarget::Child("c".to_string()),
                        target_name: "foo_data".into(),
                        rights: Some(*READ_RIGHTS),
                        subdir: Some(PathBuf::from("s3")),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_data".into(),
                        target: ExposeTarget::Parent,
                        target_name: "foo_data".into(),
                        rights: Some(*READ_RIGHTS),
                        subdir: Some(PathBuf::from("s1/s2")),
                    }))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "foo_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .create_static_file(Path::new("foo/s1/s2/s3/inner"), "hello")
            .await
            .expect("failed to create file");
        model
            .check_use(
                vec!["c:0"].into(),
                CheckUse::Directory {
                    path: default_directory_capability(),
                    file: PathBuf::from("inner"),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// c: exposes /data/foo from self
    /// b: exposes /data/foo from `c` with subdir `s1/s2`
    /// a: exposes /data/foo from `b` with subdir `s3` as /data/hippo
    /// use /data/hippo from a's exposed dir
    pub async fn test_expose_directory_with_subdir(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Child("b".to_string()),
                        source_name: "foo_data".into(),
                        target_name: "hippo_data".into(),
                        target: ExposeTarget::Parent,
                        rights: None,
                        subdir: Some(PathBuf::from("s3")),
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Child("c".to_string()),
                        source_name: "foo_data".into(),
                        target_name: "foo_data".into(),
                        target: ExposeTarget::Parent,
                        rights: None,
                        subdir: Some(PathBuf::from("s1/s2")),
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "foo_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .create_static_file(Path::new("foo/s1/s2/s3/inner"), "hello")
            .await
            .expect("failed to create file");
        model
            .check_use_exposed_dir(
                vec![].into(),
                CheckUse::Directory {
                    path: "/hippo_data".try_into().unwrap(),
                    file: PathBuf::from("inner"),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    pub async fn test_expose_from_self_and_child(&self) {
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Child("c".to_string()),
                        source_name: "hippo_data".into(),
                        target_name: "hippo_bar_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Child("c".to_string()),
                        source_name: "hippo_svc".into(),
                        target_name: "hippo_bar_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "hippo_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "hippo_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use_exposed_dir(
                vec!["b:0"].into(),
                CheckUse::Directory {
                    path: "/hippo_bar_data".try_into().unwrap(),
                    file: PathBuf::from("hippo"),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model
            .check_use_exposed_dir(
                vec!["b:0"].into(),
                CheckUse::Protocol {
                    path: "/hippo_bar_svc".try_into().unwrap(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model
            .check_use_exposed_dir(
                vec!["b:0", "c:0"].into(),
                CheckUse::Directory {
                    path: "/hippo_data".try_into().unwrap(),
                    file: PathBuf::from("hippo"),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model
            .check_use_exposed_dir(
                vec!["b:0", "c:0"].into(),
                CheckUse::Protocol {
                    path: "/hippo_svc".try_into().unwrap(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    pub async fn test_use_not_exposed(&self) {
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            ("b", ComponentDeclBuilder::new().add_lazy_child("c").build()),
            (
                "c",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .protocol(ProtocolDeclBuilder::new("foo_svc").build())
                    .expose(ExposeDecl::Directory(ExposeDirectoryDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "hippo_data".into(),
                        target: ExposeTarget::Parent,
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                    }))
                    .expose(ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Self_,
                        source_name: "foo_svc".into(),
                        target_name: "hippo_svc".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        // Capability is only exposed from "c", so it only be usable from there.

        // When trying to open a capability that's not exposed to realm, there's no node for it in the
        // exposed dir, so no routing takes place.
        model
            .check_use_exposed_dir(
                vec!["b:0"].into(),
                CheckUse::Directory {
                    path: "/hippo_data".try_into().unwrap(),
                    file: PathBuf::from("hippo"),
                    expected_res: ExpectedResult::Err(zx::Status::NOT_FOUND),
                },
            )
            .await;
        model
            .check_use_exposed_dir(
                vec!["b:0"].into(),
                CheckUse::Protocol {
                    path: "/hippo_svc".try_into().unwrap(),
                    expected_res: ExpectedResult::Err(zx::Status::NOT_FOUND),
                },
            )
            .await;
        model
            .check_use_exposed_dir(
                vec!["b:0", "c:0"].into(),
                CheckUse::Directory {
                    path: "/hippo_data".try_into().unwrap(),
                    file: PathBuf::from("hippo"),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model
            .check_use_exposed_dir(
                vec!["b:0", "c:0"].into(),
                CheckUse::Protocol {
                    path: "/hippo_svc".try_into().unwrap(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///   (cm)
    ///    |
    ///    a
    ///
    /// a: uses an invalid service from the component manager.
    pub async fn test_invalid_use_from_component_manager(&self) {
        let components = vec![(
            "a",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_name: "invalid".into(),
                    target_path: CapabilityPath::try_from("/svc/valid").unwrap(),
                    dependency_type: DependencyType::Strong,
                }))
                .build(),
        )];

        // Try and use the service. We expect a failure.
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec![].into(),
                CheckUse::Protocol {
                    path: CapabilityPath::try_from("/svc/valid").unwrap(),
                    expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
                },
            )
            .await;
    }

    ///   (cm)
    ///    |
    ///    a
    ///    |
    ///    b
    ///
    /// a: offers an invalid service from the component manager to "b".
    /// b: attempts to use the service
    pub async fn test_invalid_offer_from_component_manager(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source_name: "invalid".into(),
                        source: OfferSource::Parent,
                        target_name: "valid".into(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "valid".into(),
                        target_path: CapabilityPath::try_from("/svc/valid").unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];

        // Try and use the service. We expect a failure.
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Protocol {
                    path: CapabilityPath::try_from("/svc/valid").unwrap(),
                    expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///
    /// b: uses framework events "started", and "capability_requested"
    pub async fn test_use_event_from_framework(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        source: UseSource::Framework,
                        source_name: "capability_requested".into(),
                        target_name: "capability_requested".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        source: UseSource::Framework,
                        source_name: "started".into(),
                        target_name: "started".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        source: UseSource::Framework,
                        source_name: "resolved".into(),
                        target_name: "resolved".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::EventStream(UseEventStreamDecl {
                        name: CapabilityName::try_from("StartComponentTree").unwrap(),
                        subscriptions: vec![cm_rust::EventSubscription {
                            event_name: "resolved".into(),
                            mode: cm_rust::EventMode::Sync,
                        }],
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new("capability_requested".into(), EventMode::Sync),
                    start_component_tree: false,
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new("started".into(), EventMode::Sync),
                    start_component_tree: true,
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a; attempts to offer event "capability_requested" to b.
    pub async fn test_can_offer_capability_requested_event(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Event(OfferEventDecl {
                        source: OfferSource::Framework,
                        source_name: "capability_requested".into(),
                        target_name: "capability_requested_on_a".into(),
                        target: OfferTarget::Child("b".to_string()),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        source: UseSource::Parent,
                        source_name: "capability_requested_on_a".into(),
                        target_name: "capability_requested_from_parent".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        source: UseSource::Framework,
                        source_name: "resolved".into(),
                        target_name: "resolved".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::EventStream(UseEventStreamDecl {
                        name: CapabilityName::try_from("StartComponentTree").unwrap(),
                        subscriptions: vec![cm_rust::EventSubscription {
                            event_name: "resolved".into(),
                            mode: cm_rust::EventMode::Sync,
                        }],
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new(
                        "capability_requested_from_parent".into(),
                        EventMode::Sync,
                    ),
                    start_component_tree: true,
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///
    /// a: uses framework event "started" and offers to b as "started_on_a"
    /// b: uses framework event "started_on_a" as "started"
    pub async fn test_use_event_from_parent(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Event(OfferEventDecl {
                        source: OfferSource::Framework,
                        source_name: "started".into(),
                        target_name: "started_on_a".into(),
                        target: OfferTarget::Child("b".to_string()),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        source: UseSource::Parent,
                        source_name: "started_on_a".into(),
                        target_name: "started".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        source: UseSource::Framework,
                        source_name: "resolved".into(),
                        target_name: "resolved".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::EventStream(UseEventStreamDecl {
                        name: CapabilityName::try_from("StartComponentTree").unwrap(),
                        subscriptions: vec![cm_rust::EventSubscription {
                            event_name: "resolved".into(),
                            mode: cm_rust::EventMode::Sync,
                        }],
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new("started".into(), EventMode::Sync),
                    start_component_tree: true,
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    ///
    /// a: uses framework event "started" and offers to b as "started_on_a"
    /// a: uses framework event "stopped" and offers to b as "stopped_on_a"
    /// b: offers realm event "started_on_a" to c
    /// b: offers realm event "destroyed" from framework
    /// c: uses realm event "started_on_a"
    /// c: uses realm event "destroyed"
    /// c: uses realm event "stopped_on_a" but fails to do so
    pub async fn test_use_event_from_grandparent(&self) {
        let components = vec![
        (
            "a",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferSource::Framework,
                    source_name: "started".into(),
                    target_name: "started_on_a".into(),
                    target: OfferTarget::Child("b".to_string()),
                    filter: None,
                    mode: cm_rust::EventMode::Sync,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::Parent,
                    source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                    target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                    target: OfferTarget::Child("b".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferSource::Framework,
                    source_name: "stopped".into(),
                    target_name: "stopped_on_b".into(),
                    target: OfferTarget::Child("b".to_string()),
                    filter: None,
                    mode: cm_rust::EventMode::Sync,
                }))
                .add_lazy_child("b")
                .build(),
        ),
        (
            "b",
            ComponentDeclBuilder::new()
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferSource::Parent,
                    source_name: "started_on_a".into(),
                    target_name: "started_on_a".into(),
                    target: OfferTarget::Child("c".to_string()),
                    filter: None,
                    mode: cm_rust::EventMode::Sync,
                }))
                .offer(OfferDecl::Protocol(OfferProtocolDecl {
                    source: OfferSource::Parent,
                    source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                    target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                    target: OfferTarget::Child("c".to_string()),
                    dependency_type: DependencyType::Strong,
                }))
                .offer(OfferDecl::Event(OfferEventDecl {
                    source: OfferSource::Framework,
                    source_name: "destroyed".into(),
                    target_name: "destroyed".into(),
                    target: OfferTarget::Child("c".to_string()),
                    filter: Some(hashmap!{"path".to_string() => DictionaryValue::Str("/diagnostics".to_string())}),
                    mode: cm_rust::EventMode::Sync,
                }))
                .add_lazy_child("c")
                .build(),
        ),
        (
            "c",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    source: UseSource::Parent,
                    source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                    target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
                    dependency_type: DependencyType::Strong,
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Parent,
                    source_name: "started_on_a".into(),
                    target_name: "started".into(),
                    filter: None,
                    mode: cm_rust::EventMode::Sync,
                    dependency_type: DependencyType::Strong,

                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Parent,
                    source_name: "destroyed".into(),
                    target_name: "destroyed".into(),
                    filter: Some(hashmap!{"path".to_string() => DictionaryValue::Str("/diagnostics".to_string())}),
                    mode: cm_rust::EventMode::Sync,
                    dependency_type: DependencyType::Strong,
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Parent,
                    source_name: "stopped_on_a".into(),
                    target_name: "stopped".into(),
                    filter: None,
                    mode: cm_rust::EventMode::Sync,
                    dependency_type: DependencyType::Strong,
                }))
                .use_(UseDecl::Event(UseEventDecl {
                    source: UseSource::Framework,
                    source_name: "resolved".into(),
                    target_name: "resolved".into(),
                    filter: None,
                    mode: cm_rust::EventMode::Sync,
                    dependency_type: DependencyType::Strong,
                }))
                .use_(UseDecl::EventStream(UseEventStreamDecl {
                    name: CapabilityName::try_from("StartComponentTree").unwrap(),
                    subscriptions: vec![cm_rust::EventSubscription {
                        event_name: "resolved".into(),
                        mode: cm_rust::EventMode::Sync,
                    }],
                }))
                .build(),
        ),
    ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new("started".into(), EventMode::Sync),
                    start_component_tree: false,
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new("destroyed".into(), EventMode::Sync),
                    start_component_tree: false,
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new("stopped".into(), EventMode::Sync),
                    start_component_tree: true,
                    expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
                },
            )
            .await;
    }

    ///   a
    ///   |
    ///   b
    ///  / \
    /// c   d
    ///
    /// a: offer framework event "directory_ready" with filters "/foo", "/bar", "/baz" to b
    /// b: uses realm event "directory_ready" with filters "/foo"
    /// b: offers realm event "capabilty_ready" with filters "/foo", "/bar" to c, d
    /// c: uses realm event "directory_ready" with filters "/foo", "/bar"
    /// d: uses realm event "directory_ready" with filters "/baz" (fails)
    pub async fn test_event_filter_routing(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Event(OfferEventDecl {
                        source: OfferSource::Framework,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready".into(),
                        target: OfferTarget::Child("b".to_string()),
                        filter: Some(hashmap! {
                            "name".to_string() => DictionaryValue::StrVec(vec![
                                "foo".to_string(), "bar".to_string(), "baz".to_string()
                            ])
                        }),
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready_foo".into(),
                        filter: Some(hashmap! {
                            "name".to_string() => DictionaryValue::Str("foo".into()),
                        }),
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Framework,
                        source_name: "resolved".into(),
                        target_name: "resolved".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .use_(UseDecl::EventStream(UseEventStreamDecl {
                        name: CapabilityName::try_from("StartComponentTree").unwrap(),
                        subscriptions: vec![cm_rust::EventSubscription {
                            event_name: "resolved".into(),
                            mode: cm_rust::EventMode::Sync,
                        }],
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target: OfferTarget::Child("d".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Event(OfferEventDecl {
                        source: OfferSource::Parent,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready".into(),
                        target: OfferTarget::Child("c".to_string()),
                        filter: Some(hashmap! {
                            "name".to_string() => DictionaryValue::StrVec(vec![
                                "foo".to_string(), "bar".to_string()
                            ])
                        }),
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .offer(OfferDecl::Event(OfferEventDecl {
                        source: OfferSource::Parent,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready".into(),
                        target: OfferTarget::Child("d".to_string()),
                        filter: Some(hashmap! {
                            "name".to_string() => DictionaryValue::StrVec(vec![
                                "foo".to_string(), "bar".to_string()
                            ])
                        }),
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .add_lazy_child("c")
                    .add_lazy_child("d")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready_foo_bar".into(),
                        filter: Some(hashmap! {
                            "name".to_string() => DictionaryValue::StrVec(vec![
                                "foo".to_string(), "bar".to_string()
                            ])
                        }),
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Framework,
                        source_name: "resolved".into(),
                        target_name: "resolved".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .use_(UseDecl::EventStream(UseEventStreamDecl {
                        name: CapabilityName::try_from("StartComponentTree").unwrap(),
                        subscriptions: vec![cm_rust::EventSubscription {
                            event_name: "resolved".into(),
                            mode: cm_rust::EventMode::Sync,
                        }],
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready_baz".into(),
                        filter: Some(hashmap! {
                            "name".to_string() => DictionaryValue::Str("baz".into()),
                        }),
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Framework,
                        source_name: "resolved".into(),
                        target_name: "resolved".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .use_(UseDecl::EventStream(UseEventStreamDecl {
                        name: CapabilityName::try_from("StartComponentTree").unwrap(),
                        subscriptions: vec![cm_rust::EventSubscription {
                            event_name: "resolved".into(),
                            mode: cm_rust::EventMode::Sync,
                        }],
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new("directory_ready_foo".into(), EventMode::Sync),
                    start_component_tree: true,
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new(
                        "directory_ready_foo_bar".into(),
                        EventMode::Sync,
                    ),
                    start_component_tree: true,
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model
            .check_use(
                vec!["b:0", "d:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new("directory_ready_baz".into(), EventMode::Sync),
                    start_component_tree: true,
                    expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
                },
            )
            .await;
    }

    ///   a
    ///   |
    ///   b
    ///   |
    ///   c
    ///
    /// a: offer framework event "directory_ready" with mode "async".
    /// b: offers parent event "capabilty_ready" with mode "sync".
    /// c: uses realm event "directory_ready" with mode "sync"
    pub async fn test_event_mode_routing_failure(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Event(OfferEventDecl {
                        source: OfferSource::Framework,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready".into(),
                        target: OfferTarget::Child("b".to_string()),
                        filter: None,
                        mode: cm_rust::EventMode::Async,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Event(OfferEventDecl {
                        source: OfferSource::Parent,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready".into(),
                        target: OfferTarget::Child("c".to_string()),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Event(UseEventDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready_foo_bar".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Framework,
                        source_name: "resolved".into(),
                        target_name: "resolved".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new(
                        "directory_ready_foo_bar".into(),
                        EventMode::Sync,
                    ),
                    start_component_tree: true,
                    expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
                },
            )
            .await;
    }

    ///   a
    ///   |
    ///   b
    ///   |
    ///   c
    ///
    /// a: offer framework event "directory_ready" with mode "sync".
    /// b: offers parent event "capabilty_ready" with mode "async".
    /// c: uses realm event "directory_ready" with mode "async"
    pub async fn test_event_mode_routing_success(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Event(OfferEventDecl {
                        source: OfferSource::Framework,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready".into(),
                        target: OfferTarget::Child("b".to_string()),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Event(OfferEventDecl {
                        source: OfferSource::Parent,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready".into(),
                        target: OfferTarget::Child("c".to_string()),
                        filter: None,
                        mode: cm_rust::EventMode::Async,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Event(UseEventDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "directory_ready".into(),
                        target_name: "directory_ready_foo_bar".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Async,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "fuchsia.sys2.EventSource".try_into().unwrap(),
                        target_path: "/svc/fuchsia.sys2.EventSource".try_into().unwrap(),
                    }))
                    .use_(UseDecl::Event(UseEventDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Framework,
                        source_name: "resolved".into(),
                        target_name: "resolved".into(),
                        filter: None,
                        mode: cm_rust::EventMode::Sync,
                    }))
                    .use_(UseDecl::EventStream(UseEventStreamDecl {
                        name: CapabilityName::try_from("StartComponentTree").unwrap(),
                        subscriptions: vec![cm_rust::EventSubscription {
                            event_name: "resolved".into(),
                            mode: cm_rust::EventMode::Sync,
                        }],
                    }))
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new(
                        "directory_ready_foo_bar".into(),
                        EventMode::Async,
                    ),
                    start_component_tree: true,
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;
        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Event {
                    request: EventSubscription::new(
                        "directory_ready_foo_bar".into(),
                        EventMode::Sync,
                    ),
                    start_component_tree: true,
                    expected_res: ExpectedResult::Err(zx::Status::UNAVAILABLE),
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///
    /// b: uses service /svc/hippo as /svc/hippo.
    /// a: provides b with the service but policy prevents it.
    pub async fn test_use_protocol_denied_by_capability_policy(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDeclBuilder::new("hippo_svc").build())
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Self_,
                        source_name: "hippo_svc".into(),
                        target_name: "hippo_svc".into(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .build(),
            ),
        ];
        let mut builder = T::new("a", components);
        builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::root()),
                source_name: CapabilityName::from("hippo_svc"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            HashSet::new(),
        );

        let model = builder.build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::ACCESS_DENIED),
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///
    /// b: uses directory /data/foo as /data/bar.
    /// a: provides b with the directory but policy prevents it.
    pub async fn test_use_directory_with_alias_denied_by_capability_policy(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .directory(DirectoryDeclBuilder::new("foo_data").build())
                    .offer(OfferDecl::Directory(OfferDirectoryDecl {
                        source: OfferSource::Self_,
                        source_name: "foo_data".into(),
                        target_name: "bar_data".into(),
                        target: OfferTarget::Child("b".to_string()),
                        rights: Some(*READ_RIGHTS),
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Directory(UseDirectoryDecl {
                        source: UseSource::Parent,
                        source_name: "bar_data".into(),
                        target_path: CapabilityPath::try_from("/data/hippo").unwrap(),
                        rights: *READ_RIGHTS,
                        subdir: None,
                        dependency_type: DependencyType::Strong,
                    }))
                    .build(),
            ),
        ];
        let mut builder = T::new("a", components);
        builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::root()),
                source_name: CapabilityName::from("foo_data"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Directory,
            },
            HashSet::new(),
        );
        let model = builder.build().await;
        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::default_directory(ExpectedResult::Err(zx::Status::ACCESS_DENIED)),
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///      \
    ///       c
    /// c: uses service /svc/hippo as /svc/hippo.
    /// b: uses service /svc/hippo as /svc/hippo.
    /// a: provides b with the service policy allows it.
    /// b: provides c with the service policy does not allow it.
    pub async fn test_use_protocol_partial_chain_allowed_by_capability_policy(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDeclBuilder::new("hippo_svc").build())
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Self_,
                        source_name: "hippo_svc".into(),
                        target_name: "hippo_svc".into(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_name: "hippo_svc".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .add_lazy_child("c")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .build(),
            ),
        ];

        let mut allowlist = HashSet::new();
        allowlist.insert(AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["b:0"])));

        let mut builder = T::new("a", components);
        builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::root()),
                source_name: CapabilityName::from("hippo_svc"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            allowlist,
        );
        let model = builder.build().await;

        model
            .check_use(
                vec!["b:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;

        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::ACCESS_DENIED),
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///    /  \
    ///   c    d
    /// b: provides d with the service policy allows denies it.
    /// b: provides c with the service policy allows it.
    /// c: uses service /svc/hippo as /svc/hippo.
    /// Tests component provided caps in the middle of a path
    pub async fn test_use_protocol_component_provided_capability_policy(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDeclBuilder::new("hippo_svc").build())
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Self_,
                        source_name: "hippo_svc".into(),
                        target_name: "hippo_svc".into(),
                        target: OfferTarget::Child("b".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("b")
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_name: "hippo_svc".into(),
                        target: OfferTarget::Child("c".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .offer(OfferDecl::Protocol(OfferProtocolDecl {
                        source: OfferSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_name: "hippo_svc".into(),
                        target: OfferTarget::Child("d".to_string()),
                        dependency_type: DependencyType::Strong,
                    }))
                    .add_lazy_child("c")
                    .add_lazy_child("d")
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Parent,
                        source_name: "hippo_svc".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .build(),
            ),
        ];

        let mut allowlist = HashSet::new();
        allowlist.insert(AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["b:0"])));
        allowlist.insert(AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["b:0", "c:0"])));

        let mut builder = T::new("a", components);
        builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::root()),
                source_name: CapabilityName::from("hippo_svc"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            allowlist,
        );
        let model = builder.build().await;

        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;

        model
            .check_use(
                vec!["b:0", "d:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::ACCESS_DENIED),
                },
            )
            .await;
    }

    ///  component manager's namespace
    ///   |
    ///   a
    ///
    /// a: uses service /use_from_cm_namespace/svc/foo as foo_svc
    pub async fn test_use_from_component_manager_namespace_denied_by_policy(&self) {
        let components = vec![(
            "a",
            ComponentDeclBuilder::new()
                .use_(UseDecl::Protocol(UseProtocolDecl {
                    dependency_type: DependencyType::Strong,
                    source: UseSource::Parent,
                    source_name: "foo_svc".into(),
                    target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                }))
                .build(),
        )];
        let namespace_capabilities = vec![CapabilityDecl::Protocol(
            ProtocolDeclBuilder::new("foo_svc").path("/use_from_cm_namespace/svc/foo").build(),
        )];
        let mut builder = T::new("a", components);
        builder.set_namespace_capabilities(namespace_capabilities);
        builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: CapabilityName::from("foo_svc"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            HashSet::new(),
        );
        let model = builder.build().await;

        model.install_namespace_directory("/use_from_cm_namespace");
        model
            .check_use(
                vec![].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::ACCESS_DENIED),
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///    /  \
    ///   c    d
    /// b: provides d with the service policy allows denies it.
    /// b: provides c with the service policy allows it.
    /// c: uses service svc_allowed as /svc/hippo.
    /// a: offers services using environment.
    /// Tests component provided caps in the middle of a path
    pub async fn test_use_protocol_component_provided_debug_capability_policy_at_root_from_self(
        &self,
    ) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDeclBuilder::new("svc_allowed").build())
                    .protocol(ProtocolDeclBuilder::new("svc_not_allowed").build())
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env_a")
                            .extends(fsys::EnvironmentExtends::Realm)
                            .add_debug_registration(cm_rust::DebugRegistration::Protocol(
                                cm_rust::DebugProtocolRegistration {
                                    source_name: "svc_allowed".into(),
                                    target_name: "svc_allowed".into(),
                                    source: RegistrationSource::Self_,
                                },
                            ))
                            .add_debug_registration(cm_rust::DebugRegistration::Protocol(
                                cm_rust::DebugProtocolRegistration {
                                    source_name: "svc_not_allowed".into(),
                                    target_name: "svc_not_allowed".into(),
                                    source: RegistrationSource::Self_,
                                },
                            )),
                    )
                    .add_child(ChildDeclBuilder::new_lazy_child("b").environment("env_a"))
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("c"))
                    .add_child(ChildDeclBuilder::new_lazy_child("d"))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Debug,
                        source_name: "svc_allowed".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Debug,
                        source_name: "svc_not_allowed".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .build(),
            ),
        ];

        let mut allowlist = HashSet::new();
        allowlist.insert((AbsoluteMoniker::root(), "env_a".to_owned()));

        let mut builder = T::new("a", components);
        builder.add_debug_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::root()),
                source_name: CapabilityName::from("svc_allowed"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            allowlist,
        );
        let model = builder.build().await;

        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;

        model
            .check_use(
                vec!["b:0", "d:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::ACCESS_DENIED),
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///    /  \
    ///   c    d
    /// b: provides d with the service policy allows denies it.
    /// b: provides c with the service policy allows it.
    /// c: uses service svc_allowed as /svc/hippo.
    /// b: offers services using environment.
    /// Tests component provided debug caps in the middle of a path
    pub async fn test_use_protocol_component_provided_debug_capability_policy_from_self(&self) {
        let components = vec![
            (
                "a",
                ComponentDeclBuilder::new()
                    .add_child(ChildDeclBuilder::new_lazy_child("b"))
                    .build(),
            ),
            (
                "b",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDeclBuilder::new("svc_allowed").build())
                    .protocol(ProtocolDeclBuilder::new("svc_not_allowed").build())
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env_b")
                            .extends(fsys::EnvironmentExtends::Realm)
                            .add_debug_registration(cm_rust::DebugRegistration::Protocol(
                                cm_rust::DebugProtocolRegistration {
                                    source_name: "svc_allowed".into(),
                                    target_name: "svc_allowed".into(),
                                    source: RegistrationSource::Self_,
                                },
                            ))
                            .add_debug_registration(cm_rust::DebugRegistration::Protocol(
                                cm_rust::DebugProtocolRegistration {
                                    source_name: "svc_not_allowed".into(),
                                    target_name: "svc_not_allowed".into(),
                                    source: RegistrationSource::Self_,
                                },
                            )),
                    )
                    .add_child(ChildDeclBuilder::new_lazy_child("c").environment("env_b"))
                    .add_child(ChildDeclBuilder::new_lazy_child("d").environment("env_b"))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Debug,
                        source_name: "svc_allowed".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Debug,
                        source_name: "svc_not_allowed".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .build(),
            ),
        ];

        let mut allowlist = HashSet::new();
        allowlist.insert((AbsoluteMoniker::from(vec!["b:0"]), "env_b".to_owned()));

        let mut builder = T::new("a", components);
        builder.add_debug_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::from(vec![
                    "b:0",
                ])),
                source_name: CapabilityName::from("svc_allowed"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            allowlist,
        );
        let model = builder.build().await;

        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;

        model
            .check_use(
                vec!["b:0", "d:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::ACCESS_DENIED),
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///   / \ \
    ///  c  d  e
    /// b: provides e with the service policy which denies it.
    /// b: provides c with the service policy which allows it.
    /// c: uses service svc_allowed as /svc/hippo.
    /// b: offers services using environment.
    /// d: exposes the service to b
    /// Tests component provided debug caps in the middle of a path
    pub async fn test_use_protocol_component_provided_debug_capability_policy_from_child(&self) {
        let expose_decl_svc_allowed = ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: "svc_allowed".into(),
            target_name: "svc_allowed".into(),
            target: ExposeTarget::Parent,
        };
        let expose_decl_svc_not_allowed = ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: "svc_not_allowed".into(),
            target_name: "svc_not_allowed".into(),
            target: ExposeTarget::Parent,
        };
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env_b")
                            .extends(fsys::EnvironmentExtends::Realm)
                            .add_debug_registration(cm_rust::DebugRegistration::Protocol(
                                cm_rust::DebugProtocolRegistration {
                                    source_name: "svc_allowed".into(),
                                    target_name: "svc_allowed".into(),
                                    source: RegistrationSource::Child("d".into()),
                                },
                            ))
                            .add_debug_registration(cm_rust::DebugRegistration::Protocol(
                                cm_rust::DebugProtocolRegistration {
                                    source_name: "svc_not_allowed".into(),
                                    target_name: "svc_not_allowed".into(),
                                    source: RegistrationSource::Child("d".into()),
                                },
                            )),
                    )
                    .add_child(ChildDeclBuilder::new_lazy_child("c").environment("env_b"))
                    .add_child(ChildDeclBuilder::new_lazy_child("d").environment("env_b"))
                    .add_child(ChildDeclBuilder::new_lazy_child("e").environment("env_b"))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Debug,
                        source_name: "svc_allowed".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDeclBuilder::new("svc_allowed").build())
                    .expose(cm_rust::ExposeDecl::Protocol(expose_decl_svc_allowed))
                    .protocol(ProtocolDeclBuilder::new("svc_not_allowed").build())
                    .expose(cm_rust::ExposeDecl::Protocol(expose_decl_svc_not_allowed))
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Debug,
                        source_name: "svc_not_allowed".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .build(),
            ),
        ];

        let mut allowlist = HashSet::new();
        allowlist.insert((AbsoluteMoniker::from(vec!["b:0"]), "env_b".to_owned()));

        let mut builder = T::new("a", components);
        builder.add_debug_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::from(vec![
                    "b:0", "d:0",
                ])),
                source_name: CapabilityName::from("svc_allowed"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            allowlist,
        );
        let model = builder.build().await;

        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;

        model
            .check_use(
                vec!["b:0", "e:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::ACCESS_DENIED),
                },
            )
            .await;
    }

    ///   a
    ///    \
    ///     b
    ///    /  \
    ///   c    d
    ///         \
    ///          e
    /// b: provides d with the service policy which denies it.
    /// b: provides c with the service policy which allows it.
    /// c: uses service svc_allowed as /svc/hippo.
    /// b: offers services using environment.
    /// e: exposes the service to b
    /// Tests component provided debug caps in the middle of a path
    pub async fn test_use_protocol_component_provided_debug_capability_policy_from_grandchild(
        &self,
    ) {
        let expose_decl_svc_allowed = ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: "svc_allowed".into(),
            target_name: "svc_allowed".into(),
            target: ExposeTarget::Parent,
        };
        let expose_decl_svc_not_allowed = ExposeProtocolDecl {
            source: ExposeSource::Self_,
            source_name: "svc_not_allowed".into(),
            target_name: "svc_not_allowed".into(),
            target: ExposeTarget::Parent,
        };
        let components = vec![
            ("a", ComponentDeclBuilder::new().add_lazy_child("b").build()),
            (
                "b",
                ComponentDeclBuilder::new()
                    .add_environment(
                        EnvironmentDeclBuilder::new()
                            .name("env_b")
                            .extends(fsys::EnvironmentExtends::Realm)
                            .add_debug_registration(cm_rust::DebugRegistration::Protocol(
                                cm_rust::DebugProtocolRegistration {
                                    source_name: "svc_allowed".into(),
                                    target_name: "svc_allowed".into(),
                                    source: RegistrationSource::Child("d".into()),
                                },
                            ))
                            .add_debug_registration(cm_rust::DebugRegistration::Protocol(
                                cm_rust::DebugProtocolRegistration {
                                    source_name: "svc_not_allowed".into(),
                                    target_name: "svc_not_allowed".into(),
                                    source: RegistrationSource::Child("d".into()),
                                },
                            )),
                    )
                    .add_child(ChildDeclBuilder::new_lazy_child("c").environment("env_b"))
                    .add_child(ChildDeclBuilder::new_lazy_child("d").environment("env_b"))
                    .add_child(ChildDeclBuilder::new_lazy_child("e").environment("env_b"))
                    .build(),
            ),
            (
                "c",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Debug,
                        source_name: "svc_allowed".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .build(),
            ),
            (
                "d",
                ComponentDeclBuilder::new()
                    .use_(UseDecl::Protocol(UseProtocolDecl {
                        dependency_type: DependencyType::Strong,
                        source: UseSource::Debug,
                        source_name: "svc_not_allowed".into(),
                        target_path: CapabilityPath::try_from("/svc/hippo").unwrap(),
                    }))
                    .expose(cm_rust::ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Child("e".into()),
                        source_name: "svc_allowed".into(),
                        target_name: "svc_allowed".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .expose(cm_rust::ExposeDecl::Protocol(ExposeProtocolDecl {
                        source: ExposeSource::Child("e".into()),
                        source_name: "svc_not_allowed".into(),
                        target_name: "svc_not_allowed".into(),
                        target: ExposeTarget::Parent,
                    }))
                    .add_lazy_child("e".into())
                    .build(),
            ),
            (
                "e",
                ComponentDeclBuilder::new()
                    .protocol(ProtocolDeclBuilder::new("svc_allowed").build())
                    .expose(cm_rust::ExposeDecl::Protocol(expose_decl_svc_allowed))
                    .protocol(ProtocolDeclBuilder::new("svc_not_allowed").build())
                    .expose(cm_rust::ExposeDecl::Protocol(expose_decl_svc_not_allowed))
                    .build(),
            ),
        ];

        let mut allowlist = HashSet::new();
        allowlist.insert((AbsoluteMoniker::from(vec!["b:0"]), "env_b".to_owned()));

        let mut builder = T::new("a", components);
        builder.add_debug_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::from(vec![
                    "b:0", "d:0", "e:0",
                ])),
                source_name: CapabilityName::from("svc_allowed"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            allowlist,
        );
        let model = builder.build().await;

        model
            .check_use(
                vec!["b:0", "c:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Ok,
                },
            )
            .await;

        model
            .check_use(
                vec!["b:0", "d:0"].into(),
                CheckUse::Protocol {
                    path: default_service_capability(),
                    expected_res: ExpectedResult::Err(zx::Status::ACCESS_DENIED),
                },
            )
            .await;
    }

    ///   a
    ///  /
    /// b
    ///
    /// a: offer to b from self
    /// b: use from parent
    pub async fn test_route_service_from_parent(&self) {
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
                        source_path: "/svc/foo".try_into().unwrap(),
                    })
                    .add_lazy_child("b")
                    .build(),
            ),
            ("b", ComponentDeclBuilder::new().use_(use_decl.clone().into()).build()),
        ];
        let model = T::new("a", components).build().await;
        let b_component = model.look_up_instance(&vec!["b:0"].into()).await.expect("b instance");
        let a_component =
            model.look_up_instance(&AbsoluteMoniker::root()).await.expect("root instance");
        let source = route_capability(RouteRequest::UseService(use_decl), &b_component)
            .await
            .expect("failed to route service");
        match source {
            RouteSource::Service(CapabilitySourceInterface::<
                <<T as RoutingTestModelBuilder>::Model as RoutingTestModel>::C,
            >::Component {
                capability: ComponentCapability::Service(ServiceDecl { name, source_path }),
                component,
            }) => {
                assert_eq!(name, CapabilityName("foo".into()));
                assert_eq!(source_path, "/svc/foo".parse::<CapabilityPath>().unwrap());
                assert!(Arc::ptr_eq(&component.upgrade().unwrap(), &a_component));
            }
            _ => panic!("bad capability source"),
        };
    }

    ///   a
    ///  /
    /// b
    ///
    /// a: use from #b
    /// b: expose to parent from self
    pub async fn test_route_service_from_child(&self) {
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
                        source_path: "/svc/foo".try_into().unwrap(),
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
        let model = T::new("a", components).build().await;
        let a_component =
            model.look_up_instance(&AbsoluteMoniker::root()).await.expect("root instance");
        let b_component = model.look_up_instance(&vec!["b:0"].into()).await.expect("b instance");
        let source = route_capability(RouteRequest::UseService(use_decl), &a_component)
            .await
            .expect("failed to route service");
        match source {
            RouteSource::Service(CapabilitySourceInterface::<
                <<T as RoutingTestModelBuilder>::Model as RoutingTestModel>::C,
            >::Component {
                capability: ComponentCapability::Service(ServiceDecl { name, source_path }),
                component,
            }) => {
                assert_eq!(name, CapabilityName("foo".into()));
                assert_eq!(source_path, "/svc/foo".parse::<CapabilityPath>().unwrap());
                assert!(Arc::ptr_eq(&component.upgrade().unwrap(), &b_component));
            }
            _ => panic!("bad capability source"),
        };
    }

    ///   a
    ///  / \
    /// b   c
    ///
    /// a: offer to b from child c
    /// b: use from parent
    /// c: expose from self
    pub async fn test_route_service_from_sibling(&self) {
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
                        source_path: "/svc/foo".try_into().unwrap(),
                    })
                    .build(),
            ),
        ];
        let model = T::new("a", components).build().await;
        let b_component = model.look_up_instance(&vec!["b:0"].into()).await.expect("b instance");
        let c_component = model.look_up_instance(&vec!["c:0"].into()).await.expect("c instance");
        let source = route_capability(RouteRequest::UseService(use_decl), &b_component)
            .await
            .expect("failed to route service");

        // Verify this source comes from `c`.
        match source {
            RouteSource::Service(CapabilitySourceInterface::<
                <<T as RoutingTestModelBuilder>::Model as RoutingTestModel>::C,
            >::Component {
                capability: ComponentCapability::Service(ServiceDecl { name, source_path }),
                component,
            }) => {
                assert_eq!(name, CapabilityName("foo".into()));
                assert_eq!(source_path, "/svc/foo".parse::<CapabilityPath>().unwrap());
                assert!(Arc::ptr_eq(&component.upgrade().unwrap(), &c_component));
            }
            _ => panic!("bad capability source"),
        };
    }
}
