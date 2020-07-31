// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        model::moniker::{AbsoluteMoniker, MonikerError},
        startup,
    },
    anyhow::{Context, Error},
    serde::Deserialize,
    std::{convert::TryFrom, path::PathBuf, sync::Weak},
    thiserror::Error,
};

/// Runtime configuration options.
/// This configuration intended to be "global", in that the same configuration
/// is applied throughout a given running instance of component_manager.
#[derive(Debug, Deserialize, PartialEq, Eq)]
#[serde(default, deny_unknown_fields)]
pub struct RuntimeConfig {
    /// How many children, maximum, are returned by a call to `ChildIterator.next()`.
    pub list_children_batch_size: usize,

    /// Security policy configuration.
    pub security_policy: SecurityPolicy,
}

/// Runtime security policy.
#[derive(Debug, Default, Deserialize, PartialEq, Eq)]
#[serde(default, deny_unknown_fields)]
pub struct SecurityPolicy {
    /// Allowlists for Zircon job policy.
    job_policy: JobPolicyAllowlists,
}

/// Allowlists for Zircon job policy. Part of runtime security policy.
#[derive(Debug, Default, Deserialize, PartialEq, Eq)]
#[serde(default, deny_unknown_fields)]
pub struct JobPolicyAllowlists {
    /// Absolute monikers for components allowed to be given the ZX_POL_AMBIENT_MARK_VMO_EXEC job
    /// policy.
    ///
    /// Components must request this policy by including "job_policy_ambient_mark_vmo_exec: true" in
    /// their manifest's program object and must be using the ELF runner.

    /// This is equivalent to the v1 'deprecated-ambient-replace-as-executable' feature.
    #[serde(deserialize_with = "absolute_monikers_from_strings")]
    ambient_mark_vmo_exec: Vec<AbsoluteMoniker>,

    /// Absolute monikers for components allowed to have their original process marked as critical
    /// to component_manager's job.
    ///
    /// Components must request this critical marking by including "main_process_critical: true" in
    /// their manifest's program object and must be using the ELF runner.
    #[serde(deserialize_with = "absolute_monikers_from_strings")]
    main_process_critical: Vec<AbsoluteMoniker>,
}

fn absolute_monikers_from_strings<'de, D>(deserializer: D) -> Result<Vec<AbsoluteMoniker>, D::Error>
where
    D: serde::de::Deserializer<'de>,
{
    let strs: Vec<String> = Deserialize::deserialize(deserializer)?;
    let result: Result<Vec<AbsoluteMoniker>, MonikerError> =
        strs.iter().map(|s| AbsoluteMoniker::parse_string_without_instances(s)).collect();
    result.map_err(serde::de::Error::custom)
}

impl Default for RuntimeConfig {
    fn default() -> Self {
        Self {
            list_children_batch_size: 1000,

            // security_policy must default to empty to ensure that it fails closed if no
            // configuration is present or it fails to load.
            security_policy: Default::default(),
        }
    }
}

impl RuntimeConfig {
    /// Load RuntimeConfig from the '--config-file' command line arg if one was given. If one was
    /// given and it is loaded successfully, returns both the RuntimeConfig and the path that the
    /// config was loaded from. Returns Ok(None) if no config file path is given, or Err() if an
    /// an error occurs loading it.
    pub async fn load_from_file(
        args: &startup::Arguments,
    ) -> Result<Option<(Self, PathBuf)>, Error> {
        if let Some(args_path) = &args.config_file {
            let contents = io_util::file::read_in_namespace_to_string(args_path)
                .await
                .context(format!("Failed to read config file {}", args_path))?;
            return Self::try_from(contents.as_str())
                .map(|s| Some((s, PathBuf::from(args_path))))
                .context(format!("Failed to load config file {}", args_path));
        }
        Ok(None)
    }
}

impl TryFrom<&str> for RuntimeConfig {
    type Error = Error;

    fn try_from(s: &str) -> Result<Self, Error> {
        // TODO(viktard): Configuration is currently encoded as JSON5. This will likely be replaced
        // with persistent FIDL.
        serde_json5::from_str(s).map_err(|e| e.into())
    }
}

