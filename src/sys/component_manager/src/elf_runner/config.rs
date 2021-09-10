// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::error::ElfRunnerError, crate::model::policy::ScopedPolicyChecker,
    fidl_fuchsia_data as fdata,
};

const CREATE_RAW_PROCESSES_KEY: &str = "job_policy_create_raw_processes";
const CRITICAL_KEY: &str = "main_process_critical";
const ENVIRON_KEY: &str = "environ";
const FORWARD_STDOUT_KEY: &str = "forward_stdout_to";
const FORWARD_STDERR_KEY: &str = "forward_stderr_to";
const VMEX_KEY: &str = "job_policy_ambient_mark_vmo_exec";
const STOP_EVENT_KEY: &str = "lifecycle.stop_event";
const STOP_EVENT_VARIANTS: [&'static str; 2] = ["notify", "ignore"];

/// Target sink for stdout and stderr output streams.
#[derive(Clone, Copy, Debug, Eq, PartialEq)]
pub enum StreamSink {
    Log,
    None,
}

/// Wraps ELF runner-specific keys in component's "program" dictionary.
#[derive(Debug, Default, Eq, PartialEq)]
pub struct ElfProgramConfig {
    notify_lifecycle_stop: bool,
    ambient_mark_vmo_exec: bool,
    main_process_critical: bool,
    create_raw_processes: bool,
    stdout_sink: StreamSink,
    stderr_sink: StreamSink,
    environ: Option<Vec<String>>,
}

impl Default for StreamSink {
    fn default() -> Self {
        StreamSink::None
    }
}

impl ElfProgramConfig {
    /// Parse the given dictionary into an ElfProgramConfig, checking it against security policy as
    /// needed.
    ///
    /// Checking against security policy is intentionally combined with parsing here, so that policy
    /// enforcement is as close to the point of parsing as possible and can't be inadvertently skipped.
    pub fn parse_and_check(
        program: &fdata::Dictionary,
        checker: &ScopedPolicyChecker,
        url: &str,
    ) -> Result<Self, ElfRunnerError> {
        let notify_lifecycle_stop =
            match runner::get_enum(program, STOP_EVENT_KEY, &STOP_EVENT_VARIANTS)
                .map_err(|_err| ElfRunnerError::program_dictionary_error(STOP_EVENT_KEY, url))?
            {
                Some("notify") => true,
                _ => false,
            };

        let ambient_mark_vmo_exec = runner::get_bool(program, VMEX_KEY)
            .map_err(|_err| ElfRunnerError::program_dictionary_error(VMEX_KEY, url))?;
        if ambient_mark_vmo_exec {
            checker.ambient_mark_vmo_exec_allowed()?;
        }

        let main_process_critical = runner::get_bool(program, CRITICAL_KEY)
            .map_err(|_err| ElfRunnerError::program_dictionary_error(CRITICAL_KEY, url))?;
        if main_process_critical {
            checker.main_process_critical_allowed()?;
        }

        let create_raw_processes =
            runner::get_bool(program, CREATE_RAW_PROCESSES_KEY).map_err(|_err| {
                ElfRunnerError::program_dictionary_error(CREATE_RAW_PROCESSES_KEY, url)
            })?;
        if create_raw_processes {
            checker.create_raw_processes_allowed()?;
        }

        let stdout_sink = get_stream_sink(&program, FORWARD_STDOUT_KEY, url)?;
        let stderr_sink = get_stream_sink(&program, FORWARD_STDERR_KEY, url)?;

        let environ = runner::get_environ(&program)
            .map_err(|_err| ElfRunnerError::program_dictionary_error(ENVIRON_KEY, url))?;

        Ok(ElfProgramConfig {
            notify_lifecycle_stop,
            ambient_mark_vmo_exec,
            main_process_critical,
            create_raw_processes,
            stdout_sink,
            stderr_sink,
            environ,
        })
    }

    pub fn notify_when_stopped(&self) -> bool {
        self.notify_lifecycle_stop
    }

    pub fn has_ambient_mark_vmo_exec(&self) -> bool {
        self.ambient_mark_vmo_exec
    }

    pub fn has_critical_main_process(&self) -> bool {
        self.main_process_critical
    }

    pub fn can_create_raw_processes(&self) -> bool {
        self.create_raw_processes
    }

    pub fn get_stdout_sink(&self) -> StreamSink {
        self.stdout_sink
    }

    pub fn get_stderr_sink(&self) -> StreamSink {
        self.stderr_sink
    }

    pub fn get_environ(&self) -> Option<Vec<String>> {
        self.environ.clone()
    }
}

fn get_stream_sink(
    dict: &fdata::Dictionary,
    key: &str,
    url: &str,
) -> Result<StreamSink, ElfRunnerError> {
    match runner::get_enum(dict, key, &["log", "none"]) {
        Ok(maybe_value) => match maybe_value {
            Some("log") => Ok(StreamSink::Log),
            _ => Ok(StreamSink::None),
        },
        // TODO: Add more informative error types. Users should know why a value
        // for a given key is invalid, if possible.
        Err(_err) => Err(ElfRunnerError::program_dictionary_error(key, url)),
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            config::{
                AllowlistEntry, ChildPolicyAllowlists, JobPolicyAllowlists, RuntimeConfig,
                SecurityPolicy,
            },
            model::policy::{PolicyError, ScopedPolicyChecker},
        },
        fidl_fuchsia_data as fdata,
        lazy_static::lazy_static,
        moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
        std::{collections::HashMap, default::Default, sync::Arc},
        test_case::test_case,
    };

    const TEST_URL: &str = "test_url";

    lazy_static! {
        static ref TEST_MONIKER: AbsoluteMoniker = AbsoluteMoniker::root();
        static ref PERMISSIVE_RUNTIME_CONFIG: Arc<RuntimeConfig> = {
            Arc::new(RuntimeConfig {
                security_policy: SecurityPolicy {
                    job_policy: JobPolicyAllowlists {
                        ambient_mark_vmo_exec: vec![AllowlistEntry::Exact(TEST_MONIKER.clone())],
                        main_process_critical: vec![AllowlistEntry::Exact(TEST_MONIKER.clone())],
                        create_raw_processes: vec![AllowlistEntry::Exact(TEST_MONIKER.clone())],
                    },
                    capability_policy: HashMap::new(),
                    debug_capability_policy: HashMap::new(),
                    child_policy: ChildPolicyAllowlists { reboot_on_terminate: vec![] },
                },
                ..Default::default()
            })
        };
        static ref RESTRICTIVE_RUNTIME_CONFIG: Arc<RuntimeConfig> =
            Arc::new(RuntimeConfig::default());
    }

    macro_rules! assert_is_program_dictionary_error {
        ($result:expr, $expected_key:expr) => {
            match $result {
                Err(ElfRunnerError::ProgramDictionaryError { key, url }) => {
                    assert_eq!(
                        $expected_key, key,
                        "key for ElfRunnerError doesn't match. Expected {}, got {}",
                        $expected_key, key
                    );
                    assert_eq!(
                        TEST_URL, url,
                        "url for ElfRunnerError doesn't match. Expected {}, got {}",
                        $expected_key, key
                    );
                }
                Err(_) => {
                    assert!(false, "expected error of type ElfRunnerError::ProgramDictionaryError")
                }
                Ok(_) => assert!(false, "expected error value but got Ok(_)"),
            }
        };
    }

    macro_rules! assert_error_is_disallowed_job_policy {
        ($result:expr, $expected_policy:expr) => {
            match $result {
                Err(ElfRunnerError::SecurityPolicyError { err }) => match err {
                    PolicyError::JobPolicyDisallowed { policy, moniker } => {
                        assert_eq!(
                            policy, $expected_policy,
                            "policy for PolicyError doesn't match. Expected {}, got {}",
                            $expected_policy, policy
                        );
                        assert_eq!(
                            moniker,
                            TEST_MONIKER.to_partial(),
                            "moniker for PolicyError doesn't match. Expected {}, got {}",
                            *TEST_MONIKER,
                            moniker
                        );
                    }
                    _ => assert!(false, "expected error of type PolicyError::JobPolicyDisallowed"),
                },
                Err(_) => {
                    assert!(false, "expected error of type ElfRunnerError::SecurityPolicyError")
                }
                Ok(_) => assert!(false, "expected error value but got Ok(_)"),
            }
        };
    }

    #[test_case("forward_stdout_to", new_string("log"), ElfProgramConfig { stdout_sink: StreamSink::Log, ..Default::default()} ; "when_stdout_log")]
    #[test_case("forward_stdout_to", new_string("none"), ElfProgramConfig { stdout_sink: StreamSink::None, ..Default::default()} ; "when_stdout_none")]
    #[test_case("forward_stderr_to", new_string("log"), ElfProgramConfig { stderr_sink: StreamSink::Log, ..Default::default()} ; "when_stderr_log")]
    #[test_case("forward_stderr_to", new_string("none"), ElfProgramConfig { stderr_sink: StreamSink::None, ..Default::default()} ; "when_stderr_none")]
    #[test_case("environ", new_empty_vec(), ElfProgramConfig { environ: None, ..Default::default()} ; "when_environ_empty")]
    #[test_case("environ", new_vec(vec!["FOO=BAR"]), ElfProgramConfig { environ: Some(vec!["FOO=BAR".into()]), ..Default::default()} ; "when_environ_has_values")]
    #[test_case("lifecycle.stop_event", new_string("notify"), ElfProgramConfig { notify_lifecycle_stop: true, ..Default::default()} ; "when_stop_event_notify")]
    #[test_case("lifecycle.stop_event", new_string("ignore"), ElfProgramConfig { notify_lifecycle_stop: false, ..Default::default()} ; "when_stop_event_ignore")]
    #[test_case("main_process_critical", new_string("true"), ElfProgramConfig { main_process_critical: true, ..Default::default()} ; "when_main_process_critical_true")]
    #[test_case("main_process_critical", new_string("false"), ElfProgramConfig { main_process_critical: false, ..Default::default()} ; "when_main_process_critical_false")]
    #[test_case("job_policy_ambient_mark_vmo_exec", new_string("true"), ElfProgramConfig { ambient_mark_vmo_exec: true, ..Default::default()} ; "when_ambient_mark_vmo_exec_true")]
    #[test_case("job_policy_ambient_mark_vmo_exec", new_string("false"), ElfProgramConfig { ambient_mark_vmo_exec: false, ..Default::default()} ; "when_ambient_mark_vmo_exec_false")]
    #[test_case("job_policy_create_raw_processes", new_string("true"), ElfProgramConfig { create_raw_processes: true, ..Default::default()} ; "when_create_raw_processes_true")]
    #[test_case("job_policy_create_raw_processes", new_string("false"), ElfProgramConfig { create_raw_processes: false, ..Default::default()} ; "when_create_raw_processes_false")]
    fn test_parse_and_check_with_permissive_policy(
        key: &str,
        value: fdata::DictionaryValue,
        expected: ElfProgramConfig,
    ) {
        let checker = ScopedPolicyChecker::new(
            Arc::downgrade(&(*PERMISSIVE_RUNTIME_CONFIG)),
            TEST_MONIKER.clone(),
        );
        let program = new_program_stanza(key, value);

        let actual = ElfProgramConfig::parse_and_check(&program, &checker, TEST_URL).unwrap();

        assert_eq!(actual, expected);
    }

    #[test_case("job_policy_ambient_mark_vmo_exec", new_string("true") , "ambient_mark_vmo_exec" ; "when_ambient_mark_vmo_exec_true")]
    #[test_case("main_process_critical", new_string("true"), "main_process_critical" ; "when_main_process_critical_true")]
    #[test_case("job_policy_create_raw_processes", new_string("true"), "create_raw_processes" ; "when_create_raw_processes_true")]
    fn test_parse_and_check_with_restrictive_policy(
        key: &str,
        value: fdata::DictionaryValue,
        policy: &str,
    ) {
        let checker = ScopedPolicyChecker::new(
            Arc::downgrade(&(*RESTRICTIVE_RUNTIME_CONFIG)),
            TEST_MONIKER.clone(),
        );
        let program = new_program_stanza(key, value);

        let actual = ElfProgramConfig::parse_and_check(&program, &checker, TEST_URL);

        assert_error_is_disallowed_job_policy!(actual, policy);
    }

    #[test_case("lifecycle.stop_event", new_empty_vec() ; "for_stop_event")]
    #[test_case("job_policy_ambient_mark_vmo_exec", new_empty_vec() ; "for_ambient_mark_vmo_exec")]
    #[test_case("main_process_critical", new_empty_vec() ; "for_main_process_critical")]
    #[test_case("job_policy_create_raw_processes", new_empty_vec() ; "for_create_raw_processes")]
    #[test_case("forward_stdout_to", new_empty_vec() ; "for_stdout")]
    #[test_case("forward_stderr_to", new_empty_vec() ; "for_stderr")]
    #[test_case("environ", new_empty_string() ; "for_environ")]
    fn test_parse_and_check_with_invalid_value(key: &str, value: fdata::DictionaryValue) {
        // Use a permissive policy because we want to fail *iff* value set for
        // key is invalid.
        let checker = ScopedPolicyChecker::new(
            Arc::downgrade(&(*PERMISSIVE_RUNTIME_CONFIG)),
            TEST_MONIKER.clone(),
        );
        let program = new_program_stanza(key, value);

        let actual = ElfProgramConfig::parse_and_check(&program, &checker, TEST_URL);

        assert_is_program_dictionary_error!(actual, key);
    }

    fn new_program_stanza(key: &str, value: fdata::DictionaryValue) -> fdata::Dictionary {
        fdata::Dictionary {
            entries: Some(vec![fdata::DictionaryEntry {
                key: key.to_owned(),
                value: Some(Box::new(value)),
            }]),
            ..fdata::Dictionary::EMPTY
        }
    }

    fn new_string(value: &str) -> fdata::DictionaryValue {
        fdata::DictionaryValue::Str(value.to_owned())
    }

    fn new_vec(values: Vec<&str>) -> fdata::DictionaryValue {
        fdata::DictionaryValue::StrVec(values.into_iter().map(str::to_owned).collect())
    }

    fn new_empty_string() -> fdata::DictionaryValue {
        fdata::DictionaryValue::Str("".to_owned())
    }

    fn new_empty_vec() -> fdata::DictionaryValue {
        fdata::DictionaryValue::StrVec(vec![])
    }
}
