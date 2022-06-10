// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate as image_assembly_config;
use crate::FileEntry;
use anyhow::ensure;
use camino::Utf8PathBuf;
use fidl_fuchsia_logger::MAX_TAGS;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};

/// ffx config flag for enabling configuring the assembly+structured config example.
const EXAMPLE_ENABLED_FLAG: &str = "assembly_example_enabled";

/// Configuration for a Product Assembly operation.  This is a high-level operation
/// that takes a more abstract description of what is desired in the assembled
/// product images, and then generates the complete Image Assembly configuration
/// (`crate::config::ImageAssemblyConfig`) from that.
#[derive(Debug, Deserialize, Serialize)]
pub struct ProductAssemblyConfig {
    pub platform: PlatformConfig,
    pub product: ProductConfig,
}

/// Platform configuration options.  These are the options that pertain to the
/// platform itself, not anything provided by the product.
#[derive(Debug, Deserialize, Serialize, PartialEq)]
pub struct PlatformConfig {
    #[serde(default)]
    pub build_type: BuildType,

    /// List of logging tags to forward to the serial console.
    ///
    /// Appended to the list of tags defined for the platform.
    #[serde(default)]
    additional_serial_log_tags: Vec<String>,
}

/// The platform BuildTypes.
///
/// These control security and behavioral settings within the platform.
///
/// Not presently used.
#[derive(Debug, Deserialize, Serialize, PartialEq)]
pub enum BuildType {
    #[serde(rename = "eng")]
    Eng,
}

impl Default for BuildType {
    fn default() -> Self {
        BuildType::Eng
    }
}

/// The Product-provided configuration details.
#[derive(Debug, Deserialize, Serialize)]
pub struct ProductConfig {
    #[serde(default)]
    pub packages: ProductPackagesConfig,

    /// Start URL to pass to `session_manager`.
    ///
    /// Default to the empty string which creates a "paused" config that launches nothing to start.
    #[serde(default)]
    session_url: String,
}

/// Packages provided by the product, to add to the assembled images.
#[derive(Debug, Default, Deserialize, Serialize)]
pub struct ProductPackagesConfig {
    /// Paths to package manifests for packages to add to the 'base' package set.
    #[serde(default)]
    pub base: Vec<Utf8PathBuf>,

    /// Paths to package manifests for packages to add to the 'cache' package set.
    #[serde(default)]
    pub cache: Vec<Utf8PathBuf>,
}

const BASE_CONSOLE_ALLOWED_TAGS: &[&str] = &[
    "console-launcher",
    "driver_manager.cm",
    "driver_host2.cm",
    "driver",
    "device",
    "mdns",
    "netcfg",
    "netstack",
    "fshost",
    "blobfs",
    "minfs",
    "wlan",
    "fxfs",
    "sshd-host",
];
static_assertions::const_assert!(BASE_CONSOLE_ALLOWED_TAGS.len() <= MAX_TAGS as usize);

const BASE_CONSOLE_DENIED_TAGS: &[&str] = &["NUD", "klog"];

impl ProductAssemblyConfig {
    /// Convert the high-level description of product configuration into a series of configuration
    /// value files with concrete component tuples.
    ///
    /// Returns a map from components manifest paths to configuration fields.
    pub fn define_bootfs_config(&self) -> anyhow::Result<PackageConfigPatch> {
        let mut bootfs_patches = PackageConfigPatch::default();

        // Configure the serial console.
        let allowed_log_tags = {
            let mut allowed_log_tags: BTreeSet<_> =
                BASE_CONSOLE_ALLOWED_TAGS.iter().map(|s| s.to_string()).collect();

            let num_product_tags = self.platform.additional_serial_log_tags.len();
            let max_product_tags = MAX_TAGS as usize - BASE_CONSOLE_ALLOWED_TAGS.len();
            ensure!(
                num_product_tags <= max_product_tags,
                "Max {max_product_tags} tags can be forwarded to serial, got {num_product_tags}."
            );
            allowed_log_tags.extend(self.platform.additional_serial_log_tags.iter().cloned());
            allowed_log_tags.into_iter().collect::<Vec<_>>()
        };
        let denied_log_tags: Vec<_> =
            BASE_CONSOLE_DENIED_TAGS.iter().map(|s| s.to_string()).collect();
        bootfs_patches
            .component("meta/console.cm")
            .field("allowed_log_tags", allowed_log_tags)
            .field("denied_log_tags", denied_log_tags);

        Ok(bootfs_patches)
    }

