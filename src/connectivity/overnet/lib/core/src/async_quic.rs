// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Async wrapper around QUIC

use crate::future_help::{LockInner, PollMutex, PollWeakMutex};
use crate::labels::{Endpoint, NodeId};
use anyhow::{format_err, Context as _, Error};
use fidl_fuchsia_overnet_protocol::StreamId;
use fuchsia_async::{Task, Timer};
use futures::{
    future::{poll_fn, Either},
    lock::{Mutex, MutexLockFuture},
    prelude::*,
    ready,
};
use quiche::{Connection, Shutdown};
use std::cmp::{max, min};
use std::collections::BTreeMap;
use std::pin::Pin;
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Weak};
use std::task::{Context, Poll, Waker};
use std::time::{Duration, Instant};

struct IO {
    conn: Pin<Box<Connection>>,
    seen_established: bool,
    closed: bool,
    waiting_for_conn_send: Option<Waker>,
    waiting_for_stream_recv: BTreeMap<u64, Waker>,
    waiting_for_stream_send: BTreeMap<u64, Waker>,
    new_timeout: Option<Waker>,
    timeout: Option<Instant>,
}

impl std::fmt::Debug for IO {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}|est={}", self.conn.trace_id(), self.seen_established)
    }
}

impl IO {
    fn wake_conn_send(&mut self) {
        self.waiting_for_conn_send.take().map(|w| w.wake());
    }

    fn wait_for_conn_send<R>(&mut self, ctx: &mut Context<'_>) -> Poll<R> {
        self.waiting_for_conn_send = Some(ctx.waker().clone());
        Poll::Pending
    }

    fn wait_for_stream_recv<R>(&mut self, stream: u64, ctx: &mut Context<'_>) -> Poll<R> {
        self.waiting_for_stream_recv.insert(stream, ctx.waker().clone());
        Poll::Pending
    }

    fn wait_for_stream_send<R>(&mut self, stream: u64, ctx: &mut Context<'_>) -> Poll<R> {
        self.waiting_for_stream_send.insert(stream, ctx.waker().clone());
        Poll::Pending
    }

    fn wake_stream_recv(&mut self, stream: u64) {
        self.waiting_for_stream_recv.remove(&stream).map(|w| w.wake());
    }

    fn wake_stream_send(&mut self, stream: u64) {
        self.waiting_for_stream_send.remove(&stream).map(|w| w.wake());
    }

    fn wake_stream_io(&mut self) {
        if !self.seen_established && self.conn.is_established() {
            self.seen_established = true;
            return self.wake_all_streams();
        }
        for s in self.conn.readable() {
            self.wake_stream_recv(s);
        }
        for s in self.conn.writable() {
            self.wake_stream_send(s);
        }
    }

    fn wake_all_streams(&mut self) {
        std::mem::replace(&mut self.waiting_for_stream_recv, BTreeMap::new())
            .into_iter()
            .for_each(|(_, w)| w.wake());
        std::mem::replace(&mut self.waiting_for_stream_send, BTreeMap::new())
            .into_iter()
            .for_each(|(_, w)| w.wake());
    }

    fn wait_for_new_timeout(
        &mut self,
        ctx: &mut Context<'_>,
        last_seen: Option<Instant>,
    ) -> Poll<Option<Instant>> {
        if last_seen == self.timeout {
            self.new_timeout = Some(ctx.waker().clone());
            Poll::Pending
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
        self.new_timeout.take().map(|w| w.wake());
    }
}

pub struct NextSend<'a>(PollMutex<'a, IO>, NodeId, Endpoint);

impl<'a> NextSend<'a> {
    pub fn poll(
        &mut self,
        ctx: &mut Context<'_>,
        frame: &mut [u8],
    ) -> Poll<Result<Option<usize>, Error>> {
        let mut guard = ready!(self.0.poll(ctx));
        match guard.conn.send(frame) {
            Ok(n) => {
                guard.update_timeout();
                guard.wake_stream_io();
                Poll::Ready(Ok(Some(n)))
            }
            Err(quiche::Error::Done) if guard.conn.is_closed() => Poll::Ready(Ok(None)),
            Err(quiche::Error::Done) => {
                guard.update_timeout();
                guard.wait_for_conn_send(ctx)
            }
            Err(e) => Poll::Ready(Err(e.into())),
        }
    }
}

