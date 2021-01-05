// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fuchsia_async::{Task, Timer};
use futures::future::{poll_fn, Either};
use futures::lock::Mutex;
use futures::ready;
use overnet_core::{
    log_errors, ConnectionId, Endpoint, LinkReceiver, LinkSender, MutexTicket, SendFrame,
    MAX_FRAME_LENGTH,
};
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll, Waker};
use std::time::{Duration, Instant};

#[derive(Default)]
struct Wakeup(Option<Waker>);
impl Wakeup {
    fn pending<R>(&mut self, ctx: &mut Context<'_>) -> Poll<R> {
        self.0 = Some(ctx.waker().clone());
        Poll::Pending
    }

    fn ready(&mut self) {
        self.0.take().map(|w| w.wake());
    }
}

// Shared state for link. Public to statisfy LockInner, but should not expose any fields nor
// methods -- this is really a private type.
pub(crate) struct Quic {
    connection: Pin<Box<quiche::Connection>>,
    conn_send: Wakeup,
    dgram_send: Wakeup,
    dgram_recv: Wakeup,
    new_timeout: Wakeup,
    timeout: Option<Instant>,
}

impl std::fmt::Debug for Quic {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.write_str(self.connection.trace_id())?;
        f.write_str("|")?;
        f.write_str(self.conn_send.0.as_ref().map(|_| "s").unwrap_or(""))?;
        f.write_str(self.dgram_send.0.as_ref().map(|_| "S").unwrap_or(""))?;
        f.write_str(self.dgram_recv.0.as_ref().map(|_| "R").unwrap_or(""))?;
        f.write_str(self.new_timeout.0.as_ref().map(|_| "t").unwrap_or(""))?;
        if let Some(t) = self.timeout {
            f.write_str("|")?;
            t.fmt(f)?;
        }
        Ok(())
    }
}

