// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    cm_rust::{
        CapabilityName, CapabilityTypeName, ProtocolDecl, StorageDecl, StorageDirectorySource,
    },
    fidl_fuchsia_sys2 as fsys,
    matches::assert_matches,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase, ExtendedMoniker},
    routing::{
        capability_source::{CapabilitySourceInterface, ComponentCapability, InternalCapability},
        component_instance::ComponentInstanceInterface,
        config::{
            AllowlistEntry, CapabilityAllowlistKey, CapabilityAllowlistSource,
            ChildPolicyAllowlists, JobPolicyAllowlists, RuntimeConfig, SecurityPolicy,
        },
        policy::GlobalPolicyChecker,
    },
    std::{
        collections::{HashMap, HashSet},
        iter::FromIterator,
        sync::{Arc, Weak},
    },
};

/// These GlobalPolicyChecker tests are run under multiple contexts, e.g. both on Fuchsia under
/// component_manager and on the build host under cm_fidl_analyzer. This macro helps ensure that all
/// tests are run in each context.
#[macro_export]
macro_rules! instantiate_global_policy_checker_tests {
    ($fixture_impl:path) => {
        // New GlobalPolicyCheckerTest tests must be added to this list to run.
        instantiate_global_policy_checker_tests! {
            $fixture_impl,
            global_policy_checker_can_route_capability_framework_cap,
            global_policy_checker_can_route_capability_namespace_cap,
            global_policy_checker_can_route_capability_component_cap,
            global_policy_checker_can_route_capability_capability_cap,
            global_policy_checker_can_route_debug_capability_capability_cap,
            global_policy_checker_can_route_capability_builtin_cap,
            global_policy_checker_can_route_capability_with_instance_ids_cap,
            global_policy_checker_can_route_capability_with_realm_allowlist_entry,
            global_policy_checker_can_route_capability_with_collection_allowlist_entry,
        }
    };
    ($fixture_impl:path, $test:ident, $($remaining:ident),+ $(,)?) => {
        instantiate_global_policy_checker_tests! { $fixture_impl, $test }
        instantiate_global_policy_checker_tests! { $fixture_impl, $($remaining),+ }
    };
    ($fixture_impl:path, $test:ident) => {
        #[test]
        fn $test() -> Result<(), Error> {
            <$fixture_impl as Default>::default().$test()
        }
    };
}

