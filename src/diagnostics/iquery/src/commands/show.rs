// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

/// Prints the inspect hierarchies that match the given selectors. If none are given, it prints
/// everything.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "show")]
pub struct ShowCommand {
    #[argh(option)]
    /// maximum depth to use under the selector node provided. If the selector points to a
    /// property, this won’t have any effect. For example, --depth 1 will print only the names of
    /// children of the given node and properties. When omitted: all of the inspect tree will be printed.
    pub depth: usize,

    #[argh(option)]
    /// the name of the manifest file that we are interested in. If this is provided, the output
    /// will only contain monikers for components whose url contains the provided name.
    pub manifest_name: String,

    #[argh(positional)]
    /// component or tree selectors for which the selectors should be queried. Minimum: 1.
    pub selectors: Vec<String>,
}