#[derive(Debug)]
struct AsyncConnectionInner {
    next_bidi: AtomicU64,
    next_uni: AtomicU64,
    endpoint: Endpoint,
    io: Mutex<IO>,
    own_node_id: NodeId,
    peer_node_id: NodeId,
}

impl LockInner for AsyncConnectionInner {
    type Inner = IO;
    fn lock_inner<'a>(&'a self) -> MutexLockFuture<'a, IO> {
        self.io.lock()
    }
}

#[derive(Clone, Debug)]
pub struct AsyncConnection(Arc<AsyncConnectionInner>);

impl AsyncConnection {
    pub fn from_connection(
        conn: Pin<Box<Connection>>,
        endpoint: Endpoint,
        own_node_id: NodeId,
        peer_node_id: NodeId,
    ) -> Self {
        let inner = Arc::new(AsyncConnectionInner {
            io: Mutex::new(IO {
                conn,
                seen_established: false,
                closed: false,
                waiting_for_conn_send: None,
                waiting_for_stream_recv: BTreeMap::new(),
                waiting_for_stream_send: BTreeMap::new(),
                timeout: None,
                new_timeout: None,
            }),
            next_bidi: AtomicU64::new(match endpoint {
                Endpoint::Client => 1,
                Endpoint::Server => 0,
            }),
            next_uni: AtomicU64::new(0),
            endpoint,
            own_node_id,
            peer_node_id,
        });
        Self(inner)
    }

    /// Perform maintence tasks vital to keeping the connection running.
    /// Owner of the AsyncConnection is responsible for calling this function and providing it a task to run within.
    pub fn run(&self) -> impl Future<Output = Result<(), Error>> {
        run_timers(Arc::downgrade(&self.0))
    }

    pub async fn close(&self) {
        let mut io = self.0.io.lock().await;
        log::trace!("{:?} close()", self.debug_id());
        io.closed = true;
        let _ = io.conn.close(false, 0, b"");
        io.wake_all_streams();
        io.wake_conn_send();
    }

    pub fn next_send<'a>(&'a self) -> NextSend<'a> {
        NextSend(PollMutex::new(&self.0.io), self.0.peer_node_id, self.0.endpoint)
    }

    #[allow(dead_code)]
    pub fn debug_id(&self) -> impl std::fmt::Debug {
        (self.0.own_node_id, self.0.peer_node_id, self.0.endpoint)
    }

    pub async fn recv(&self, packet: &mut [u8]) -> Result<(), Error> {
        let mut io = self.0.io.lock().await;
        match io.conn.recv(packet) {
            Ok(_) => (),
            Err(quiche::Error::Done) => (),
            Err(x) => {
                return Err(x).with_context(|| format!("quice_trace_id:{}", io.conn.trace_id()));
            }
        }
        io.update_timeout();
        io.wake_stream_io();
        io.wake_conn_send();
        Ok(())
    }

    pub fn alloc_bidi(&self) -> (AsyncQuicStreamWriter, AsyncQuicStreamReader) {
        let n = self.0.next_bidi.fetch_add(1, Ordering::Relaxed);
        let id = n * 4 + self.0.endpoint.quic_id_bit();
        self.bind_id(id)
    }

    pub fn alloc_uni(&self) -> AsyncQuicStreamWriter {
        let n = self.0.next_uni.fetch_add(1, Ordering::Relaxed);
        let id = n * 4 + 2 + self.0.endpoint.quic_id_bit();
        AsyncQuicStreamWriter { conn: self.clone(), id, sent_fin: false }
    }

    pub fn bind_uni_id(&self, id: u64) -> AsyncQuicStreamReader {
        AsyncQuicStreamReader {
            conn: self.clone(),
            id,
            ready: false,
            observed_closed: false,
            buffered: Vec::new(),
        }
    }

    pub fn bind_id(&self, id: u64) -> (AsyncQuicStreamWriter, AsyncQuicStreamReader) {
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

    pub async fn stats(&self) -> quiche::Stats {
        self.0.io.lock().await.conn.stats()
    }

    pub async fn is_established(&self) -> bool {
        self.0.io.lock().await.conn.is_established()
    }

    pub fn peer_node_id(&self) -> NodeId {
        self.0.peer_node_id
    }

    pub fn own_node_id(&self) -> NodeId {
        self.0.own_node_id
    }

    pub fn endpoint(&self) -> Endpoint {
        self.0.endpoint
    }
}

