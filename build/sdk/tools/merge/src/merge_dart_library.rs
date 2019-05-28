// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::DartLibrary;

use crate::app::Result;
use crate::file_provider::FileProvider;
use crate::tarball::{OutputTarball, SourceTarball};

impl FileProvider for DartLibrary {
    fn get_common_files(&self) -> Vec<String> {
        self.sources.clone()
    }
}

pub fn merge_dart_library(
    meta_path: &str, base: &SourceTarball, _complement: &SourceTarball, output: &mut OutputTarball,
) -> Result<()> {
    // For now, just copy the base version.
    // TODO(DX-495): verify that contents are the exact same.
    let meta = base.get_metadata::<DartLibrary>(meta_path)?;
    let mut paths = meta.get_all_files();
    paths.push(meta_path.to_owned());
    for path in &paths {
        base.get_file(path, |file| output.write_file(path, file))?;
    }
    Ok(())
}
