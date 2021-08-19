// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "create",
    example = "To create a component instance of the hello-world-rust component
    in the ffx-laboratory collection:

    $ ffx component create \\
    /core/ffx-laboratory:foo \\
    fuchsia-pkg://fuchsia.com/hello-world#meta/hello-world-rust.cm",
    description = "Create a component instance in a specific collection",
    note = "The component owning the collection must also have a `use` declaration
for the fuchsia.sys2.Realm protocol in its manifest.
    
The <url> must follow the format:

`fuchsia-pkg://fuchsia.com/<package>#meta/<component>.cm`"
)]

pub struct CreateComponentCommand {
    #[argh(positional)]
    /// A moniker to a (currently non-existent) component instance in a collection.
    /// The component instance will be created at this moniker if the command succeeds.
    pub moniker: String,

    #[argh(positional)]
    /// url of component to run
    pub url: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["create"];

    #[test]
    fn test_command() {
        let moniker = "/core/ffx-laboratory:foo";
        let url = "fuchsia-pkg://fuchsia.com/test#meta/test.cm";
        let args = &[moniker, url];
        assert_eq!(
            CreateComponentCommand::from_args(CMD_NAME, args),
            Ok(CreateComponentCommand { moniker: moniker.to_string(), url: url.to_string() })
        )
    }
}
