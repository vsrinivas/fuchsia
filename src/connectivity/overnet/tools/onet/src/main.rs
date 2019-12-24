// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fidl_fuchsia_overnet::ServiceConsumerProxyInterface, structopt::StructOpt};

mod host_pipe;
mod probe_reports;

#[derive(StructOpt)]
#[structopt(about = "Overnet debug tool", author = "Fuchsia Team", rename_all = "kebab")]
enum Command {
    /// List known peer nodes
    ListPeers,
    /// Use stdin/stdout as a link to another overnet instance
    HostPipe,
    /// Construct a detailed graphviz map of the Overnet mesh
    FullMap(probe_reports::FullMapArgs),
}

async fn ls_peers() -> Result<(), Error> {
    for peer in hoist::connect_as_service_consumer()?.list_peers().await? {
        println!("PEER: {:?}", peer);
    }
    Ok(())
}

async fn async_main() -> Result<(), Error> {
    match Command::from_args() {
        Command::ListPeers => ls_peers().await,
        Command::HostPipe => host_pipe::host_pipe().await,
        Command::FullMap(args) => {
            println!("{}", probe_reports::full_map(args).await?);
            Ok(())
        }
    }
}

fn main() {
    hoist::run(async move {
        if let Err(e) = async_main().await {
            log::warn!("Error: {}", e)
        }
    });
}