// TODO: merge this loop with the one in quic_link (and maybe the one in ping_tracker too).
async fn run_timers(inner: Weak<AsyncConnectionInner>) -> Result<(), Error> {
    let mut timeout_lock = PollWeakMutex::new(inner.clone());
    // TODO: we shouldn't need this: we should just sleep forever if we get a timeout of None.
    const A_VERY_LONG_TIME: Duration = Duration::from_secs(10000);
    let timer_for_timeout = move |timeout: Option<Instant>| {
        Timer::new(timeout.unwrap_or_else(move || Instant::now() + A_VERY_LONG_TIME))
    };

    let mut current_timeout = None;
    let mut timeout_fut = timer_for_timeout(current_timeout);
    loop {
        let poll_timeout = |ctx: &mut Context<'_>| -> Poll<Option<Option<Instant>>> {
            timeout_lock.poll_fn(ctx, |ctx, io| io.wait_for_new_timeout(ctx, current_timeout))
        };
        match futures::future::select(poll_fn(poll_timeout), &mut timeout_fut).await {
            Either::Left((Some(timeout), _)) => {
                log::trace!("new timeout: {:?} old timeout: {:?}", timeout, current_timeout);
                current_timeout = timeout;
                timeout_fut = timer_for_timeout(current_timeout);
            }
            Either::Left((None, _)) => return Ok(()),
            Either::Right(_) => {
                timeout_fut = Timer::new(A_VERY_LONG_TIME);
                timeout_lock
                    .with_lock(|io| {
                        io.conn.on_timeout();
                        io.update_timeout();
                        io.wake_stream_io();
                        io.wake_conn_send();
                    })
                    .await
                    .ok_or_else(|| format_err!("Connection disappeared before timeout expired"))?;
            }
        }
    }
}

pub trait StreamProperties {
    fn conn(&self) -> &AsyncConnection;
    fn id(&self) -> u64;

    fn is_initiator(&self) -> bool {
        // QUIC stream id's use the lower two bits as a stream type designator.
        // Bit 0 of that type is the initiator of the stream: 0 for client, 1 for server.
        self.id() & 1 == self.endpoint().quic_id_bit()
    }

    fn peer_node_id(&self) -> NodeId {
        self.conn().peer_node_id()
    }

    fn own_node_id(&self) -> NodeId {
        self.conn().own_node_id()
    }

    fn endpoint(&self) -> Endpoint {
        self.conn().endpoint()
    }

    fn stream_id(&self) -> StreamId {
        StreamId { id: self.id() }
    }

    fn debug_id(&self) -> (NodeId, NodeId, Endpoint, u64) {
        (self.own_node_id(), self.peer_node_id(), self.endpoint(), self.id())
    }
}

#[derive(Debug)]
pub struct AsyncQuicStreamWriter {
    conn: AsyncConnection,
    id: u64,
    sent_fin: bool,
}

impl AsyncQuicStreamWriter {
    pub async fn abandon(&mut self) {
        self.sent_fin = true;
        log::trace!("{:?} writer abandon", self.debug_id());
        if let Err(e) =
            self.conn.0.io.lock().await.conn.stream_shutdown(self.id, Shutdown::Write, 0)
        {
            log::trace!("shutdown stream failed: {:?}", e);
        }
    }

    pub async fn send(&mut self, bytes: &[u8], fin: bool) -> Result<(), Error> {
        assert_eq!(self.sent_fin, false);
        QuicSend {
            conn: &*self.conn.0,
            id: self.id,
            bytes,
            fin,
            n: 0,
            sent_fin: &mut self.sent_fin,
            io_lock: PollMutex::new(&self.conn.0.io),
        }
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
                let _ = conn.0.io.lock().await.conn.stream_shutdown(id, Shutdown::Write, 0);
            })
            .detach();
        }
    }
}

