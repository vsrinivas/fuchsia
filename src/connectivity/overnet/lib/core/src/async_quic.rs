// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Async wrapper around QUIC

use crate::future_help::{log_errors, Observable};
use crate::labels::Endpoint;
use crate::runtime::{maybe_wait_until, spawn};
use anyhow::{format_err, Context as _, Error};
use futures::{prelude::*, select};
use std::cell::RefCell;
use std::collections::BTreeMap;
use std::pin::Pin;
use std::rc::{Rc, Weak};
use std::sync::atomic::{AtomicU64, Ordering};
use std::task::{Context, Poll, Waker};
use std::time::Instant;

struct IO {
    conn: Box<quiche::Connection>,
    seen_established: bool,
    waiting_for_conn_send: Option<Waker>,
    waiting_for_stream_recv: BTreeMap<u64, Waker>,
    waiting_for_stream_send: BTreeMap<u64, Waker>,
    timeout: Observable<Option<Instant>>,
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
            std::mem::replace(&mut self.waiting_for_stream_recv, BTreeMap::new())
                .into_iter()
                .for_each(|(_, w)| w.wake());
            std::mem::replace(&mut self.waiting_for_stream_send, BTreeMap::new())
                .into_iter()
                .for_each(|(_, w)| w.wake());
            return;
        }
        for s in self.conn.readable() {
            self.wake_stream_recv(s);
        }
        for s in self.conn.writable() {
            self.wake_stream_send(s);
        }
    }

    fn update_timeout(&mut self) {
        self.timeout.push(self.conn.timeout().map(|d| Instant::now() + d));
    }
}

struct AsyncConnectionInner {
    next_bidi: AtomicU64,
    endpoint: Endpoint,
    io: RefCell<IO>,
}

pub struct AsyncConnection(Rc<AsyncConnectionInner>);

impl AsyncConnection {
    pub fn from_connection(conn: Box<quiche::Connection>, endpoint: Endpoint) -> Self {
        let inner = Rc::new(AsyncConnectionInner {
            io: RefCell::new(IO {
                conn,
                seen_established: false,
                waiting_for_conn_send: None,
                waiting_for_stream_recv: BTreeMap::new(),
                waiting_for_stream_send: BTreeMap::new(),
                timeout: Observable::new(None),
            }),
            next_bidi: AtomicU64::new(match endpoint {
                Endpoint::Client => 1,
                Endpoint::Server => 0,
            }),
            endpoint,
        });
        spawn(log_errors(run_timers(Rc::downgrade(&inner)), "Error checking QUIC timers"));
        Self(inner)
    }

    fn clone(&self) -> Self {
        Self(self.0.clone())
    }

    pub fn poll_next_send<'a>(
        &self,
        frame: &'a mut [u8],
        ctx: &mut Context<'_>,
    ) -> Poll<Result<Option<usize>, Error>> {
        let mut io = self.0.io.borrow_mut();
        match io.conn.send(frame) {
            Ok(n) => {
                io.update_timeout();
                io.wake_stream_io();
                Poll::Ready(Ok(Some(n)))
            }
            Err(quiche::Error::Done) => io.wait_for_conn_send(ctx),
            Err(e) => Poll::Ready(Err(e.into())),
        }
    }

    pub fn recv(&self, packet: &mut [u8]) -> Result<(), Error> {
        let mut io = self.0.io.borrow_mut();
        match io.conn.recv(packet) {
            Ok(_) => (),
            Err(quiche::Error::Done) => (),
            Err(x) => return Err(x.into()),
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

    pub fn bind_id(&self, id: u64) -> (AsyncQuicStreamWriter, AsyncQuicStreamReader) {
        (
            AsyncQuicStreamWriter { conn: self.clone(), id },
            AsyncQuicStreamReader { conn: self.clone(), id, ready: false },
        )
    }

    pub fn stats(&self) -> quiche::Stats {
        self.0.io.borrow().conn.stats()
    }

    pub fn is_established(&self) -> bool {
        self.0.io.borrow().conn.is_established()
    }
}

async fn run_timers(inner: Weak<AsyncConnectionInner>) -> Result<(), Error> {
    let mut observer = Weak::upgrade(&inner)
        .ok_or_else(|| format_err!("Connection disappeared before running timers"))?
        .io
        .borrow()
        .timeout
        .new_observer();

    enum Action {
        OnTimeout,
        UpdateTimeout(Option<Option<Instant>>),
    }

    let mut current_timeout = None;
    loop {
        let action = select! {
            _ = maybe_wait_until(current_timeout).fuse() => Action::OnTimeout,
            x = observer.next().fuse() => Action::UpdateTimeout(x),
        };
        match action {
            Action::OnTimeout => {
                let inner = Weak::upgrade(&inner)
                    .ok_or_else(|| format_err!("Connection disappeared before timeout expired"))?;
                let mut io = inner.io.borrow_mut();
                io.conn.on_timeout();
                io.update_timeout();
            }
            Action::UpdateTimeout(None) => return Ok(()),
            Action::UpdateTimeout(Some(timeout)) => current_timeout = timeout,
        }
    }
}

pub struct AsyncQuicStreamWriter {
    conn: AsyncConnection,
    id: u64,
}

impl AsyncQuicStreamWriter {
    pub fn send<'b>(&mut self, bytes: &'b [u8], fin: bool) -> QuicSend<'b> {
        QuicSend { conn: self.conn.clone(), id: self.id, bytes, fin, n: 0 }
    }

    pub fn conn(&self) -> &AsyncConnection {
        &self.conn
    }

    pub fn id(&self) -> u64 {
        self.id
    }
}

