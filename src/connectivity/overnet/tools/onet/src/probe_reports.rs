// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    fidl_fuchsia_overnet_protocol::{
        DiagnosticMarker, DiagnosticProxy, LinkDiagnosticInfo, NodeDescription, NodeId,
        PeerConnectionDiagnosticInfo, ProbeResult, ProbeSelector,
    },
    futures::{
        future::{select, Either},
        prelude::*,
    },
    std::{collections::HashMap, time::Duration},
    structopt::StructOpt,
};

const PROBE_TIMEOUT: Duration = Duration::from_millis(500);
const LIST_PEERS_TIMEOUT: Duration = Duration::from_millis(500);

async fn timeout_after<R>(
    fut: impl Unpin + Future<Output = Result<R, Error>>,
    dur: Duration,
    timeout_result: Error,
) -> Result<R, Error> {
    let (tx, rx) = futures::channel::oneshot::channel();
    std::thread::spawn(move || {
        std::thread::sleep(dur);
        let _ = tx.send(timeout_result);
    });
    match select(fut, rx).await {
        Either::Left((r, _)) => r,
        Either::Right((Ok(r), _)) => Err(r),
        Either::Right((_, _)) => Err(anyhow::format_err!("Canceled timeout")),
    }
}

async fn probe_node(
    mut node_id: NodeId,
    probe_bits: ProbeSelector,
) -> Result<(NodeId, ProbeResult), Error> {
    timeout_after(
        async move {
            let (s, p) = fidl::Channel::create().context("failed to create zx channel")?;
            hoist::connect_as_service_consumer()?.connect_to_service(
                &mut node_id,
                DiagnosticMarker::NAME,
                s,
            )?;
            let probe_result = DiagnosticProxy::new(
                fidl::AsyncChannel::from_channel(p).context("failed to make async channel")?,
            )
            .probe(probe_bits)
            .await?;
            Ok((node_id, probe_result))
        }
        .boxed(),
        PROBE_TIMEOUT,
        anyhow::format_err!("Probe timed out"),
    )
    .await
}

// List peers, but wait for things to settle out first
async fn list_peers() -> Result<(NodeId, Vec<NodeId>), Error> {
    let svc = hoist::connect_as_service_consumer()?;
    // Do an initial query without timeout
    let mut peers = svc.list_peers().await?;
    // Now loop until we see an error
    loop {
        match timeout_after(
            async { Ok(svc.list_peers().await?) }.boxed(),
            LIST_PEERS_TIMEOUT,
            anyhow::format_err!("Timeout"),
        )
        .await
        {
            Ok(r) => peers = r,
            Err(_) => break,
        }
    }
    let own_id = (|| -> Result<NodeId, Error> {
        for peer in peers.iter() {
            if peer.is_self {
                return Ok(peer.id);
            }
        }
        return Err(anyhow::format_err!("Cannot find myself"));
    })()?;
    let peers = peers.into_iter().map(|peer| peer.id).collect();
    Ok((own_id, peers))
}

async fn probe(
    mut descriptions: Option<&mut HashMap<NodeId, NodeDescription>>,
    mut peer_connections: Option<&mut Vec<PeerConnectionDiagnosticInfo>>,
    mut links: Option<&mut Vec<LinkDiagnosticInfo>>,
) -> Result<NodeId, Error> {
    let probe_bits = ProbeSelector::empty()
        | descriptions.as_ref().map_or(ProbeSelector::empty(), |_| ProbeSelector::NodeDescription)
        | peer_connections
            .as_ref()
            .map_or(ProbeSelector::empty(), |_| ProbeSelector::PeerConnections)
        | links.as_ref().map_or(ProbeSelector::empty(), |_| ProbeSelector::Links);
    assert_ne!(probe_bits, ProbeSelector::empty());

    let (own_id, peers) = list_peers().await?;
    let mut futures: futures::stream::FuturesUnordered<_> =
        peers.into_iter().map(|peer| probe_node(peer, probe_bits)).collect();
    while let Some((node_id, result)) = futures.try_next().await? {
        if let Some(node_description) = result.node_description {
            if let Some(ref mut descriptions) = descriptions {
                descriptions.insert(node_id, node_description);
            }
        }
        if let Some(node_peer_connections) = result.peer_connections {
            for peer_connection in node_peer_connections.iter() {
                if let Some(source) = peer_connection.source {
                    if node_id != source {
                        return Err(anyhow::format_err!(
                            "Invalid source node id {:?} from {:?}",
                            source,
                            node_id
                        ));
                    }
                } else {
                    return Err(anyhow::format_err!("No source node id from {:?}", node_id));
                }
                if peer_connection.destination.is_none() {
                    return Err(anyhow::format_err!("No destination node id from {:?}", node_id));
                }
            }
            if let Some(ref mut peer_connections) = peer_connections {
                peer_connections.extend(node_peer_connections.into_iter());
            }
        }
        if let Some(node_links) = result.links {
            for link in node_links.iter() {
                if let Some(source) = link.source {
                    if node_id != source {
                        return Err(anyhow::format_err!(
                            "Invalid source node id {:?} from {:?}",
                            source,
                            node_id
                        ));
                    }
                } else {
                    return Err(anyhow::format_err!("No source node id from {:?}", node_id));
                }
                if link.destination.is_none() {
                    return Err(anyhow::format_err!("No destination node id from {:?}", node_id));
                }
            }
            if let Some(ref mut links) = links {
                links.extend(node_links.into_iter());
            }
        }
    }
    Ok(own_id)
}

enum Attr {
    HTML(String),
    Text(String),
    Bool(bool),
}

struct AttrWriter {
    attrs: std::collections::BTreeMap<String, Attr>,
}

