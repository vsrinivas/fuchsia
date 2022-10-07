// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate as image_assembly_config;
use crate::FileEntry;
use assembly_package_utils::{PackageInternalPathBuf, PackageManifestPathBuf, SourcePathBuf};
use camino::Utf8PathBuf;
use serde::{Deserialize, Serialize};
use std::collections::{BTreeMap, BTreeSet};

/// Configuration for a Product Assembly operation.  This is a high-level operation
/// that takes a more abstract description of what is desired in the assembled
/// product images, and then generates the complete Image Assembly configuration
/// (`crate::config::ImageAssemblyConfig`) from that.
#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ProductAssemblyConfig {
    pub platform: PlatformConfig,
    pub product: ProductConfig,
}

/// Platform configuration options.  These are the options that pertain to the
/// platform itself, not anything provided by the product.
#[derive(Debug, Deserialize, Serialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct PlatformConfig {
    /// The minimum service-level that the platform will provide, or the main
    /// set of platform features that are necessary (or desired) by the product.
    ///
    /// This is the most-significant determination of the availability of major
    /// subsystems.
    #[serde(default)]
    pub feature_set_level: FeatureSupportLevel,

    /// The RFC-0115 Build Type of the assembled product + platform.
    ///
    /// https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0115_build_types
    ///
    /// After the FeatureSupportLevel, this is the next most-influential
    /// determinant of the makeup of the platform.  It selects platform
    /// components and configuration, and is used to disallow various platform
    /// configuration settings when producing Userdebug and User images.
    pub build_type: BuildType,

    /// List of logging tags to forward to the serial console.
    ///
    /// Appended to the list of tags defined for the platform.
    #[serde(default)]
    pub additional_serial_log_tags: Vec<String>,

    /// Platform configuration options for the identity area.
    #[serde(default)]
    pub identity: PlatformIdentityConfig,

    /// Platform configuration options for the input area.
    #[serde(default)]
    pub input: PlatformInputConfig,
}

/// The platform's base service level.
///
/// This is the basis for the contract with the product as to what the minimal
/// set of services that are available in the platform will be.  Features can
/// be enabled on top of this most-basic level, but some features will require
/// a higher basic level of support.
///
/// These are (initially) based on the product definitions that are used to
/// provide the basis for all other products:
///
/// bringup.gni
///   +--> minimal.gni
///         +--> core.gni
///               +--> (everything else)
#[derive(Debug, Deserialize, Serialize, PartialEq)]
pub enum FeatureSupportLevel {
    /// THIS IS FOR TESTING AND MIGRATIONS ONLY!
    ///
    /// It creates an assembly with no platform.
    #[serde(rename = "empty")]
    Empty,

    /// Bootable, but serial-only.  No netstack, no storage drivers, etc.  this
    /// is the smallest bootable system, and is primarily used for board-level
    /// bringup.
    ///
    /// https://fuchsia.dev/fuchsia-src/development/build/build_system/bringup
    #[serde(rename = "bringup")]
    Bringup,

    /// This is the smallest "full Fuchsia" configuration.  This has a netstack,
    /// can update itself, and has all the subsystems that are required to
    /// ship a production-level product.
    ///
    /// This is the default level unless otherwise specified.
    #[serde(rename = "minimal")]
    Minimal,
    // Core  (in the future)
}

impl Default for FeatureSupportLevel {
    fn default() -> Self {
        FeatureSupportLevel::Minimal
    }
}

/// The platform BuildTypes.
///
/// These control security and behavioral settings within the platform, and can
/// change the platform packages placed into the assembled product image.
///
#[derive(Debug, Deserialize, Serialize, PartialEq)]
pub enum BuildType {
    #[serde(rename = "eng")]
    Eng,

    #[serde(rename = "userdebug")]
    UserDebug,

    #[serde(rename = "user")]
    User,
}

/// Platform configuration options for the identity area.
#[derive(Debug, Default, Deserialize, Serialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct PlatformIdentityConfig {
    /// Whether the pinweaver protocol should be enabled in `password_authenticator` on supported
    /// boards. Pinweaver will always be disabled if the board does not support the protocol.
    #[serde(default)]
    pub password_pinweaver: FeatureControl,
}

