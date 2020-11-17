// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{config::RuntimeConfig, model::moniker::AbsoluteMoniker},
    std::sync::{Arc, Weak},
    thiserror::Error,
};
/// Errors returned by the PolicyChecker and the ScopedPolicyChecker.
#[derive(Debug, Clone, Error)]
pub enum PolicyError {
    #[error("Security policy was unavailable to check")]
    PolicyUnavailable,

    #[error("security policy disallows \"{policy}\" job policy for \"{moniker}\"")]
    JobPolicyDisallowed { policy: String, moniker: AbsoluteMoniker },
}

impl PolicyError {
    fn job_policy_disallowed(policy: impl Into<String>, moniker: &AbsoluteMoniker) -> Self {
        PolicyError::JobPolicyDisallowed { policy: policy.into(), moniker: moniker.clone() }
    }
}

/// Evaluates security policy globally across the entire Model and all realms.
/// This is used to enforce runtime capability routing restrictions across all
/// realms to prevent high privilleged capabilities from being routed to
/// components outside of the list defined in the runtime configs security
/// policy.
pub struct GlobalPolicyChecker {
    /// The runtime configuration containing the security policy to apply.
    _config: Arc<RuntimeConfig>,
}

impl GlobalPolicyChecker {
    /// Constructs a new PolicyChecker object configured by the
    /// RuntimeConfig::SecurityPolicy.
    pub fn new(config: Arc<RuntimeConfig>) -> Self {
        Self { _config: config }
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
            config::{JobPolicyAllowlists, SecurityPolicy},
            model::moniker::ChildMoniker,
        },
        matches::assert_matches,
        std::sync::Arc,
    };

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
                capability_policy: vec![],
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
                capability_policy: vec![],
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
}
