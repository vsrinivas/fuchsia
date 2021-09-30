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
    description = "Lists component instances matching a selector",
    example = "To show services exposed by remote-control:

    $ ffx component select moniker remote-control:expose:*'

Or to show all services offered by v1 components:

    $ ffx component select moniker core/appmgr:out:*

Or to show all components that expose a capability:

    $ ffx component select capability fuchsia.sys.Loader",
    note = "Component select allows for 
1). looking up various services exposed by the
component. The command expects a <selector> with the following format:

`<component moniker>:(in|out|exposed)[:<service name>]`

Wildcards may be used anywhere in the selector.

2). looking up components that expose a capability. The command takes in 
a capability name consists of a string containing the characters a to z, 
A to Z, 0 to 9, underscore (_), hyphen (-), or the full stop character (.)."
)]
pub struct ComponentSelectCommand {
    #[argh(subcommand)]
    pub nested: SubcommandEnum,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum SubcommandEnum {
    Moniker(MonikerStruct),
    Capability(CapabilityStruct),
}

#[derive(FromArgs, PartialEq, Debug)]
/// subcommand moniker
#[argh(subcommand, name = "moniker")]
pub struct MonikerStruct {
    #[argh(positional)]
    /// output services exposed/used/offered by the moniker
    pub moniker: String,
}

#[derive(FromArgs, PartialEq, Debug)]
/// subcommand capability
#[argh(subcommand, name = "capability")]
pub struct CapabilityStruct {
    #[argh(positional)]
    /// output all components that expose the capability
    pub capability: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["select"];

    #[test]
    fn test_moniker_command() {
        fn check(args: &[&str], expected_moniker: String) {
            assert_eq!(
                ComponentSelectCommand::from_args(CMD_NAME, args),
                Ok(ComponentSelectCommand {
                    nested: SubcommandEnum::Moniker(MonikerStruct { moniker: expected_moniker },)
                })
            )
        }

        check(&["moniker", "remote-control:expose:*"], "remote-control:expose:*".to_string());
        check(&["moniker", "core/appmgr:out:*"], "core/appmgr:out:*".to_string());
    }

    #[test]
    fn test_capability_command() {
        fn check(args: &[&str], expected_capability: String) {
            assert_eq!(
                ComponentSelectCommand::from_args(CMD_NAME, args),
                Ok(ComponentSelectCommand {
                    nested: SubcommandEnum::Capability(CapabilityStruct {
                        capability: expected_capability,
                    },)
                })
            )
        }

        check(&["capability", "fuchsia.appmgr.Startup"], "fuchsia.appmgr.Startup".to_string());
        check(&["capability", "diagnostics"], "diagnostics".to_string());
    }
}