    /// Convert the high-level description of product configuration into a series of configuration
    /// value files with concrete package/component tuples.
    ///
    /// Returns a map from package names to configuration updates.
    pub fn define_repackaging(&self) -> anyhow::Result<StructuredConfigPatches> {
        let mut patches = PatchesBuilder::default();

        // Configure the Product Assembly + Structured Config example, if enabled.
        if should_configure_example() {
            patches
                .package("configured_by_assembly")
                .component("meta/to_configure.cm")
                .field("enable_foo", matches!(self.platform.build_type, BuildType::Eng));
        }

        // Configure the session URL.
        ensure!(
            self.product.session_url.is_empty()
                || self.product.session_url.starts_with("fuchsia-pkg://"),
            "valid session URLs must start with `fuchsia-pkg://`, got `{}`",
            self.product.session_url
        );
        patches
            .package("session_manager")
            .component("meta/session_manager.cm")
            .field("session_url", self.product.session_url.to_owned());

        Ok(patches.inner)
    }
}

/// Check ffx config for whether we should execute example code.
fn should_configure_example() -> bool {
    futures::executor::block_on(ffx_config::get::<bool, _>(EXAMPLE_ENABLED_FLAG))
        .unwrap_or_default()
}

/// A builder for collecting all of the structure configuration repackaging to perform in a given
/// system.
#[derive(Default)]
struct PatchesBuilder {
    inner: StructuredConfigPatches,
}

impl PatchesBuilder {
    fn package(&mut self, name: &str) -> &mut PackageConfigPatch {
        self.inner.entry(name.to_string()).or_default()
    }
}

/// A map from package names to patches to apply to their structured configuration.
pub type StructuredConfigPatches = BTreeMap<String, PackageConfigPatch>;

#[derive(Clone, Debug, Default)]
pub struct PackageConfigPatch {
    /// A map from manifest paths within the package namespace to the values for the component.
    pub components: BTreeMap<String, ComponentConfig>,
}

impl PackageConfigPatch {
    fn component(&mut self, pkg_path: &str) -> &mut ComponentConfig {
        assert!(
            self.components.insert(pkg_path.to_owned(), Default::default()).is_none(),
            "each component's config can only be defined once"
        );
        self.components.get_mut(pkg_path).expect("just inserted this value")
    }
}

#[derive(Clone, Debug, Default)]
pub struct ComponentConfig {
    pub fields: BTreeMap<String, serde_json::Value>,
}

impl ComponentConfig {
    fn field(&mut self, key: &str, value: impl Into<serde_json::Value>) -> &mut Self {
        assert!(
            self.fields.insert(key.to_owned(), value.into()).is_none(),
            "each configuration key can only be defined once"
        );
        self
    }
}

/// A bundle of inputs to be used in the assembly of a product.  This is closely
/// related to the ImageAssembly Product config, but has more fields.
#[derive(Debug, Default, Deserialize, Serialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct AssemblyInputBundle {
    /// The Image Assembly's ImageAssemblyConfiguration is most of the fields here, so
    /// it's re-used to gain access to the methods it has for merging.
    #[serde(flatten)]
    pub image_assembly: image_assembly_config::PartialImageAssemblyConfig,

    /// Entries for the `config_data` package.
    #[serde(default)]
    pub config_data: BTreeMap<String, Vec<FileEntry>>,

    /// The blobs index of the AIB.  This currently isn't used by product
    /// assembly, as the package manifests contain the same information.
    #[serde(default)]
    pub blobs: Vec<Utf8PathBuf>,
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::PartialKernelConfig;
    use assembly_util as util;
    use std::path::PathBuf;

    #[test]
    fn test_product_assembly_config_from_json5() {
        let json5 = r#"
            {
              platform: {
                build_type: "eng",
              },
              product: {},
            }
        "#;

        let mut cursor = std::io::Cursor::new(json5);
        let config: ProductAssemblyConfig = util::from_reader(&mut cursor).unwrap();
        assert_eq!(config.platform.build_type, BuildType::Eng);
    }

    #[test]
    fn test_product_assembly_config_with_product_provided_parts() {
        let json5 = r#"
            {
              platform: {
              },
              product: {
                  packages: {
                      base: [
                          "path/to/base/package_manifest.json"
                      ],
                      cache: [
                          "path/to/cache/package_manifest.json"
                      ]
                  }
              },
            }
        "#;

        let mut cursor = std::io::Cursor::new(json5);
        let config: ProductAssemblyConfig = util::from_reader(&mut cursor).unwrap();
        assert_eq!(config.platform.build_type, BuildType::Eng);
        assert_eq!(config.product.packages.base, vec!["path/to/base/package_manifest.json"]);
        assert_eq!(config.product.packages.cache, vec!["path/to/cache/package_manifest.json"]);
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
