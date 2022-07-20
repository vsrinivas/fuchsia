// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "explore",
    description = "Spawns a dash process scoped to a component instance.",
    example = "To explore the Archivist instance interactively:

> ffx component explore /bootstrap/archivist
$ ls
bin
exposed
ns
out
runtime
svc
$ exit
Connection to terminal closed

To run a command directly from the command line:
> ffx component explore /bootstrap/archivist -c 'printenv'
PATH=/bin:/ns/pkg/bin
PWD=/
",
    note = "The environment may contain the following directories of the explored instance:
* /ns       The namespace of the instance, if it is resolved
* /exposed  The capabilities exposed by the instance, if it is resolved
* /out      The outgoing directory of the instance, if it is running
* /runtime  The runtime directory of the instance, if it is running

The environment also contains the following directories, irrespective of the explored instance:
* /bin      Basic command-line tools like ls, cat and more
* /svc      Protocols required by dash"
)]

pub struct ExploreComponentCommand {
    #[argh(positional)]
    /// moniker of a component instance
    pub moniker: String,

    #[argh(option)]
    /// URL of a tools package to include
    pub tools: Option<String>,

    #[argh(option, short = 'c', long = "command")]
    /// optional command to execute
    pub command: Option<String>,
}
