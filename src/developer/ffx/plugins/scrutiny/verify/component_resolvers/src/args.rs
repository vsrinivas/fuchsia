// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "component-resolvers",
    description = "Verifies that component configured to use custom component resolvers are permitted by an allowlist.",
    example = "To verify component resolvers on your current build:

        $ffx scrutiny verify component-resolvers --allowlist path/to/allowlist.json5",
    note = "Verifies all components that use a custom component resolver."
)]
pub struct ScrutinyComponentResolversCommand {
    /// path to allowlist file that specifies which components may use
    /// particular custom component resolvers.
    #[argh(option)]
    pub allowlist: String,
    /// path to depfile that gathers dependencies during execution.
    #[argh(option)]
    pub depfile: Option<String>,
    /// path to stamp file to write to if and only if verification succeeds.
    #[argh(option)]
    pub stamp: Option<String>,
}
