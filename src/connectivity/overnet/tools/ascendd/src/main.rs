// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::Handle;
use fidl_fuchsia_overnet_protocol::StreamSocketGreeting;
use futures::prelude::*;
use overnet_core::{
    LinkId, Node, NodeOptions, NodeRuntime, RouterTime, SendHandle, StreamDeframer, StreamFramer,
};
use salt_slab::{SaltSlab, SaltedID};
use std::cell::RefCell;
use tokio::io::AsyncRead;
use tokio::runtime::current_thread;

fn app<'a, 'b>() -> clap::App<'a, 'b> {
    // TODO(ctiller): move to argh?
    clap::App::new("ascendd")
        .version("0.1.0")
        .about("Daemon to lift a non-Fuchsia device into Overnet")
        .author("Fuchsia Team")
        .arg(
            clap::Arg::with_name("sockpath")
                .long("sockpath")
                .value_name("FILE")
                .help("The path to the ascendd socket"),
        )
}

thread_local! {
    // Always access via with_app_mut
    static APP: RefCell<App> = RefCell::new(App::new());
}

fn with_app_mut<R>(f: impl FnOnce(&mut App) -> R) -> R {
    APP.with(|rcapp| f(&mut rcapp.borrow_mut()))
}

struct AscenddRuntime;

#[derive(Clone, Copy, Debug)]
enum PhysLinkId {
    UnixLink(SaltedID<UnixLink>),
}

#[derive(PartialEq, Eq, PartialOrd, Ord, Clone, Copy, Debug)]
struct Time(std::time::Instant);

impl RouterTime for Time {
    type Duration = std::time::Duration;

    fn now() -> Self {
        Time(std::time::Instant::now())
    }

    fn after(t: Self, dt: Self::Duration) -> Self {
        Time(t.0 + dt)
    }
}

impl NodeRuntime for AscenddRuntime {
    type Time = Time;
    type LinkId = PhysLinkId;
    const IMPLEMENTATION: fidl_fuchsia_overnet_protocol::Implementation =
        fidl_fuchsia_overnet_protocol::Implementation::Ascendd;

    fn handle_type(_hdl: &Handle) -> Result<SendHandle, Error> {
        unimplemented!();
    }

    fn spawn_local<F>(&mut self, future: F)
    where
        F: Future<Output = ()> + 'static,
    {
        spawn_local(future);
    }

    fn at(&mut self, t: Time, f: impl FnOnce() + 'static) {
        spawn_local(async move {
            futures::compat::Compat01As03::new(tokio::timer::Delay::new(t.0)).await.unwrap();
            f();
        })
    }

    fn router_link_id(&self, id: Self::LinkId) -> LinkId<overnet_core::PhysLinkId<PhysLinkId>> {
        with_app_mut(|app| match id {
            PhysLinkId::UnixLink(id) => {
                app.unix_links.get(id).map(|link| link.router_id).unwrap_or(LinkId::invalid())
            }
        })
    }

    fn send_on_link(&mut self, id: Self::LinkId, packet: &mut [u8]) -> Result<(), Error> {
        with_app_mut(|app| {
            match id {
                PhysLinkId::UnixLink(id) => {
                    let link = app
                        .unix_links
                        .get_mut(id)
                        .ok_or_else(|| failure::format_err!("No such link {:?}", id))?;
                    link.framer.queue_send(packet)?;
                    if let Some(tx) = link.state.become_writing() {
                        start_writes(id, tx, link.framer.take_sends());
                    }
                }
            }
            Ok(())
        })
    }
}

#[derive(Debug)]
enum UnixLinkState {
    Idle(tokio::io::WriteHalf<tokio::net::UnixStream>),
    Writing,
}

impl UnixLinkState {
    fn become_writing(&mut self) -> Option<tokio::io::WriteHalf<tokio::net::UnixStream>> {
        match std::mem::replace(self, Self::Writing) {
            UnixLinkState::Idle(w) => Some(w),
            UnixLinkState::Writing => None,
        }
    }
}

