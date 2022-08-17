// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Async wrapper around QUIC

use anyhow::{format_err, Context as _, Error};
use async_utils::mutex_ticket::MutexTicket;
use fuchsia_async::{Task, Timer};
use futures::{future::poll_fn, lock::Mutex, prelude::*, ready};
use quiche::{Connection, Shutdown};
use std::collections::BTreeMap;
use std::net::SocketAddr;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::task::{Context, Poll, Waker};
use std::time::Instant;

/// Labels the endpoint of a client/server connection.
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Clone, Copy)]
pub enum Endpoint {
    /// Client endpoint.
    Client,
    /// Server endpoint.
    Server,
}

impl Endpoint {
    pub(crate) fn quic_id_bit(&self) -> u64 {
        match self {
            Endpoint::Client => 0,
            Endpoint::Server => 1,
        }
    }

    /// Returns the other end of this endpoint.
    pub fn opposite(&self) -> Endpoint {
        match self {
            Endpoint::Client => Endpoint::Server,
            Endpoint::Server => Endpoint::Client,
        }
    }
}

#[derive(Default)]
struct Wakeup(Option<Waker>);
impl Wakeup {
    #[must_use]
    fn pending<R>(&mut self, ctx: &mut Context<'_>) -> Poll<R> {
        self.0 = Some(ctx.waker().clone());
        Poll::Pending
    }

    fn ready(&mut self) {
        self.0.take().map(|w| w.wake());
    }
}

#[derive(Default)]
struct WakeupMap(BTreeMap<u64, Waker>);
impl WakeupMap {
    #[must_use]
    fn pending<R>(&mut self, ctx: &mut Context<'_>, k: u64) -> Poll<R> {
        self.0.insert(k, ctx.waker().clone());
        Poll::Pending
    }

    fn ready_iter(&mut self, iter: impl Iterator<Item = u64>) {
        for k in iter {
            self.0.remove(&k).map(|w| w.wake());
        }
    }

    fn all_ready(&mut self) {
        std::mem::replace(&mut self.0, BTreeMap::new()).into_iter().for_each(|(_, w)| w.wake());
    }
}

/// Current state of a connection - mutex guarded by AsyncConnection.
pub struct ConnState {
    local_addr: SocketAddr,
    conn: Connection,
    seen_established: bool,
    closed: bool,
    conn_send: Wakeup,
    stream_recv: WakeupMap,
    stream_send: WakeupMap,
    stream_send_init: WakeupMap,
    dgram_send: Wakeup,
    dgram_recv: Wakeup,
    timeout: Option<Timer>,
    version_negotiation: VersionNegotiationState,
}

impl std::fmt::Debug for ConnState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}|est={}", self.conn.trace_id(), self.seen_established)
    }
}

impl ConnState {
    pub fn poll_send(
        &mut self,
        ctx: &mut Context<'_>,
        frame: &mut [u8],
    ) -> Poll<Result<Option<usize>, Error>> {
        self.conn.on_timeout();

        if let Some(len) = ready!(self.version_negotiation.poll_send(ctx, frame))? {
            return Poll::Ready(Ok(Some(len)));
        }

        match self.conn.send(frame) {
            Ok(n) => {
                self.update_timeout(ctx);
                self.wake_stream_io();
                self.dgram_send.ready();
                Poll::Ready(Ok(Some(n.0)))
            }
            Err(quiche::Error::Done) if self.conn.is_closed() => Poll::Ready(Ok(None)),
            Err(quiche::Error::Done) => {
                self.update_timeout(ctx);
                self.conn_send.pending(ctx)
            }
            Err(e) => Poll::Ready(Err(e.into())),
        }
    }

    fn wake_stream_io(&mut self) {
        if !self.seen_established && self.conn.is_established() {
            self.seen_established = true;
            self.stream_send.all_ready();
            self.stream_recv.all_ready();
        } else {
            self.stream_send.ready_iter(self.conn.writable());
            self.stream_recv.ready_iter(self.conn.readable());
        }
        self.stream_send_init.all_ready();
    }

