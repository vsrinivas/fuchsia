// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use sdk_metadata::{JsonObject, LoadableModule, TargetArchitecture};

use crate::app::Result;
use crate::file_provider::{merge_files, FileProvider};
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for LoadableModule {
    fn get_common_files(&self) -> Vec<String> {
        self.resources.clone()
    }

    fn get_arch_files(&self) -> HashMap<TargetArchitecture, Vec<String>> {
        self.binaries.clone()
    }
}

pub fn merge_loadable_module<F: TarballContent>(
    meta_path: &str,
    base: &impl InputTarball<F>,
    complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    let base_meta: LoadableModule = base.get_metadata(meta_path)?;
    let complement_meta: LoadableModule = complement.get_metadata(meta_path)?;
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
            "type": "loadable_module",
            "root": "pkg/foobar",
            "resources": [
                "pkg/foobar/res.one",
                "pkg/foobar/res.two"
            ],
            "binaries": {
                "x64": [
                    "arch/x64/lib/foobar.stg"
                ]
            }
        }
        "#;
        let complement_data = r#"
        {
            "name": "foobar",
            "type": "loadable_module",
            "root": "pkg/foobar",
            "resources": [
                "pkg/foobar/res.one",
                "pkg/foobar/res.two"
            ],
            "binaries": {
                "arm64": [
                    "arch/arm64/lib/foobar.stg"
                ]
            }
        }
        "#;
        let base = MockInputTarball::new();
        base.add(meta, base_data);
        base.add("pkg/foobar/res.one", "one");
        base.add("pkg/foobar/res.two", "two");
        base.add("arch/x64/lib/foobar.stg", "x64 version");
        let complement = MockInputTarball::new();
        complement.add(meta, complement_data);
        complement.add("pkg/foobar/res.one", "one");
        complement.add("pkg/foobar/res.two", "two");
        complement.add("arch/arm64/lib/foobar.stg", "arm64 version");
        let mut output = MockOutputTarball::new();
        merge_loadable_module(meta, &base, &complement, &mut output).expect("Merge routine failed");
        output.assert_has_file(meta);
        output.assert_has_file("pkg/foobar/res.one");
        output.assert_has_file("pkg/foobar/res.two");
        output.assert_has_file("arch/x64/lib/foobar.stg");
        output.assert_has_file("arch/arm64/lib/foobar.stg");
        let data = LoadableModule::new(output.get_content(meta).as_bytes())
            .expect("Generated metadata is invalid");
        assert!(data.binaries.len() == 2, "Invalid number of architectures");
    }

    #[test]
    fn test_merge_failed() {
        let meta = "pkg/foobar/meta.json";
        let base_data = r#"
        {
            "name": "foobar",
            "type": "loadable_module",
            "root": "pkg/foobar",
            "resources": [
                "pkg/foobar/res.one"
            ],
            "binaries": {
                "x64": [
                    "arch/x64/lib/foobar.stg"
                ]
            }
        }
        "#;
        // This metadata has different resources.
        let complement_data = r#"
        {
            "name": "foobar",
            "type": "loadable_module",
            "root": "pkg/foobar",
            "resources": [
                "pkg/foobar/res.two"
            ],
            "binaries": {
                "arm64": [
                    "arch/arm64/lib/foobar.stg"
                ]
            }
        }
        "#;
        let base = MockInputTarball::new();
        base.add(meta, base_data);
        base.add("pkg/foobar/res.one", "one");
        base.add("arch/x64/lib/foobar.stg", "x64 version");
        let complement = MockInputTarball::new();
        complement.add(meta, complement_data);
        complement.add("pkg/foobar/res.two", "two");
        complement.add("arch/arm64/lib/foobar.stg", "arm64 version");
        let mut output = MockOutputTarball::new();
        let result = merge_loadable_module(meta, &base, &complement, &mut output);
        assert!(result.is_err(), "Merge routine should have failed");
    }
}