#[derive(Debug)]
struct UnixLink {
    state: UnixLinkState,
    framer: StreamFramer,
    router_id: LinkId<overnet_core::PhysLinkId<PhysLinkId>>,
    connection_label: Option<String>,
}

struct App {
    node: Node<AscenddRuntime>,
    unix_links: SaltSlab<UnixLink>,
}

impl App {
    fn new() -> Self {
        App {
            node: Node::new(
                AscenddRuntime,
                NodeOptions::new()
                    .set_quic_server_key_file(hoist::hard_coded_server_key().unwrap())
                    .set_quic_server_cert_file(hoist::hard_coded_server_cert().unwrap()),
            )
            .unwrap(),
            unix_links: SaltSlab::new(),
        }
    }
}

fn spawn_local<F>(future: F)
where
    F: Future<Output = ()> + 'static,
{
    current_thread::spawn(future.unit_error().boxed_local().compat());
}

fn start_writes(
    id: SaltedID<UnixLink>,
    tx: tokio::io::WriteHalf<tokio::net::UnixStream>,
    bytes: Vec<u8>,
) {
    if bytes.len() == 0 {
        with_app_mut(|app| {
            if let Some(link) = app.unix_links.get_mut(id) {
                link.state = UnixLinkState::Idle(tx);
            }
        });
        return;
    }
    let wr = tokio::io::write_all(tx, bytes);
    let wr = futures::compat::Compat01As03::new(wr);
    spawn_local(finish_writes(id, wr));
}

async fn finish_writes(
    id: SaltedID<UnixLink>,
    wr: impl Future<
        Output = Result<(tokio::io::WriteHalf<tokio::net::UnixStream>, Vec<u8>), std::io::Error>,
    >,
) {
    match wr.await {
        Ok((tx, _)) => {
            let bytes = with_app_mut(|app| {
                if let Some(link) = app.unix_links.get_mut(id) {
                    link.framer.take_sends()
                } else {
                    vec![]
                }
            });
            start_writes(id, tx, bytes);
        }
        Err(e) => log::warn!("Write failed: {}", e),
    }
}

async fn read_incoming_inner(
    stream: tokio::io::ReadHalf<tokio::net::UnixStream>,
    mut chan: futures::channel::mpsc::Sender<Vec<u8>>,
) -> Result<(), Error> {
    let mut buf = [0u8; 1024];
    let mut stream = Some(stream);
    let mut deframer = StreamDeframer::new();

    loop {
        let rd = tokio::io::read(stream.take().unwrap(), &mut buf[..]);
        let rd = futures::compat::Compat01As03::new(rd);
        let (returned_stream, _, n) = rd.await?;
        if n == 0 {
            return Ok(());
        }
        stream = Some(returned_stream);
        deframer.queue_recv(&mut buf[..n]);
        while let Some(frame) = deframer.next_incoming_frame() {
            chan.send(frame).await?;
        }
    }
}

async fn read_incoming(
    stream: tokio::io::ReadHalf<tokio::net::UnixStream>,
    chan: futures::channel::mpsc::Sender<Vec<u8>>,
) {
    if let Err(e) = read_incoming_inner(stream, chan).await {
        log::warn!("Error reading: {}", e);
    }
}