    fn update_timeout(&mut self, ctx: &mut Context<'_>) {
        self.timeout = self.conn.timeout().map(|d| Timer::new(Instant::now() + d));
        if let Some(timer) = self.timeout.as_mut() {
            if let Poll::Ready(_) = timer.poll_unpin(ctx) {
                log::warn!("Timeout happened immediately!");
            }
        }
    }
}

/// State of the version negotiation process.
enum VersionNegotiationState {
    /// Version negotiation is pending. The `packet` method will instruct the caller to reject all
    /// packets until it sees an initial frame, at which point it will negotiate a version, which
    /// will include transitioning to another state, waking all the wakers in the `Vec` as it does.
    /// `poll_send` will always return `Poll::Pending` in this state, so we won't send any packets
    /// until we've negotiated a version.
    Pending(Vec<Waker>),

    /// We've gotten a version header, and will reply with a version negotiation packet.
    /// The arguments are the scid and dcid used to generate that packet. `poll_send` will generate
    /// and return the packet when next called and transition to the `Ready` state. The `packet`
    /// method now does nothing and always returns `true`.
    PendingSend(quiche::ConnectionId<'static>, quiche::ConnectionId<'static>),

    /// Version negotiation is complete. Both `packet` and `poll_send` now effectively do nothing.
    Ready,
}

impl VersionNegotiationState {
    /// Check whether we need to send a packet for version negotiation. This will return `Pending`
    /// if we still need to receive packets to perform negotiation, and `Ready(None)` if negotiation
    /// is complete. `Ready(Some(..))` means we need to send a packet to continue version
    /// negotiation; the returned `usize` is the length of the packet, and the `frame` argument has
    /// been populated with it.
    fn poll_send(
        &mut self,
        ctx: &Context<'_>,
        frame: &mut [u8],
    ) -> Poll<Result<Option<usize>, Error>> {
        match self {
            VersionNegotiationState::Pending(wakers) => {
                wakers.push(ctx.waker().clone());
                Poll::Pending
            }
            VersionNegotiationState::PendingSend(scid, dcid) => {
                let ret = Poll::Ready(Ok(Some(quiche::negotiate_version(&scid, &dcid, frame)?)));
                *self = VersionNegotiationState::Ready;
                ret
            }
            VersionNegotiationState::Ready => Poll::Ready(Ok(None)),
        }
    }

    /// Handle a packet in terms of version negotiation.
    ///
    /// If `Ok(true)` is returned, we should pass this packet to `quiche::Connection::recv` next. If
    /// `Ok(false)` is returned this packet has been gobbled up and shouldn't be processed further.
    fn packet(&mut self, packet: &mut [u8]) -> Result<bool, Error> {
        match self {
            VersionNegotiationState::Pending(wakers) => {
                let header = quiche::Header::from_slice(packet, quiche::MAX_CONN_ID_LEN)?;

                if header.ty != quiche::Type::Initial {
                    log::warn!("Dropped packet during version negotiation");
                    Ok(false)
                } else {
                    wakers.drain(..).for_each(Waker::wake);

                    *self = if quiche::version_is_supported(header.version) {
                        VersionNegotiationState::Ready
                    } else {
                        VersionNegotiationState::PendingSend(
                            header.scid.into_owned(),
                            header.dcid.into_owned(),
                        )
                    };

                    Ok(true)
                }
            }
            _ => Ok(true),
        }
    }
}

pub struct AsyncConnection {
    trace_id: String,
    next_bidi: AtomicU64,
    next_uni: AtomicU64,
    endpoint: Endpoint,
    io: Mutex<ConnState>,
}

impl std::fmt::Debug for AsyncConnection {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("AsyncConnection")
            .field("trace_id", &self.trace_id)
            .field("next_bidi", &self.next_bidi)
            .field("next_uni", &self.next_uni)
            .field("endpoint", &self.endpoint)
            .finish()
    }
}

