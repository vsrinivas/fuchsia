// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::collections::HashMap;

use sdk_metadata::{JsonObject, Sysroot, TargetArchitecture};

use crate::app::Result;
use crate::file_provider::{merge_files, FileProvider};
use crate::tarball::{InputTarball, OutputTarball, TarballContent};

impl FileProvider for Sysroot {
    fn get_arch_files(&self) -> HashMap<TargetArchitecture, Vec<String>> {
        let mut result = HashMap::new();
        for (arch, version) in &self.versions {
            let mut files = Vec::new();
            files.extend(version.headers.clone());
            files.extend(version.dist_libs.clone());
            files.extend(version.link_libs.clone());
            files.extend(version.debug_libs.clone());
            result.insert(arch.clone(), files);
        }
        result
    }
}

pub fn merge_sysroot<F: TarballContent>(
    meta_path: &str, base: &impl InputTarball<F>, complement: &impl InputTarball<F>,
    output: &mut impl OutputTarball<F>,
) -> Result<()> {
    let base_meta: Sysroot = base.get_metadata(meta_path)?;
    let complement_meta: Sysroot = complement.get_metadata(meta_path)?;
    merge_files(&base_meta, base, &complement_meta, complement, output)?;

    let mut meta = base_meta.clone();
    for (arch, group) in &complement_meta.versions {
        if !meta.versions.contains_key(arch) {
            meta.versions.insert(arch.clone(), group.clone());
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
        let meta = "pkg/sysroot/meta.json";
        let base_data = r#"
        {
            "name": "sysroot",
            "type": "sysroot",
            "versions": {
                "x64": {
                    "root": "arch/x64/sysroot",
                    "include_dir": "arch/x64/sysroot/include/",
                    "headers": [
                        "arch/x64/sysroot/include/foo.h"
                    ],
                    "dist_dir": "arch/x64/sysroot/dist",
                    "dist_libs": [
                        "arch/x64/sysroot/dist/one.so",
                        "arch/x64/sysroot/dist/two.so"
                    ],
                    "link_libs": [
                        "arch/x64/sysroot/lib/one.so",
                        "arch/x64/sysroot/lib/two.so"
                    ],
                    "debug_libs": [
                        ".build-id/aa/bb.so",
                        ".build-id/cc/dd.so"
                    ]
                }
            }
        }
        "#;
        let complement_data = r#"
        {
            "name": "sysroot",
            "type": "sysroot",
            "versions": {
                "arm64": {
                    "root": "arch/arm64/sysroot",
                    "include_dir": "arch/arm64/sysroot/include/",
                    "headers": [
                        "arch/arm64/sysroot/include/foo.h"
                    ],
                    "dist_dir": "arch/arm64/sysroot/dist",
                    "dist_libs": [
                        "arch/arm64/sysroot/dist/one.so",
                        "arch/arm64/sysroot/dist/two.so"
                    ],
                    "link_libs": [
                        "arch/arm64/sysroot/lib/one.so",
                        "arch/arm64/sysroot/lib/two.so"
                    ],
                    "debug_libs": [
                        ".build-id/ee/ff.so",
                        ".build-id/gg/hh.so"
                    ]
                }
            }
        }
        "#;
        let base = MockInputTarball::new();
        base.add(meta, base_data);
        base.add("arch/x64/sysroot/include/foo.h", "include");
        base.add("arch/x64/sysroot/dist/one.so", "dist_one");
        base.add("arch/x64/sysroot/dist/two.so", "dist_two");
        base.add("arch/x64/sysroot/lib/one.so", "lib_one");
        base.add("arch/x64/sysroot/lib/two.so", "lib_two");
        base.add(".build-id/aa/bb.so", "debug_one");
        base.add(".build-id/cc/dd.so", "debug_two");
        let complement = MockInputTarball::new();
        complement.add(meta, complement_data);
        complement.add("arch/arm64/sysroot/include/foo.h", "arm_include");
        complement.add("arch/arm64/sysroot/dist/one.so", "arm_dist_one");
        complement.add("arch/arm64/sysroot/dist/two.so", "arm_dist_two");
        complement.add("arch/arm64/sysroot/lib/one.so", "arm_lib_one");
        complement.add("arch/arm64/sysroot/lib/two.so", "arm_lib_two");
        complement.add(".build-id/ee/ff.so", "arm_debug_one");
        complement.add(".build-id/gg/hh.so", "arm_debug_two");
        let mut output = MockOutputTarball::new();
        merge_sysroot(meta, &base, &complement, &mut output).expect("Merge routine failed");
        output.assert_has_file(meta);
        output.assert_has_file("arch/x64/sysroot/include/foo.h");
        output.assert_has_file("arch/x64/sysroot/dist/one.so");
        output.assert_has_file("arch/x64/sysroot/dist/two.so");
        output.assert_has_file("arch/x64/sysroot/lib/one.so");
        output.assert_has_file("arch/x64/sysroot/lib/two.so");
        output.assert_has_file(".build-id/aa/bb.so");
        output.assert_has_file(".build-id/cc/dd.so");
        output.assert_has_file("arch/arm64/sysroot/include/foo.h");
        output.assert_has_file("arch/arm64/sysroot/dist/one.so");
        output.assert_has_file("arch/arm64/sysroot/dist/two.so");
        output.assert_has_file("arch/arm64/sysroot/lib/one.so");
        output.assert_has_file("arch/arm64/sysroot/lib/two.so");
        output.assert_has_file(".build-id/ee/ff.so");
        output.assert_has_file(".build-id/gg/hh.so");
        let data = Sysroot::new(output.get_content(meta).as_bytes())
            .expect("Generated metadata is invalid");
        assert!(data.versions.len() == 2, "Invalid number of architectures");
    }

    #[test]
    fn test_merge_failed() {
        let meta = "pkg/sysroot/meta.json";
        let base_data = r#"
        {
            "name": "sysroot",
            "type": "sysroot",
            "versions": {
                "x64": {
                    "root": "arch/x64/sysroot",
                    "include_dir": "arch/x64/sysroot/include/",
                    "headers": [
                        "arch/x64/sysroot/include/foo.h"
                    ],
                    "dist_dir": "arch/x64/sysroot/dist",
                    "dist_libs": [
                        "arch/x64/sysroot/dist/one.so",
                        "arch/x64/sysroot/dist/two.so"
                    ],
                    "link_libs": [
                        "arch/x64/sysroot/lib/one.so",
                        "arch/x64/sysroot/lib/two.so"
                    ],
                    "debug_libs": [
                        ".build-id/aa/bb.so",
                        ".build-id/cc/dd.so"
                    ]
                }
            }
        }
        "#;
        // This metadata is missing a debug library.
        let complement_data = r#"
        {
            "name": "sysroot",
            "type": "sysroot",
            "versions": {
                "x64": {
                    "root": "arch/x64/sysroot",
                    "include_dir": "arch/x64/sysroot/include/",
                    "headers": [
                        "arch/x64/sysroot/include/foo.h"
                    ],
                    "dist_dir": "arch/x64/sysroot/dist",
                    "dist_libs": [
                        "arch/x64/sysroot/dist/one.so",
                        "arch/x64/sysroot/dist/two.so"
                    ],
                    "link_libs": [
                        "arch/x64/sysroot/lib/one.so",
                        "arch/x64/sysroot/lib/two.so"
                    ],
                    "debug_libs": [
                        ".build-id/aa/bb.so",
                    ]
                }
            }
        }
        "#;
        let base = MockInputTarball::new();
        base.add(meta, base_data);
        base.add("arch/x64/sysroot/include/foo.h", "include");
        base.add("arch/x64/sysroot/dist/one.so", "dist_one");
        base.add("arch/x64/sysroot/dist/two.so", "dist_two");
        base.add("arch/x64/sysroot/lib/one.so", "lib_one");
        base.add("arch/x64/sysroot/lib/two.so", "lib_two");
        base.add(".build-id/aa/bb.so", "debug_one");
        base.add(".build-id/cc/dd.so", "debug_two");
        let complement = MockInputTarball::new();
        complement.add(meta, complement_data);
        complement.add("arch/x64/sysroot/include/foo.h", "include");
        complement.add("arch/x64/sysroot/dist/one.so", "dist_one");
        complement.add("arch/x64/sysroot/dist/two.so", "dist_two");
        complement.add("arch/x64/sysroot/lib/one.so", "lib_one");
        complement.add("arch/x64/sysroot/lib/two.so", "lib_two");
        complement.add(".build-id/aa/bb.so", "debug_one");
        let mut output = MockOutputTarball::new();
        let result = merge_sysroot(meta, &base, &complement, &mut output);
        assert!(result.is_err(), "Merge routine should have failed");
    }
}
