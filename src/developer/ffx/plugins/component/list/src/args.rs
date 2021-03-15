// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list",
    description = "List all components, with the option of listing only cmx/cml components",
    example = "To list all components in the topology:

    $ ffx component list
    
    To list all cmx components in the topology:
    
    $ ffx component list --only cmx
    
    To list all cml components in the topology:
    
    $ ffx comopnent list --only cml",
    note = "Lists all the components on the running target. If no <component_type> is entered, 
the default option outputs a tree of all cmx and cml components on the system. If a valid <component_type>
is entered, the command outputs a tree of only cmx/cml component in the system.

If the command fails or times out, ensure RCS is running on the target.
This can be verified by running `ffx target list` and seeing the status
on the RCS column.",
    error_code(1, "The command has timed out")
)]

pub struct ComponentListCommand {
    #[argh(option, long = "only", short = 'o')]
    /// output only cmx/cml components depending on the flag.
    pub component_type: Option<String>,

    #[argh(switch, long = "verbose", short = 'v')]
    /// whether or not to display a column showing component type
    pub verbose: bool,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["list"];

    #[test]
    fn test_command() {
        fn check(args: &[&str], expected_component_type: Option<String>, expected_verbose: bool) {
            assert_eq!(
                ComponentListCommand::from_args(CMD_NAME, args),
                Ok(ComponentListCommand {
                    component_type: expected_component_type,
                    verbose: expected_verbose
                })
            )
        }

        check(&["--only", "cmx", "--verbose"], Some("cmx".to_string()), true);
        check(&["--only", "cml", "--verbose"], Some("cml".to_string()), true);
        check(&["-o", "cmx", "--verbose"], Some("cmx".to_string()), true);
        check(&["-o", "cml", "--verbose"], Some("cml".to_string()), true);
        check(&["--only", "cmx", "-v"], Some("cmx".to_string()), true);
        check(&["--only", "cml"], Some("cml".to_string()), false);
        check(&["-v"], None, true);
    }
}
