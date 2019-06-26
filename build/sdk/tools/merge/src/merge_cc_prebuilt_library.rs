// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use sdk_metadata::{CcPrebuiltLibrary, JsonObject, TargetArchitecture};

use crate::app::Result;
use crate::file_provider::{merge_files, FileProvider};
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for CcPrebuiltLibrary {
    fn get_common_files(&self) -> Vec<String> {
        self.headers.clone()
    }

    fn get_arch_files(&self) -> HashMap<TargetArchitecture, Vec<String>> {
        let mut result = HashMap::new();
        for (arch, group) in &self.binaries {
            let mut files = Vec::new();
            files.push(group.link.clone());
            if let Some(dist) = group.dist.clone() {
                files.push(dist);
            }
            if let Some(debug) = group.debug.clone() {
                files.push(debug);
            }
            result.insert(arch.clone(), files);
        }
        result
    }
}

pub fn merge_cc_prebuilt_library<F: TarballContent>(
    meta_path: &str, base: &impl InputTarball<F>, complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    let base_meta: CcPrebuiltLibrary = base.get_metadata(meta_path)?;
    let complement_meta: CcPrebuiltLibrary = complement.get_metadata(meta_path)?;
    merge_files(&base_meta, base, &complement_meta, complement, output)?;

    let mut meta = base_meta.clone();
    for (arch, group) in &complement_meta.binaries {
        if !meta.binaries.contains_key(arch) {
            meta.binaries.insert(arch.clone(), group.clone());
        }
    }
    meta.validate()?;
    output.write_json(meta_path, &meta)?;

    Ok(())
}

#[cfg(test)]
mod tests {
    use crate::testing::{MockInputTarball, MockOutputTarball};

    use super::*;

    #[test]
    fn test_merge() {
        let meta = "pkg/foobar/meta.json";
        let base_data = r#"
        {
            "name": "foobar",
            "type": "cc_prebuilt_library",
            "format": "shared",
            "root": "pkg/foobar",
            "headers": [
                "pkg/foobar/include/one.h",
                "pkg/foobar/include/two.h"
            ],
            "include_dir": "pkg/foobar/include",
            "deps": [
                "raboof"
            ],
            "binaries": {
                "x64": {
                    "link": "arch/x64/lib/libfoobar.so"
                }
            }
        }
        "#;
        let complement_data = r#"
        {
            "name": "foobar",
            "type": "cc_prebuilt_library",
            "format": "shared",
            "root": "pkg/foobar",
            "headers": [
                "pkg/foobar/include/one.h",
                "pkg/foobar/include/two.h"
            ],
            "include_dir": "pkg/foobar/include",
            "deps": [
                "raboof"
            ],
            "binaries": {
                "arm64": {
                    "link": "arch/arm64/lib/libfoobar.so"
                }
            }
        }
        "#;
        let base = MockInputTarball::new();
        base.add(meta, base_data);
        base.add("pkg/foobar/include/one.h", "one");
        base.add("pkg/foobar/include/two.h", "two");
        base.add("arch/x64/lib/libfoobar.so", "libx64");
        let complement = MockInputTarball::new();
        complement.add(meta, complement_data);
        complement.add("pkg/foobar/include/one.h", "one");
        complement.add("pkg/foobar/include/two.h", "two");
        complement.add("arch/arm64/lib/libfoobar.so", "libarm64");
        let mut output = MockOutputTarball::new();
        merge_cc_prebuilt_library(meta, &base, &complement, &mut output).expect("Should not fail!");
        output.assert_has_file(meta);
        output.assert_has_file("pkg/foobar/include/one.h");
        output.assert_has_file("pkg/foobar/include/two.h");
        output.assert_has_file("arch/x64/lib/libfoobar.so");
        output.assert_has_file("arch/arm64/lib/libfoobar.so");
        let data = CcPrebuiltLibrary::new(output.get_content(meta).as_bytes()).unwrap();
        assert!(data.binaries.len() == 2, "Invalid number of architectures");
    }
}
