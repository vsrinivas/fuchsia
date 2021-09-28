// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        capability_source::CapabilitySourceInterface,
        component_instance::ComponentInstanceInterface,
        config::{
            AllowlistEntry, CapabilityAllowlistKey, CapabilityAllowlistSource, RuntimeConfig,
        },
    },
    fuchsia_zircon_status as zx,
    log::{error, warn},
    moniker::{
        AbsoluteMoniker, AbsoluteMonikerBase, ChildMoniker, ChildMonikerBase, ExtendedMoniker,
        PartialAbsoluteMoniker, RelativeMoniker, RelativeMonikerBase,
    },
    std::sync::{Arc, Weak},
    thiserror::Error,
};

#[cfg(feature = "serde")]
use serde::{Deserialize, Serialize};

/// Errors returned by the PolicyChecker and the ScopedPolicyChecker.
#[cfg_attr(feature = "serde", derive(Deserialize, Serialize), serde(rename_all = "snake_case"))]
#[derive(Debug, Clone, Error, PartialEq)]
pub enum PolicyError {
    #[error("security policy was unavailable to check")]
    PolicyUnavailable,

    #[error("feature \"{policy}\" used by \"{moniker}\" is not supported by this instance of component manager")]
    Unsupported { policy: String, moniker: PartialAbsoluteMoniker },

    #[error("security policy disallows \"{policy}\" job policy for \"{moniker}\"")]
    JobPolicyDisallowed { policy: String, moniker: PartialAbsoluteMoniker },

    #[error("security policy disallows \"{policy}\" child policy for \"{moniker}\"")]
    ChildPolicyDisallowed { policy: String, moniker: PartialAbsoluteMoniker },

    #[error("security policy was unable to extract the source from the routed capability")]
    InvalidCapabilitySource,

    #[error("security policy disallows \"{cap}\" from \"{source_moniker}\" being used at \"{target_moniker}\"")]
    CapabilityUseDisallowed {
        cap: String,
        source_moniker: ExtendedMoniker,
        target_moniker: PartialAbsoluteMoniker,
    },

    #[error("debug security policy disallows \"{cap}\" from \"{source_moniker}\" being routed from environment \"{env_moniker}:{env_name}\" to \"{target_moniker}\"")]
    DebugCapabilityUseDisallowed {
        cap: String,
        source_moniker: ExtendedMoniker,
        env_moniker: PartialAbsoluteMoniker,
        env_name: String,
        target_moniker: PartialAbsoluteMoniker,
    },
}

impl PolicyError {
    fn unsupported(policy: impl Into<String>, moniker: &PartialAbsoluteMoniker) -> Self {
        PolicyError::Unsupported { policy: policy.into(), moniker: moniker.clone() }
    }

    fn job_policy_disallowed(policy: impl Into<String>, moniker: &PartialAbsoluteMoniker) -> Self {
        PolicyError::JobPolicyDisallowed { policy: policy.into(), moniker: moniker.clone() }
    }

    fn child_policy_disallowed(
        policy: impl Into<String>,
        moniker: &PartialAbsoluteMoniker,
    ) -> Self {
        PolicyError::ChildPolicyDisallowed { policy: policy.into(), moniker: moniker.clone() }
    }

    fn capability_use_disallowed(
        cap: impl Into<String>,
        source_moniker: &ExtendedMoniker,
        target_moniker: &PartialAbsoluteMoniker,
    ) -> Self {
        PolicyError::CapabilityUseDisallowed {
            cap: cap.into(),
            source_moniker: source_moniker.clone(),
            target_moniker: target_moniker.clone(),
        }
    }

    fn debug_capability_use_disallowed(
        cap: impl Into<String>,
        source_moniker: &ExtendedMoniker,
        env_moniker: &PartialAbsoluteMoniker,
        env_name: impl Into<String>,
        target_moniker: &PartialAbsoluteMoniker,
    ) -> Self {
        PolicyError::DebugCapabilityUseDisallowed {
            cap: cap.into(),
            source_moniker: source_moniker.clone(),
            env_moniker: env_moniker.clone(),
            env_name: env_name.into(),
            target_moniker: target_moniker.clone(),
        }
    }

