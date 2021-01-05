// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability::CapabilitySource,
        config::{CapabilityAllowlistKey, CapabilityAllowlistSource, RuntimeConfig},
        model::error::ModelError,
    },
    fuchsia_zircon as zx,
    log::{error, warn},
    moniker::{AbsoluteMoniker, ChildMoniker, ExtendedMoniker},
    std::sync::{Arc, Weak},
    thiserror::Error,
};
/// Errors returned by the PolicyChecker and the ScopedPolicyChecker.
#[derive(Debug, Clone, Error)]
pub enum PolicyError {
    #[error("security policy was unavailable to check")]
    PolicyUnavailable,

    #[error("security policy disallows \"{policy}\" job policy for \"{moniker}\"")]
    JobPolicyDisallowed { policy: String, moniker: AbsoluteMoniker },

    #[error("security policy was unable to extract the source from the routed capability")]
    InvalidCapabilitySource,

    #[error("security policy disallows \"{cap}\" from \"{source_moniker}\" being used at \"{target_moniker}\"")]
    CapabilityUseDisallowed {
        cap: String,
        source_moniker: ExtendedMoniker,
        target_moniker: AbsoluteMoniker,
    },
}

impl PolicyError {
    fn job_policy_disallowed(policy: impl Into<String>, moniker: &AbsoluteMoniker) -> Self {
        PolicyError::JobPolicyDisallowed { policy: policy.into(), moniker: moniker.clone() }
    }

    fn capability_use_disallowed(
        cap: impl Into<String>,
        source_moniker: &ExtendedMoniker,
        target_moniker: &AbsoluteMoniker,
    ) -> Self {
        PolicyError::CapabilityUseDisallowed {
            cap: cap.into(),
            source_moniker: source_moniker.clone(),
            target_moniker: target_moniker.clone(),
        }
    }

    /// Convert this error into its approximate `zx::Status` equivalent.
    pub fn as_zx_status(&self) -> zx::Status {
        zx::Status::UNAVAILABLE
    }
}

/// Evaluates security policy globally across the entire Model and all realms.
/// This is used to enforce runtime capability routing restrictions across all
/// realms to prevent high privilleged capabilities from being routed to
/// components outside of the list defined in the runtime configs security
/// policy.
pub struct GlobalPolicyChecker {
    /// The runtime configuration containing the security policy to apply.
    config: Arc<RuntimeConfig>,
}

impl GlobalPolicyChecker {
    /// Constructs a new PolicyChecker object configured by the
    /// RuntimeConfig::SecurityPolicy.
    pub fn new(config: Arc<RuntimeConfig>) -> Self {
        Self { config: config }
    }

    /// Absolute monikers contain instance_id. This change normalizes all
    /// incoming instance identifiers to 0 so for example
    /// /foo:1/bar:0 -> /foo:0/bar:0.
    fn strip_moniker_instance_id(moniker: &AbsoluteMoniker) -> AbsoluteMoniker {
        let mut normalized_children = Vec::with_capacity(moniker.path().len());
        for child in moniker.path().iter() {
            normalized_children.push(ChildMoniker::new(
                child.name().to_string(),
                child.collection().map(String::from),
                0,
            ));
        }
        AbsoluteMoniker::new(normalized_children)
    }

    fn get_policy_key<'a>(
        capability_source: &'a CapabilitySource,
    ) -> Result<CapabilityAllowlistKey, ModelError> {
        Ok(match &capability_source {
            CapabilitySource::Namespace { capability } => CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: capability
                    .source_name()
                    .ok_or(PolicyError::InvalidCapabilitySource)?
                    .clone(),
                source: CapabilityAllowlistSource::Self_,
                capability: capability.type_name(),
            },
            CapabilitySource::Component { capability, realm } => CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(
                    realm.upgrade()?.abs_moniker.clone(),
                ),
                source_name: capability
                    .source_name()
                    .ok_or(PolicyError::InvalidCapabilitySource)?
                    .clone(),
                source: CapabilityAllowlistSource::Self_,
                capability: capability.type_name(),
            },
            CapabilitySource::Builtin { capability } => CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: capability.source_name().clone(),
                source: CapabilityAllowlistSource::Self_,
                capability: capability.type_name(),
            },
            CapabilitySource::Framework { capability, scope_moniker } => CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(scope_moniker.clone()),
                source_name: capability.source_name().clone(),
                source: CapabilityAllowlistSource::Framework,
                capability: capability.type_name(),
            },
            CapabilitySource::Capability { source_capability, realm } => CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(
                    realm.upgrade()?.abs_moniker.clone(),
                ),
                source_name: source_capability
                    .source_name()
                    .ok_or(PolicyError::InvalidCapabilitySource)?
                    .clone(),
                source: CapabilityAllowlistSource::Capability,
                capability: source_capability.type_name(),
            },
        })
    }

    /// Returns Ok(()) if the provided capability source can be routed to the
    /// given target_moniker, else a descriptive PolicyError.
    pub fn can_route_capability<'a>(
        &self,
        capability_source: &'a CapabilitySource,
        target_moniker: &'a AbsoluteMoniker,
    ) -> Result<(), ModelError> {
        let target_moniker = Self::strip_moniker_instance_id(&target_moniker);
        let policy_key = Self::get_policy_key(capability_source).map_err(|e| {
            error!("Security policy could not generate a policy key for `{}`", capability_source);
            e
        })?;

        match self.config.security_policy.capability_policy.get(&policy_key) {
            Some(allowed_monikers) => match allowed_monikers.get(&target_moniker) {
                Some(_) => Ok(()),
                None => {
                    warn!(
                        "Security policy prevented `{}` from `{}` being routed to `{}`.",
                        policy_key.source_name, policy_key.source_moniker, target_moniker
                    );
                    Err(ModelError::PolicyError {
                        err: PolicyError::capability_use_disallowed(
                            policy_key.source_name.str(),
                            &policy_key.source_moniker,
                            &target_moniker,
                        ),
                    })
                }
            },
            None => Ok(()),
        }
    }
}

