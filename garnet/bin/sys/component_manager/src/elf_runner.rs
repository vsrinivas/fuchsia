// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::data::DictionaryExt,
    crate::model::{Runner, RunnerError},
    fidl_fuchsia_data as fdata, fidl_fuchsia_sys2 as fsys,
    futures::future::FutureObj,
};

/// Runs components with ELF binaries.
/// TODO: Actually implement this.
pub struct ElfRunner {}

impl ElfRunner {
    pub fn new() -> ElfRunner {
        ElfRunner {}
    }

    async fn start_async(&self, start_info: fsys::ComponentStartInfo) -> Result<(), RunnerError> {
        // TODO: Actually start a process!
        let resolved_uri = get_resolved_uri(&start_info)?;
        let binary = get_program_binary(&start_info)?;
        println!(
            "ElfRunner: pretending to start '{}' with binary '{}'",
            resolved_uri, binary
        );
        Err(RunnerError::ComponentNotAvailable)
    }
}

impl Runner for ElfRunner {
    fn start(&self, start_info: fsys::ComponentStartInfo) -> FutureObj<Result<(), RunnerError>> {
        FutureObj::new(Box::new(self.start_async(start_info)))
    }
}

fn get_resolved_uri(start_info: &fsys::ComponentStartInfo) -> Result<&str, RunnerError> {
    match &start_info.resolved_uri {
        Some(uri) => Ok(uri),
        _ => Err(RunnerError::InvalidArgs),
    }
}

fn get_program_binary(start_info: &fsys::ComponentStartInfo) -> Result<&str, RunnerError> {
    match start_info
        .program
        .as_ref()
        .and_then(|program| program.find("binary"))
    {
        Some(fdata::Value::Str(ref str)) => Ok(str),
        _ => Err(RunnerError::InvalidArgs),
    }
}
