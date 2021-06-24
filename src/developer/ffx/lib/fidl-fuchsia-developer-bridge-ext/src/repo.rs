// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_developer_bridge as fidl,
    serde::{Deserialize, Serialize},
    std::{convert::TryFrom, path::PathBuf},
    thiserror::Error,
};

#[derive(Clone, Debug, Serialize, Deserialize)]
#[serde(tag = "type", rename_all = "snake_case")]
pub enum RepositorySpec {
    FileSystem { path: PathBuf },

    Pm { path: PathBuf },
}

impl TryFrom<fidl::RepositorySpec> for RepositorySpec {
    type Error = RepositoryError;

    fn try_from(repo: fidl::RepositorySpec) -> Result<Self, RepositoryError> {
        match repo {
            fidl::RepositorySpec::FileSystem(filesystem_spec) => {
                let path = filesystem_spec.path.ok_or(RepositoryError::MissingRepositorySpecField)?;
                Ok(RepositorySpec::FileSystem { path: path.into() })
            }
            fidl::RepositorySpec::Pm(pm_spec) => {
                let path = pm_spec.path.ok_or(RepositoryError::MissingRepositorySpecField)?;
                Ok(RepositorySpec::Pm { path: path.into() })
            }
            fidl::RepositorySpecUnknown!() => Err(RepositoryError::UnknownRepositorySpec),
        }
    }
}

impl From<RepositorySpec> for fidl::RepositorySpec {
    fn from(repo: RepositorySpec) -> Self {
        match repo {
            RepositorySpec::FileSystem { path } => {
                let path = path.to_str().expect("paths must be UTF-8").clone();
                fidl::RepositorySpec::FileSystem(fidl::FileSystemRepositorySpec {
                    path: Some(path.into()),
                    ..fidl::FileSystemRepositorySpec::EMPTY
                })
            }
            RepositorySpec::Pm { path } => {
                let path = path.to_str().expect("paths must be UTF-8").clone();
                fidl::RepositorySpec::Pm(fidl::PmRepositorySpec {
                    path: Some(path.into()),
                    ..fidl::PmRepositorySpec::EMPTY
                })
            }
        }
    }
}

#[derive(Debug, Error)]
pub enum RepositoryError {
    #[error("the repository name is missing")]
    MissingRepositoryName,

    #[error("repository does not exist")]
    NoMatchingRepository,

    #[error("error communicating with target device")]
    TargetCommunicationFailure,

    #[error("error interacting with the target's RepositoryManager")]
    RepositoryManagerError,

    #[error("error iteracting with the target's RewriteEngine")]
    RewriteEngineError,

    #[error("unknown repository spec type")]
    UnknownRepositorySpec,

    #[error("repository spec is missing a required field")]
    MissingRepositorySpecField,
}

impl From<fidl::RepositoryError> for RepositoryError {
    fn from(err: fidl::RepositoryError) -> Self {
        match err {
            fidl::RepositoryError::MissingRepositoryName => RepositoryError::MissingRepositoryName,
            fidl::RepositoryError::NoMatchingRepository => RepositoryError::NoMatchingRepository,
            fidl::RepositoryError::TargetCommunicationFailure => {
                RepositoryError::TargetCommunicationFailure
            }
            fidl::RepositoryError::RepositoryManagerError => {
                RepositoryError::RepositoryManagerError
            }
            fidl::RepositoryError::RewriteEngineError => RepositoryError::RewriteEngineError,
            fidl::RepositoryError::UnknownRepositorySpec => RepositoryError::UnknownRepositorySpec,
            fidl::RepositoryError::MissingRepositorySpecField => RepositoryError::MissingRepositorySpecField,
        }
    }
}

impl From<RepositoryError> for fidl::RepositoryError {
    fn from(err: RepositoryError) -> Self {
        match err {
            RepositoryError::MissingRepositoryName => fidl::RepositoryError::MissingRepositoryName,
            RepositoryError::NoMatchingRepository => fidl::RepositoryError::NoMatchingRepository,
            RepositoryError::TargetCommunicationFailure => {
                fidl::RepositoryError::TargetCommunicationFailure
            }
            RepositoryError::RepositoryManagerError => {
                fidl::RepositoryError::RepositoryManagerError
            }
            RepositoryError::RewriteEngineError => fidl::RepositoryError::RewriteEngineError,
            RepositoryError::UnknownRepositorySpec => fidl::RepositoryError::UnknownRepositorySpec,
            RepositoryError::MissingRepositorySpecField => fidl::RepositoryError::MissingRepositorySpecField,
        }
    }
}