impl Quic {
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
            self.connection.timeout().map(|d| std::cmp::max(d, Duration::from_millis(1)));
        log::trace!("new timeout: {:?}", new_timeout);
        let new_timeout = new_timeout.map(|d| Instant::now() + d);
        if new_timeout != self.timeout {
            self.timeout = new_timeout;
            self.new_timeout.ready();
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

async fn poll_quic<R>(
    quic: &Mutex<Quic>,
    mut f: impl FnMut(&mut Quic, &mut Context<'_>) -> Poll<R>,
) -> R {
    let mut lock = MutexTicket::new(quic);
    poll_fn(|ctx| {
        let mut guard = ready!(lock.poll(ctx));
        f(&mut *guard, ctx)
    })
    .await
}

/// Create a QUIC link to tunnel an Overnet link through.
pub async fn new_quic_link(
    sender: LinkSender,
    receiver: LinkReceiver,
    endpoint: Endpoint,
) -> Result<(QuicSender, QuicReceiver, ConnectionId), Error> {
    let scid = ConnectionId::new();
    let mut config = sender.router().new_quiche_config().await?;
    config.set_application_protos(b"\x10overnet.link/0.2")?;
    config.set_initial_max_data(0);
    config.set_initial_max_stream_data_bidi_local(0);
    config.set_initial_max_stream_data_bidi_remote(0);
    config.set_initial_max_stream_data_uni(0);
    config.set_initial_max_streams_bidi(0);
    config.set_initial_max_streams_uni(0);
    const DGRAM_QUEUE_SIZE: usize = 1024 * 1024;
    config.enable_dgram(true, DGRAM_QUEUE_SIZE, DGRAM_QUEUE_SIZE);

    let quic = Arc::new(Mutex::new(Quic {
        connection: match endpoint {
            Endpoint::Client => quiche::connect(None, &scid.to_array(), &mut config)?,
            Endpoint::Server => quiche::accept(&scid.to_array(), None, &mut config)?,
        },
        timeout: None,
        new_timeout: Default::default(),
        conn_send: Default::default(),
        dgram_send: Default::default(),
        dgram_recv: Default::default(),
    }));

    Ok((
        QuicSender { quic: quic.clone() },
        QuicReceiver {
            _task: Task::spawn(log_errors(
                run_link(sender, receiver, quic.clone()),
                "QUIC link failed",
            )),
            quic,
        },
        scid,
    ))
}

impl QuicReceiver {
    /// Report a packet was received.
    /// An error processing a packet does not indicate that the link should be closed.
    pub async fn received_frame(&self, packet: &mut [u8]) {
        let mut q = self.quic.lock().await;
        match q.connection.recv(packet) {
            Ok(_) => (),
            Err(quiche::Error::Done) => (),
            Err(x) => {
                log::info!("Bad packet received: {:?}", x);
                return;
            }
        }
        q.update_timeout();
        q.conn_send.ready();
        q.dgram_recv.ready();
    }
}

impl QuicSender {
    /// Fetch the next frame that should be sent by the link. Returns Ok(None) on link
    /// closure, Ok(Some(packet_length)) on successful read, and an error otherwise.
    pub async fn next_send(&self, frame: &mut [u8]) -> Result<Option<usize>, Error> {
        poll_quic(&self.quic, |q, ctx| match q.connection.send(frame) {
            Ok(n) => {
                q.update_timeout();
                q.dgram_send.ready();
                Poll::Ready(Ok(Some(n)))
            }
            Err(quiche::Error::Done) => q.conn_send.pending(ctx),
            Err(e) => Poll::Ready(Err(e.into())),
        })
        .await
    }
}

async fn run_link(
    sender: LinkSender,
    receiver: LinkReceiver,
    quic: Arc<Mutex<Quic>>,
) -> Result<(), Error> {
    futures::future::try_join3(
        link_to_quic(sender, quic.clone()),
        quic_to_link(receiver, quic.clone()),
        check_timers(quic),
    )
    .await?;
    Ok(())
}

async fn link_to_quic(mut link: LinkSender, quic: Arc<Mutex<Quic>>) -> Result<(), Error> {
    fn drop_frame<S: std::fmt::Display>(
        p: &SendFrame<'_>,
        make_reason: impl FnOnce() -> S,
    ) -> Poll<Result<(), Error>> {
        log::info!("Drop frame of length {}b: {}", p.bytes().len(), make_reason());
        Poll::Ready(Ok(()))
    }

    while let Some(mut p) = link.next_send().await {
        poll_quic(&quic, move |q, ctx| match q.connection.dgram_send(p.bytes()) {
            Ok(()) => {
                q.conn_send.ready();
                Poll::Ready(Ok(()))
            }
            Err(quiche::Error::Done) => {
                p.drop_inner_locks();
                q.dgram_send.pending(ctx)
            }
            Err(quiche::Error::InvalidState) => drop_frame(&p, || "invalid state"),
            Err(quiche::Error::BufferTooShort) => drop_frame(&p, || {
                format!("buffer too short (max = {:?})", q.connection.dgram_max_writable_len())
            }),
            Err(e) => Poll::Ready(Err(e.into())),
        })
        .await?
    }
    Ok(())
}

async fn quic_to_link(mut link: LinkReceiver, quic: Arc<Mutex<Quic>>) -> Result<(), Error> {
    let mut frame = [0u8; MAX_FRAME_LENGTH];

    loop {
        let n = poll_quic(&quic, |q, ctx| match q.connection.dgram_recv(&mut frame) {
            Ok(n) => Poll::Ready(Ok(n)),
            Err(quiche::Error::Done) => q.dgram_recv.pending(ctx),
            Err(e) => Poll::Ready(Err(e)),
        })
        .await?;
        link.received_frame(&mut frame[..n]).await
    }
}

async fn check_timers(quic: Arc<Mutex<Quic>>) -> Result<(), Error> {
    let mut poll_mutex = MutexTicket::new(&*quic);
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
                q.conn_send.ready();
                q.update_timeout();
            }
        }
    }
}
