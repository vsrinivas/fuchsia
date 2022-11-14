// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::{FromArgValue, FromArgs},
    ffx_core::ffx_command,
    std::path::PathBuf,
};

#[derive(Clone, Debug, PartialEq)]
pub enum CapabilityType {
    Directory,
    Protocol,
}

impl FromArgValue for CapabilityType {
    fn from_arg_value(value: &str) -> Result<Self, String> {
        match value {
            "directory" => Ok(Self::Directory),
            "protocol" => Ok(Self::Protocol),
            _ => Err(format!("Unsupported capability type \"{}\"; possible values are: \"directory\", \"protocol\".", value)),
        }
    }
}

impl Into<String> for CapabilityType {
    fn into(self) -> String {
        String::from(match self {
            Self::Directory => "directory",
            Self::Protocol => "protocol",
        })
    }
}

#[derive(Clone, Debug, PartialEq)]
pub enum ResponseLevel {
    Verbose,
    All,
    Warn,
    Error,
}

impl FromArgValue for ResponseLevel {
    fn from_arg_value(value: &str) -> Result<Self, String> {
        match value {
            "verbose" => Ok(Self::Verbose),
            "all" => Ok(Self::All),
            "warn" => Ok(Self::Warn),
            "error" => Ok(Self::Error),
            _ => Err(format!("Unsupported response level \"{}\"; possible values are: \"verbose\", \"all\", \"warn\", \"error\".", value)),
        }
    }
}

impl Into<String> for ResponseLevel {
    fn into(self) -> String {
        String::from(match self {
            Self::Verbose => "verbose",
            Self::All => "all",
            Self::Warn => "warn",
            Self::Error => "error",
        })
    }
}

pub fn default_capability_types() -> Vec<CapabilityType> {
    vec![CapabilityType::Directory, CapabilityType::Protocol]
}

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "routes",
    description = "Verifies capability routes in the component tree",
    example = r#"To verify routes on your current build:

    $ ffx scrutiny verify routes \
        --product-bundle $(fx get-build-dir)/obj/build/images/fuchsia/product_bundle \
        --allowlist ../../src/security/policy/build/verify_routes_exceptions_allowlist.json5"#
)]
pub struct Command {
    /// capability types to verify.
    #[argh(option)]
    pub capability_type: Vec<CapabilityType>,
    /// response level to report from routes scrutiny plugin.
    #[argh(option, default = "ResponseLevel::Error")]
    pub response_level: ResponseLevel,
    /// absolute or working directory-relative path to a product bundle.
    #[argh(option)]
    pub product_bundle: PathBuf,
    /// absolute or working directory-relative path(s) to allowlist(s) used to verify routes.
    #[argh(option)]
    pub allowlist: Vec<PathBuf>,
    /// absolute or working path-relative path to component tree configuration file that affects
    /// how component tree data is gathered.
    #[argh(option)]
    pub component_tree_config: Option<PathBuf>,
}
