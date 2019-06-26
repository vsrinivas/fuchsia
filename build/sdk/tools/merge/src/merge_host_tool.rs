// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use sdk_metadata::{HostTool, JsonObject, TargetArchitecture};

use crate::app::Result;
use crate::file_provider::{merge_files, FileProvider};
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for HostTool {
    fn get_common_files(&self) -> Vec<String> {
        if let Some(files) = self.files.clone() {
            return files;
        }
        Vec::new()
    }

    fn get_arch_files(&self) -> HashMap<TargetArchitecture, Vec<String>> {
        if let Some(files) = self.target_files.clone() {
            return files;
        }
        HashMap::new()
    }
}

pub fn merge_host_tool<F: TarballContent>(
    meta_path: &str, base: &impl InputTarball<F>, complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    let base_meta: HostTool = base.get_metadata(meta_path)?;
    let complement_meta: HostTool = complement.get_metadata(meta_path)?;
    merge_files(&base_meta, base, &complement_meta, complement, output)?;

    let mut meta = base_meta.clone();
    if let Some(complement_target_files) = &complement_meta.target_files {
        if let Some(meta_target_files) = &mut meta.target_files {
            for (arch, files) in complement_target_files {
                meta_target_files.insert(arch.clone(), files.to_vec());
            }
        } else {
            meta.target_files = complement_meta.target_files.clone();
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
        let meta = "tools/foobar-meta.json";
        let data = r#"
        {
            "name": "foobar",
            "type": "host_tool",
            "root": "tools",
            "files": [
                "tools/foobar"
            ]
        }
        "#;
        let base = MockInputTarball::new();
        base.add(meta, data);
        base.add("tools/foobar", "I am a tool");
        let complement = MockInputTarball::new();
        complement.add(meta, data);
        complement.add("tools/foobar", "I am a tool");
        let mut output = MockOutputTarball::new();
        merge_host_tool(meta, &base, &complement, &mut output).expect("Should not fail!");
        output.assert_has_file(meta);
        output.assert_has_file("tools/foobar");
    }

    #[test]
    fn test_merge_multiple_architectures() {
        let meta = "tools/foobar-meta.json";
        let base_data = r#"
        {
            "name": "foobar",
            "type": "host_tool",
            "root": "tools",
            "target_files": {
                "x64": [
                    "tools/foobar_x64"
                ]
            }
        }
        "#;
        let complement_data = r#"
        {
            "name": "foobar",
            "type": "host_tool",
            "root": "tools",
            "target_files": {
                "arm64": [
                    "tools/foobar_arm64"
                ]
            }
        }
        "#;
        let base = MockInputTarball::new();
        base.add(meta, base_data);
        base.add("tools/foobar_x64", "I am a tool");
        let complement = MockInputTarball::new();
        complement.add(meta, complement_data);
        complement.add("tools/foobar_arm64", "I am a tool too");
        let mut output = MockOutputTarball::new();
        merge_host_tool(meta, &base, &complement, &mut output).expect("Should not fail!");
        output.assert_has_file(meta);
        output.assert_has_file("tools/foobar_x64");
        output.assert_has_file("tools/foobar_arm64");
        let data = HostTool::new(output.get_content(meta).as_bytes()).unwrap();
        assert!(data.target_files.is_some());
        assert!(
            data.target_files.unwrap().len() == 2,
            "Invalid number of architectures"
        );
    }
}