impl AsyncConnection {
    fn from_connection(local_addr: SocketAddr, conn: Connection, endpoint: Endpoint) -> Arc<Self> {
        Arc::new(Self {
            trace_id: conn.trace_id().to_string(),
            io: Mutex::new(ConnState {
                local_addr,
                conn,
                seen_established: false,
                closed: false,
                conn_send: Default::default(),
                stream_recv: Default::default(),
                stream_send: Default::default(),
                stream_send_init: Default::default(),
                dgram_recv: Default::default(),
                dgram_send: Default::default(),
                timeout: None,
                version_negotiation: if endpoint == Endpoint::Server {
                    VersionNegotiationState::Pending(Vec::new())
                } else {
                    VersionNegotiationState::Ready
                },
            }),
            next_bidi: AtomicU64::new(0),
            next_uni: AtomicU64::new(0),
            endpoint,
        })
    }

    pub fn connect(
        server_name: Option<&str>,
        scid: &quiche::ConnectionId<'_>,
        to: SocketAddr,
        config: &mut quiche::Config,
    ) -> Result<Arc<Self>, Error> {
        Ok(Self::from_connection(
            to,
            quiche::connect(server_name, scid, to, config)?,
            Endpoint::Client,
        ))
    }

    pub fn accept(
        scid: &quiche::ConnectionId<'_>,
        from: SocketAddr,
        config: &mut quiche::Config,
    ) -> Result<Arc<Self>, Error> {
        Ok(Self::from_connection(from, quiche::accept(scid, None, from, config)?, Endpoint::Server))
    }

    pub async fn close(&self) {
        let mut io = self.io.lock().await;
        log::trace!("{:?} close()", self.debug_id());
        io.closed = true;
        let _ = io.conn.close(false, 0, b"");
        io.stream_send.all_ready();
        io.stream_recv.all_ready();
        io.conn_send.ready();
    }

