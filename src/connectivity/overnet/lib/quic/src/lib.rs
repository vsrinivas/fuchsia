// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Async wrapper around QUIC

use anyhow::{format_err, Context as _, Error};
use async_utils::mutex_ticket::MutexTicket;
use fuchsia_async::{Task, Timer};
use futures::{
    future::{poll_fn, Either},
    lock::Mutex,
    prelude::*,
    ready,
};
use quiche::{Connection, Shutdown};
use std::cmp::{max, min};
use std::collections::BTreeMap;
use std::pin::Pin;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Arc;
use std::task::{Context, Poll, Waker};
use std::time::{Duration, Instant};

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

    fn ready(&mut self, k: u64) {
        self.0.remove(&k).map(|w| w.wake());
    }

    fn ready_iter(&mut self, iter: impl Iterator<Item = u64>) {
        for k in iter {
            self.ready(k)
        }
    }

    fn all_ready(&mut self) {
        std::mem::replace(&mut self.0, BTreeMap::new()).into_iter().for_each(|(_, w)| w.wake());
    }
}

/// Current state of a connection - mutex guarded by AsyncConnection.
pub struct ConnState {
    conn: Pin<Box<Connection>>,
    seen_established: bool,
    closed: bool,
    conn_send: Wakeup,
    stream_recv: WakeupMap,
    stream_send: WakeupMap,
    dgram_send: Wakeup,
    dgram_recv: Wakeup,
    new_timeout: Wakeup,
    timeout: Option<Instant>,
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
        match self.conn.send(frame) {
            Ok(n) => {
                self.update_timeout();
                self.wake_stream_io();
                self.dgram_send.ready();
                Poll::Ready(Ok(Some(n)))
            }
            Err(quiche::Error::Done) if self.conn.is_closed() => Poll::Ready(Ok(None)),
            Err(quiche::Error::Done) => {
                self.update_timeout();
                self.conn_send.pending(ctx)
            }
            Err(e) => Poll::Ready(Err(e.into())),
        }
    }

    fn wake_stream_io(&mut self) {
        if !self.seen_established && self.conn.is_established() {
            self.seen_established = true;
            self.stream_recv.all_ready();
            self.stream_send.all_ready();
        } else {
            self.stream_recv.ready_iter(self.conn.readable());
            self.stream_send.ready_iter(self.conn.writable());
        }
    }

    fn wait_for_new_timeout(
        &mut self,
        ctx: &mut Context<'_>,
        last_seen: Option<Instant>,
    ) -> Poll<Option<Instant>> {
        if last_seen == self.timeout {
            self.new_timeout.pending(ctx)
        } else {
            Poll::Ready(self.timeout)
        }
    }

    fn update_timeout(&mut self) {
        // TODO: the max(d, 1ms) below is a hedge against unreasonable values coming out of quiche.
        // In particular, at least one version has been observed to produce 0 length durations,
        // which jams us on some platforms/executors into a spin loop, freezing out other activity.
        let new_timeout =
            self.conn.timeout().map(|d| Instant::now() + max(d, Duration::from_millis(1)));
        match (new_timeout, self.timeout) {
            (None, None) => return,
            (Some(a), Some(b)) => {
                if max(a, b) - min(a, b) < Duration::from_millis(1) {
                    return;
                }
            }
            _ => (),
        }
        self.timeout = new_timeout;
        self.new_timeout.ready();
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
    pub fn from_connection(conn: Pin<Box<Connection>>, endpoint: Endpoint) -> Arc<Self> {
        Arc::new(Self {
            trace_id: conn.trace_id().to_string(),
            io: Mutex::new(ConnState {
                conn,
                seen_established: false,
                closed: false,
                conn_send: Default::default(),
                stream_recv: Default::default(),
                stream_send: Default::default(),
                dgram_recv: Default::default(),
                dgram_send: Default::default(),
                timeout: None,
                new_timeout: Default::default(),
            }),
            next_bidi: AtomicU64::new(0),
            next_uni: AtomicU64::new(0),
            endpoint,
        })
    }

    pub async fn run(self: Arc<Self>) -> Result<(), Error> {
        let mut timeout_lock = MutexTicket::new(&self.io);
        // TODO: we shouldn't need this: we should just sleep forever if we get a timeout of None.
        const A_VERY_LONG_TIME: Duration = Duration::from_secs(10000);
        let timer_for_timeout = move |timeout: Option<Instant>| {
            Timer::new(timeout.unwrap_or_else(move || Instant::now() + A_VERY_LONG_TIME))
        };

        let mut current_timeout = None;
        let mut timeout_fut = timer_for_timeout(current_timeout);
        loop {
            let poll_timeout = |ctx: &mut Context<'_>| -> Poll<Option<Instant>> {
                ready!(timeout_lock.poll(ctx)).wait_for_new_timeout(ctx, current_timeout)
            };
            match futures::future::select(poll_fn(poll_timeout), &mut timeout_fut).await {
                Either::Left((timeout, _)) => {
                    log::trace!("new timeout: {:?} old timeout: {:?}", timeout, current_timeout);
                    current_timeout = timeout;
                    timeout_fut = timer_for_timeout(current_timeout);
                }
                Either::Right(_) => {
                    timeout_fut = Timer::new(A_VERY_LONG_TIME);
                    let mut io = timeout_lock.lock().await;
                    io.conn.on_timeout();
                    io.update_timeout();
                    io.wake_stream_io();
                    io.conn_send.ready();
                }
            }
        }
    }

    pub async fn close(&self) {
        let mut io = self.io.lock().await;
        log::trace!("{:?} close()", self.debug_id());
        io.closed = true;
        let _ = io.conn.close(false, 0, b"");
        io.stream_recv.all_ready();
        io.stream_send.all_ready();
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
        match io.conn.recv(packet) {
            Ok(_) => (),
            Err(quiche::Error::Done) => (),
            Err(x) => {
                return Err(x).with_context(|| format!("quice_trace_id:{}", io.conn.trace_id()));
            }
        }
        io.update_timeout();
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
                let n = io
                    .conn
                    .stream_send(id, &bytes[sent..], fin)
                    .or_else(|e| if quiche::Error::Done == e { Ok(0) } else { Err(e) })
                    .with_context(|| format!("sending on stream {}", id))?;
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
    pub fn read<'b>(&'b mut self, bytes: impl Into<ReadBuf<'b>>) -> QuicRead<'b> {
        QuicRead {
            conn: &self.conn,
            id: self.id,
            observed_closed: &mut self.observed_closed,
            ready: &mut self.ready,
            buffered: &mut self.buffered,
            bytes: bytes.into(),
            bytes_offset: 0,
            io_lock: MutexTicket::new(&self.conn.io),
        }
    }

    pub fn read_exact<'b>(&'b mut self, bytes: &'b mut [u8]) -> ReadExact<'b> {
        ReadExact { read: self.read(bytes), done: false }
    }

    pub async fn abandon(&mut self) {
        log::trace!("{:?} reader abandon", self.debug_id());
        self.observed_closed = true;
        if let Err(e) = self.conn.io.lock().await.conn.stream_shutdown(self.id, Shutdown::Read, 0) {
            log::trace!("shutdown stream failed: {:?}", e);
        }
    }
}