/// Platform configuration options for the input area.
#[derive(Debug, Default, Deserialize, Serialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct PlatformInputConfig {
    /// The idle threshold is how much time has passed since the last user
    /// input activity for the system to become idle.
    #[serde(default)]
    pub idle_threshold_minutes: Option<u64>,

    /// The relevant input device bindings from which to install appropriate
    /// input handlers. Default to an empty set.
    #[serde(default)]
    pub supported_input_devices: Vec<InputDeviceType>,
}

/// Options for input devices that may be supported.
#[derive(Clone, Debug, Deserialize, Serialize, PartialEq)]
#[serde(rename_all = "lowercase", deny_unknown_fields)]
pub enum InputDeviceType {
    Button,
    Keyboard,
    LightSensor,
    Mouse,
    Touchscreen,
}

/// Options for features that may either be forced on, forced off, or allowed
/// to be either on or off. Features default to disabled.
#[derive(Debug, Deserialize, Serialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub enum FeatureControl {
    #[serde(rename = "disabled")]
    Disabled,

    #[serde(rename = "allowed")]
    Allowed,

    #[serde(rename = "required")]
    Required,
}

impl Default for FeatureControl {
    fn default() -> Self {
        FeatureControl::Disabled
    }
}

impl PartialEq<FeatureControl> for &FeatureControl {
    fn eq(&self, other: &FeatureControl) -> bool {
        self.eq(&other)
    }
}

/// The Product-provided configuration details.
#[derive(Debug, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ProductConfig {
    #[serde(default)]
    pub packages: ProductPackagesConfig,

    /// List of base drivers to include in the product.
    #[serde(default)]
    pub drivers: Vec<DriverDetails>,

    /// Start URL to pass to `session_manager`.
    ///
    /// Default to the empty string which creates a "paused" config that launches nothing to start.
    #[serde(default)]
    pub session_url: String,
}

/// Packages provided by the product, to add to the assembled images.
///
/// This also includes configuration for those packages:
///
/// ```json5
///   packages: {
///     base: [
///       {
///         manifest: "path/to/package_a/package_manifest.json",
///       },
///       {
///         manifest: "path/to/package_b/package_manifest.json",
///         config_data: {
///           "foo.cfg": "path/to/some/source/file/foo.cfg",
///           "bar/more/data.json": "path/to/some.json",
///         },
///       },
///     ],
///     cache: []
///   }
/// ```
///
#[derive(Debug, Default, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ProductPackagesConfig {
    /// Paths to package manifests, or more detailed json entries for packages
    /// to add to the 'base' package set.
    #[serde(default)]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub base: Vec<ProductPackageDetails>,

    /// Paths to package manifests, or more detailed json entries for packages
    /// to add to the 'cache' package set.
    #[serde(default)]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub cache: Vec<ProductPackageDetails>,
}

/// Describes in more detail a package to add to the assembly.
#[derive(Debug, PartialEq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ProductPackageDetails {
    /// Path to the package manifest for this package.
    pub manifest: PackageManifestPathBuf,

    /// Map of config_data entries for this package, from the destination path
    /// within the package, to the path where the source file is to be found.
    #[serde(default)]
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub config_data: Vec<ProductConfigData>,
}

impl From<PackageManifestPathBuf> for ProductPackageDetails {
    fn from(manifest: PackageManifestPathBuf) -> Self {
        Self { manifest, config_data: Vec::default() }
    }
}

impl From<&str> for ProductPackageDetails {
    fn from(s: &str) -> Self {
        ProductPackageDetails { manifest: s.into(), config_data: Vec::default() }
    }
}

#[derive(Debug, PartialEq, Deserialize, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ProductConfigData {
    /// Path to the config file on the host.
    pub source: SourcePathBuf,

    /// Path to find the file in the package on the target.
    pub destination: PackageInternalPathBuf,
}

/// A typename to clarify intent around what Strings are package names.
type PackageName = String;

/// A typename to represent a package that contains shell command binaries,
/// and the paths to those binaries
pub type ShellCommands = BTreeMap<PackageName, BTreeSet<PackageInternalPathBuf>>;

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

    /// Configuration of driver packages. Driver packages should not be listed
    /// in the base package list and will be included automatically.
    #[serde(default)]
    pub base_drivers: Vec<DriverDetails>,

    /// Map of the names of packages that contain shell commands to the list of
    /// commands within each.
    #[serde(default)]
    pub shell_commands: ShellCommands,
}