async fn process_incoming_inner(
    mut rx_frames: futures::channel::mpsc::Receiver<Vec<u8>>,
    tx_bytes: tokio::io::WriteHalf<tokio::net::UnixStream>,
) -> Result<(), Error> {
    let node_id = with_app_mut(|app| app.node.id().0);

    // Send first frame
    let mut framer = StreamFramer::new();
    let mut greeting = StreamSocketGreeting {
        magic_string: Some(hoist::ASCENDD_SERVER_CONNECTION_STRING.to_string()),
        node_id: Some(fidl_fuchsia_overnet_protocol::NodeId { id: node_id }),
        connection_label: Some("ascendd".to_string()),
    };
    let mut bytes = Vec::new();
    let mut handles = Vec::new();
    fidl::encoding::Encoder::encode(&mut bytes, &mut handles, &mut greeting)?;
    assert_eq!(handles.len(), 0);
    framer.queue_send(bytes.as_slice())?;
    let send = framer.take_sends();
    let wr = tokio::io::write_all(tx_bytes, send);
    let wr = futures::compat::Compat01As03::new(wr).map_err(|e| -> Error { e.into() });

    let first_frame = rx_frames
        .next()
        .map(|r| r.ok_or_else(|| failure::format_err!("Stream closed before greeting received")));
    let (mut frame, (tx_bytes, _)) = futures::try_join!(first_frame, wr)?;
    let mut greeting = StreamSocketGreeting::empty();
    // WARNING: Since we are decoding without a transaction header, we have to
    // provide a context manually. This could cause problems in future FIDL wire
    // format migrations, which are driven by header flags.
    let context = fidl::encoding::Context {};
    fidl::encoding::Decoder::decode_with_context(&context, frame.as_mut(), &mut [], &mut greeting)?;

    let node_id = match greeting {
        StreamSocketGreeting { magic_string: None, .. } => failure::bail!(
            "Required magic string '{}' not present in greeting",
            hoist::ASCENDD_CLIENT_CONNECTION_STRING
        ),
        StreamSocketGreeting { magic_string: Some(ref x), .. }
            if x != hoist::ASCENDD_CLIENT_CONNECTION_STRING =>
        {
            failure::bail!(
                "Expected magic string '{}' in greeting, got '{}'",
                hoist::ASCENDD_CLIENT_CONNECTION_STRING,
                x
            )
        }
        StreamSocketGreeting { node_id: None, .. } => failure::bail!("No node id in greeting"),
        StreamSocketGreeting { node_id: Some(n), .. } => n.id,
    };

    // Register our new link!
    let (router_id, phys_id) = with_app_mut(|app| {
        let id = app.unix_links.insert(UnixLink {
            state: UnixLinkState::Idle(tx_bytes),
            router_id: LinkId::invalid(),
            framer,
            connection_label: greeting.connection_label,
        });
        match app.node.new_link(node_id.into(), PhysLinkId::UnixLink(id)) {
            Err(e) => {
                app.unix_links.remove(id);
                failure::bail!(e);
            }
            Ok(x) => {
                app.unix_links.get_mut(id).unwrap().router_id = x;
                Ok((x, id))
            }
        }
    })?;

    // Supply node with incoming frames
    while let Some(mut frame) = rx_frames.next().await {
        with_app_mut(|app| app.node.queue_recv(router_id, frame.as_mut()));
    }

    with_app_mut(|app| {
        app.unix_links.remove(phys_id);
    });

    Ok(())
}

async fn process_incoming(
    rx: futures::channel::mpsc::Receiver<Vec<u8>>,
    tx_bytes: tokio::io::WriteHalf<tokio::net::UnixStream>,
) {
    if let Err(e) = process_incoming_inner(rx, tx_bytes).await {
        log::warn!("Error processing incoming frame: {}", e);
    }
}

async fn async_main() -> Result<(), Error> {
    let args = app().get_matches();

    hoist::logger::init()?;

    let sockpath = args.value_of("sockpath").unwrap_or(hoist::DEFAULT_ASCENDD_PATH);
    let _ = std::fs::remove_file(sockpath);

    let incoming = tokio::net::UnixListener::bind(sockpath)?.incoming();
    let mut incoming = futures::compat::Compat01As03::new(incoming);

    while let Some(stream) = incoming.next().await {
        let stream = stream?;
        let (rx_bytes, tx_bytes) = stream.split();
        let (tx_frames, rx_frames) = futures::channel::mpsc::channel(8);
        spawn_local(read_incoming(rx_bytes, tx_frames));
        spawn_local(process_incoming(rx_frames, tx_bytes));
    }

    Ok(())
}

fn main() {
    current_thread::run(
        (async move {
            if let Err(e) = async_main().await {
                log::warn!("Error: {}", e);
            }
        })
        .unit_error()
        .boxed_local()
        .compat(),
    );
}
