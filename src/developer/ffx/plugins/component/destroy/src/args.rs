// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "destroy",
    example = "To destroy a component instance with a given moniker:

    $ ffx component destroy /core/ffx-laboratory:foo",
    description = "Destroy a component instance in a specific collection",
    note = "The component owning the collection must also have a `use` declaration
for the fuchsia.sys2.Realm protocol in its manifest."
)]

pub struct DestroyComponentCommand {
    #[argh(positional)]
    /// A moniker to a component instance in a collection.
    pub moniker: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["destroy"];

    #[test]
    fn test_command() {
        let moniker = "/core/ffx-laboratory:foo";
        let args = &[moniker];
        assert_eq!(
            DestroyComponentCommand::from_args(CMD_NAME, args),
            Ok(DestroyComponentCommand { moniker: moniker.to_string() })
        )
    }
}
