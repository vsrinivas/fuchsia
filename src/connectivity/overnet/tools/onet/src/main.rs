// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    argh::FromArgs,
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::prelude::*,
    std::{fmt::Write, time::Duration},
};

mod generator;
mod host_pipe;
mod list_peers;
mod probe_node;
mod probe_reports;

#[derive(FromArgs)]
/// Overnet debug tool
struct Opts {
    #[argh(subcommand)]
    command: Command,
}

#[derive(FromArgs)]
#[argh(subcommand)]
enum Command {
    ListPeers(ListPeers),
    ListLinks(ListLinks),
    HostPipe(host_pipe::HostPipe),
    FullMap(probe_reports::FullMapArgs),
}

#[derive(FromArgs)]
#[argh(subcommand, name = "list-peers")]
/// List known peer nodes
struct ListPeers {}

async fn list_peers() -> Result<(), Error> {
    list_peers::list_peers()
        .try_for_each_concurrent(None, |peer| async move {
            let desc = probe_node::probe_node(peer, probe_node::Selector::NodeDescription).await?;
            let mut out = String::new();
            write!(&mut out, "{}", peer.id)?;
            if let Some(desc) = desc.node_description {
                if let Some(operating_system) = desc.operating_system {
                    write!(&mut out, " os:{:?}", operating_system)?;
                }
                if let Some(implementation) = desc.implementation {
                    write!(&mut out, " impl:{:?}", implementation)?;
                }
                if let Some(binary) = desc.binary {
                    write!(&mut out, " bin:{}", binary)?;
                }
                if let Some(hostname) = desc.hostname {
                    write!(&mut out, " hostname:{}", hostname)?;
                }
            }
            println!("{}", out);
            Ok(())
        })
        .await
}

#[derive(FromArgs, Debug)]
#[argh(subcommand, name = "list-links")]
/// List links on a particular peer
struct ListLinks {
    #[argh(positional)]
    node: u64,
}

fn fmtq<T: std::fmt::Display>(a: Option<T>) -> String {
    if let Some(a) = a {
        format!("{}", a)
    } else {
        "?".to_string()
    }
}

async fn list_links(args: ListLinks) -> Result<(), Error> {
    if list_peers::list_peers()
        .try_filter(|n| future::ready(n.id == args.node))
        .next()
        .await
        .is_none()
    {
        return Err(format_err!("Could not find node {}", args.node));
    }
    for link in probe_node::probe_node(NodeId { id: args.node }, probe_node::Selector::Links)
        .await?
        .links
        .ok_or_else(|| format_err!("No links in probe result"))?
    {
        println!(
            "LINK {} is {} -> {}",
            fmtq(link.source_local_id),
            fmtq(link.source.map(|n| n.id)),
            fmtq(link.destination.map(|n| n.id))
        );
        println!(
            "  packets/bytes recv: {}/{} sent: {}/{}",
            fmtq(link.received_packets),
            fmtq(link.received_bytes),
            fmtq(link.sent_packets),
            fmtq(link.sent_bytes),
        );
        println!(
            "  rtt: {}",
            fmtq(
                link.round_trip_time_microseconds
                    .map(|us| format!("{:?}", Duration::from_micros(us)))
            )
        );
        println!(
            "  pings sent: {} packets forwarded: {}",
            fmtq(link.pings_sent),
            fmtq(link.packets_forwarded)
        );
    }
    Ok(())
}

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    match argh::from_env::<Opts>().command {
        Command::ListPeers(_) => list_peers().await,
        Command::ListLinks(args) => list_links(args).await,
        Command::HostPipe(_) => host_pipe::host_pipe().await,
        Command::FullMap(args) => {
            println!("{}", probe_reports::full_map(args).await?);
            Ok(())
        }
    }
}