    pub fn poll_lock_state<'a>(&'a self) -> MutexTicket<'a, ConnState> {
        MutexTicket::new(&self.io)
    }

    pub async fn next_send(&self, frame: &mut [u8]) -> Result<Option<usize>, Error> {
        self.poll_io(|io, ctx| io.poll_send(ctx, frame)).await
    }

    pub fn debug_id(&self) -> impl std::fmt::Debug + '_ {
        (&self.trace_id, self.endpoint)
    }

    pub async fn recv(&self, packet: &mut [u8]) -> Result<(), Error> {
        let mut io = self.io.lock().await;
        if !io.version_negotiation.packet(packet)? {
            return Ok(());
        }
        let from = io.local_addr.clone();
        match io.conn.recv(packet, quiche::RecvInfo { from }) {
            Ok(_) => (),
            Err(quiche::Error::Done) => (),
            Err(x) => {
                return Err(x).with_context(|| format!("quiche_trace_id:{}", io.conn.trace_id()));
            }
        }
        io.wake_stream_io();
        io.conn_send.ready();
        io.dgram_recv.ready();
        Ok(())
    }

    pub async fn dgram_send(&self, packet: &mut [u8]) -> Result<(), Error> {
        fn drop_frame<S: std::fmt::Display>(
            p: &mut [u8],
            make_reason: impl FnOnce() -> S,
        ) -> Poll<Result<(), Error>> {
            log::info!("Drop frame of length {}b: {}", p.len(), make_reason());
            Poll::Ready(Ok(()))
        }

        self.poll_io(|io, ctx| match io.conn.dgram_send(packet) {
            Ok(()) => {
                io.conn_send.ready();
                Poll::Ready(Ok(()))
            }
            Err(quiche::Error::Done) => io.dgram_send.pending(ctx),
            Err(quiche::Error::InvalidState) => drop_frame(packet, || "invalid state"),
            Err(quiche::Error::BufferTooShort) => drop_frame(packet, || {
                format!("buffer too short (max = {:?})", io.conn.dgram_max_writable_len())
            }),
            Err(e) => Poll::Ready(Err(e.into())),
        })
        .await
    }

    pub async fn dgram_recv(&self, packet: &mut [u8]) -> Result<usize, Error> {
        self.poll_io(|io, ctx| match io.conn.dgram_recv(packet) {
            Ok(n) => Poll::Ready(Ok(n)),
            Err(quiche::Error::Done) => io.dgram_recv.pending(ctx),
            Err(e) => Poll::Ready(Err(e.into())),
        })
        .await
    }

    pub fn alloc_bidi(self: &Arc<Self>) -> (AsyncQuicStreamWriter, AsyncQuicStreamReader) {
        let n = self.next_bidi.fetch_add(1, Ordering::Relaxed);
        let id = n * 4 + self.endpoint.quic_id_bit();
        self.bind_id(id)
    }

    pub fn alloc_uni(self: &Arc<Self>) -> AsyncQuicStreamWriter {
        let n = self.next_uni.fetch_add(1, Ordering::Relaxed);
        let id = n * 4 + 2 + self.endpoint.quic_id_bit();
        AsyncQuicStreamWriter { conn: self.clone(), id, sent_fin: false }
    }

    pub fn bind_uni_id(self: &Arc<Self>, id: u64) -> AsyncQuicStreamReader {
        AsyncQuicStreamReader {
            conn: self.clone(),
            id,
            ready: false,
            observed_closed: false,
            buffered: Vec::new(),
        }
    }

    pub fn bind_id(self: &Arc<Self>, id: u64) -> (AsyncQuicStreamWriter, AsyncQuicStreamReader) {
        (
            AsyncQuicStreamWriter { conn: self.clone(), id, sent_fin: false },
            AsyncQuicStreamReader {
                conn: self.clone(),
                id,
                ready: false,
                observed_closed: false,
                buffered: Vec::new(),
            },
        )
    }

    pub fn endpoint(&self) -> Endpoint {
        self.endpoint
    }

    pub fn trace_id(&self) -> &str {
        &self.trace_id
    }

    pub async fn stats(&self) -> quiche::Stats {
        self.io.lock().await.conn.stats()
    }

    pub async fn is_established(&self) -> bool {
        self.io.lock().await.conn.is_established()
    }

    async fn poll_io<R>(
        &self,
        mut f: impl FnMut(&mut ConnState, &mut Context<'_>) -> Poll<R>,
    ) -> R {
        let mut lock = MutexTicket::new(&self.io);
        poll_fn(|ctx| {
            let mut guard = ready!(lock.poll(ctx));
            f(&mut *guard, ctx)
        })
        .await
    }
}

pub trait StreamProperties {
    fn id(&self) -> u64;
    fn conn(&self) -> &Arc<AsyncConnection>;

    fn is_initiator(&self) -> bool {
        // QUIC stream id's use the lower two bits as a stream type designator.
        // Bit 0 of that type is the initiator of the stream: 0 for client, 1 for server.
        self.id() & 1 == self.conn().endpoint().quic_id_bit()
    }

    fn debug_id(&self) -> (&str, u64) {
        (self.conn().trace_id(), self.id())
    }
}

#[derive(Debug)]
pub struct AsyncQuicStreamWriter {
    conn: Arc<AsyncConnection>,
    id: u64,
    sent_fin: bool,
}

impl AsyncQuicStreamWriter {
    pub async fn abandon(&mut self) {
        self.sent_fin = true;
        log::trace!("{:?} writer abandon", self.debug_id());
        if let Err(e) = self.conn.io.lock().await.conn.stream_shutdown(self.id, Shutdown::Write, 0)
        {
            log::trace!("shutdown stream failed: {:?}", e);
        }
    }

