// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::DartLibrary;

use crate::app::Result;
use crate::file_provider::FileProvider;
use crate::tarball::{InputTarball, OutputTarball};

impl FileProvider for DartLibrary {
    fn get_common_files(&self) -> Vec<String> {
        self.sources.clone()
    }
}

pub fn merge_dart_library<F>(
    meta_path: &str, base: &impl InputTarball<F>, _complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
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

#[cfg(test)]
mod tests {
    use crate::testing::{MockInputTarball, MockOutputTarball};

    use super::*;

    #[test]
    fn test_merge() {
        let meta = "dart/foobar/meta.json";
        let data = r#"
        {
            "name": "foobar",
            "type": "dart_library",
            "root": "dart/foobar",
            "sources": [
                "dart/foobar/lib/one.dart",
                "dart/foobar/lib/two.dart"
            ],
            "deps": [],
            "fidl_deps": [],
            "third_party_deps": []
        }
        "#;
        let base = MockInputTarball::new();
        base.add(meta, data);
        base.add("dart/foobar/lib/one.dart", "one");
        base.add("dart/foobar/lib/two.dart", "two");
        let complement = MockInputTarball::new();
        complement.add(meta, data);
        complement.add("dart/foobar/lib/one.dart", "one");
        complement.add("dart/foobar/lib/two.dart", "two");
        let mut output = MockOutputTarball::new();
        merge_dart_library(meta, &base, &complement, &mut output).expect("Should not fail!");
        output.assert_has_file(meta);
        output.assert_has_file("dart/foobar/lib/one.dart");
        output.assert_has_file("dart/foobar/lib/two.dart");
    }
}
