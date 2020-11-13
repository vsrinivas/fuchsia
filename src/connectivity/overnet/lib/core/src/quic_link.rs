// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::future_help::{log_errors, LockInner, PollMutex};
use crate::labels::Endpoint;
use crate::link::{LinkReceiver, LinkSender};
use crate::security_context::quiche_config_from_security_context;
use anyhow::Error;
use fuchsia_async::{Task, Timer};
use futures::future::{poll_fn, Either};
use futures::lock::{Mutex, MutexGuard, MutexLockFuture};
use futures::ready;
use rand::Rng;
use std::collections::BTreeMap;
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll, Waker};
use std::time::{Duration, Instant};

const MAX_FRAME_SIZE: usize = 4096;

// Shared state for link. Public to statisfy LockInner, but should not expose any fields nor
// methods -- this is really a private type.
pub(crate) struct Quic {
    connection: Pin<Box<quiche::Connection>>,
    waiting_for_send: Option<Waker>,
    waiting_for_read: Option<Waker>,
    new_timeout: Option<Waker>,
    waiting_for_established: Option<Waker>,
    timeout: Option<Instant>,
}

impl std::fmt::Debug for Quic {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.connection.trace_id())?;
        f.write_str("|")?;
        if self.waiting_for_send.is_some() {
            f.write_str("s")?;
        }
        if self.waiting_for_read.is_some() {
            f.write_str("r")?;
        }
        if self.new_timeout.is_some() {
            f.write_str("t")?;
        }
        if self.waiting_for_established.is_some() {
            f.write_str("e")?;
        }
        if let Some(t) = self.timeout {
            f.write_str("|")?;
            t.fmt(f)?;
        }
        Ok(())
    }
}

impl Quic {
    fn wait_for_send<R>(&mut self, ctx: &mut Context<'_>) -> Poll<R> {
        self.waiting_for_send = Some(ctx.waker().clone());
        Poll::Pending
    }

    fn wait_for_read<R>(&mut self, ctx: &mut Context<'_>) -> Poll<R> {
        self.waiting_for_read = Some(ctx.waker().clone());
        Poll::Pending
    }

    fn wait_for_established<R>(&mut self, ctx: &mut Context<'_>) -> Poll<R> {
        self.waiting_for_established = Some(ctx.waker().clone());
        Poll::Pending
    }

    fn wakeup_read(&mut self) {
        self.waiting_for_read.take().map(|w| w.wake());
    }

    fn wakeup_send(&mut self) {
        self.waiting_for_send.take().map(|w| w.wake());
    }

    fn wakeup_established(&mut self) {
        self.waiting_for_established.take().map(|w| w.wake());
    }

    fn maybe_wakeup_established(&mut self) {
        if self.connection.is_established() {
            self.wakeup_established();
        }
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
            self.connection.timeout().map(|d| std::cmp::max(d, Duration::from_millis(1)));
        log::trace!("new timeout: {:?}", new_timeout);
        let new_timeout = new_timeout.map(|d| Instant::now() + d);
        if new_timeout != self.timeout {
            self.timeout = new_timeout;
            self.new_timeout.take().map(|w| w.wake());
        }
    }
}

/// Send half for QUIC links
pub struct QuicSender {
    quic: Arc<Mutex<Quic>>,
}

/// Receive half for QUIC links
pub struct QuicReceiver {
    quic: Arc<Mutex<Quic>>,
    // Processing loop for the link - once there's no receiver this can stop
    _task: Task<()>,
}

impl LockInner for QuicSender {
    type Inner = Quic;
    fn lock_inner<'a>(&'a self) -> MutexLockFuture<'a, Quic> {
        self.quic.lock()
    }
}

