// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use sdk_metadata::DartLibrary;

use crate::app::Result;
use crate::file_provider::{merge_files, FileProvider};
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for DartLibrary {
    fn get_common_files(&self) -> Vec<String> {
        self.sources.clone()
    }
}

pub fn merge_dart_library<F: TarballContent>(
    meta_path: &str, base: &impl InputTarball<F>, complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    let base_meta: DartLibrary = base.get_metadata(meta_path)?;
    let complement_meta: DartLibrary = complement.get_metadata(meta_path)?;
    merge_files(&base_meta, base, &complement_meta, complement, output)?;

    output.write_json(meta_path, &base_meta)?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use crate::testing::{MockInputTarball, MockOutputTarball};

    use sdk_metadata::JsonObject;

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
        let out_data = DartLibrary::new(output.get_content(meta).as_bytes()).unwrap();
        assert!(out_data.sources.len() == 2);
    }
}