    pub async fn send(&mut self, bytes: &[u8], fin: bool) -> Result<(), Error> {
        assert_eq!(self.sent_fin, false);
        let mut sent = 0;
        let sent_fin = &mut self.sent_fin;
        let id = self.id;
        self.conn
            .poll_io(|io, ctx| {
                if !io.conn.is_established() {
                    return io.stream_send.pending(ctx, id);
                }
                let n = match io.conn.stream_send(id, &bytes[sent..], fin) {
                    Ok(n) => n,
                    Err(quiche::Error::InvalidStreamState(_)) => {
                        // We're writing to a stream that hasn't been written yet. Could be we know
                        // the stream will be established by the other end but due to some raciness
                        // that hasn't happened yet. Time to block!
                        return io.stream_send.pending(ctx, id);
                    }
                    Err(quiche::Error::Done) => {
                        io.conn_send.ready();
                        return io.stream_send_init.pending(ctx, id);
                    }
                    e @ Err(_) => e.with_context(|| format!("sending on stream {}", id))?,
                };
                io.conn_send.ready();
                sent += n;
                if sent == bytes.len() {
                    if fin {
                        *sent_fin = true;
                    }
                    Poll::Ready(Ok(()))
                } else {
                    io.stream_send.pending(ctx, id)
                }
            })
            .await
    }
}

impl Drop for AsyncQuicStreamWriter {
    fn drop(&mut self) {
        if !self.sent_fin {
            let conn = self.conn.clone();
            let id = self.id;
            // TODO: don't detach
            Task::spawn(async move {
                let _ = conn.io.lock().await.conn.stream_shutdown(id, Shutdown::Write, 0);
            })
            .detach();
        }
    }
}

impl StreamProperties for AsyncQuicStreamWriter {
    fn conn(&self) -> &Arc<AsyncConnection> {
        &self.conn
    }

    fn id(&self) -> u64 {
        self.id
    }
}

#[derive(Debug)]
pub struct AsyncQuicStreamReader {
    conn: Arc<AsyncConnection>,
    id: u64,
    ready: bool,
    observed_closed: bool,
    buffered: Vec<u8>,
}

impl Drop for AsyncQuicStreamReader {
    fn drop(&mut self) {
        if !self.observed_closed {
            let conn = self.conn.clone();
            let id = self.id;
            // TODO: don't detach
            Task::spawn(async move {
                let _ = conn.io.lock().await.conn.stream_shutdown(id, Shutdown::Read, 0);
            })
            .detach();
        }
    }
}

impl AsyncQuicStreamReader {
    pub async fn read<'b>(&'b mut self, bytes: &'b mut [u8]) -> Result<(usize, bool), Error> {
        if !self.buffered.is_empty() {
            let to_drain = std::cmp::min(self.buffered.len(), bytes.len());
            self.buffered
                .drain(..to_drain)
                .zip(bytes.iter_mut())
                .for_each(|(src, dest)| *dest = src);
            return Ok((to_drain, self.observed_closed && self.buffered.is_empty()));
        }

        let (n, fin) = loop {
            let mut io = self.conn.io.lock().await;
            let got = {
                io.conn_send.ready();
                match io.conn.stream_recv(self.id, bytes) {
                    Ok((n, fin)) => {
                        self.ready = true;
                        Some(Ok((n, fin)))
                    }
                    Err(quiche::Error::StreamReset(_)) | Err(quiche::Error::Done) => {
                        self.ready = true;
                        let finished = io.conn.stream_finished(self.id);
                        if finished {
                            Some(Ok((0, true)))
                        } else if io.closed {
                            log::trace!("{:?} reader abandon", self.debug_id());
                            let _ = io.conn.stream_shutdown(self.id, Shutdown::Read, 0);
                            Some(Ok((0, true)))
                        } else {
                            None
                        }
                    }
                    Err(quiche::Error::InvalidStreamState(_)) if !self.ready => None,
                    Err(quiche::Error::InvalidStreamState(_))
                        if io.conn.stream_finished(self.id) =>
                    {
                        Some(Ok((0, true)))
                    }
                    Err(x) => Some(Err(x).with_context(|| {
                        format_err!(
                            "async quic read: stream_id={:?} ready={:?}",
                            self.id,
                            self.ready,
                        )
                    })),
                }
            };

            if let Some(got) = got {
                break got?;
            } else {
                let mut io = Some(io);
                poll_fn(|ctx| {
                    if let Some(mut io) = io.take() {
                        io.stream_recv.pending(ctx, self.id)
                    } else {
                        Poll::Ready(())
                    }
                })
                .await;
            }
        };