/// Evaluates security policy relative to a specific Realm (based on that Realm's AbsoluteMoniker).
pub struct ScopedPolicyChecker {
    /// The runtime configuration containing the security policy to apply.
    config: Weak<RuntimeConfig>,

    /// The absolute moniker of the realm that policy will be evaluated for.
    moniker: AbsoluteMoniker,
}

impl ScopedPolicyChecker {
    pub fn new(config: Weak<RuntimeConfig>, moniker: AbsoluteMoniker) -> Self {
        ScopedPolicyChecker { config, moniker }
    }

    // This interface is super simple for now since there's only two allowlists. In the future
    // we'll probably want a different interface than an individual function per policy item.

    pub fn ambient_mark_vmo_exec_allowed(&self) -> Result<(), PolicyError> {
        let config = self.config.upgrade().ok_or(PolicyError::PolicyUnavailable)?;
        if config.security_policy.job_policy.ambient_mark_vmo_exec.contains(&self.moniker) {
            Ok(())
        } else {
            Err(PolicyError::job_policy_disallowed("ambient_mark_vmo_exec", &self.moniker))
        }
    }

    pub fn main_process_critical_allowed(&self) -> Result<(), PolicyError> {
        let config = self.config.upgrade().ok_or(PolicyError::PolicyUnavailable)?;
        if config.security_policy.job_policy.main_process_critical.contains(&self.moniker) {
            Ok(())
        } else {
            Err(PolicyError::job_policy_disallowed("main_process_critical", &self.moniker))
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            capability::{ComponentCapability, InternalCapability},
            config::{JobPolicyAllowlists, SecurityPolicy},
            model::{
                environment::{Environment, RunnerRegistry},
                realm::Realm,
                resolver::ResolverRegistry,
            },
        },
        cm_rust::*,
        matches::assert_matches,
        moniker::ChildMoniker,
        std::{collections::HashMap, collections::HashSet, iter::FromIterator, sync::Arc},
    };

    /// Creates a RuntimeConfig based on the capability allowlist entries provided during
    /// construction.
    struct CapabilityAllowlistConfigBuilder {
        capability_policy: HashMap<CapabilityAllowlistKey, HashSet<AbsoluteMoniker>>,
    }

    impl CapabilityAllowlistConfigBuilder {
        pub fn new() -> Self {
            Self { capability_policy: HashMap::new() }
        }

