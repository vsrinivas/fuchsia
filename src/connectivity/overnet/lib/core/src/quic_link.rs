// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::future_help::{log_errors, Observable};
use crate::labels::Endpoint;
use crate::link::Link as InsecureLink;
use crate::runtime::{maybe_wait_until, spawn};
use anyhow::{format_err, Context as _, Error};
use futures::future::poll_fn;
use futures::prelude::*;
use futures::select;
use rand::Rng;
use std::cell::RefCell;
use std::collections::BTreeMap;
use std::pin::Pin;
use std::rc::{Rc, Weak};
use std::task::{Context, Poll, Waker};
use std::time::{Duration, Instant};

const MAX_FRAME_SIZE: usize = 4096;

struct Quic {
    connection: Pin<Box<quiche::Connection>>,
    waiting_for_send: Option<Waker>,
    waiting_for_read: Option<Waker>,
    timeout: Observable<Option<Instant>>,
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

    fn wakeup(&mut self) {
        self.waiting_for_send.take().map(|w| w.wake());
        self.waiting_for_read.take().map(|w| w.wake());
    }

    fn wakeup_send(&mut self) {
        self.waiting_for_send.take().map(|w| w.wake());
    }

    fn update_timeout(&mut self) {
        // TODO: the max(d, 1ms) below is a hedge against unreasonable values coming out of quiche.
        // In particular, at least one version has been observed to produce 0 length durations,
        // which jams us on some platforms/executors into a spin loop, freezing out other activity.
        self.timeout.push(
            self.connection
                .timeout()
                .map(|d| Instant::now() + std::cmp::max(d, Duration::from_millis(1))),
        );
    }
}

/// A link secured by DTLS.
pub struct Link {
    link: Rc<InsecureLink>,
    quic: RefCell<Quic>,
    _key: Box<dyn AsRef<std::path::Path>>,
    _cert: Box<dyn AsRef<std::path::Path>>,
}

impl Link {
    /// Create a quic link.
    pub fn new(
        link: Rc<InsecureLink>,
        endpoint: Endpoint,
        cert: Box<dyn AsRef<std::path::Path>>,
        key: Box<dyn AsRef<std::path::Path>>,
    ) -> Result<Rc<Self>, Error> {
        let scid: Vec<u8> = rand::thread_rng()
            .sample_iter(&rand::distributions::Standard)
            .take(quiche::MAX_CONN_ID_LEN)
            .collect();
        let mut config = quiche::Config::new(quiche::PROTOCOL_VERSION)?;
        config.set_application_protos(b"\x10overnet.link/0.1")?;
        config.set_initial_max_data(10_000_000);
        config.set_initial_max_stream_data_bidi_local(0);
        config.set_initial_max_stream_data_bidi_remote(0);
        config.set_initial_max_stream_data_uni(1_000_000);
        config.set_initial_max_streams_bidi(0);
        config.set_initial_max_streams_uni(100);
        let cert_file =
            cert.as_ref().as_ref().to_str().ok_or_else(|| format_err!("Failed to load cert"))?;
        let key_file =
            key.as_ref().as_ref().to_str().ok_or_else(|| format_err!("Failed to load key"))?;
        config
            .load_cert_chain_from_pem_file(cert_file)
            .context(format!("Loading server certificate '{}'", cert_file))?;
        config
            .load_priv_key_from_pem_file(key_file)
            .context(format!("Loading server private key '{}'", key_file))?;
        config.verify_peer(false);

        let link = Rc::new(Self {
            link,
            quic: RefCell::new(Quic {
                connection: match endpoint {
                    Endpoint::Client => quiche::connect(None, &scid, &mut config)?,
                    Endpoint::Server => quiche::accept(&scid, None, &mut config)?,
                },
                waiting_for_read: None,
                waiting_for_send: None,
                timeout: Observable::new(None),
            }),
            _cert: cert,
            _key: key,
        });

        spawn(log_errors(run_link(Rc::downgrade(&link), endpoint), "Link failed"));

        Ok(link)
    }

    /// Report a packet was received.
    /// An error processing a packet does not indicate that the link should be closed.
    pub fn received_packet(&self, packet: &mut [u8]) -> Result<(), Error> {
        let mut q = self.quic.borrow_mut();
        match q.connection.recv(packet) {
            Ok(_) => (),
            Err(quiche::Error::Done) => (),
            Err(x) => return Err(x.into()),
        }
        q.update_timeout();
        q.wakeup();
        Ok(())
    }

    /// Fetch the next frame that should be sent by the link. Returns Ok(None) on link
    /// closure, Ok(Some(packet_length)) on successful read, and an error otherwise.
    pub async fn next_send(&self, frame: &mut [u8]) -> Result<Option<usize>, Error> {
        poll_fn(|ctx| self.poll_next_send(ctx, frame)).await
    }