#[derive(Debug)]
pub struct ReadExact<'b> {
    read: QuicRead<'b>,
    done: bool,
}

impl<'b> ReadExact<'b> {
    #[allow(dead_code)]
    pub fn debug_id(&self) -> impl std::fmt::Debug {
        self.read.debug_id()
    }
}

impl<'b> Future for ReadExact<'b> {
    type Output = Result<bool, Error>;
    fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = Pin::into_inner(self);
        assert!(!this.done);
        loop {
            let read = &mut this.read;
            let (n, fin) = ready!(read.poll_unpin(ctx))?;
            read.bytes_offset += n;
            if read.bytes_offset == read.bytes.len() {
                this.done = true;
                return Poll::Ready(Ok(fin));
            }
            if fin {
                this.done = true;
                return Poll::Ready(Err(format_err!("End of stream")));
            }
        }
    }
}

impl<'b> ReadExact<'b> {
    pub fn read_buf_mut(&mut self) -> &mut ReadBuf<'b> {
        self.read.read_buf_mut()
    }

    pub fn rearm(&mut self, bytes: impl Into<ReadBuf<'b>>) {
        assert!(self.done);
        self.read.bytes_offset = 0;
        self.read.bytes = bytes.into();
        self.done = false;
    }

    pub fn conn(&self) -> &Arc<AsyncConnection> {
        &self.read.conn
    }
}

impl<'b> Drop for ReadExact<'b> {
    fn drop(&mut self) {
        // If we never returned Ready but did read some data, then return that data to the readers
        // buffer so that we can try to consume it next time.
        if !self.done && self.read.bytes_offset != 0 {
            assert!(self.read.buffered.is_empty());
            *self.read.buffered = self.read.bytes.take_vec_to(self.read.bytes_offset);
        }
    }
}

#[derive(Debug)]
pub enum ReadBuf<'b> {
    Slice(&'b mut [u8]),
    Vec(&'b mut Vec<u8>),
}

impl<'b> ReadBuf<'b> {
    fn as_mut_slice_from<'a>(&'a mut self, offset: usize) -> &'a mut [u8] {
        match self {
            Self::Slice(buf) => &mut buf[offset..],
            Self::Vec(buf) => &mut buf[offset..],
        }
    }

    pub fn as_mut_slice<'a>(&'a mut self) -> &'a mut [u8] {
        match self {
            Self::Slice(buf) => buf,
            Self::Vec(buf) => buf.as_mut(),
        }
    }

    fn take_vec_to(&mut self, offset: usize) -> Vec<u8> {
        match self {
            Self::Slice(buf) => buf[..offset].to_vec(),
            Self::Vec(buf) => {
                buf.truncate(offset);
                std::mem::replace(buf, Vec::new())
            }
        }
    }

    pub fn take_vec(&mut self) -> Vec<u8> {
        match self {
            Self::Slice(buf) => buf.to_vec(),
            Self::Vec(buf) => std::mem::replace(buf, Vec::new()),
        }
    }

    fn len(&self) -> usize {
        match self {
            Self::Slice(buf) => buf.len(),
            Self::Vec(buf) => buf.len(),
        }
    }
}