impl StreamProperties for AsyncQuicStreamWriter {
    fn conn(&self) -> &AsyncConnection {
        &self.conn
    }

    fn id(&self) -> u64 {
        self.id
    }
}

pub struct QuicSend<'b> {
    conn: &'b AsyncConnectionInner,
    id: u64,
    bytes: &'b [u8],
    n: usize,
    fin: bool,
    sent_fin: &'b mut bool,
    io_lock: PollMutex<'b, IO>,
}

impl<'b> QuicSend<'b> {
    #[allow(dead_code)]
    fn debug_id(&self) -> impl std::fmt::Debug {
        (self.conn.own_node_id, self.conn.peer_node_id, self.conn.endpoint, self.id)
    }
}

impl<'b> Future for QuicSend<'b> {
    type Output = Result<(), Error>;

    fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = Pin::into_inner(self);
        let mut io = ready!(this.io_lock.poll(ctx));
        if !io.conn.is_established() {
            return io.wait_for_stream_send(this.id, ctx);
        }
        let n = io
            .conn
            .stream_send(this.id, &this.bytes[this.n..], this.fin)
            .with_context(|| format!("sending on stream {}", this.id))?;
        io.wake_conn_send();
        this.n += n;
        if this.n == this.bytes.len() {
            if this.fin {
                *this.sent_fin = true;
            }
            Poll::Ready(Ok(()))
        } else {
            io.wait_for_stream_send(this.id, ctx)
        }
    }
}

#[derive(Debug)]
pub struct AsyncQuicStreamReader {
    conn: AsyncConnection,
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
                let _ = conn.0.io.lock().await.conn.stream_shutdown(id, Shutdown::Read, 0);
            })
            .detach();
        }
    }
}

impl AsyncQuicStreamReader {
    pub fn read<'b>(&'b mut self, bytes: impl Into<ReadBuf<'b>>) -> QuicRead<'b> {
        QuicRead {
            conn: &self.conn.0,
            id: self.id,
            observed_closed: &mut self.observed_closed,
            ready: &mut self.ready,
            buffered: &mut self.buffered,
            bytes: bytes.into(),
            bytes_offset: 0,
            io_lock: PollMutex::new(&self.conn.0.io),
        }
    }

    pub fn read_exact<'b>(&'b mut self, bytes: &'b mut [u8]) -> ReadExact<'b> {
        ReadExact { read: self.read(bytes), done: false }
    }