// Tests `GlobalPolicyChecker` for implementations of `ComponentInstanceInterface`.
pub trait GlobalPolicyCheckerTest<C>
where
    C: ComponentInstanceInterface,
{
    // Creates a `ComponentInstanceInterface` with the given `AbsoluteMoniker`.
    fn make_component(&self, abs_moniker: AbsoluteMoniker) -> Arc<C>;

    // Tests `GlobalPolicyChecker::can_route_capability()` for framework capability sources.
    fn global_policy_checker_can_route_capability_framework_cap(&self) -> Result<(), Error> {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::from(vec![
                    "foo:0", "bar:0",
                ])),
                source_name: CapabilityName::from("running"),
                source: CapabilityAllowlistSource::Framework,
                capability: CapabilityTypeName::Event,
            },
            vec![
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["foo:0", "bar:0"])),
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["foo:0", "bar:0", "baz:0"])),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());
        let component = self.make_component(vec!["foo:0", "bar:0"].into());

        let event_capability = CapabilitySourceInterface::<C>::Framework {
            capability: InternalCapability::Event(CapabilityName::from("running")),
            component: component.as_weak(),
        };
        let valid_path_0 = AbsoluteMoniker::from(vec!["foo:0", "bar:0"]);
        let valid_path_1 = AbsoluteMoniker::from(vec!["foo:0", "bar:0", "baz:0"]);
        let invalid_path_0 = AbsoluteMoniker::from(vec!["foobar:0"]);
        let invalid_path_1 = AbsoluteMoniker::from(vec!["foo:0", "bar:0", "foobar:0"]);

        assert_matches!(
            global_policy_checker.can_route_capability(&event_capability, &valid_path_0),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&event_capability, &valid_path_1),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&event_capability, &invalid_path_0),
            Err(_)
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&event_capability, &invalid_path_1),
            Err(_)
        );
        Ok(())
    }

    // Tests `GlobalPolicyChecker::can_route_capability()` for namespace capability sources.
    fn global_policy_checker_can_route_capability_namespace_cap(&self) -> Result<(), Error> {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: CapabilityName::from("fuchsia.kernel.RootResource"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            vec![
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["root:0"])),
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["root:0", "bootstrap:0"])),
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["root:0", "core:0"])),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());

        let protocol_capability = CapabilitySourceInterface::<C>::Namespace {
            capability: ComponentCapability::Protocol(ProtocolDecl {
                name: "fuchsia.kernel.RootResource".into(),
                source_path: Some("/svc/fuchsia.kernel.RootResource".parse().unwrap()),
            }),
            top_instance: Weak::new(),
        };
        let valid_path_0 = AbsoluteMoniker::from(vec!["root:0"]);
        let valid_path_1 = AbsoluteMoniker::from(vec!["root:0", "bootstrap:0"]);
        let valid_path_2 = AbsoluteMoniker::from(vec!["root:0", "core:0"]);
        let invalid_path_0 = AbsoluteMoniker::from(vec!["foobar:0"]);
        let invalid_path_1 = AbsoluteMoniker::from(vec!["foo:0", "bar:0", "foobar:0"]);

        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &valid_path_0),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &valid_path_1),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &valid_path_2),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &invalid_path_0),
            Err(_)
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &invalid_path_1),
            Err(_)
        );
        Ok(())
    }

    // Tests `GlobalPolicyChecker::can_route_capability()` for component capability sources.
    fn global_policy_checker_can_route_capability_component_cap(&self) -> Result<(), Error> {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::from(vec![
                    "foo:0",
                ])),
                source_name: CapabilityName::from("fuchsia.foo.FooBar"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            vec![
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["foo:0"])),
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["root:0", "bootstrap:0"])),
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["root:0", "core:0"])),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());
        let component = self.make_component(vec!["foo:0"].into());

        let protocol_capability = CapabilitySourceInterface::<C>::Component {
            capability: ComponentCapability::Protocol(ProtocolDecl {
                name: "fuchsia.foo.FooBar".into(),
                source_path: Some("/svc/fuchsia.foo.FooBar".parse().unwrap()),
            }),
            component: component.as_weak(),
        };
        let valid_path_0 = AbsoluteMoniker::from(vec!["root:0", "bootstrap:0"]);
        let valid_path_1 = AbsoluteMoniker::from(vec!["root:0", "core:0"]);
        let invalid_path_0 = AbsoluteMoniker::from(vec!["foobar:0"]);
        let invalid_path_1 = AbsoluteMoniker::from(vec!["foo:0", "bar:0", "foobar:0"]);

        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &valid_path_0),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &valid_path_1),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &invalid_path_0),
            Err(_)
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &invalid_path_1),
            Err(_)
        );
        Ok(())
    }

    // Tests `GlobalPolicyChecker::can_route_capability()` for capability sources of type `Capability`.
    fn global_policy_checker_can_route_capability_capability_cap(&self) -> Result<(), Error> {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::from(vec![
                    "foo:0",
                ])),
                source_name: CapabilityName::from("cache"),
                source: CapabilityAllowlistSource::Capability,
                capability: CapabilityTypeName::Storage,
            },
            vec![
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["foo:0"])),
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["root:0", "bootstrap:0"])),
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["root:0", "core:0"])),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());
        let component = self.make_component(vec!["foo:0"].into());

        let protocol_capability = CapabilitySourceInterface::<C>::Capability {
            source_capability: ComponentCapability::Storage(StorageDecl {
                backing_dir: "/cache".into(),
                name: "cache".into(),
                source: StorageDirectorySource::Parent,
                subdir: None,
                storage_id: fsys::StorageId::StaticInstanceIdOrMoniker,
            }),
            component: component.as_weak(),
        };
        let valid_path_0 = AbsoluteMoniker::from(vec!["root:0", "bootstrap:0"]);
        let valid_path_1 = AbsoluteMoniker::from(vec!["root:0", "core:0"]);
        let invalid_path_0 = AbsoluteMoniker::from(vec!["foobar:0"]);
        let invalid_path_1 = AbsoluteMoniker::from(vec!["foo:0", "bar:0", "foobar:0"]);

        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &valid_path_0),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &valid_path_1),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &invalid_path_0),
            Err(_)
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&protocol_capability, &invalid_path_1),
            Err(_)
        );
        Ok(())
    }

    // Tests `GlobalPolicyChecker::can_route_debug_capability()` for capability sources of type `Capability`.
    fn global_policy_checker_can_route_debug_capability_capability_cap(&self) -> Result<(), Error> {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add_debug_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::from(vec![
                    "foo:0",
                ])),
                source_name: CapabilityName::from("debug_service1"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            vec![
                (AbsoluteMoniker::from(vec!["foo:0"]), "foo_env".to_string()),
                (AbsoluteMoniker::from(vec!["root:0", "bootstrap:0"]), "bootstrap_env".to_string()),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());
        let component = self.make_component(vec!["foo:0"].into());

        let protocol_capability = CapabilitySourceInterface::<C>::Component {
            capability: ComponentCapability::Protocol(ProtocolDecl {
                name: "debug_service1".into(),
                source_path: Some("/svc/debug_service1".parse().unwrap()),
            }),
            component: component.as_weak(),
        };
        let valid_0 =
            (AbsoluteMoniker::from(vec!["root:0", "bootstrap:0"]), "bootstrap_env".to_string());
        let valid_1 = (AbsoluteMoniker::from(vec!["foo:0"]), "foo_env".to_string());
        let invalid_0 = (AbsoluteMoniker::from(vec!["foobar:0"]), "foobar_env".to_string());
        let invalid_1 =
            (AbsoluteMoniker::from(vec!["foo:0", "bar:0", "foobar:0"]), "foobar_env".to_string());
        let target_moniker = AbsoluteMoniker::from(vec!["target:0"]);

        assert_matches!(
            global_policy_checker.can_route_debug_capability(
                &protocol_capability,
                &valid_0.0,
                &valid_0.1,
                &target_moniker
            ),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_debug_capability(
                &protocol_capability,
                &valid_1.0,
                &valid_1.1,
                &target_moniker
            ),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_debug_capability(
                &protocol_capability,
                &invalid_0.0,
                &invalid_0.1,
                &target_moniker
            ),
            Err(_)
        );
        assert_matches!(
            global_policy_checker.can_route_debug_capability(
                &protocol_capability,
                &invalid_1.0,
                &invalid_1.1,
                &target_moniker
            ),
            Err(_)
        );
        Ok(())
    }

    // Tests `GlobalPolicyChecker::can_route_capability()` for builtin capabilities.
    fn global_policy_checker_can_route_capability_builtin_cap(&self) -> Result<(), Error> {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: CapabilityName::from("hub"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Directory,
            },
            vec![
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["root:0"])),
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["root:0", "core:0"])),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());

        let dir_capability = CapabilitySourceInterface::<C>::Builtin {
            capability: InternalCapability::Directory(CapabilityName::from("hub")),
            top_instance: Weak::new(),
        };
        let valid_path_0 = AbsoluteMoniker::from(vec!["root:0"]);
        let valid_path_1 = AbsoluteMoniker::from(vec!["root:0", "core:0"]);
        let invalid_path_0 = AbsoluteMoniker::from(vec!["foobar:0"]);
        let invalid_path_1 = AbsoluteMoniker::from(vec!["foo:0", "bar:0", "foobar:0"]);

        assert_matches!(
            global_policy_checker.can_route_capability(&dir_capability, &valid_path_0),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&dir_capability, &valid_path_1),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&dir_capability, &invalid_path_0),
            Err(_)
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&dir_capability, &invalid_path_1),
            Err(_)
        );
        Ok(())
    }

    // Tests `GlobalPolicyChecker::can_route_capability()` when component monikers contain
    // nonzero instance IDs.
    fn global_policy_checker_can_route_capability_with_instance_ids_cap(
        &self,
    ) -> Result<(), Error> {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: CapabilityName::from("hub"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Directory,
            },
            vec![
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["root:0"])),
                AllowlistEntry::Exact(AbsoluteMoniker::from(vec!["root:0", "core:0"])),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());
        let dir_capability = CapabilitySourceInterface::<C>::Builtin {
            capability: InternalCapability::Directory(CapabilityName::from("hub")),
            top_instance: Weak::new(),
        };
        let valid_path_0 = AbsoluteMoniker::from(vec!["root:1"]);
        let valid_path_1 = AbsoluteMoniker::from(vec!["root:5", "core:3"]);
        let invalid_path_0 = AbsoluteMoniker::from(vec!["foobar:0"]);
        let invalid_path_1 = AbsoluteMoniker::from(vec!["foo:0", "bar:2", "foobar:0"]);

        assert_matches!(
            global_policy_checker.can_route_capability(&dir_capability, &valid_path_0),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&dir_capability, &valid_path_1),
            Ok(())
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&dir_capability, &invalid_path_0),
            Err(_)
        );
        assert_matches!(
            global_policy_checker.can_route_capability(&dir_capability, &invalid_path_1),
            Err(_)
        );
        Ok(())
    }

    // Tests `GlobalPolicyChecker::can_route_capability()` for policy that includes non-exact
    // `AllowlistEntry::Realm` entries.
    fn global_policy_checker_can_route_capability_with_realm_allowlist_entry(
        &self,
    ) -> Result<(), Error> {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: CapabilityName::from("fuchsia.kernel.RootResource"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            vec![
                AllowlistEntry::Realm(AbsoluteMoniker::from(vec!["tests:0"])),
                AllowlistEntry::Realm(AbsoluteMoniker::from(vec!["core:0", "tests:0"])),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());
        let protocol_capability = CapabilitySourceInterface::<C>::Namespace {
            capability: ComponentCapability::Protocol(ProtocolDecl {
                name: "fuchsia.kernel.RootResource".into(),
                source_path: Some("/svc/fuchsia.kernel.RootResource".parse().unwrap()),
            }),
            top_instance: Weak::new(),
        };

        macro_rules! can_route {
            ($moniker:expr) => {
                global_policy_checker.can_route_capability(&protocol_capability, $moniker)
            };
        }

        assert!(can_route!(&AbsoluteMoniker::from(vec!["tests:0", "test1:0"])).is_ok());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["tests:0", "coll:test1:0"])).is_ok());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["tests:0", "test1:0", "util:0"])).is_ok());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["tests:0", "test2:0"])).is_ok());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["core:0", "tests:0", "test:0"])).is_ok());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["core:0", "tests:0", "coll:t:0"])).is_ok());

        assert!(can_route!(&AbsoluteMoniker::from(vec!["foo:0"])).is_err());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["tests:0"])).is_err());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["core:0", "foo:0"])).is_err());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["core:0", "tests:0"])).is_err());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["core:0", "tests:test:0"])).is_err());
        Ok(())
    }

    // Tests `GlobalPolicyChecker::can_route_capability()` for policy that includes non-exact
    // `AllowlistEntry::Collection` entries.
    fn global_policy_checker_can_route_capability_with_collection_allowlist_entry(
        &self,
    ) -> Result<(), Error> {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add_capability_policy(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: CapabilityName::from("fuchsia.kernel.RootResource"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            vec![
                AllowlistEntry::Collection(AbsoluteMoniker::root(), "tests".into()),
                AllowlistEntry::Collection(AbsoluteMoniker::from(vec!["core:0"]), "tests".into()),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());
        let protocol_capability = CapabilitySourceInterface::<C>::Namespace {
            capability: ComponentCapability::Protocol(ProtocolDecl {
                name: "fuchsia.kernel.RootResource".into(),
                source_path: Some("/svc/fuchsia.kernel.RootResource".parse().unwrap()),
            }),
            top_instance: Weak::new(),
        };

        macro_rules! can_route {
            ($moniker:expr) => {
                global_policy_checker.can_route_capability(&protocol_capability, $moniker)
            };
        }

        assert!(can_route!(&AbsoluteMoniker::from(vec!["tests:t1:0"])).is_ok());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["tests:t2:0"])).is_ok());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["tests:t1:0", "util:0"])).is_ok());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["core:0", "tests:t1:0"])).is_ok());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["core:0", "tests:t2:0"])).is_ok());

        assert!(can_route!(&AbsoluteMoniker::from(vec!["foo:0"])).is_err());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["tests:0"])).is_err());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["coll:foo:0"])).is_err());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["core:0", "foo:0"])).is_err());
        assert!(can_route!(&AbsoluteMoniker::from(vec!["core:0", "coll:tests:0"])).is_err());
        Ok(())
    }
}