    /// Convert this error into its approximate `zx::Status` equivalent.
    pub fn as_zx_status(&self) -> zx::Status {
        zx::Status::ACCESS_DENIED
    }
}

/// Evaluates security policy globally across the entire Model and all components.
/// This is used to enforce runtime capability routing restrictions across all
/// components to prevent high privilleged capabilities from being routed to
/// components outside of the list defined in the runtime configs security
/// policy.
#[derive(Clone, Debug)]
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

    fn get_policy_key<'a, C>(
        capability_source: &'a CapabilitySourceInterface<C>,
    ) -> Result<CapabilityAllowlistKey, PolicyError>
    where
        C: ComponentInstanceInterface,
    {
        Ok(match &capability_source {
            CapabilitySourceInterface::Namespace { capability, .. } => CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: capability
                    .source_name()
                    .ok_or(PolicyError::InvalidCapabilitySource)?
                    .clone(),
                source: CapabilityAllowlistSource::Self_,
                capability: capability.type_name(),
            },
            CapabilitySourceInterface::Component { capability, component } => {
                CapabilityAllowlistKey {
                    source_moniker: ExtendedMoniker::ComponentInstance(component.moniker.clone()),
                    source_name: capability
                        .source_name()
                        .ok_or(PolicyError::InvalidCapabilitySource)?
                        .clone(),
                    source: CapabilityAllowlistSource::Self_,
                    capability: capability.type_name(),
                }
            }
            CapabilitySourceInterface::Builtin { capability, .. } => CapabilityAllowlistKey {
                source_moniker: ExtendedMoniker::ComponentManager,
                source_name: capability.source_name().clone(),
                source: CapabilityAllowlistSource::Self_,
                capability: capability.type_name(),
            },
            CapabilitySourceInterface::Framework { capability, component } => {
                CapabilityAllowlistKey {
                    source_moniker: ExtendedMoniker::ComponentInstance(component.moniker.clone()),
                    source_name: capability.source_name().clone(),
                    source: CapabilityAllowlistSource::Framework,
                    capability: capability.type_name(),
                }
            }
            CapabilitySourceInterface::Capability { source_capability, component } => {
                CapabilityAllowlistKey {
                    source_moniker: ExtendedMoniker::ComponentInstance(component.moniker.clone()),
                    source_name: source_capability
                        .source_name()
                        .ok_or(PolicyError::InvalidCapabilitySource)?
                        .clone(),
                    source: CapabilityAllowlistSource::Capability,
                    capability: source_capability.type_name(),
                }
            }
            CapabilitySourceInterface::Collection { source_name, component, .. } => {
                CapabilityAllowlistKey {
                    source_moniker: ExtendedMoniker::ComponentInstance(component.moniker.clone()),
                    source_name: source_name.clone(),
                    source: CapabilityAllowlistSource::Self_,
                    capability: cm_rust::CapabilityTypeName::Service,
                }
            }
        })
    }

    /// Returns Ok(()) if the provided capability source can be routed to the
    /// given target_moniker, else a descriptive PolicyError.
    pub fn can_route_capability<'a, C>(
        &self,
        capability_source: &'a CapabilitySourceInterface<C>,
        target_moniker: &'a AbsoluteMoniker,
    ) -> Result<(), PolicyError>
    where
        C: ComponentInstanceInterface,
    {
        let target_moniker = Self::strip_moniker_instance_id(&target_moniker);
        let policy_key = Self::get_policy_key(capability_source).map_err(|e| {
            error!("Security policy could not generate a policy key for `{}`", capability_source);
            e
        })?;

        match self.config.security_policy.capability_policy.get(&policy_key) {
            Some(entries) => {
                // Use the HashSet to find any exact matches quickly.
                if entries.contains(&AllowlistEntry::Exact(target_moniker.clone())) {
                    return Ok(());
                }

                // Otherwise linear search for any non-exact matches.
                if entries.iter().any(|entry| allowlist_entry_matches(entry, &target_moniker)) {
                    Ok(())
                } else {
                    warn!(
                        "Security policy prevented `{}` from `{}` being routed to `{}`.",
                        policy_key.source_name, policy_key.source_moniker, target_moniker
                    );
                    Err(PolicyError::capability_use_disallowed(
                        policy_key.source_name.str(),
                        &policy_key.source_moniker,
                        &target_moniker.to_partial(),
                    ))
                }
            }
            None => Ok(()),
        }
    }

    /// Returns Ok(()) if the provided debug capability source is allowed to be routed from given
    /// environment.
    pub fn can_route_debug_capability<'a, C>(
        &self,
        capability_source: &'a CapabilitySourceInterface<C>,
        env_moniker: &'a AbsoluteMoniker,
        env_name: &'a str,
        target_moniker: &'a AbsoluteMoniker,
    ) -> Result<(), PolicyError>
    where
        C: ComponentInstanceInterface,
    {
        let policy_key = Self::get_policy_key(capability_source).map_err(|e| {
            error!("Security policy could not generate a policy key for `{}`", capability_source);
            e
        })?;
        if let Some(allowed_envs) =
            self.config.security_policy.debug_capability_policy.get(&policy_key)
        {
            if let Some(_) = allowed_envs.get(&(env_moniker.clone(), env_name.to_string())) {
                return Ok(());
            }
        }
        warn!(
            "Debug security policy prevented `{}` from `{}` being routed to `{}`.",
            policy_key.source_name, policy_key.source_moniker, target_moniker
        );
        Err(PolicyError::debug_capability_use_disallowed(
            policy_key.source_name.str(),
            &policy_key.source_moniker,
            &env_moniker.to_partial(),
            env_name,
            &target_moniker.to_partial(),
        ))
    }

    /// Returns Ok(()) if `target_moniker` is allowed to have `on_terminate=REBOOT` set.
    pub fn reboot_on_terminate_allowed(
        &self,
        target_moniker: &AbsoluteMoniker,
    ) -> Result<(), PolicyError> {
        if !self.config.reboot_on_terminate_enabled {
            return Err(PolicyError::unsupported(
                "reboot_on_terminate",
                &target_moniker.to_partial(),
            ));
        }
        if self
            .config
            .security_policy
            .child_policy
            .reboot_on_terminate
            .iter()
            .any(|entry| allowlist_entry_matches(entry, &target_moniker))
        {
            Ok(())
        } else {
            Err(PolicyError::child_policy_disallowed(
                "reboot_on_terminate",
                &target_moniker.to_partial(),
            ))
        }
    }
}

