// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        model::moniker::{AbsoluteMoniker, MonikerError},
        startup,
    },
    anyhow::{Context, Error},
    fidl_fuchsia_component_internal as component_internal,
    std::{convert::TryFrom, path::PathBuf, sync::Weak},
    thiserror::Error,
};

/// Runtime configuration options.
/// This configuration intended to be "global", in that the same configuration
/// is applied throughout a given running instance of component_manager.
#[derive(Debug, PartialEq, Eq)]
pub struct RuntimeConfig {
    /// How many children, maximum, are returned by a call to `ChildIterator.next()`.
    pub list_children_batch_size: usize,

    /// Security policy configuration.
    pub security_policy: SecurityPolicy,

    /// If true, component manager will be in debug mode. In this mode, component manager
    /// provides the `BlockingEventSource` protocol and exposes this protocol. Component
    /// manager will not start until it is resumed by a call to
    /// `BlockingEventSource.StartComponentTree`.
    ///
    /// This is done so that an external component (say an integration test) can subscribe
    /// to events before the root component has started.
    pub debug: bool,

    /// If true, component_manager will serve an instance of fuchsia.process.Launcher and use this
    /// launcher for the built-in ELF component runner. The root component can additionally
    /// use and/or offer this service using '/builtin/fuchsia.process.Launcher' from realm.
    // This flag exists because the built-in process launcher *only* works when
    // component_manager runs under a job that has ZX_POL_NEW_PROCESS set to allow, like the root
    // job. Otherwise, the component_manager process cannot directly create process through
    // zx_process_create. When we run component_manager elsewhere, like in test environments, it
    // has to use the fuchsia.process.Launcher service provided through its namespace instead.
    pub use_builtin_process_launcher: bool,

    /// If true, component_manager will maintain a UTC kernel clock and vend write handles through
    /// an instance of `fuchsia.time.Maintenance`. This flag should only be used with the top-level
    /// component_manager.
    pub maintain_utc_clock: bool,
}

/// Runtime security policy.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct SecurityPolicy {
    /// Allowlists for Zircon job policy.
    pub job_policy: JobPolicyAllowlists,
}

/// Allowlists for Zircon job policy. Part of runtime security policy.
#[derive(Debug, Default, PartialEq, Eq)]
pub struct JobPolicyAllowlists {
    /// Absolute monikers for components allowed to be given the ZX_POL_AMBIENT_MARK_VMO_EXEC job
    /// policy.
    ///
    /// Components must request this policy by including "job_policy_ambient_mark_vmo_exec: true" in
    /// their manifest's program object and must be using the ELF runner.
    /// This is equivalent to the v1 'deprecated-ambient-replace-as-executable' feature.
    pub ambient_mark_vmo_exec: Vec<AbsoluteMoniker>,

    /// Absolute monikers for components allowed to have their original process marked as critical
    /// to component_manager's job.
    ///
    /// Components must request this critical marking by including "main_process_critical: true" in
    /// their manifest's program object and must be using the ELF runner.
    pub main_process_critical: Vec<AbsoluteMoniker>,
}

impl Default for RuntimeConfig {
    fn default() -> Self {
        Self {
            list_children_batch_size: 1000,

            // security_policy must default to empty to ensure that it fails closed if no
            // configuration is present or it fails to load.
            security_policy: Default::default(),

            debug: false,

            use_builtin_process_launcher: false,

            maintain_utc_clock: false,
        }
    }
}

impl RuntimeConfig {
    /// Load RuntimeConfig from the '--config' command line arg. Returns both the RuntimeConfig
    /// and the path that the config was loaded from. Returns Err() if an an error occurs
    /// loading it.
    pub async fn load_from_file(args: &startup::Arguments) -> Result<(Self, PathBuf), Error> {
        let config =
            io_util::file::read_in_namespace_to_fidl::<component_internal::Config>(&args.config)
                .await
                .context(format!("Failed to read config file {}", &args.config))?;

        Self::try_from(&config)
            .map(|s| (s, PathBuf::from(&args.config)))
            .context(format!("Failed to apply config file {}", &args.config))
    }
}

fn parse_absolute_monikers_from_strings(
    strs: &Option<Vec<String>>,
) -> Result<Vec<AbsoluteMoniker>, Error> {
    let result: Result<Vec<AbsoluteMoniker>, MonikerError> = if let Some(strs) = strs {
        strs.iter().map(|s| AbsoluteMoniker::parse_string_without_instances(s)).collect()
    } else {
        Ok(Vec::new())
    };
    result.context(format!("Moniker parsing error for {:?}", strs))
}