        if fin {
            self.observed_closed = true;
        }

        Ok((n, fin))
    }

    pub async fn read_exact<'b>(&'b mut self, bytes: &'b mut [u8]) -> Result<bool, Error> {
        struct State<'b> {
            this: &'b mut AsyncQuicStreamReader,
            bytes: &'b mut [u8],
            read: usize,
            done: bool,
        }

        let mut state = State { this: self, bytes, read: 0, done: false };

        impl Drop for State<'_> {
            fn drop(&mut self) {
                // If we never returned Ready but did read some data, then return that data to the readers
                // buffer so that we can try to consume it next time.
                if !self.done && self.read != 0 {
                    assert!(self.this.buffered.is_empty());
                    self.this.buffered = self.bytes[..self.read].to_vec();
                }
            }
        }

        loop {
            let (n, fin) = state.this.read(&mut state.bytes[state.read..]).await?;
            state.read += n;

            if state.read == state.bytes.len() {
                state.done = true;
                break Ok(fin);
            }

            if fin {
                break Err(format_err!("Endo of stream"));
            }
        }
    }

    pub async fn abandon(&mut self) {
        log::trace!("{:?} reader abandon", self.debug_id());
        self.observed_closed = true;
        if let Err(e) = self.conn.io.lock().await.conn.stream_shutdown(self.id, Shutdown::Read, 0) {
            log::trace!("shutdown stream failed: {:?}", e);
        }
    }
}

impl StreamProperties for AsyncQuicStreamReader {
    fn conn(&self) -> &Arc<AsyncConnection> {
        &self.conn
    }

    fn id(&self) -> u64 {
        self.id
    }
}

#[cfg(test)]
mod test_util {

    use super::*;
    use futures::future::poll_fn;
    use rand::Rng;
    #[cfg(not(target_os = "fuchsia"))]
    fn path_for(name: &str) -> String {
        let relative_path = &format!("overnet_test_certs/{}", name);
        let mut path = std::env::current_exe().unwrap();
        // We don't know exactly where the binary is in the out directory (varies by target platform and
        // architecture), so search up the file tree for the certificate file.
        loop {
            if path.join(relative_path).exists() {
                path.push(relative_path);
                break;
            }
            if !path.pop() {
                // Reached the root of the file system
                panic!(
                    "Couldn't find {} near {:?}",
                    relative_path,
                    std::env::current_exe().unwrap()
                );
            }
        }
        path.to_str().unwrap().to_string()
    }

    #[cfg(target_os = "fuchsia")]
    fn path_for(name: &str) -> String {
        format!("/pkg/data/{}", name)
    }

    async fn server_config() -> Result<quiche::Config, Error> {
        let mut config = quiche::Config::new(quiche::PROTOCOL_VERSION)?;
        // TODO(ctiller): don't hardcode these
        config
            .set_application_protos(b"\x0bovernet-test/0.1")
            .context("Setting application protocols")?;
        config.verify_peer(false);
        config.load_cert_chain_from_pem_file(&path_for("cert.crt"))?;
        config.load_priv_key_from_pem_file(&path_for("cert.key"))?;
        config.load_verify_locations_from_file(&path_for("rootca.crt"))?;
        config.set_initial_max_data(10_000_000);
        config.set_initial_max_stream_data_bidi_local(1_000_000);
        config.set_initial_max_stream_data_bidi_remote(1_000_000);
        config.set_initial_max_stream_data_uni(1_000_000);
        config.set_initial_max_streams_bidi(100);
        config.set_initial_max_streams_uni(100);
        Ok(config)
    }