fn allowlist_entry_matches(
    allowlist_entry: &AllowlistEntry,
    target_moniker: &AbsoluteMoniker,
) -> bool {
    match allowlist_entry {
        AllowlistEntry::Exact(moniker) => {
            // An exact absolute moniker must match exactly.
            moniker == target_moniker
        }
        AllowlistEntry::Realm(realm) => {
            // For a Realm entry we are looking for the target_moniker to be
            // contained in the realm (i.e. empty up path) and the down_path to be
            // non-empty (i.e. children are allowed but not the realm itself).
            let relative = RelativeMoniker::from_absolute(realm, &target_moniker);
            relative.up_path().is_empty() && !relative.down_path().is_empty()
        }
        AllowlistEntry::Collection(realm, collection) => {
            // For a Collection entry we are looking for the target_moniker to be
            // contained in the realm (i.e. empty up path) and that the first element of
            // the down path is in a collection with a matching name.
            let relative = RelativeMoniker::from_absolute(realm, &target_moniker);
            relative.up_path().is_empty()
                && relative
                    .down_path()
                    .first()
                    .map_or(false, |first| first.collection() == Some(collection))
        }
    }
}

/// Evaluates security policy relative to a specific Component (based on that Component's
/// AbsoluteMoniker).
pub struct ScopedPolicyChecker {
    /// The runtime configuration containing the security policy to apply.
    config: Weak<RuntimeConfig>,

    /// The absolute moniker of the component that policy will be evaluated for.
    moniker: AbsoluteMoniker,
}

impl ScopedPolicyChecker {
    pub fn new(config: Weak<RuntimeConfig>, moniker: AbsoluteMoniker) -> Self {
        ScopedPolicyChecker { config, moniker }
    }

