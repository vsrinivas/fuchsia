// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

// TODO (72655): Unified selector format for selecting capabilities and selecting moniker.
#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "select",
    description = "Lists component instances that expose/use a capability",
    example = "To show all components that expose/use a capability:

    $ ffx component select capability fuchsia.sys.Loader"
)]
pub struct ComponentSelectCommand {
    #[argh(subcommand)]
    pub nested: SubCommandEnum,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubCommandEnum {
    Capability(CapabilityStruct),
}

#[derive(FromArgs, PartialEq, Debug)]
/// subcommand capability
#[argh(subcommand, name = "capability")]
pub struct CapabilityStruct {
    #[argh(positional)]
    /// output all components that expose/use the capability
    pub capability: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["select"];

    #[test]
    fn test_capability_command() {
        fn check(args: &[&str], expected_capability: String) {
            assert_eq!(
                ComponentSelectCommand::from_args(CMD_NAME, args),
                Ok(ComponentSelectCommand {
                    nested: SubCommandEnum::Capability(CapabilityStruct {
                        capability: expected_capability,
                    },)
                })
            )
        }

        check(&["capability", "fuchsia.appmgr.Startup"], "fuchsia.appmgr.Startup".to_string());
        check(&["capability", "diagnostics"], "diagnostics".to_string());
    }
}
