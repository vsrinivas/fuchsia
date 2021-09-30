// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "destroy",
    description = "Destroys a component instance, removing it from the component topology",
    example = "To destroy a component instance designated by the moniker `/core/ffx-laboratory:foo`:

    $ ffx component destroy /core/ffx-laboratory:foo",
    note = "To learn more about running components, visit https://fuchsia.dev/fuchsia-src/development/components/run"
)]

pub struct DestroyComponentCommand {
    #[argh(positional)]
    /// A moniker to a component instance in a collection.
    /// This component instance will be removed from the topology if this command succeeds.
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
