// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    argh::FromArgs,
    fidl_fuchsia_overnet_protocol::LinkConfig,
    futures::prelude::*,
    std::{fmt::Write, time::Duration},
};

mod generator;
mod host_pipe;
mod list_peers;
mod probe_node;
mod probe_reports;

#[derive(FromArgs, PartialEq, Debug)]
/// Overnet debug tool
pub struct Opts {
    #[argh(subcommand)]
    pub command: Command,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum Command {
    ListPeers(ListPeers),
    ListLinks(ListLinks),
    HostPipe(host_pipe::HostPipe),
    FullMap(probe_reports::FullMapArgs),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list-peers")]
/// List known peer nodes
pub struct ListPeers {}

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

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list-links")]
/// List links on a particular peer
pub struct ListLinks {
    #[argh(positional)]
    /// list of nodes to display links for, or 'all' to display links from all nodes
    nodes: String,
}

fn fmtq<T: std::fmt::Display>(a: Option<T>) -> String {
    if let Some(a) = a {
        format!("{}", a)
    } else {
        "?".to_string()
    }
}

async fn list_links(args: ListLinks) -> Result<(), Error> {
    list_peers::list_peers_from_argument(&args.nodes)?
        .try_for_each_concurrent(None, |node| async move {
            let mut links = probe_node::probe_node(node, probe_node::Selector::Links)
                .await?
                .links
                .ok_or_else(|| format_err!("No links in probe result"))?;
            links.sort_by(|a, b| a.source_local_id.cmp(&b.source_local_id));
            for link in links {
                println!(
                    "{}#{} -> {}",
                    fmtq(link.source.map(|n| n.id)),
                    fmtq(link.source_local_id),
                    fmtq(link.destination.map(|n| n.id))
                );
                match link.config {
                    None => (),
                    Some(LinkConfig::Socket(opt)) => {
                        print!("  external");
                        if let Some(label) = opt.connection_label {
                            print!(" label={:?}", label);
                        }
                        if let Some(bytes_per_second) = opt.bytes_per_second {
                            print!(" unreliable with bytes per second={}", bytes_per_second);
                        } else {
                            print!(" reliable_transport");
                        }
                        println!("");
                    }
                    Some(LinkConfig::Udp(addr)) => {
                        let addr = std::net::SocketAddrV6::new(
                            addr.address.addr.into(),
                            addr.port,
                            0,
                            addr.zone_index as u32,
                        );
                        println!("  udp {}", addr);
                    }
                    Some(LinkConfig::SerialServer(desc)) => {
                        println!("  serial server descriptor={}", desc);
                    }
                    Some(LinkConfig::SerialClient(desc)) => {
                        println!("  serial client descriptor={}", desc);
                    }
                    Some(LinkConfig::AscenddClient(config)) => {
                        print!("  ascendd client");
                        if let Some(connection_label) = config.connection_label {
                            print!(" label={:?}", connection_label);
                        }
                        if let Some(path) = config.path {
                            print!(" @ {}", path);
                        }
                        println!("");
                    }
                    Some(LinkConfig::AscenddServer(config)) => {
                        print!("  ascendd server");
                        if let Some(connection_label) = config.connection_label {
                            print!(" label={:?}", connection_label);
                        }
                        if let Some(path) = config.path {
                            print!(" @ {}", path);
                        }
                        println!("");
                    }
                }
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
        })
        .await
}

pub async fn run_onet(opts: Opts) -> Result<(), Error> {
    match opts.command {
        Command::ListPeers(_) => list_peers().await,
        Command::ListLinks(args) => list_links(args).await,
        Command::HostPipe(_) => host_pipe::host_pipe().await,
        Command::FullMap(args) => {
            println!("{}", probe_reports::full_map(args).await?);
            Ok(())
        }
    }
}
