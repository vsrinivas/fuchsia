// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "capability",
    description = "Lists component instances that expose/use a capability",
    example = "To show all components that expose/use a capability:

    $ ffx component capability fuchsia.sys.Loader"
)]
pub struct ComponentCapabilityCommand {
    #[argh(positional)]
    /// output all components that expose/use the capability
    pub capability: String,
}