    pub async fn abandon(&mut self) {
        log::trace!("{:?} reader abandon", self.debug_id());
        self.observed_closed = true;
        if let Err(e) = self.conn.0.io.lock().await.conn.stream_shutdown(self.id, Shutdown::Read, 0)
        {
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
    pub(crate) fn debug_id(&self) -> impl std::fmt::Debug {
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
    pub(crate) fn read_buf_mut(&mut self) -> &mut ReadBuf<'b> {
        self.read.read_buf_mut()
    }

    pub(crate) fn rearm(&mut self, bytes: impl Into<ReadBuf<'b>>) {
        assert!(self.done);
        self.read.bytes_offset = 0;
        self.read.bytes = bytes.into();
        self.done = false;
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

    pub(crate) fn as_mut_slice<'a>(&'a mut self) -> &'a mut [u8] {
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

    pub(crate) fn take_vec(&mut self) -> Vec<u8> {
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
    fn id(&self) -> u64 {
        self.id
    }

    fn conn(&self) -> &AsyncConnection {
        &self.conn
    }
}

#[derive(Debug)]
pub struct QuicRead<'b> {
    id: u64,
    ready: &'b mut bool,
    observed_closed: &'b mut bool,
    buffered: &'b mut Vec<u8>,
    conn: &'b AsyncConnectionInner,
    bytes: ReadBuf<'b>,
    bytes_offset: usize,
    io_lock: PollMutex<'b, IO>,
}

impl<'b> QuicRead<'b> {
    #[allow(dead_code)]
    pub(crate) fn debug_id(&self) -> impl std::fmt::Debug {
        (self.conn.own_node_id, self.conn.peer_node_id, self.conn.endpoint, self.id)
    }

    pub(crate) fn read_buf_mut(&mut self) -> &mut ReadBuf<'b> {
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
        io.wake_conn_send();
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
                    io.wait_for_stream_recv(self.id, ctx)
                }
            }
            Err(quiche::Error::InvalidStreamState) if !*self.ready => {
                io.wait_for_stream_recv(self.id, ctx)
            }
            Err(quiche::Error::InvalidStreamState) if io.conn.stream_finished(self.id) => {
                Poll::Ready(Ok((0, true)))
            }
            Err(x) => Poll::Ready(Err(x).with_context(|| {
                format_err!(
                    "async quic read: stream_id={:?} ready={:?} endpoint={:?}",
                    self.id,
                    *self.ready,
                    self.conn.endpoint
                )
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
pub(crate) mod test_util {

    use super::*;
    use crate::security_context::quiche_config_from_security_context;
    use crate::test_util::NodeIdGenerator;
    use futures::future::poll_fn;
    use rand::Rng;

    async fn server_config() -> Result<quiche::Config, Error> {
        let mut config =
            quiche_config_from_security_context(&crate::test_util::test_security_context()).await?;

        // TODO(ctiller): don't hardcode these
        config
            .set_application_protos(b"\x0bovernet/0.1")
            .context("Setting application protocols")?;
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
        config.set_application_protos(b"\x0bovernet/0.1")?;
        config.set_initial_max_data(10_000_000);
        config.set_initial_max_stream_data_bidi_local(1_000_000);
        config.set_initial_max_stream_data_bidi_remote(1_000_000);
        config.set_initial_max_stream_data_uni(1_000_000);
        config.set_initial_max_streams_bidi(100);
        config.set_initial_max_streams_uni(100);
        Ok(config)
    }

    async fn direct_packets(from: AsyncConnection, to: AsyncConnection) -> Result<(), Error> {
        let mut frame = [0u8; 2048];
        while let Some(length) = {
            let mut ns = from.next_send();
            poll_fn(|ctx| ns.poll(ctx, &mut frame)).await?
        } {
            to.recv(&mut frame[..length]).await?;
        }
        Ok(())
    }

    /// Generate a test connection pair, that automatically forwards packets from client to server.
    pub async fn run_client_server<
        F: 'static + Sync + Clone + Send + FnOnce(AsyncConnection, AsyncConnection) -> Fut,
        Fut: 'static + Send + Future<Output = ()>,
    >(
        name: &'static str,
        run: usize,
        f: F,
    ) {
        crate::test_util::init();
        let mut node_id_gen = NodeIdGenerator::new(name, run);
        let cli_id = node_id_gen.next().unwrap();
        let svr_id = node_id_gen.next().unwrap();
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let client = AsyncConnection::from_connection(
            quiche::connect(None, &scid, &mut client_config().unwrap()).unwrap(),
            Endpoint::Client,
            cli_id,
            svr_id,
        );
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let server = AsyncConnection::from_connection(
            quiche::accept(&scid, None, &mut server_config().await.unwrap()).unwrap(),
            Endpoint::Server,
            svr_id,
            cli_id,
        );
        let forward = futures::future::try_join(
            direct_packets(client.clone(), server.clone()),
            direct_packets(server.clone(), client.clone()),
        );
        let run_client = client.run();
        let run_server = server.run();
        let _1 = Task::spawn(async move {
            forward.await.unwrap();
        });
        let _2 = Task::spawn(async move {
            run_client.await.unwrap();
        });
        let _3 = Task::spawn(async move {
            run_server.await.unwrap();
        });
        f(client, server).await;
    }
}

#[cfg(test)]
mod test {

    use super::test_util::run_client_server;
    use super::StreamProperties;

    #[fuchsia_async::run(1, test)]
    async fn simple_send(run: usize) {
        run_client_server("simple_send", run, |client, server| async move {
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

    #[fuchsia_async::run(1, test)]
    async fn send_fin(run: usize) {
        run_client_server("send_fin", run, |client, server| async move {
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

    #[fuchsia_async::run(1, test)]
    async fn recv_before_send(run: usize) {
        run_client_server("recv_before_send", run, |client, server| async move {
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
