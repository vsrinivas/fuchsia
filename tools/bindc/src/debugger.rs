// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::compiler::CompilerError;
use crate::device_specification;
use std::fs::File;
use std::io::Read;
use std::path::PathBuf;
use std::str::FromStr;

pub fn debug(device_file: PathBuf) -> Result<(), CompilerError> {
    let mut file =
        File::open(&device_file).map_err(|_| CompilerError::FileOpenError(device_file.clone()))?;
    let mut buf = String::new();
    file.read_to_string(&mut buf).map_err(|_| CompilerError::FileReadError(device_file.clone()))?;
    let _ast = device_specification::Ast::from_str(&buf).map_err(CompilerError::BindParserError)?;

    Ok(())
}
