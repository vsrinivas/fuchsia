// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    warnings::Warning, ELF_STDIO_SHARD, ELF_TEST_RUNNER_SHARD, RUST_TEST_RUNNER_SHARD, SYSLOG_SHARD,
};
use anyhow::{bail, Error};
use std::{collections::BTreeSet, str::FromStr};

#[derive(Clone, Copy, Debug)]
pub enum RunnerSelection {
    Elf,
    ElfTest,
    RustTest,
    // TODO support dart
    // TODO maybe support gtest/gunit?
}

impl FromStr for RunnerSelection {
    type Err = Error;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        Ok(match s {
            "elf" => Self::Elf,
            "elf-test" => Self::ElfTest,
            "rust-test" => Self::RustTest,
            other => {
                bail!("unrecognized runner {other}, options are `elf`, `elf-test`, `rust-test`")
            }
        })
    }
}

impl RunnerSelection {
    pub fn supports_args_and_env(&self) -> bool {
        matches!(self, Self::Elf | Self::ElfTest)
    }

    pub fn is_for_testing(&self) -> bool {
        matches!(self, Self::ElfTest | Self::RustTest)
    }

    /// Return any warnings that should be surfaced to the user based on their choice of runner.
    pub fn warning(&self) -> Option<Warning> {
        match self {
            // ELF components usually expose some capabilities but this isn't written down in
            // CMX files, so we insert a comment telling users to do so manually.
            Self::Elf => Some(Warning::DeclareExpose),

            // The ELF test runner is a lowest-common-denominator for C++ tests, warn users
            // that they might want to pick a more specific runner.
            Self::ElfTest => Some(Warning::ElfTestRunnerUsed),

            Self::RustTest => None,
        }
    }

    pub fn fix_includes(&self, include: &mut BTreeSet<String>) {
        match self {
            Self::Elf => {
                // we need the stdio shard to make sure that stdout/stderr still go somewhere
                // after migrating, and the stdio shard already includes the syslog shard
                include.remove(SYSLOG_SHARD);
                include.insert(ELF_STDIO_SHARD.to_owned());
            }
            Self::ElfTest => {
                include.insert(ELF_TEST_RUNNER_SHARD.to_owned());
            }
            Self::RustTest => {
                include.insert(RUST_TEST_RUNNER_SHARD.to_owned());
            }
        }
    }

    pub fn runner_literal(&self) -> Option<cml::Name> {
        match self {
            Self::Elf => {
                Some(cml::Name::new("elf".to_string()).expect("elf is always a valid name"))
            }

            // handled by shards
            Self::ElfTest | Self::RustTest => None,
        }
    }
}