/// Errors retured by ScopedPolicyChecker.
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
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_io as fio,
        fuchsia_async::EHandle,
        fuchsia_zircon as zx,
        matches::assert_matches,
        std::sync::Arc,
        vfs::{
            directory::entry::DirectoryEntry, execution_scope::ExecutionScope,
            file::pcb::asynchronous::read_only_static, path, pseudo_directory,
        },
    };

    macro_rules! test_config_from_str_err {
        (
            $(
                $test_name:ident => $input:expr,
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    assert_matches!(RuntimeConfig::try_from($input), Err(_));
                }
            )+
        };
    }
    macro_rules! test_config_from_str_ok {
        (
            $(
                $test_name:ident => ($input:expr, $expected:expr),
            )+
        ) => {
            $(
                #[test]
                fn $test_name() {
                    assert_matches!(RuntimeConfig::try_from($input), Ok(v) if v == $expected);
                }
            )+
        };
    }

    test_config_from_str_err! {
        empty_string => "",
        bad_json_1 => "{",
        bad_json_2 => "{ security_policy }",
        unknown_field_1 => "{ foobar: 1",
        unknown_field_2 => "{ list_children_batch_size: 1, foobar: 1 }",
        unknown_field_3 => "{ security_policy: { foobar: 1 } }",
        invalid_moniker => r#"{ security_policy: { job_policy: { ambient_mark_vmo_exec: [\"/\", \"bad\"] } } }"#,
    }
    test_config_from_str_ok! {
        empty_object => ("{}", RuntimeConfig::default()),
        valid_1 => ("{ security_policy: {} }", RuntimeConfig::default()),
        valid_2 => ("{ list_children_batch_size: 5 }", RuntimeConfig { list_children_batch_size: 5, ..Default::default() }),
        valid_3 => (r#"{ security_policy: { job_policy: { ambient_mark_vmo_exec: ["/foo"] } } }"#,
            RuntimeConfig {
                security_policy: SecurityPolicy {
                    job_policy: JobPolicyAllowlists {
                        ambient_mark_vmo_exec: vec![AbsoluteMoniker::from(vec!["foo:0"])],
                        main_process_critical: vec![],
                    }
                },
                ..Default::default()
            }
        ),
        valid_4 => (r#"{ security_policy: { job_policy: { ambient_mark_vmo_exec: ["/", "/foo/bar"] } } }"#,
            RuntimeConfig {
                security_policy: SecurityPolicy {
                    job_policy: JobPolicyAllowlists {
                        ambient_mark_vmo_exec: vec![
                            AbsoluteMoniker::root(),
                            AbsoluteMoniker::from(vec!["foo:0", "bar:0"]),
                        ],
                        main_process_critical: vec![
                        ],
                    }
                },
                ..Default::default()
            }
        ),
        valid_5 => (r#"{ security_policy: { job_policy: { main_process_critical: ["/", "/foo/bar"] } } }"#,
            RuntimeConfig {
                security_policy: SecurityPolicy {
                    job_policy: JobPolicyAllowlists {
                        ambient_mark_vmo_exec: vec![],
                        main_process_critical: vec![
                            AbsoluteMoniker::root(),
                            AbsoluteMoniker::from(vec!["foo:0", "bar:0"]),
                        ],
                    }
                },
                ..Default::default()
            }
        ),
        valid_6 => (r#"{
                security_policy: {
                    job_policy: {
                        ambient_mark_vmo_exec: ["/", "/foo/bar"],
                        main_process_critical: ["/something/important"]
                    }
                }
            }"#,
            RuntimeConfig {
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
                ..Default::default()
            }
        ),
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn config_from_file_no_arg() -> Result<(), Error> {
        let args = startup::Arguments::default();
        assert_matches!(RuntimeConfig::load_from_file(&args).await, Ok(None));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn config_from_file_missing() -> Result<(), Error> {
        let args =
            startup::Arguments { config_file: Some("/foo/bar".to_string()), ..Default::default() };
        assert_matches!(RuntimeConfig::load_from_file(&args).await, Err(_));
        Ok(())
    }

    fn install_config_dir_in_namespace(config_dir: &str, config_file: &str, config_str: &str) {
        let dir = pseudo_directory!(
            config_file => read_only_static(config_str.to_string()),
        );
        let (dir_server, dir_client) = zx::Channel::create().unwrap();
        dir.open(
            ExecutionScope::from_executor(Box::new(EHandle::local())),
            fio::OPEN_RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            path::Path::empty(),
            ServerEnd::new(dir_server),
        );

        let ns = fdio::Namespace::installed().expect("Failed to get installed namespace");
        ns.bind(config_dir, dir_client).expect("Failed to bind test directory");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn config_from_file_valid() -> Result<(), Error> {
        // Install a directory containing a test config file in the test process's namespace.
        let config_dir = "/valid_config";
        let config_file = "test_config";
        install_config_dir_in_namespace(
            config_dir,
            config_file,
            "{ list_children_batch_size: 42 }",
        );

        let config_path = [config_dir, "/", config_file].concat();
        let args =
            startup::Arguments { config_file: Some(config_path.to_string()), ..Default::default() };
        let expected = (
            RuntimeConfig { list_children_batch_size: 42, ..Default::default() },
            PathBuf::from(config_path),
        );
        assert_matches!(RuntimeConfig::load_from_file(&args).await, Ok(Some(v)) if v == expected);
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn config_from_file_invalid() -> Result<(), Error> {
        // Install a directory containing a test config file in the test process's namespace.
        let config_dir = "/invalid_config";
        let config_file = "test_config";
        install_config_dir_in_namespace(config_dir, config_file, "{");

        let config_path = [config_dir, "/", config_file].concat();
        let args =
            startup::Arguments { config_file: Some(config_path.to_string()), ..Default::default() };
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
