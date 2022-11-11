// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, fidl_fuchsia_dash as fdash, std::str::FromStr};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "explore",
    description = "Spawns a shell scoped to a component instance.",
    example = "To explore the Archivist instance interactively:

> ffx component explore /bootstrap/archivist
$ ls
exposed
ns
out
runtime
svc
$ exit
Connection to terminal closed

To run a command directly from the command line:
> ffx component explore /bootstrap/archivist -c 'printenv'
PATH=/.dash/bin:/ns/pkg/bin
PWD=/
",
    note = "The environment contains the following directories of the explored instance:
* /ns       The namespace of the instance
* /exposed  The capabilities exposed by the instance
* /out      The outgoing directory of the instance, if it is running
* /runtime  The runtime directory of the instance, if it is running

The environment also contains the following directories, irrespective of the explored instance:
* /.dash/bin  Basic command-line tools like ls, cat and more
* /svc        Protocols required by the dash shell"
)]

pub struct ExploreComponentCommand {
    #[argh(positional)]
    /// component URL, moniker or instance ID. Partial matches allowed.
    pub query: String,

    #[argh(option)]
    /// URL of a tools package to include in the shell environment.
    /// the PATH variable will be updated to include binaries from this tools package.
    pub tools: Option<String>,

    #[argh(option, short = 'c', long = "command")]
    /// execute a command instead of reading from stdin.
    /// the exit code of the command will be forwarded to the host.
    pub command: Option<String>,

    #[argh(option, short = 'l', long = "layout")]
    /// changes the namespace layout that is created for the shell.
    /// nested: nests all instance directories under subdirs (default)
    /// namespace: sets the instance namespace as the root (works better for tools)
    pub ns_layout: Option<DashNamespaceLayout>,
}

#[derive(Debug, PartialEq)]
pub struct DashNamespaceLayout(pub fdash::DashNamespaceLayout);

impl FromStr for DashNamespaceLayout {
    type Err = String;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match s {
            "nested" => Ok(DashNamespaceLayout(fdash::DashNamespaceLayout::NestAllInstanceDirs)),
            "namespace" => {
                Ok(DashNamespaceLayout(fdash::DashNamespaceLayout::InstanceNamespaceIsRoot))
            }
            _ => Err("Unknown namespace layout (supported values: nested, namespace)".to_string()),
        }
    }
}