impl AttrWriter {
    fn new() -> Self {
        AttrWriter { attrs: std::collections::BTreeMap::new() }
    }

    fn set_value(&mut self, key: &str, attr: Attr) -> &mut Self {
        self.attrs.insert(key.to_string(), attr);
        self
    }

    fn set(&mut self, key: &str, value: &str) -> &mut Self {
        self.set_value(key, Attr::Text(value.to_string()))
    }

    fn set_html(&mut self, key: &str, value: &str) -> &mut Self {
        self.set_value(key, Attr::HTML(value.to_string()))
    }

    fn set_bool(&mut self, key: &str, value: bool) -> &mut Self {
        self.set_value(key, Attr::Bool(value))
    }

    fn render(self) -> String {
        let mut out = String::new();
        for (key, value) in self.attrs.into_iter() {
            out += if out.is_empty() { " [" } else { ", " };
            out += &key;
            match value {
                Attr::HTML(s) => {
                    out += "=<";
                    out += &s;
                    out += ">";
                }
                Attr::Text(s) => {
                    out += "=\"";
                    out += &s;
                    out += "\"";
                }
                Attr::Bool(true) => out += "=true",
                Attr::Bool(false) => out += "=false",
            }
        }
        if !out.is_empty() {
            out += "]";
        }
        out
    }
}

struct LabelAttrWriter {
    out: String,
}

impl LabelAttrWriter {
    fn new() -> LabelAttrWriter {
        LabelAttrWriter { out: "<table border=\"0\">".to_string() }
    }

    fn set<T: std::fmt::Display>(mut self, name: &str, value: Option<T>) -> Self {
        if let Some(value) = value {
            self.out += &format!("<tr><td>{}</td><td>{}</td></tr>", name, value);
        }
        self
    }

    fn render(self) -> String {
        self.out + "</table>"
    }
}

#[derive(StructOpt)]
pub struct FullMapArgs {
    #[structopt(short, long)]
    exclude_self: bool,
}

pub async fn full_map(args: FullMapArgs) -> Result<String, Error> {
    let mut descriptions = HashMap::new();
    let mut peer_connections = Vec::new();
    let mut links = Vec::new();
    let own_id =
        probe(Some(&mut descriptions), Some(&mut peer_connections), Some(&mut links)).await?;
    let mut out = String::new();
    out += "digraph G {\n";
    for (node_id, description) in descriptions.iter() {
        let is_self = node_id.id == own_id.id;
        if args.exclude_self && is_self {
            continue;
        }
        let mut attrs = AttrWriter::new();
        if is_self {
            attrs.set("shape", "box");
        }
        let mut label = String::new();
        if let Some(os) = description.operating_system {
            label += &format!("{:?}", os);
            label += " ";
        }
        if let Some(imp) = description.implementation {
            label += &format!("{:?}", imp);
            label += ":";
        }
        label += &format!("{}", node_id.id);
        attrs.set("label", &label);
        out += &format!("  _{}{}\n", node_id.id, attrs.render());
    }
    for conn in peer_connections.iter() {
        let source = conn.source.unwrap();
        let dest = conn.destination.unwrap();
        if args.exclude_self && (source.id == own_id.id || dest.id == own_id.id) {
            continue;
        }
        let mut attrs = AttrWriter::new();
        attrs
            .set(
                "color",
                match conn.is_client {
                    None => "gray",
                    Some(true) => "red",
                    Some(false) => "magenta",
                },
            )
            .set("weight", "0.9")
            .set_bool("constraint", true);
        attrs.set(
            "style",
            match conn.is_established {
                None => "dotted",
                Some(true) => "solid",
                Some(false) => "dashed",
            },
        );
        attrs.set_html(
            "label",
            &LabelAttrWriter::new()
                .set("recv", conn.received_packets)
                .set("sent", conn.sent_packets)
                .set("lost", conn.lost_packets)
                .set("rtt", conn.round_trip_time_microseconds)
                .set("cwnd", conn.congestion_window_bytes)
                .set("msgsent", conn.messages_sent)
                .set("msgbsent", conn.bytes_sent)
                .set("connect_to_service_sends", conn.connect_to_service_sends)
                .set("connect_to_service_send_bytes", conn.connect_to_service_send_bytes)
                .set("update_node_description_sends", conn.update_node_description_sends)
                .set("update_node_description_send_bytes", conn.update_node_description_send_bytes)
                .set("update_link_status_sends", conn.update_link_status_sends)
                .set("update_link_status_send_bytes", conn.update_link_status_send_bytes)
                .set("update_link_status_ack_sends", conn.update_link_status_ack_sends)
                .set("update_link_status_ack_send_bytes", conn.update_link_status_ack_send_bytes)
                .render(),
        );
        out += &format!("  _{} -> _{}{}\n", source.id, dest.id, attrs.render());
    }
    for link in links {
        let source = link.source.unwrap();
        let dest = link.destination.unwrap();
        if args.exclude_self && (source.id == own_id.id || dest.id == own_id.id) {
            continue;
        }
        let mut attrs = AttrWriter::new();
        attrs.set("color", "blue").set("weight", "1.0").set("penwidth", "4.0");
        attrs.set_html(
            "label",
            &LabelAttrWriter::new()
                .set("id", link.source_local_id)
                .set("recv", link.received_packets)
                .set("sent", link.sent_packets)
                .set("recvb", link.received_bytes)
                .set("sentb", link.sent_bytes)
                .set("pings", link.pings_sent)
                .set("fwd", link.packets_forwarded)
                .set("rtt", link.round_trip_time_microseconds)
                .render(),
        );
        out += &format!("  _{} -> _{}{}\n", source.id, dest.id, attrs.render());
    }
    out += "}\n";
    Ok(out)
}