        /// Add a new entry to the configuration.
        pub fn add<'a>(
            &'a mut self,
            key: CapabilityAllowlistKey,
            value: Vec<AbsoluteMoniker>,
        ) -> &'a mut Self {
            let value_set = HashSet::from_iter(value.iter().cloned());
            self.capability_policy.insert(key, value_set);
            self
        }

        /// Creates a configuration from the provided policies.
        pub fn build(&self) -> Arc<RuntimeConfig> {
            let config = Arc::new(RuntimeConfig {
                security_policy: SecurityPolicy {
                    job_policy: JobPolicyAllowlists {
                        ambient_mark_vmo_exec: vec![],
                        main_process_critical: vec![],
                    },
                    capability_policy: self.capability_policy.clone(),
                },
                ..Default::default()
            });
            config
        }
    }

    #[test]
    fn scoped_policy_checker_vmex() {
        macro_rules! assert_vmex_allowed_matches {
            ($config:expr, $moniker:expr, $expected:pat) => {
                let result = ScopedPolicyChecker::new($config.clone(), $moniker.clone())
                    .ambient_mark_vmo_exec_allowed();
                assert_matches!(result, $expected);
            };
        }
        macro_rules! assert_vmex_disallowed {
            ($config:expr, $moniker:expr) => {
                assert_vmex_allowed_matches!(
                    $config,
                    $moniker,
                    Err(PolicyError::JobPolicyDisallowed { .. })
                );
            };
        }
        let strong_config = Arc::new(RuntimeConfig::default());
        let config = Arc::downgrade(&strong_config);
        assert_vmex_disallowed!(config, AbsoluteMoniker::root());
        assert_vmex_disallowed!(config, AbsoluteMoniker::from(vec!["foo:0"]));

        let allowed1 = AbsoluteMoniker::from(vec!["foo:0", "bar:0"]);
        let allowed2 = AbsoluteMoniker::from(vec!["baz:0", "fiz:0"]);
        let strong_config = Arc::new(RuntimeConfig {
            security_policy: SecurityPolicy {
                job_policy: JobPolicyAllowlists {
                    ambient_mark_vmo_exec: vec![allowed1.clone(), allowed2.clone()],
                    main_process_critical: vec![allowed1.clone(), allowed2.clone()],
                },
                capability_policy: HashMap::new(),
            },
            ..Default::default()
        });
        let config = Arc::downgrade(&strong_config);
        assert_vmex_allowed_matches!(config, allowed1, Ok(()));
        assert_vmex_allowed_matches!(config, allowed2, Ok(()));
        assert_vmex_disallowed!(config, AbsoluteMoniker::root());
        assert_vmex_disallowed!(config, allowed1.parent().unwrap());
        assert_vmex_disallowed!(config, allowed1.child(ChildMoniker::from("baz:0")));

        drop(strong_config);
        assert_vmex_allowed_matches!(config, allowed1, Err(PolicyError::PolicyUnavailable));
        assert_vmex_allowed_matches!(config, allowed2, Err(PolicyError::PolicyUnavailable));
    }

    #[test]
    fn scoped_policy_checker_critical_allowed() {
        macro_rules! assert_critical_allowed_matches {
            ($config:expr, $moniker:expr, $expected:pat) => {
                let result = ScopedPolicyChecker::new($config.clone(), $moniker.clone())
                    .main_process_critical_allowed();
                assert_matches!(result, $expected);
            };
        }
        macro_rules! assert_critical_disallowed {
            ($config:expr, $moniker:expr) => {
                assert_critical_allowed_matches!(
                    $config,
                    $moniker,
                    Err(PolicyError::JobPolicyDisallowed { .. })
                );
            };
        }
        let strong_config = Arc::new(RuntimeConfig::default());
        let config = Arc::downgrade(&strong_config);
        assert_critical_disallowed!(config, AbsoluteMoniker::root());
        assert_critical_disallowed!(config, AbsoluteMoniker::from(vec!["foo:0"]));

        let allowed1 = AbsoluteMoniker::from(vec!["foo:0", "bar:0"]);
        let allowed2 = AbsoluteMoniker::from(vec!["baz:0", "fiz:0"]);
        let strong_config = Arc::new(RuntimeConfig {
            security_policy: SecurityPolicy {
                job_policy: JobPolicyAllowlists {
                    ambient_mark_vmo_exec: vec![allowed1.clone(), allowed2.clone()],
                    main_process_critical: vec![allowed1.clone(), allowed2.clone()],
                },
                capability_policy: HashMap::new(),
            },
            ..Default::default()
        });
        let config = Arc::downgrade(&strong_config);
        assert_critical_allowed_matches!(config, allowed1, Ok(()));
        assert_critical_allowed_matches!(config, allowed2, Ok(()));
        assert_critical_disallowed!(config, AbsoluteMoniker::root());
        assert_critical_disallowed!(config, allowed1.parent().unwrap());
        assert_critical_disallowed!(config, allowed1.child(ChildMoniker::from("baz:0")));

        drop(strong_config);
        assert_critical_allowed_matches!(config, allowed1, Err(PolicyError::PolicyUnavailable));
        assert_critical_allowed_matches!(config, allowed2, Err(PolicyError::PolicyUnavailable));
    }

    #[test]
    fn global_policy_checker_can_route_capability_framework_cap() {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::from(vec![
                    "foo:0", "bar:0",
                ])),
                source_name: CapabilityName::from("running"),
                source: CapabilityAllowlistSource::Framework,
                capability: CapabilityTypeName::Event,
            },
            vec![
                AbsoluteMoniker::from(vec!["foo:0", "bar:0"]),
                AbsoluteMoniker::from(vec!["foo:0", "bar:0", "baz:0"]),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());

        let event_capability = CapabilitySource::Framework {
            capability: InternalCapability::Event(CapabilityName::from("running")),
            scope_moniker: AbsoluteMoniker::from(vec!["foo:0", "bar:0"]),
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
    }

    #[test]
    fn global_policy_checker_can_route_capability_namespace_cap() {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: CapabilityName::from("fuchsia.kernel.RootResource"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            vec![
                AbsoluteMoniker::from(vec!["root:0"]),
                AbsoluteMoniker::from(vec!["root:0", "bootstrap:0"]),
                AbsoluteMoniker::from(vec!["root:0", "core:0"]),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());

        let protocol_capability = CapabilitySource::Namespace {
            capability: ComponentCapability::Protocol(ProtocolDecl {
                name: "fuchsia.kernel.RootResource".into(),
                source_path: "/svc/fuchsia.kernel.RootResource".parse().unwrap(),
            }),
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
    }

    #[test]
    fn global_policy_checker_can_route_capability_component_cap() {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::from(vec![
                    "foo:0",
                ])),
                source_name: CapabilityName::from("fuchsia.foo.FooBar"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Protocol,
            },
            vec![
                AbsoluteMoniker::from(vec!["foo:0"]),
                AbsoluteMoniker::from(vec!["root:0", "bootstrap:0"]),
                AbsoluteMoniker::from(vec!["root:0", "core:0"]),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());

        // Create a fake realm.
        let resolver = ResolverRegistry::new();
        let root_component_url = "test:///foo".to_string();
        let mut realm = Realm::new_root_realm(
            Environment::new_root(RunnerRegistry::default(), resolver),
            Weak::new(),
            Weak::new(),
            root_component_url,
        );
        realm.abs_moniker = AbsoluteMoniker::from(vec!["foo:0"]);
        let realm = Arc::new(realm);
        let weak_realm = realm.as_weak();

        let protocol_capability = CapabilitySource::Component {
            capability: ComponentCapability::Protocol(ProtocolDecl {
                name: "fuchsia.foo.FooBar".into(),
                source_path: "/svc/fuchsia.foo.FooBar".parse().unwrap(),
            }),
            realm: weak_realm,
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
    }

    #[test]
    fn global_policy_checker_can_route_capability_capability_cap() {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentInstance(AbsoluteMoniker::from(vec![
                    "foo:0",
                ])),
                source_name: CapabilityName::from("cache"),
                source: CapabilityAllowlistSource::Capability,
                capability: CapabilityTypeName::Storage,
            },
            vec![
                AbsoluteMoniker::from(vec!["foo:0"]),
                AbsoluteMoniker::from(vec!["root:0", "bootstrap:0"]),
                AbsoluteMoniker::from(vec!["root:0", "core:0"]),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());

        // Create a fake realm.
        let resolver = ResolverRegistry::new();
        let root_component_url = "test:///foo".to_string();
        let mut realm = Realm::new_root_realm(
            Environment::new_root(RunnerRegistry::default(), resolver),
            Weak::new(),
            Weak::new(),
            root_component_url,
        );
        realm.abs_moniker = AbsoluteMoniker::from(vec!["foo:0"]);
        let realm = Arc::new(realm);
        let weak_realm = realm.as_weak();

        let protocol_capability = CapabilitySource::Capability {
            source_capability: ComponentCapability::Storage(StorageDecl {
                backing_dir: "/cache".into(),
                name: "cache".into(),
                source: StorageDirectorySource::Parent,
                subdir: None,
            }),
            realm: weak_realm,
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
    }

    #[test]
    fn global_policy_checker_can_route_capability_builtin_cap() {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: CapabilityName::from("hub"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Directory,
            },
            vec![
                AbsoluteMoniker::from(vec!["root:0"]),
                AbsoluteMoniker::from(vec!["root:0", "core:0"]),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());

        let dir_capability = CapabilitySource::Builtin {
            capability: InternalCapability::Directory(CapabilityName::from("hub")),
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
    }

    #[test]
    fn global_policy_checker_can_route_capability_with_instance_ids_cap() {
        let mut config_builder = CapabilityAllowlistConfigBuilder::new();
        config_builder.add(
            CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: CapabilityName::from("hub"),
                source: CapabilityAllowlistSource::Self_,
                capability: CapabilityTypeName::Directory,
            },
            vec![
                AbsoluteMoniker::from(vec!["root:0"]),
                AbsoluteMoniker::from(vec!["root:0", "core:0"]),
            ],
        );
        let global_policy_checker = GlobalPolicyChecker::new(config_builder.build());

        let dir_capability = CapabilitySource::Builtin {
            capability: InternalCapability::Directory(CapabilityName::from("hub")),
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
    }
}
