// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::error::ElfRunnerError, crate::model::policy::ScopedPolicyChecker,
    fidl_fuchsia_data as fdata,
};

/// Wraps ELF runner-specific keys in component's "program" dictionary.
#[derive(Default)]
pub struct ElfProgramConfig {
    pub notify_lifecycle_stop: bool,
    pub ambient_mark_vmo_exec: bool,
    pub main_process_critical: bool,
    pub create_raw_processes: bool,
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
        const STOP_EVENT_KEY: &str = "lifecycle.stop_event";
        let notify_lifecycle_stop = match Self::find(program, STOP_EVENT_KEY) {
            Some(fdata::DictionaryValue::Str(str_val)) => match &str_val[..] {
                "notify" => Ok(true),
                "ignore" => Ok(false),
                _ => Err(()),
            },
            Some(_) => Err(()),
            None => Ok(false),
        }
        .map_err(|_| ElfRunnerError::program_dictionary_error(STOP_EVENT_KEY, url))?;

        const VMEX_KEY: &str = "job_policy_ambient_mark_vmo_exec";
        let ambient_mark_vmo_exec = match Self::find(program, VMEX_KEY) {
            Some(fdata::DictionaryValue::Str(str_val)) => match &str_val[..] {
                "true" => Ok(true),
                "false" => Ok(false),
                _ => Err(()),
            },
            Some(_) => Err(()),
            None => Ok(false),
        }
        .map_err(|_| ElfRunnerError::program_dictionary_error(VMEX_KEY, url))?;
        if ambient_mark_vmo_exec {
            checker.ambient_mark_vmo_exec_allowed()?;
        }

        const CRITICAL_KEY: &str = "main_process_critical";
        let main_process_critical = match Self::find(program, CRITICAL_KEY) {
            Some(fdata::DictionaryValue::Str(str_val)) => match &str_val[..] {
                "true" => Ok(true),
                "false" => Ok(false),
                _ => Err(()),
            },
            Some(_) => Err(()),
            None => Ok(false),
        }
        .map_err(|_| ElfRunnerError::program_dictionary_error(CRITICAL_KEY, url))?;
        if main_process_critical {
            checker.main_process_critical_allowed()?;
        }

        const CREATE_RAW_PROCESSES_KEY: &str = "job_policy_create_raw_processes";
        let create_raw_processes = match Self::find(program, CREATE_RAW_PROCESSES_KEY) {
            Some(fdata::DictionaryValue::Str(str_val)) => match &str_val[..] {
                "true" => Ok(true),
                "false" => Ok(false),
                _ => Err(()),
            },
            Some(_) => Err(()),
            None => Ok(false),
        }
        .map_err(|_| ElfRunnerError::program_dictionary_error(CREATE_RAW_PROCESSES_KEY, url))?;
        if create_raw_processes {
            checker.create_raw_processes_allowed()?;
        }

        Ok(ElfProgramConfig {
            notify_lifecycle_stop,
            ambient_mark_vmo_exec,
            main_process_critical,
            create_raw_processes,
        })
    }

    fn find<'a>(dict: &'a fdata::Dictionary, key: &str) -> Option<&'a fdata::DictionaryValue> {
        match &dict.entries {
            Some(entries) => {
                for entry in entries {
                    if entry.key == key {
                        return entry.value.as_ref().map(|val| &**val);
                    }
                }
                None
            }
            _ => None,
        }
    }
}