pub struct QuicSend<'b> {
    conn: AsyncConnection,
    id: u64,
    bytes: &'b [u8],
    n: usize,
    fin: bool,
}

impl<'b> Future for QuicSend<'b> {
    type Output = Result<(), Error>;

    fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = Pin::into_inner(self);
        let io = &mut *this.conn.0.io.borrow_mut();
        if !io.conn.is_established() {
            return io.wait_for_stream_send(this.id, ctx);
        }
        match io
            .conn
            .stream_send(this.id, &this.bytes[this.n..], this.fin)
            .with_context(|| format!("sending on stream {}", this.id))
        {
            Err(e) => Poll::Ready(Err(e).with_context(|| {
                format_err!(
                    "async quic send: stream_id={:?} endpoint={:?}",
                    this.id,
                    this.conn.0.endpoint
                )
            })),
            Ok(n) => {
                io.wake_conn_send();
                this.n += n;
                if this.n == this.bytes.len() {
                    Poll::Ready(Ok(()))
                } else {
                    io.wait_for_stream_send(this.id, ctx)
                }
            }
        }
    }
}

pub struct AsyncQuicStreamReader {
    conn: AsyncConnection,
    id: u64,
    ready: bool,
}

impl AsyncQuicStreamReader {
    pub fn read<'a, 'b>(&'a mut self, bytes: &'b mut [u8]) -> QuicRead<'a, 'b> {
        QuicRead { conn: self.conn.clone(), id: self.id, bytes, ready: &mut self.ready }
    }

    pub async fn read_exact(&mut self, mut bytes: &mut [u8]) -> Result<bool, Error> {
        loop {
            let (n, fin) = self.read(bytes).await?;
            if n == bytes.len() {
                return Ok(fin);
            }
            if fin {
                anyhow::bail!("End of stream");
            }
            bytes = &mut bytes[n..];
        }
    }

    pub fn conn(&self) -> &AsyncConnection {
        &self.conn
    }
}

pub struct QuicRead<'a, 'b> {
    conn: AsyncConnection,
    id: u64,
    bytes: &'b mut [u8],
    ready: &'a mut bool,
}

impl<'a, 'b> Future for QuicRead<'a, 'b> {
    type Output = Result<(usize, bool), Error>;

    fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<Self::Output> {
        let this = Pin::into_inner(self);
        let io = &mut *this.conn.0.io.borrow_mut();
        match io.conn.stream_recv(this.id, this.bytes) {
            Ok((n, false)) => {
                *this.ready = true;
                Poll::Ready(Ok((n, false)))
            }
            Ok((n, true)) => {
                *this.ready = true;
                Poll::Ready(Ok((n, true)))
            }
            Err(quiche::Error::Done) => {
                *this.ready = true;
                io.wait_for_stream_recv(this.id, ctx)
            }
            Err(quiche::Error::InvalidStreamState) if !*this.ready => {
                io.wait_for_stream_recv(this.id, ctx)
            }
            Err(x) => Poll::Ready(Err(x).with_context(|| {
                format_err!(
                    "async quic read: stream_id={:?} ready={:?} endpoint={:?}",
                    this.id,
                    this.ready,
                    this.conn.0.endpoint
                )
            })),
        }
    }
}

#[cfg(test)]
pub(crate) mod test_util {

    use super::*;
    use crate::future_help::log_errors;
    use crate::runtime::spawn;
    use futures::future::poll_fn;
    use rand::Rng;

