// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "reload",
    description = "Recursively stops, unresolves, and starts a component instance, updating the code and topology while preserving resources",
    example = "To reload a component instance designated by the moniker `/core/ffx-laboratory:foo`:

    $ ffx component reload /core/ffx-laboratory:foo",
    note = "To learn more about running components, visit https://fuchsia.dev/fuchsia-src/development/components/run"
)]

pub struct ReloadComponentCommand {
    #[argh(positional)]
    /// moniker of a component instance or realm
    pub moniker: String,
}

#[cfg(test)]
mod tests {
    use super::*;
    const CMD_NAME: &'static [&'static str] = &["reload"];

    #[test]
    fn test_command() {
        let moniker = "/core/ffx-laboratory:foo";
        let args = &[moniker];
        assert_eq!(
            ReloadComponentCommand::from_args(CMD_NAME, args),
            Ok(ReloadComponentCommand { moniker: moniker.to_string() })
        )
    }
}