    fn client_config() -> Result<quiche::Config, Error> {
        let mut config = quiche::Config::new(quiche::PROTOCOL_VERSION)?;
        // TODO(ctiller): don't hardcode these
        config.set_application_protos(b"\x0bovernet-test/0.1")?;
        config.set_initial_max_data(10_000_000);
        config.set_initial_max_stream_data_bidi_local(1_000_000);
        config.set_initial_max_stream_data_bidi_remote(1_000_000);
        config.set_initial_max_stream_data_uni(1_000_000);
        config.set_initial_max_streams_bidi(100);
        config.set_initial_max_streams_uni(100);
        Ok(config)
    }

    async fn direct_packets(
        from: Arc<AsyncConnection>,
        to: Arc<AsyncConnection>,
    ) -> Result<(), Error> {
        let mut frame = [0u8; 2048];
        while let Some(length) = {
            let mut lock_state = from.poll_lock_state();
            poll_fn(|ctx| ready!(lock_state.poll(ctx)).poll_send(ctx, &mut frame)).await?
        } {
            to.recv(&mut frame[..length]).await?;
        }
        Ok(())
    }

    /// Generate a test connection pair, that automatically forwards packets from client to server.
    pub async fn get_client_server() -> (Arc<AsyncConnection>, Arc<AsyncConnection>, Task<()>) {
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let client = AsyncConnection::connect(
            None,
            &scid.into(),
            "127.0.0.1:999".parse().unwrap(),
            &mut client_config().unwrap(),
        )
        .unwrap();
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let server = AsyncConnection::accept(
            &scid.into(),
            "127.0.0.2:999".parse().unwrap(),
            &mut server_config().await.unwrap(),
        )
        .unwrap();
        let forward = futures::future::try_join(
            direct_packets(client.clone(), server.clone()),
            direct_packets(server.clone(), client.clone()),
        );
        let task = Task::spawn(async move {
            forward.await.unwrap();
        });
        (client, server, task)
    }

    /// Generate a test connection pair, that automatically forwards packets from client to server.
    pub async fn run_client_server<
        F: 'static + Sync + Clone + Send + FnOnce(Arc<AsyncConnection>, Arc<AsyncConnection>) -> Fut,
        Fut: 'static + Send + Future<Output = ()>,
    >(
        f: F,
    ) {
        let (client, server, _1) = get_client_server().await;
        f(client, server).await;
    }
}

#[cfg(test)]
mod test {

    use super::test_util::{get_client_server, run_client_server};
    use super::StreamProperties;
    use fuchsia_async::Task;
    use futures::future::{join, join_all};

    #[fuchsia::test]
    async fn simple_send() {
        run_client_server(|client, server| async move {
            let (mut cli_tx, _cli_rx) = client.alloc_bidi();
            let (_svr_tx, mut svr_rx) = server.bind_id(cli_tx.id());

            cli_tx.send(&[1, 2, 3], false).await.unwrap();
            let mut buf = [0u8; 32];
            let (n, fin) = svr_rx.read(buf.as_mut()).await.unwrap();
            assert_eq!(n, 3);
            assert_eq!(fin, false);
            assert_eq!(&buf[..n], &[1, 2, 3]);
        })
        .await
    }

    #[fuchsia::test]
    async fn send_fin() {
        run_client_server(|client, server| async move {
            let (mut cli_tx, _cli_rx) = client.alloc_bidi();
            let (_svr_tx, mut svr_rx) = server.bind_id(cli_tx.id());

            cli_tx.send(&[1, 2, 3], true).await.unwrap();
            let mut buf = [0u8; 32];
            let (n, fin) = svr_rx.read(buf.as_mut()).await.unwrap();
            assert_eq!(n, 3);
            assert_eq!(fin, true);
            assert_eq!(&buf[..n], &[1, 2, 3]);
        })
        .await
    }

