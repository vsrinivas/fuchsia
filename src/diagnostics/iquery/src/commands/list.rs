// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;

/// Lists all monikers (relative to the scope where the archivist receives events from) of
/// components that expose inspect.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list")]
pub struct ListCommand {
    #[argh(switch)]
    /// print v1 /hub entries that contain inspect.
    pub hub: bool,

    #[argh(option)]
    /// the name of the manifest file that we are interested in. If this is provided, the output
    /// will only contain monikers for components whose url contains the provided name.
    pub manifest_name: String,

    #[argh(switch)]
    /// also print the URL of the component.
    pub with_url: bool,
}