    fn server_config() -> Result<quiche::Config, Error> {
        let test_router_options = crate::router::test_util::test_router_options();

        let mut config = quiche::Config::new(quiche::PROTOCOL_VERSION)
            .context("Creating quic configuration for server connection")?;
        let cert_file = test_router_options
            .quic_server_cert_file
            .as_ref()
            .unwrap()
            .as_ref()
            .as_ref()
            .to_str()
            .ok_or_else(|| format_err!("Cannot convert path to string"))?;
        let key_file = test_router_options
            .quic_server_key_file
            .as_ref()
            .unwrap()
            .as_ref()
            .as_ref()
            .to_str()
            .ok_or_else(|| format_err!("Cannot convert path to string"))?;
        config
            .load_cert_chain_from_pem_file(cert_file)
            .context(format!("Loading server certificate '{}'", cert_file))?;
        config
            .load_priv_key_from_pem_file(key_file)
            .context(format!("Loading server private key '{}'", key_file))?;
        // TODO(ctiller): don't hardcode these
        config
            .set_application_protos(b"\x0bovernet/0.1")
            .context("Setting application protocols")?;
        config.set_initial_max_data(10_000_000);
        config.set_initial_max_stream_data_bidi_local(1_000_000);
        config.set_initial_max_stream_data_bidi_remote(1_000_000);
        config.set_initial_max_stream_data_uni(1_000_000);
        config.set_initial_max_streams_bidi(100);
        config.set_initial_max_streams_uni(0);
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
        config.set_initial_max_streams_uni(0);
        Ok(config)
    }

    async fn direct_packets(from: AsyncConnection, to: AsyncConnection) -> Result<(), Error> {
        let mut frame = [0u8; 2048];
        while let Some(length) = poll_fn(|ctx| from.poll_next_send(&mut frame, ctx)).await? {
            to.recv(&mut frame[..length])?;
        }
        Ok(())
    }

    /// Generate a test connection pair, that automatically forwards packets from client to server.
    pub fn new_client_server() -> Result<(AsyncConnection, AsyncConnection), Error> {
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let client = AsyncConnection::from_connection(
            quiche::connect(None, &scid, &mut client_config()?)?,
            Endpoint::Client,
        );
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let server = AsyncConnection::from_connection(
            quiche::accept(&scid, None, &mut server_config()?)?,
            Endpoint::Server,
        );
        spawn(log_errors(
            direct_packets(client.clone(), server.clone()),
            "directing packets from client->server",
        ));
        spawn(log_errors(
            direct_packets(server.clone(), client.clone()),
            "directing packets from server->client",
        ));
        Ok((client, server))
    }
}

#[cfg(test)]
mod test {

    use super::test_util::new_client_server;
    use crate::future_help::log_errors;
    use crate::router::test_util::run;
    use crate::runtime::spawn;

    #[test]
    fn simple_send() {
        run(|| async move {
            let (client, server) = new_client_server().unwrap();
            let (mut cli_tx, _cli_rx) = client.alloc_bidi();
            let (_svr_tx, mut svr_rx) = server.bind_id(cli_tx.id());

            cli_tx.send(&[1, 2, 3], false).await.unwrap();
            let mut buf = [0u8; 64];
            let (n, fin) = svr_rx.read(&mut buf).await.unwrap();
            assert_eq!(n, 3);
            assert_eq!(fin, false);
            assert_eq!(&buf[..n], &[1, 2, 3]);
        })
    }

    #[test]
    fn send_fin() {
        run(|| async move {
            let (client, server) = new_client_server().unwrap();
            let (mut cli_tx, _cli_rx) = client.alloc_bidi();
            let (_svr_tx, mut svr_rx) = server.bind_id(cli_tx.id());

            cli_tx.send(&[1, 2, 3], true).await.unwrap();
            let mut buf = [0u8; 64];
            let (n, fin) = svr_rx.read(&mut buf).await.unwrap();
            assert_eq!(n, 3);
            assert_eq!(fin, true);
            assert_eq!(&buf[..n], &[1, 2, 3]);
        })
    }

    #[test]
    fn recv_before_send() {
        run(|| async move {
            let (client, server) = new_client_server().unwrap();
            let (mut cli_tx, _cli_rx) = client.alloc_bidi();
            let (_svr_tx, mut svr_rx) = server.bind_id(cli_tx.id());

            spawn(log_errors(cli_tx.send(&[1, 2, 3], false), "send failed"));
            let mut buf = [0u8; 64];
            let (n, fin) = svr_rx.read(&mut buf).await.unwrap();
            assert_eq!(n, 3);
            assert_eq!(fin, false);
            assert_eq!(&buf[..n], &[1, 2, 3]);
        })
    }
}