/// Create a QUIC link to tunnel an Overnet link through.
pub async fn new_quic_link(
    sender: LinkSender,
    receiver: LinkReceiver,
    endpoint: Endpoint,
) -> Result<(QuicSender, QuicReceiver), Error> {
    let scid: Vec<u8> = rand::thread_rng()
        .sample_iter(&rand::distributions::Standard)
        .take(quiche::MAX_CONN_ID_LEN)
        .collect();
    let mut config =
        quiche_config_from_security_context(sender.router().security_context()).await?;
    config.set_application_protos(b"\x10overnet.link/0.1")?;
    config.set_initial_max_data(10_000_000);
    config.set_initial_max_stream_data_bidi_local(0);
    config.set_initial_max_stream_data_bidi_remote(0);
    config.set_initial_max_stream_data_uni(1_000_000);
    config.set_initial_max_streams_bidi(0);
    config.set_initial_max_streams_uni(100);

    let quic = Arc::new(Mutex::new(Quic {
        connection: match endpoint {
            Endpoint::Client => quiche::connect(None, &scid, &mut config)?,
            Endpoint::Server => quiche::accept(&scid, None, &mut config)?,
        },
        waiting_for_read: None,
        waiting_for_send: None,
        waiting_for_established: None,
        timeout: None,
        new_timeout: None,
    }));

    Ok((
        QuicSender { quic: quic.clone() },
        QuicReceiver {
            _task: Task::spawn(log_errors(
                run_link(sender, receiver, quic.clone(), endpoint),
                "QUIC link failed",
            )),
            quic,
        },
    ))
}

impl QuicReceiver {
    /// Report a packet was received.
    /// An error processing a packet does not indicate that the link should be closed.
    pub async fn received_packet(&self, packet: &mut [u8]) -> Result<(), Error> {
        let mut q = self.quic.lock().await;
        match q.connection.recv(packet) {
            Ok(_) => (),
            Err(quiche::Error::Done) => (),
            Err(x) => return Err(x.into()),
        }
        q.maybe_wakeup_established();
        q.update_timeout();
        q.wakeup_send();
        q.wakeup_read();
        Ok(())
    }
}

impl QuicSender {
    /// Fetch the next frame that should be sent by the link. Returns Ok(None) on link
    /// closure, Ok(Some(packet_length)) on successful read, and an error otherwise.
    pub async fn next_send(&self, frame: &mut [u8]) -> Result<Option<usize>, Error> {
        let mut lock = PollMutex::new(&self.quic);
        poll_fn(|ctx| self.poll_next_send(ctx, frame, &mut lock)).await
    }

    fn poll_next_send(
        &self,
        ctx: &mut Context<'_>,
        frame: &mut [u8],
        lock: &mut PollMutex<'_, Quic>,
    ) -> Poll<Result<Option<usize>, Error>> {
        let mut q = ready!(lock.poll(ctx));
        match q.connection.send(frame) {
            Ok(n) => {
                q.maybe_wakeup_established();
                q.update_timeout();
                q.wakeup_read();
                Poll::Ready(Ok(Some(n)))
            }
            Err(quiche::Error::Done) => q.wait_for_send(ctx),
            Err(e) => Poll::Ready(Err(e.into())),
        }
    }
}

async fn run_link(
    sender: LinkSender,
    receiver: LinkReceiver,
    quic: Arc<Mutex<Quic>>,
    endpoint: Endpoint,
) -> Result<(), Error> {
    futures::future::try_join3(
        link_to_quic(sender, quic.clone(), endpoint),
        quic_to_link(receiver, quic.clone()),
        check_timers(quic),
    )
    .await?;
    Ok(())
}

async fn link_to_quic(
    link: LinkSender,
    quic: Arc<Mutex<Quic>>,
    endpoint: Endpoint,
) -> Result<(), Error> {
    // QUIC stream id's use the lower two bits to designate the type of stream
    const QUIC_STREAM_SERVER_INITIATED: u64 = 0x01; // otherwise client initiated
    const QUIC_STREAM_UNIDIRECTIONAL: u64 = 0x02; // otherwise bidirectional
    const QUIC_STREAM_NUMBER_INCREMENT: u64 = 4;

    let mut send_id: u64 = match endpoint {
        Endpoint::Client => QUIC_STREAM_UNIDIRECTIONAL,
        Endpoint::Server => QUIC_STREAM_UNIDIRECTIONAL | QUIC_STREAM_SERVER_INITIATED,
    };
    let mut frame = [0u8; 1400];
    {
        let mut poll_mutex = PollMutex::new(&*quic);
        let poll_established = |ctx: &mut Context<'_>| -> Poll<()> {
            let mut q = ready!(poll_mutex.poll(ctx));
            if q.connection.is_established() {
                Poll::Ready(())
            } else {
                q.wait_for_established(ctx)
            }
        };
        poll_fn(poll_established).await;
    }
    loop {
        if let Some(n) = link.next_send(&mut frame).await? {
            let id = send_id;
            send_id += QUIC_STREAM_NUMBER_INCREMENT;
            let mut q = quic.lock().await;
            match q.connection.stream_send(id, &frame[..n], true) {
                Ok(sent) if n == sent => (),
                Ok(sent) => {
                    log::warn!("Dropping packet {} (only sent {} of {} bytes)", id, sent, n);
                    let _ = q.connection.stream_shutdown(id, quiche::Shutdown::Write, 0);
                }
                Err(e) => {
                    log::warn!("Dropping packet {} due to QUIC error {}", id, e);
                    let _ = q.connection.stream_shutdown(id, quiche::Shutdown::Write, 0);
                }
            }
            q.wakeup_send();
        } else {
            break;
        }
    }
    Ok(())
}