    pub fn get_scope(&self) -> &AbsoluteMoniker {
        &self.moniker
    }

    // This interface is super simple for now since there's only three allowlists. In the future
    // we'll probably want a different interface than an individual function per policy item.

    pub fn ambient_mark_vmo_exec_allowed(&self) -> Result<(), PolicyError> {
        let config = self.config.upgrade().ok_or(PolicyError::PolicyUnavailable)?;
        if config
            .security_policy
            .job_policy
            .ambient_mark_vmo_exec
            .iter()
            .any(|entry| allowlist_entry_matches(entry, &self.moniker))
        {
            Ok(())
        } else {
            Err(PolicyError::job_policy_disallowed(
                "ambient_mark_vmo_exec",
                &self.moniker.to_partial(),
            ))
        }
    }

    pub fn main_process_critical_allowed(&self) -> Result<(), PolicyError> {
        let config = self.config.upgrade().ok_or(PolicyError::PolicyUnavailable)?;
        if config
            .security_policy
            .job_policy
            .main_process_critical
            .iter()
            .any(|entry| allowlist_entry_matches(entry, &self.moniker))
        {
            Ok(())
        } else {
            Err(PolicyError::job_policy_disallowed(
                "main_process_critical",
                &self.moniker.to_partial(),
            ))
        }
    }