// Creates a RuntimeConfig based on the capability allowlist entries provided during
// construction.
struct CapabilityAllowlistConfigBuilder {
    capability_policy: HashMap<CapabilityAllowlistKey, HashSet<AllowlistEntry>>,
    debug_capability_policy: HashMap<CapabilityAllowlistKey, HashSet<(AbsoluteMoniker, String)>>,
}

impl CapabilityAllowlistConfigBuilder {
    pub fn new() -> Self {
        Self { capability_policy: HashMap::new(), debug_capability_policy: HashMap::new() }
    }

    /// Add a new entry to the configuration.
    pub fn add_capability_policy<'a>(
        &'a mut self,
        key: CapabilityAllowlistKey,
        value: Vec<AllowlistEntry>,
    ) -> &'a mut Self {
        let value_set = HashSet::from_iter(value.iter().cloned());
        self.capability_policy.insert(key, value_set);
        self
    }

    /// Add a new entry to the configuration.
    pub fn add_debug_capability_policy<'a>(
        &'a mut self,
        key: CapabilityAllowlistKey,
        value: Vec<(AbsoluteMoniker, String)>,
    ) -> &'a mut Self {
        let value_set = HashSet::from_iter(value.iter().cloned());
        self.debug_capability_policy.insert(key, value_set);
        self
    }

    /// Creates a configuration from the provided policies.
    pub fn build(&self) -> Arc<RuntimeConfig> {
        let config = Arc::new(RuntimeConfig {
            security_policy: SecurityPolicy {
                job_policy: JobPolicyAllowlists {
                    ambient_mark_vmo_exec: vec![],
                    main_process_critical: vec![],
                    create_raw_processes: vec![],
                },
                capability_policy: self.capability_policy.clone(),
                debug_capability_policy: self.debug_capability_policy.clone(),
                child_policy: ChildPolicyAllowlists { reboot_on_terminate: vec![] },
            },
            ..Default::default()
        });
        config
    }
}
