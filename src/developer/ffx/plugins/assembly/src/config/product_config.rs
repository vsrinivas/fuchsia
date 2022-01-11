// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::config as image_assembly_config;
use crate::config::FileEntry;
use serde::{Deserialize, Serialize};
use std::collections::BTreeMap;

/// Configuration for a Product Assembly operation.  This is a high-level operation
/// that takes a more abstract description of what is desired in the assembled
/// product images, and then generates the complete Image Assembly configuration
/// (`crate::config::ProductConfig`) from that.
#[derive(Debug, Deserialize, Serialize)]
pub struct ProductAssemblyConfig {
    pub build_type: BuildType,
}

#[derive(Debug, Deserialize, Serialize, PartialEq)]
pub enum BuildType {
    #[serde(rename = "eng")]
    Eng,
}

/// A bundle of inputs to be used in the assembly of a product.  This is closely
/// related to the ImageAssembly Product config, but has more fields.
#[derive(Debug, Default, Deserialize, Serialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct AssemblyInputBundle {
    /// The Image Assembly's ProductConfiguration is most of the fields here, so
    /// it's re-used to gain access to the methods it has for merging.
    #[serde(flatten)]
    pub image_assembly: image_assembly_config::PartialProductConfig,

    /// Entries for the `config_data` package.
    #[serde(default)]
    pub config_data: BTreeMap<String, Vec<FileEntry>>,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::{config::PartialKernelConfig, util};
    use std::path::PathBuf;

    #[test]
    fn test_product_assembly_config_from_json5() {
        let json5 = r#"
            {
              build_type: "eng"
            }
        "#;

        let mut cursor = std::io::Cursor::new(json5);
        let config: ProductAssemblyConfig = util::from_reader(&mut cursor).unwrap();
        assert_eq!(config.build_type, BuildType::Eng);
    }

    #[test]
    fn test_assembly_input_bundle_from_json5() {
        let json5 = r#"
            {
                // json5 files can have comments in them.
                system: ["package0"],
                base: ["package1", "package2"],
                cache: ["package3", "package4"],
                kernel: {
                  path: "path/to/kernel",
                  args: ["arg1", "arg2"],
                clock_backstop: 0,
                },
                // and lists can have trailing commas
                boot_args: ["arg1", "arg2", ],
                bootfs_files: [
                  {
                    source: "path/to/source",
                    destination: "path/to/destination",
                  }
                ],
                config_data: {
                    "package1": [
                        {
                            source: "path/to/source.json",
                            destination: "config.json"
                        }
                    ]
                }
            }
        "#;
        let bundle =
            util::from_reader::<_, AssemblyInputBundle>(&mut std::io::Cursor::new(json5)).unwrap();
        assert_eq!(bundle.image_assembly.system, vec!(PathBuf::from("package0")));
        assert_eq!(
            bundle.image_assembly.base,
            vec!(PathBuf::from("package1"), PathBuf::from("package2"))
        );
        assert_eq!(
            bundle.image_assembly.cache,
            vec!(PathBuf::from("package3"), PathBuf::from("package4"))
        );
        let expected_kernel = PartialKernelConfig {
            path: Some(PathBuf::from("path/to/kernel")),
            args: vec!["arg1".to_string(), "arg2".to_string()],
            clock_backstop: Some(0),
        };
        assert_eq!(bundle.image_assembly.kernel, Some(expected_kernel));
        assert_eq!(bundle.image_assembly.boot_args, vec!("arg1".to_string(), "arg2".to_string()));
        assert_eq!(
            bundle.image_assembly.bootfs_files,
            vec!(FileEntry {
                source: PathBuf::from("path/to/source"),
                destination: "path/to/destination".to_string()
            })
        );
        assert_eq!(
            bundle.config_data.get("package1").unwrap(),
            &vec!(FileEntry {
                source: PathBuf::from("path/to/source.json"),
                destination: "config.json".to_string()
            })
        );
    }
}