    pub fn create_raw_processes_allowed(&self) -> Result<(), PolicyError> {
        let config = self.config.upgrade().ok_or(PolicyError::PolicyUnavailable)?;
        if config
            .security_policy
            .job_policy
            .create_raw_processes
            .iter()
            .any(|entry| allowlist_entry_matches(entry, &self.moniker))
        {
            Ok(())
        } else {
            Err(PolicyError::job_policy_disallowed(
                "create_raw_processes",
                &self.moniker.to_partial(),
            ))
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::config::{
            ChildPolicyAllowlists, JobPolicyAllowlists, RuntimeConfig, SecurityPolicy,
        },
        matches::assert_matches,
        moniker::{AbsoluteMoniker, ChildMoniker},
        std::collections::HashMap,
    };

    #[test]
    fn allowlist_entry_checker() {
        let root = AbsoluteMoniker::root();
        let allowed = AbsoluteMoniker::from(vec!["foo:0", "bar:0"]);
        let disallowed_child_of_allowed = AbsoluteMoniker::from(vec!["foo:0", "bar:0", "baz:0"]);
        let disallowed = AbsoluteMoniker::from(vec!["baz:0", "fiz:0"]);
        let allowlist_exact = AllowlistEntry::Exact(allowed.clone());
        assert!(allowlist_entry_matches(&allowlist_exact, &allowed));
        assert!(!allowlist_entry_matches(&allowlist_exact, &root));
        assert!(!allowlist_entry_matches(&allowlist_exact, &disallowed));
        assert!(!allowlist_entry_matches(&allowlist_exact, &disallowed_child_of_allowed));

        let allowed_realm_root = AbsoluteMoniker::from(vec!["qux:0"]);
        let allowed_child_of_realm = AbsoluteMoniker::from(vec!["qux:0", "quux:0"]);
        let allowed_nested_child_of_realm = AbsoluteMoniker::from(vec!["qux:0", "quux:0", "foo:0"]);
        let allowlist_realm = AllowlistEntry::Realm(allowed_realm_root.clone());
        assert!(!allowlist_entry_matches(&allowlist_realm, &allowed_realm_root));
        assert!(allowlist_entry_matches(&allowlist_realm, &allowed_child_of_realm));
        assert!(allowlist_entry_matches(&allowlist_realm, &allowed_nested_child_of_realm));
        assert!(!allowlist_entry_matches(&allowlist_realm, &disallowed));
        assert!(!allowlist_entry_matches(&allowlist_realm, &root));

        let collection_holder = AbsoluteMoniker::from(vec!["corge:0"]);
        let collection_child = AbsoluteMoniker::from(vec!["corge:0", "collection:child:0"]);
        let collection_nested_child =
            AbsoluteMoniker::from(vec!["corge:0", "collection:child:0", "inner-child:0"]);
        let non_collection_child = AbsoluteMoniker::from(vec!["corge:0", "grault:0"]);
        let allowlist_collection =
            AllowlistEntry::Collection(collection_holder.clone(), "collection".into());
        assert!(!allowlist_entry_matches(&allowlist_collection, &collection_holder));
        assert!(allowlist_entry_matches(&allowlist_collection, &collection_child));
        assert!(allowlist_entry_matches(&allowlist_collection, &collection_nested_child));
        assert!(!allowlist_entry_matches(&allowlist_collection, &non_collection_child));
        assert!(!allowlist_entry_matches(&allowlist_collection, &disallowed));
        assert!(!allowlist_entry_matches(&allowlist_collection, &root));
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
                    ambient_mark_vmo_exec: vec![
                        AllowlistEntry::Exact(allowed1.clone()),
                        AllowlistEntry::Exact(allowed2.clone()),
                    ],
                    main_process_critical: vec![
                        AllowlistEntry::Exact(allowed1.clone()),
                        AllowlistEntry::Exact(allowed2.clone()),
                    ],
                    create_raw_processes: vec![
                        AllowlistEntry::Exact(allowed1.clone()),
                        AllowlistEntry::Exact(allowed2.clone()),
                    ],
                },
                capability_policy: HashMap::new(),
                debug_capability_policy: HashMap::new(),
                child_policy: ChildPolicyAllowlists {
                    reboot_on_terminate: vec![
                        AllowlistEntry::Exact(allowed1.clone()),
                        AllowlistEntry::Exact(allowed2.clone()),
                    ],
                },
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
    fn scoped_policy_checker_create_raw_processes() {
        macro_rules! assert_create_raw_processes_allowed_matches {
            ($config:expr, $moniker:expr, $expected:pat) => {
                let result = ScopedPolicyChecker::new($config.clone(), $moniker.clone())
                    .create_raw_processes_allowed();
                assert_matches!(result, $expected);
            };
        }
        macro_rules! assert_create_raw_processes_disallowed {
            ($config:expr, $moniker:expr) => {
                assert_create_raw_processes_allowed_matches!(
                    $config,
                    $moniker,
                    Err(PolicyError::JobPolicyDisallowed { .. })
                );
            };
        }
        let strong_config = Arc::new(RuntimeConfig::default());
        let config = Arc::downgrade(&strong_config);
        assert_create_raw_processes_disallowed!(config, AbsoluteMoniker::root());
        assert_create_raw_processes_disallowed!(config, AbsoluteMoniker::from(vec!["foo:0"]));

        let allowed1 = AbsoluteMoniker::from(vec!["foo:0", "bar:0"]);
        let allowed2 = AbsoluteMoniker::from(vec!["baz:0", "fiz:0"]);
        let strong_config = Arc::new(RuntimeConfig {
            security_policy: SecurityPolicy {
                job_policy: JobPolicyAllowlists {
                    ambient_mark_vmo_exec: vec![],
                    main_process_critical: vec![],
                    create_raw_processes: vec![
                        AllowlistEntry::Exact(allowed1.clone()),
                        AllowlistEntry::Exact(allowed2.clone()),
                    ],
                },
                capability_policy: HashMap::new(),
                debug_capability_policy: HashMap::new(),
                child_policy: ChildPolicyAllowlists { reboot_on_terminate: vec![] },
            },
            ..Default::default()
        });
        let config = Arc::downgrade(&strong_config);
        assert_create_raw_processes_allowed_matches!(config, allowed1, Ok(()));
        assert_create_raw_processes_allowed_matches!(config, allowed2, Ok(()));
        assert_create_raw_processes_disallowed!(config, AbsoluteMoniker::root());
        assert_create_raw_processes_disallowed!(config, allowed1.parent().unwrap());
        assert_create_raw_processes_disallowed!(
            config,
            allowed1.child(ChildMoniker::from("baz:0"))
        );

        drop(strong_config);
        assert_create_raw_processes_allowed_matches!(
            config,
            allowed1,
            Err(PolicyError::PolicyUnavailable)
        );
        assert_create_raw_processes_allowed_matches!(
            config,
            allowed2,
            Err(PolicyError::PolicyUnavailable)
        );
    }

    #[test]
    fn scoped_policy_checker_main_process_critical_allowed() {
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
                    ambient_mark_vmo_exec: vec![
                        AllowlistEntry::Exact(allowed1.clone()),
                        AllowlistEntry::Exact(allowed2.clone()),
                    ],
                    main_process_critical: vec![
                        AllowlistEntry::Exact(allowed1.clone()),
                        AllowlistEntry::Exact(allowed2.clone()),
                    ],
                    create_raw_processes: vec![
                        AllowlistEntry::Exact(allowed1.clone()),
                        AllowlistEntry::Exact(allowed2.clone()),
                    ],
                },
                capability_policy: HashMap::new(),
                debug_capability_policy: HashMap::new(),
                child_policy: ChildPolicyAllowlists { reboot_on_terminate: vec![] },
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
    fn scoped_policy_checker_reboot_policy_allowed() {
        macro_rules! assert_reboot_allowed_matches {
            ($config:expr, $moniker:expr, $expected:pat) => {
                let result = GlobalPolicyChecker::new($config.clone())
                    .reboot_on_terminate_allowed(&$moniker);
                assert_matches!(result, $expected);
            };
        }
        macro_rules! assert_reboot_disallowed {
            ($config:expr, $moniker:expr) => {
                assert_reboot_allowed_matches!(
                    $config,
                    $moniker,
                    Err(PolicyError::ChildPolicyDisallowed { .. })
                );
            };
        }

        // Empty config and enabled.
        let config =
            Arc::new(RuntimeConfig { reboot_on_terminate_enabled: true, ..Default::default() });
        assert_reboot_disallowed!(config, AbsoluteMoniker::root());
        assert_reboot_disallowed!(config, AbsoluteMoniker::from(vec!["foo:0"]));

        // Nonempty config and enabled.
        let allowed1 = AbsoluteMoniker::from(vec!["foo:0", "bar:0"]);
        let allowed2 = AbsoluteMoniker::from(vec!["baz:0", "fiz:0"]);
        let config = Arc::new(RuntimeConfig {
            security_policy: SecurityPolicy {
                job_policy: JobPolicyAllowlists {
                    ambient_mark_vmo_exec: vec![],
                    main_process_critical: vec![],
                    create_raw_processes: vec![],
                },
                capability_policy: HashMap::new(),
                debug_capability_policy: HashMap::new(),
                child_policy: ChildPolicyAllowlists {
                    reboot_on_terminate: vec![
                        AllowlistEntry::Exact(allowed1.clone()),
                        AllowlistEntry::Exact(allowed2.clone()),
                    ],
                },
            },
            reboot_on_terminate_enabled: true,
            ..Default::default()
        });
        assert_reboot_allowed_matches!(config, allowed1, Ok(()));
        assert_reboot_allowed_matches!(config, allowed2, Ok(()));
        assert_reboot_disallowed!(config, AbsoluteMoniker::root());
        assert_reboot_disallowed!(config, allowed1.parent().unwrap());
        assert_reboot_disallowed!(config, allowed1.child(ChildMoniker::from("baz:0")));

        // Nonempty config and disabled.
        let config = Arc::new(RuntimeConfig {
            security_policy: SecurityPolicy {
                job_policy: JobPolicyAllowlists {
                    ambient_mark_vmo_exec: vec![],
                    main_process_critical: vec![],
                    create_raw_processes: vec![],
                },
                capability_policy: HashMap::new(),
                debug_capability_policy: HashMap::new(),
                child_policy: ChildPolicyAllowlists {
                    reboot_on_terminate: vec![AllowlistEntry::Exact(allowed1.clone())],
                },
            },
            ..Default::default()
        });
        assert_reboot_allowed_matches!(config, allowed1, Err(PolicyError::Unsupported { .. }));
        assert_reboot_allowed_matches!(
            config,
            AbsoluteMoniker::root(),
            Err(PolicyError::Unsupported { .. })
        );
    }
}