impl TryFrom<&component_internal::Config> for RuntimeConfig {
    type Error = Error;

    fn try_from(config: &component_internal::Config) -> Result<Self, Error> {
        let default = RuntimeConfig::default();
        let job_policy =
            if let Some(component_internal::SecurityPolicy { job_policy: Some(job_policy) }) =
                &config.security_policy
            {
                let ambient_mark_vmo_exec =
                    parse_absolute_monikers_from_strings(&job_policy.ambient_mark_vmo_exec)?;
                let main_process_critical =
                    parse_absolute_monikers_from_strings(&job_policy.main_process_critical)?;
                JobPolicyAllowlists { ambient_mark_vmo_exec, main_process_critical }
            } else {
                JobPolicyAllowlists::default()
            };

        let list_children_batch_size = if let Some(value) = config.list_children_batch_size {
            usize::try_from(value)?
        } else {
            default.list_children_batch_size
        };
        Ok(RuntimeConfig {
            list_children_batch_size: list_children_batch_size,
            security_policy: SecurityPolicy { job_policy },
            debug: config.debug.unwrap_or(default.debug),
            use_builtin_process_launcher: config
                .use_builtin_process_launcher
                .unwrap_or(default.use_builtin_process_launcher),
            maintain_utc_clock: config.maintain_utc_clock.unwrap_or(default.maintain_utc_clock),
        })
    }
}

