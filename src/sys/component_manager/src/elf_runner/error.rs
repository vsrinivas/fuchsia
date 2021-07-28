// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::{policy::PolicyError, runner::RunnerError},
    anyhow::Error,
    clonable_error::ClonableError,
    fuchsia_zircon as zx,
    log::error,
    thiserror::Error,
};

/// Errors produced by `ElfRunner`.
#[derive(Debug, Clone, Error)]
pub enum ElfRunnerError {
    #[error("failed to register as exception handler for component with url \"{}\": {}", url, err)]
    ComponentExceptionRegistrationError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to retrieve process koid for component with url \"{}\": {}", url, err)]
    ComponentProcessIdError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to retrieve job koid for component with url \"{}\": {}", url, err)]
    ComponentJobIdError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to set job policy for component with url \"{}\": {}", url, err)]
    ComponentJobPolicyError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to create job for component with url \"{}\": {}", url, err)]
    ComponentJobCreationError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to duplicate job for component with url \"{}\": {}", url, err)]
    ComponentJobDuplicationError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to mark main process as critical for component with url \"{}\": {}", url, err)]
    ComponentCriticalMarkingError {
        url: String,
        #[source]
        err: ClonableError,
    },
    #[error("failed to add runtime/elf directory for component with url \"{}\"", url)]
    ComponentElfDirectoryError { url: String },
    #[error("program key \"{}\" invalid for component with url \"{}\"", key, url)]
    ProgramDictionaryError { key: String, url: String },
    #[error("{err}")]
    SecurityPolicyError {
        #[from]
        err: PolicyError,
    },
    #[error("{err}")]
    GenericRunnerError {
        #[from]
        err: RunnerError,
    },
    #[error("failed to duplicate UTC clock for component with url \"{}\": {}", url, status)]
    DuplicateUtcClockError { url: String, status: zx::Status },
}

impl ElfRunnerError {
    pub fn component_exception_registration_error(
        url: impl Into<String>,
        err: impl Into<Error>,
    ) -> ElfRunnerError {
        ElfRunnerError::ComponentExceptionRegistrationError {
            url: url.into(),
            err: err.into().into(),
        }
    }

    pub fn component_process_id_error(
        url: impl Into<String>,
        err: impl Into<Error>,
    ) -> ElfRunnerError {
        ElfRunnerError::ComponentProcessIdError { url: url.into(), err: err.into().into() }
    }

    pub fn component_job_id_error(url: impl Into<String>, err: impl Into<Error>) -> ElfRunnerError {
        ElfRunnerError::ComponentJobIdError { url: url.into(), err: err.into().into() }
    }

    pub fn component_job_policy_error(
        url: impl Into<String>,
        err: impl Into<Error>,
    ) -> ElfRunnerError {
        ElfRunnerError::ComponentJobPolicyError { url: url.into(), err: err.into().into() }
    }

    pub fn component_job_creation_error(
        url: impl Into<String>,
        err: impl Into<Error>,
    ) -> ElfRunnerError {
        ElfRunnerError::ComponentJobCreationError { url: url.into(), err: err.into().into() }
    }

    pub fn component_job_duplication_error(
        url: impl Into<String>,
        err: impl Into<Error>,
    ) -> ElfRunnerError {
        ElfRunnerError::ComponentJobDuplicationError { url: url.into(), err: err.into().into() }
    }

    pub fn component_critical_marking_error(
        url: impl Into<String>,
        err: impl Into<Error>,
    ) -> ElfRunnerError {
        ElfRunnerError::ComponentCriticalMarkingError { url: url.into(), err: err.into().into() }
    }

    pub fn component_elf_directory_error(url: impl Into<String>) -> ElfRunnerError {
        ElfRunnerError::ComponentElfDirectoryError { url: url.into() }
    }

    pub fn program_dictionary_error(
        key: impl Into<String>,
        url: impl Into<String>,
    ) -> ElfRunnerError {
        ElfRunnerError::ProgramDictionaryError { key: key.into(), url: url.into() }
    }

    pub fn as_zx_status(&self) -> zx::Status {
        match self {
            ElfRunnerError::GenericRunnerError { err } => err.as_zx_status(),
            ElfRunnerError::SecurityPolicyError { .. } => zx::Status::ACCESS_DENIED,
            _ => zx::Status::INTERNAL,
        }
    }
}
