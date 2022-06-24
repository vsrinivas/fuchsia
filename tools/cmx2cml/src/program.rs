// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::runner::RunnerSelection;
use anyhow::{bail, ensure, Error};
use serde::Deserialize;

#[derive(Debug, Deserialize)]
#[serde(deny_unknown_fields, untagged)]
pub enum CmxProgram {
    Elf {
        binary: String,
        args: Option<Vec<String>>,
        env_vars: Option<Vec<String>>,
    },
    #[allow(unused)] // we want to deny unknown fields but don't support this
    DartMaybe {
        data: String,
        args: Option<Vec<String>>,
    },
}

impl CmxProgram {
    pub fn convert(&self, runner: RunnerSelection) -> Result<cml::Program, Error> {
        match self {
            CmxProgram::Elf { binary, args, env_vars } => {
                let mut info = serde_json::Map::new();
                info.insert("binary".to_string(), binary.to_owned().into());

                if let Some(args) = args {
                    ensure!(runner.supports_args_and_env(), "runner must support argv");
                    info.insert("args".to_string(), args.to_owned().into());
                }

                if let Some(env_vars) = env_vars {
                    ensure!(runner.supports_args_and_env(), "runner must support env vars");
                    info.insert("environ".to_string(), env_vars.to_owned().into());
                }

                Ok(cml::Program { runner: runner.runner_literal(), info })
            }
            CmxProgram::DartMaybe { .. } => bail!("dart v1 components not supported (yet?)"),
        }
    }
}