/// Errors returned by ScopedPolicyChecker.
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
        crate::model::moniker::ChildMoniker,
        fidl::encoding::encode_persistent,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_io as fio, fuchsia_zircon as zx,
        futures::future,
        matches::assert_matches,
        std::sync::Arc,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::pcb::asynchronous::read_only, path, pseudo_directory,
        },
    };

    macro_rules! test_config_ok {
        (
            $(
                $test_name:ident => ($input:expr, $expected:expr),
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    assert_matches!(RuntimeConfig::try_from(&$input), Ok(v) if v == $expected);
                }
            )+
        };
    }

    #[test]
    fn invalid_moniker() {
        let config = component_internal::Config {
            debug: None,
            list_children_batch_size: None,
            maintain_utc_clock: None,
            use_builtin_process_launcher: None,
            security_policy: Some(component_internal::SecurityPolicy {
                job_policy: Some(component_internal::JobPolicyAllowlists {
                    main_process_critical: None,
                    ambient_mark_vmo_exec: Some(vec!["/".to_string(), "bad".to_string()]),
                }),
            }),
            namespace_capabilities: None,
        };

        assert_matches!(RuntimeConfig::try_from(&config), Err(_));
    }

    test_config_ok! {
        all_fields_none => (component_internal::Config {
            debug: None,
            list_children_batch_size: None,
            security_policy: None,
            namespace_capabilities: None,
            maintain_utc_clock: None,
            use_builtin_process_launcher: None,
        }, RuntimeConfig::default()),
        all_leaf_nodes_none => (component_internal::Config {
            debug: Some(false),
            list_children_batch_size: Some(5),
            maintain_utc_clock: Some(false),
            use_builtin_process_launcher: Some(true),
            security_policy: Some(component_internal::SecurityPolicy {
                job_policy: Some(component_internal::JobPolicyAllowlists {
                    main_process_critical: None,
                    ambient_mark_vmo_exec: None,
                }),
            }),
            namespace_capabilities: None,
        }, RuntimeConfig {
            debug:false, list_children_batch_size: 5,
            maintain_utc_clock: false, use_builtin_process_launcher:true,
            ..Default::default() }),
        all_fields_some => (
            component_internal::Config {
                debug: Some(true),
                list_children_batch_size: Some(42),
                maintain_utc_clock: Some(true),
                use_builtin_process_launcher: Some(false),
                security_policy: Some(component_internal::SecurityPolicy {
                    job_policy: Some(component_internal::JobPolicyAllowlists {
                        main_process_critical: Some(vec!["/something/important".to_string()]),
                        ambient_mark_vmo_exec: Some(vec!["/".to_string(), "/foo/bar".to_string()]),
                    }),
                }),
                namespace_capabilities: None,
            },
            RuntimeConfig {
                debug: true,
                list_children_batch_size: 42,
                maintain_utc_clock: true,
                use_builtin_process_launcher: false,
                security_policy: SecurityPolicy {
                    job_policy: JobPolicyAllowlists {
                        ambient_mark_vmo_exec: vec![
                            AbsoluteMoniker::root(),
                            AbsoluteMoniker::from(vec!["foo:0", "bar:0"]),
                        ],
                        main_process_critical: vec![
                            AbsoluteMoniker::from(vec!["something:0", "important:0"]),
                        ],
                    }
                },
            }
        ),
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn config_from_file_no_arg() -> Result<(), Error> {
        let args = startup::Arguments::default();
        assert_matches!(RuntimeConfig::load_from_file(&args).await, Err(_));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn config_from_file_missing() -> Result<(), Error> {
        let args = startup::Arguments { config: "/foo/bar".to_string(), ..Default::default() };
        assert_matches!(RuntimeConfig::load_from_file(&args).await, Err(_));
        Ok(())
    }

    fn install_config_dir_in_namespace(
        config_dir: &str,
        config_file: &str,
        content: Vec<u8>,
    ) -> Result<(), Error> {
        let dir = pseudo_directory!(
            config_file => read_only(move || future::ready(Ok(content.clone()))),
        );
        let (dir_server, dir_client) = zx::Channel::create().unwrap();
        dir.open(
            ExecutionScope::new(),
            fio::OPEN_RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            path::Path::empty(),
            ServerEnd::new(dir_server),
        );

        let ns = fdio::Namespace::installed().expect("Failed to get installed namespace");
        ns.bind(config_dir, dir_client).expect("Failed to bind test directory");
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn config_from_file_valid() -> Result<(), Error> {
        // Install a directory containing a test config file in the test process's namespace.
        let config_dir = "/valid_config";
        let config_file = "test_config";
        let mut config = component_internal::Config {
            debug: None,
            list_children_batch_size: Some(42),
            security_policy: None,
            namespace_capabilities: None,
            maintain_utc_clock: None,
            use_builtin_process_launcher: None,
        };
        install_config_dir_in_namespace(config_dir, config_file, encode_persistent(&mut config)?)?;

        let config_path = [config_dir, "/", config_file].concat();
        let args = startup::Arguments { config: config_path.to_string(), ..Default::default() };
        let expected = (
            RuntimeConfig { list_children_batch_size: 42, ..Default::default() },
            PathBuf::from(config_path),
        );
        assert_matches!(RuntimeConfig::load_from_file(&args).await, Ok(v) if v == expected);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn config_from_file_invalid() -> Result<(), Error> {
        // Install a directory containing a test config file in the test process's namespace.
        let config_dir = "/invalid_config";
        let config_file = "test_config";
        // Add config file containing garbage data.
        install_config_dir_in_namespace(config_dir, config_file, vec![0xfa, 0xde])?;

        let config_path = [config_dir, "/", config_file].concat();
        let args = startup::Arguments { config: config_path.to_string(), ..Default::default() };
        assert_matches!(RuntimeConfig::load_from_file(&args).await, Err(_));
        Ok(())
    }

    #[test]
    fn policy_checker() {
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
        assert_vmex_disallowed!(config, AbsoluteMoniker::root());
        assert_vmex_disallowed!(config, AbsoluteMoniker::from(vec!["foo:0"]));
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
            },
            ..Default::default()
        });
        let config = Arc::downgrade(&strong_config);
        assert_vmex_allowed_matches!(config, allowed1, Ok(()));
        assert_vmex_allowed_matches!(config, allowed2, Ok(()));
        assert_vmex_disallowed!(config, AbsoluteMoniker::root());
        assert_vmex_disallowed!(config, allowed1.parent().unwrap());
        assert_vmex_disallowed!(config, allowed1.child(ChildMoniker::from("baz:0")));

        assert_critical_allowed_matches!(config, allowed1, Ok(()));
        assert_critical_allowed_matches!(config, allowed2, Ok(()));
        assert_critical_disallowed!(config, AbsoluteMoniker::root());
        assert_critical_disallowed!(config, allowed1.parent().unwrap());
        assert_critical_disallowed!(config, allowed1.child(ChildMoniker::from("baz:0")));

        drop(strong_config);
        assert_vmex_allowed_matches!(config, allowed1, Err(PolicyError::PolicyUnavailable));
        assert_vmex_allowed_matches!(config, allowed2, Err(PolicyError::PolicyUnavailable));

        assert_critical_allowed_matches!(config, allowed1, Err(PolicyError::PolicyUnavailable));
        assert_critical_allowed_matches!(config, allowed2, Err(PolicyError::PolicyUnavailable));
    }
}
