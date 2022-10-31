// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "launch",
    description = "Launch a session",
    example = "To use the tiling session component:

       $ fx set workstation_eng.x64 --with //src/session/examples/tiles-session

       $ ffx session launch fuchsia-pkg://fuchsia.com/tiles-session#meta/tiles-session.cm

This will launch the tiling session component. See https://fuchsia.dev/glossary#session-component
"
)]
pub struct SessionLaunchCommand {
    #[argh(positional)]
    /// the component URL of a session.
    pub url: String,
}