    fn poll_next_send(
        &self,
        ctx: &mut Context<'_>,
        frame: &mut [u8],
    ) -> Poll<Result<Option<usize>, Error>> {
        let mut q = self.quic.borrow_mut();
        match q.connection.send(frame) {
            Ok(n) => {
                q.update_timeout();
                q.wakeup();
                Poll::Ready(Ok(Some(n)))
            }
            Err(quiche::Error::Done) => q.wait_for_send(ctx),
            Err(e) => Poll::Ready(Err(e.into())),
        }
    }

    fn poll_readable_streams(&self, ctx: &mut Context<'_>) -> Poll<impl Iterator<Item = u64>> {
        let mut q = self.quic.borrow_mut();
        let it = q.connection.readable();
        if it.len() == 0 {
            q.wait_for_read(ctx)
        } else {
            Poll::Ready(it)
        }
    }

    async fn readable_streams(&self) -> impl Iterator<Item = u64> {
        poll_fn(|ctx| self.poll_readable_streams(ctx)).await
    }
}

async fn run_link(link: Weak<Link>, endpoint: Endpoint) -> Result<(), Error> {
    futures::future::try_join3(
        link_to_quic(link.clone(), endpoint),
        quic_to_link(link.clone()),
        check_timers(link),
    )
    .await?;
    Ok(())
}

async fn link_to_quic(link: Weak<Link>, endpoint: Endpoint) -> Result<(), Error> {
    // QUIC stream id's use the lower two bits to designate the type of stream
    const QUIC_STREAM_SERVER_INITIATED: u64 = 0x01; // otherwise client initiated
    const QUIC_STREAM_UNIDIRECTIONAL: u64 = 0x02; // otherwise bidirectional
    const QUIC_STREAM_NUMBER_INCREMENT: u64 = 4;

    let mut frame = [0u8; MAX_FRAME_SIZE];
    let mut send_id: u64 = match endpoint {
        Endpoint::Client => QUIC_STREAM_UNIDIRECTIONAL,
        Endpoint::Server => QUIC_STREAM_UNIDIRECTIONAL | QUIC_STREAM_SERVER_INITIATED,
    };
    loop {
        let link = Weak::upgrade(&link).ok_or_else(|| format_err!("Link closed"))?;
        if let Some(n) = link.link.next_send(&mut frame).await? {
            let id = send_id;
            send_id += QUIC_STREAM_NUMBER_INCREMENT;
            // no more .awaits after this point for the rest of scope - since we have q as
            // a borrow against the link
            let mut q = link.quic.borrow_mut();
            let r = q.connection.stream_send(id, &frame[..n], true);
            log::trace!("send {} {}b -> {:?}", id, n, r);
            match r {
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
            break Ok::<(), Error>(());
        }
    }
}

async fn quic_to_link(link: Weak<Link>) -> Result<(), Error> {
    let mut incoming: BTreeMap<u64, Vec<u8>> = BTreeMap::new();
    let mut frame = [0u8; MAX_FRAME_SIZE];
    loop {
        let link = Weak::upgrade(&link).ok_or_else(|| format_err!("Link closed"))?;
        for stream in link.readable_streams().await {
            let r = link.quic.borrow_mut().connection.stream_recv(stream, &mut frame);
            log::trace!("recv {} -> {:?}", stream, r);
            match r {
                Err(quiche::Error::Done) => continue,
                Err(x) => {
                    log::warn!("Error reading link frame: {:?}", x);
                    incoming.remove(&stream);
                }
                Ok((n, false)) => {
                    incoming.entry(stream).or_default().extend_from_slice(&frame[..n]);
                }
                Ok((n, true)) => {
                    match incoming.remove(&stream) {
                        None => link.link.received_packet(&mut frame[..n]).await?,
                        Some(mut so_far) => {
                            so_far.extend_from_slice(&frame[..n]);
                            link.link.received_packet(&mut so_far).await?;
                        }
                    }
                    let mut only_new = incoming.split_off(&stream);
                    // `incoming` is now only streams that were before the one received.
                    // We ask quiche to cancel them (as they were received late).
                    let mut q = link.quic.borrow_mut();
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

async fn check_timers(link: Weak<Link>) -> Result<(), Error> {
    let mut observer = Weak::upgrade(&link)
        .ok_or_else(|| format_err!("Link disappeared before running timers"))?
        .quic
        .borrow()
        .timeout
        .new_observer();

    #[derive(Debug)]
    enum Action {
        OnTimeout,
        UpdateTimeout(Option<Option<Instant>>),
    }

    let mut current_timeout = None;
    loop {
        log::trace!("timeout: {:?}", current_timeout);
        let action = select! {
            _ = maybe_wait_until(current_timeout).fuse() => Action::OnTimeout,
            x = observer.next().fuse() => Action::UpdateTimeout(x),
        };
        log::trace!("action: {:?}", action);
        match action {
            Action::OnTimeout => {
                let link = Weak::upgrade(&link)
                    .ok_or_else(|| format_err!("Link disappeared before timeout expired"))?;
                let mut q = link.quic.borrow_mut();
                q.connection.on_timeout();
                q.update_timeout();
            }
            Action::UpdateTimeout(None) => return Ok(()),
            Action::UpdateTimeout(Some(timeout)) => current_timeout = timeout,
        }
    }
}