#[derive(Clone, Debug, Default, Deserialize, Serialize, PartialEq)]
#[serde(deny_unknown_fields)]
pub struct DriverDetails {
    /// The package containing the driver.
    pub package: Utf8PathBuf,

    /// The driver components within the package, e.g. meta/foo.cm.
    pub components: Vec<Utf8PathBuf>,
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
        assert_eq!(config.platform.feature_set_level, FeatureSupportLevel::Minimal);
    }

    #[test]
    fn test_bringup_product_assembly_config_from_json5() {
        let json5 = r#"
            {
              platform: {
                feature_set_level: "bringup",
                build_type: "eng",
              },
              product: {},
            }
        "#;

        let mut cursor = std::io::Cursor::new(json5);
        let config: ProductAssemblyConfig = util::from_reader(&mut cursor).unwrap();
        assert_eq!(config.platform.build_type, BuildType::Eng);
        assert_eq!(config.platform.feature_set_level, FeatureSupportLevel::Bringup);
    }

    #[test]
    fn test_minimal_product_assembly_config_from_json5() {
        let json5 = r#"
            {
              platform: {
                feature_set_level: "minimal",
                build_type: "eng",
              },
              product: {},
            }
        "#;

        let mut cursor = std::io::Cursor::new(json5);
        let config: ProductAssemblyConfig = util::from_reader(&mut cursor).unwrap();
        assert_eq!(config.platform.build_type, BuildType::Eng);
        assert_eq!(config.platform.feature_set_level, FeatureSupportLevel::Minimal);
    }

    #[test]
    fn test_empty_product_assembly_config_from_json5() {
        let json5 = r#"
            {
              platform: {
                feature_set_level: "empty",
                build_type: "eng",
              },
              product: {},
            }
        "#;

        let mut cursor = std::io::Cursor::new(json5);
        let config: ProductAssemblyConfig = util::from_reader(&mut cursor).unwrap();
        assert_eq!(config.platform.build_type, BuildType::Eng);
        assert_eq!(config.platform.feature_set_level, FeatureSupportLevel::Empty);
    }

    #[test]
    fn test_buildtype_deserialization_userdebug() {
        let json5 = r#"
            {
              platform: {
                build_type: "userdebug",
              },
              product: {},
            }
        "#;

        let mut cursor = std::io::Cursor::new(json5);
        let config: ProductAssemblyConfig = util::from_reader(&mut cursor).unwrap();
        assert_eq!(config.platform.build_type, BuildType::UserDebug);
    }

    #[test]
    fn test_buildtype_deserialization_user() {
        let json5 = r#"
            {
              platform: {
                build_type: "user",
              },
              product: {},
            }
        "#;

        let mut cursor = std::io::Cursor::new(json5);
        let config: ProductAssemblyConfig = util::from_reader(&mut cursor).unwrap();
        assert_eq!(config.platform.build_type, BuildType::User);
    }

    #[test]
    fn test_product_assembly_config_with_product_provided_parts() {
        let json5 = r#"
            {
              platform: {
                build_type: "eng",
                identity : {
                    "password_pinweaver": "allowed",
                }
              },
              product: {
                  packages: {
                      base: [
                          { manifest: "path/to/base/package_manifest.json" }
                      ],
                      cache: [
                          { manifest: "path/to/cache/package_manifest.json" }
                      ]
                  },
                  drivers: [
                    {
                      package: "path/to/base/driver/package_manifest.json",
                      components: [ "meta/path/to/component.cml" ]
                    }
                  ]
              },
            }
        "#;

        let mut cursor = std::io::Cursor::new(json5);
        let config: ProductAssemblyConfig = util::from_reader(&mut cursor).unwrap();
        assert_eq!(config.platform.build_type, BuildType::Eng);
        assert_eq!(
            config.product.packages.base,
            vec![ProductPackageDetails {
                manifest: "path/to/base/package_manifest.json".into(),
                config_data: Vec::default()
            }]
        );
        assert_eq!(
            config.product.packages.cache,
            vec![ProductPackageDetails {
                manifest: "path/to/cache/package_manifest.json".into(),
                config_data: Vec::default()
            }]
        );
        assert_eq!(config.platform.identity.password_pinweaver, FeatureControl::Allowed);
        assert_eq!(
            config.product.drivers,
            vec![DriverDetails {
                package: "path/to/base/driver/package_manifest.json".into(),
                components: vec!["meta/path/to/component.cml".into()]
            }]
        )
    }

    #[test]
    fn test_product_provided_config_data() {
        let json5 = r#"
            {
                base: [
                    {
                        manifest: "path/to/base/package_manifest.json"
                    },
                    {
                        manifest: "some/other/manifest.json",
                        config_data: [
                            {
                                destination: "dest/path/cfg.txt",
                                source: "source/path/cfg.txt",
                            },
                            {
                                destination: "other_data.json",
                                source: "source_other_data.json",
                            },
                        ]
                    }
                  ],
                cache: [
                    {
                        manifest: "path/to/cache/package_manifest.json"
                    }
                ]
            }
        "#;

        let mut cursor = std::io::Cursor::new(json5);
        let packages: ProductPackagesConfig = util::from_reader(&mut cursor).unwrap();
        assert_eq!(
            packages.base,
            vec![
                ProductPackageDetails::from("path/to/base/package_manifest.json"),
                ProductPackageDetails {
                    manifest: "some/other/manifest.json".into(),
                    config_data: vec![
                        ProductConfigData {
                            destination: "dest/path/cfg.txt".into(),
                            source: "source/path/cfg.txt".into(),
                        },
                        ProductConfigData {
                            destination: "other_data.json".into(),
                            source: "source_other_data.json".into(),
                        },
                    ]
                }
            ]
        );
        assert_eq!(packages.cache, vec!["path/to/cache/package_manifest.json".into()]);
    }

    #[test]
    fn product_package_details_deserialization() {
        let json5 = r#"
            {
                manifest: "some/other/manifest.json",
                config_data: [
                    {
                        destination: "dest/path/cfg.txt",
                        source: "source/path/cfg.txt",
                    },
                    {
                        destination: "other_data.json",
                        source: "source_other_data.json",
                    },
                ]
            }
        "#;
        let expected = ProductPackageDetails {
            manifest: "some/other/manifest.json".into(),
            config_data: vec![
                ProductConfigData {
                    destination: "dest/path/cfg.txt".into(),
                    source: "source/path/cfg.txt".into(),
                },
                ProductConfigData {
                    destination: "other_data.json".into(),
                    source: "source_other_data.json".into(),
                },
            ],
        };
        let mut cursor = std::io::Cursor::new(json5);
        let details: ProductPackageDetails = util::from_reader(&mut cursor).unwrap();
        assert_eq!(details, expected);
    }

    #[test]
    fn product_package_details_serialization() {
        let entries = vec![
            ProductPackageDetails {
                manifest: "path/to/manifest.json".into(),
                config_data: Vec::default(),
            },
            ProductPackageDetails {
                manifest: "another/path/to/a/manifest.json".into(),
                config_data: vec![
                    ProductConfigData {
                        destination: "dest/path/A".into(),
                        source: "source/path/A".into(),
                    },
                    ProductConfigData {
                        destination: "dest/path/B".into(),
                        source: "source/path/B".into(),
                    },
                ],
            },
        ];
        let serialized = serde_json::to_value(&entries).unwrap();
        let expected = serde_json::json!(
            [
                {
                    "manifest": "path/to/manifest.json"
                },
                {
                    "manifest": "another/path/to/a/manifest.json",
                    "config_data": [
                        {
                            "destination": "dest/path/A",
                            "source": "source/path/A",
                        },
                        {
                            "destination": "dest/path/B",
                            "source": "source/path/B",
                        },
                    ]
                }
            ]
        );
        assert_eq!(serialized, expected);
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
              },
              base_drivers: [
                {
                  package: "path/to/driver",
                  components: ["path/to/1234", "path/to/5678"]
                }
              ],
              shell_commands: {
                "package1": ["path/to/binary1", "path/to/binary2"]
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
        assert_eq!(
            bundle.base_drivers[0],
            DriverDetails {
                package: Utf8PathBuf::from("path/to/driver"),
                components: vec!(
                    Utf8PathBuf::from("path/to/1234"),
                    Utf8PathBuf::from("path/to/5678")
                )
            }
        );
        assert_eq!(
            bundle.shell_commands.get("package1").unwrap(),
            &BTreeSet::from([
                PackageInternalPathBuf::from("path/to/binary1"),
                PackageInternalPathBuf::from("path/to/binary2"),
            ])
        );
    }
}