async fn quic_to_link(link: LinkReceiver, quic: Arc<Mutex<Quic>>) -> Result<(), Error> {
    let mut incoming: BTreeMap<u64, Vec<u8>> = BTreeMap::new();
    let mut frame = [0u8; MAX_FRAME_SIZE];
    let mut poll_mutex = PollMutex::new(&*quic);
    let mut poll_readable_streams =
        |ctx: &mut Context<'_>| -> Poll<(MutexGuard<'_, Quic>, quiche::StreamIter)> {
            let mut q = ready!(poll_mutex.poll(ctx));
            let it = q.connection.readable();
            if it.len() == 0 {
                q.wait_for_read(ctx)
            } else {
                Poll::Ready((q, it))
            }
        };

    loop {
        let (mut q, readable) = poll_fn(&mut poll_readable_streams).await;
        for stream in readable {
            match q.connection.stream_recv(stream, &mut frame) {
                Err(quiche::Error::Done) => continue,
                Err(x) => {
                    log::warn!("Error reading link frame: {:?}", x);
                    incoming.remove(&stream);
                }
                Ok((n, false)) => {
                    incoming.entry(stream).or_default().extend_from_slice(&frame[..n]);
                }
                Ok((n, true)) => {
                    let packet_result = match incoming.remove(&stream) {
                        None => link.received_packet(&mut frame[..n]).await,
                        Some(mut so_far) => {
                            so_far.extend_from_slice(&frame[..n]);
                            link.received_packet(&mut so_far).await
                        }
                    };
                    match packet_result {
                        Ok(()) => (),
                        Err(e) => log::warn!("Error receiving packet: {:?}", e),
                    }
                    let mut only_new = incoming.split_off(&stream);
                    // `incoming` is now only streams that were before the one received.
                    // We ask quiche to cancel them (as they were received late).
                    for old_stream in incoming.keys() {
                        log::trace!(
                            "Drop old packet {} because {} was received",
                            old_stream,
                            stream
                        );
                        q.connection.stream_shutdown(*old_stream, quiche::Shutdown::Read, 0)?;
                    }
                    std::mem::swap(&mut only_new, &mut incoming);
                }
            }
        }
    }
}

async fn check_timers(quic: Arc<Mutex<Quic>>) -> Result<(), Error> {
    let mut poll_mutex = PollMutex::new(&*quic);
    const A_VERY_LONG_TIME: Duration = Duration::from_secs(10000);
    let timer_for_timeout = move |timeout: Option<Instant>| {
        Timer::new(timeout.unwrap_or_else(|| Instant::now() + A_VERY_LONG_TIME))
    };

    let mut current_timeout = None;
    let mut timeout_fut = timer_for_timeout(current_timeout);
    loop {
        log::trace!("timeout: {:?}", current_timeout);
        let poll_timeout = |ctx: &mut Context<'_>| -> Poll<Option<Instant>> {
            ready!(poll_mutex.poll(ctx)).wait_for_new_timeout(ctx, current_timeout)
        };
        match futures::future::select(poll_fn(poll_timeout), &mut timeout_fut).await {
            Either::Left((timeout, _)) => {
                current_timeout = timeout;
                timeout_fut = timer_for_timeout(current_timeout);
            }
            Either::Right(_) => {
                timeout_fut = Timer::new(A_VERY_LONG_TIME);
                let mut q = poll_mutex.lock().await;
                q.connection.on_timeout();
                q.maybe_wakeup_established();
                q.wakeup_send();
                q.update_timeout();
            }
        }
    }
}