impl<'b> From<&'b mut [u8]> for ReadBuf<'b> {
    fn from(buf: &'b mut [u8]) -> Self {
        Self::Slice(buf)
    }
}

impl<'b> From<&'b mut Vec<u8>> for ReadBuf<'b> {
    fn from(buf: &'b mut Vec<u8>) -> Self {
        Self::Vec(buf)
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

#[derive(Debug)]
pub struct QuicRead<'b> {
    id: u64,
    ready: &'b mut bool,
    observed_closed: &'b mut bool,
    buffered: &'b mut Vec<u8>,
    conn: &'b Arc<AsyncConnection>,
    bytes: ReadBuf<'b>,
    bytes_offset: usize,
    io_lock: MutexTicket<'b, ConnState>,
}

impl<'b> QuicRead<'b> {
    #[allow(dead_code)]
    pub fn debug_id(&self) -> impl std::fmt::Debug {
        (self.conn.endpoint, self.id)
    }

    pub fn read_buf_mut(&mut self) -> &mut ReadBuf<'b> {
        &mut self.bytes
    }

    fn poll_inner(&mut self, ctx: &mut Context<'_>) -> Poll<Result<(usize, bool), Error>> {
        let bytes = &mut self.bytes.as_mut_slice_from(self.bytes_offset);

        if !self.buffered.is_empty() {
            let n = match bytes.len().cmp(&self.buffered.len()) {
                std::cmp::Ordering::Less => {
                    let trail = self.buffered.split_off(bytes.len());
                    let head = std::mem::replace(self.buffered, trail);
                    bytes.copy_from_slice(&head);
                    bytes.len()
                }
                std::cmp::Ordering::Equal => {
                    bytes.copy_from_slice(self.buffered);
                    self.buffered.clear();
                    bytes.len()
                }
                std::cmp::Ordering::Greater => {
                    let n = self.buffered.len();
                    bytes[..n].copy_from_slice(self.buffered);
                    self.buffered.clear();
                    n
                }
            };
            return Poll::Ready(Ok((n, false)));
        }

        let mut io = ready!(self.io_lock.poll(ctx));
        io.conn_send.ready();
        match io.conn.stream_recv(self.id, bytes) {
            Ok((n, fin)) => {
                *self.ready = true;
                Poll::Ready(Ok((n, fin)))
            }
            Err(quiche::Error::Done) => {
                *self.ready = true;
                let finished = io.conn.stream_finished(self.id);
                if finished {
                    Poll::Ready(Ok((0, true)))
                } else if io.closed {
                    log::trace!("{:?} reader abandon", self.debug_id());
                    let _ = io.conn.stream_shutdown(self.id, Shutdown::Read, 0);
                    Poll::Ready(Ok((0, true)))
                } else {
                    io.stream_recv.pending(ctx, self.id)
                }
            }
            Err(quiche::Error::InvalidStreamState) if !*self.ready => {
                io.stream_recv.pending(ctx, self.id)
            }
            Err(quiche::Error::InvalidStreamState) if io.conn.stream_finished(self.id) => {
                Poll::Ready(Ok((0, true)))
            }
            Err(x) => Poll::Ready(Err(x).with_context(|| {
                format_err!("async quic read: stream_id={:?} ready={:?}", self.id, *self.ready,)
            })),
        }
    }
}

impl<'b> Future for QuicRead<'b> {
    type Output = Result<(usize, bool), Error>;

    fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = Pin::into_inner(self);
        let (n, fin) = ready!(this.poll_inner(ctx))?;
        if fin {
            *this.observed_closed = true;
        }
        Poll::Ready(Ok((n, fin)))
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
    pub async fn run_client_server<
        F: 'static + Sync + Clone + Send + FnOnce(Arc<AsyncConnection>, Arc<AsyncConnection>) -> Fut,
        Fut: 'static + Send + Future<Output = ()>,
    >(
        f: F,
    ) {
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let client = AsyncConnection::from_connection(
            quiche::connect(None, &scid, &mut client_config().unwrap()).unwrap(),
            Endpoint::Client,
        );
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let server = AsyncConnection::from_connection(
            quiche::accept(&scid, None, &mut server_config().await.unwrap()).unwrap(),
            Endpoint::Server,
        );
        let forward = futures::future::try_join(
            direct_packets(client.clone(), server.clone()),
            direct_packets(server.clone(), client.clone()),
        );
        let _1 = Task::spawn(async move {
            forward.await.unwrap();
        });
        f(client, server).await;
    }
}

#[cfg(test)]
mod test {

    use super::test_util::run_client_server;
    use super::StreamProperties;

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
}
