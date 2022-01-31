// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

#[derive(FromArgs, PartialEq, Debug)]
/// Manage the set of accounts on this device.
pub struct Command {
    #[argh(subcommand)]
    pub subcommand: Subcommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum Subcommand {
    List(List),
    RemoveAll(RemoveAll),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list")]
/// List the IDs and names of all accounts on the device.
pub struct List {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "remove-all")]
/// Remove all accounts on the device.
pub struct RemoveAll {}

#[cfg(test)]
mod tests {
    use {super::*, assert_matches::assert_matches};

    #[test]
    fn test_unknown_subcommand() {
        assert_matches!(Command::from_args(&["accountctl"], &["unknown"]), Err(_));
    }
}