    #[fuchsia::test]
    async fn recv_before_send() {
        run_client_server(|client, server| async move {
            let (mut cli_tx, _cli_rx) = client.alloc_bidi();
            let (_svr_tx, mut svr_rx) = server.bind_id(cli_tx.id());

            let mut buf = [0u8; 32];

            let (unpause, pause) = futures::channel::oneshot::channel();
            futures::future::join(
                async move {
                    cli_tx.send(&[1, 2, 3], false).await.unwrap();
                    log::trace!("sent first");
                    pause.await.unwrap();
                    log::trace!("finished pause");
                    cli_tx.send(&[], true).await.unwrap();
                    log::trace!("sent second");
                },
                async move {
                    let (n, fin) = svr_rx.read(buf.as_mut()).await.unwrap();
                    log::trace!("got: {} {}", n, fin);
                    assert_eq!(n, 3);
                    assert_eq!(fin, false);
                    assert_eq!(&buf[..n], &[1, 2, 3]);
                    unpause.send(()).unwrap();
                    let (n, fin) = svr_rx.read(buf.as_mut()).await.unwrap();
                    log::trace!("got: {} {}", n, fin);
                    assert_eq!(n, 0);
                    assert_eq!(fin, true);
                },
            )
            .await;
        })
        .await
    }

    #[fuchsia::test]
    async fn torture_test() {
        let mut plumbing_tasks = Vec::with_capacity(100);
        let mut loop_a_tasks = Vec::with_capacity(100);
        let mut loop_b_tasks = Vec::with_capacity(100);
        let mut loop_c_tasks = Vec::with_capacity(100);
        let mut test_tasks = Vec::with_capacity(100);

        for _ in 0..100 {
            let (client, server, task) = get_client_server().await;
            plumbing_tasks.push(task);

            let (mut client_writer_a, mut client_reader_a) = client.alloc_bidi();
            let (mut server_writer, mut server_reader) = server.bind_id(client_writer_a.id());
            loop_a_tasks.push(Task::spawn(async move {
                let mut buf = [0; 256];
                loop {
                    let (size, fin) = server_reader.read(&mut buf).await.unwrap();
                    if size > 0 {
                        server_writer.send(&buf[..size], fin).await.unwrap();
                    }
                    if fin {
                        break;
                    }
                }
            }));

            let (mut client_writer_b, mut client_reader_b) = client.alloc_bidi();
            let (mut server_writer, mut server_reader) = server.bind_id(client_writer_b.id());
            loop_b_tasks.push(Task::spawn(async move {
                let mut buf = [0; 256];
                loop {
                    let (size, fin) = server_reader.read(&mut buf).await.unwrap();
                    if size > 0 {
                        server_writer.send(&buf[..size], fin).await.unwrap();
                    }
                    if fin {
                        break;
                    }
                }
            }));

            loop_c_tasks.push(Task::spawn(async move {
                let mut buf = [0; 256];
                loop {
                    let (size, fin) = client_reader_a.read(&mut buf).await.unwrap();
                    if size > 0 {
                        client_writer_b.send(&buf[..size], fin).await.unwrap();
                    }
                    if fin {
                        break;
                    }
                }
            }));

            test_tasks.push(async move {
                let send = async move {
                    for i in 0..65536u64 {
                        client_writer_a.send(&i.to_le_bytes(), i == 65535).await.unwrap();
                    }
                };

                let recv = async move {
                    let mut buf = [0; 8];

                    for i in 0..65536u64 {
                        assert_eq!(i == 65535, client_reader_b.read_exact(&mut buf).await.unwrap());
                        assert_eq!(i, u64::from_le_bytes(buf.clone()));
                    }
                };

                join(send, recv).await;
            })
        }

        join_all(test_tasks).await;
    }
}
