// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{anyhow, Context as _};
use async_trait::async_trait;
use fuchsia_async::{self as fasync, TimeoutExt as _};
use futures::{FutureExt as _, SinkExt as _, TryFutureExt as _, TryStreamExt as _};
use std::net::SocketAddr;

const PING_MESSAGE: &str = "Hello from reachability monitor!";
const SEQ_MIN: u16 = 1;
const SEQ_MAX: u16 = 3;
const TIMEOUT: fasync::Duration = fasync::Duration::from_seconds(1);

async fn ping<I>(interface_name: &str, addr: I::Addr) -> anyhow::Result<()>
where
    I: ping::Ip,
    I::Addr: std::fmt::Display + Copy,
{
    let socket = ping::new_icmp_socket::<I>().context("failed to create socket")?;
    let () = socket
        .bind_device(Some(interface_name.as_bytes()))
        .with_context(|| format!("failed to bind socket to device {}", interface_name))?;
    let (mut sink, mut stream) = ping::new_unicast_sink_and_stream::<
        I,
        _,
        { PING_MESSAGE.len() + ping::ICMP_HEADER_LEN },
    >(&socket, &addr, PING_MESSAGE.as_bytes());

    for seq in SEQ_MIN..=SEQ_MAX {
        let deadline = fasync::Time::after(TIMEOUT);
        let () = sink
            .send(seq)
            .map_err(anyhow::Error::new)
            .on_timeout(deadline, || Err(anyhow!("timed out")))
            .await
            .with_context(|| format!("failed to send ping (seq={})", seq))?;
        if match stream.try_next().map(Some).on_timeout(deadline, || None).await {
            None => Ok(false),
            Some(Err(e)) => Err(anyhow!("failed to receive ping: {}", e)),
            Some(Ok(None)) => Err(anyhow!("ping reply stream ended unexpectedly")),
            Some(Ok(Some(got))) if got >= SEQ_MIN && got <= seq => Ok(true),
            Some(Ok(Some(got))) => Err(anyhow!(
                "received unexpected ping sequence number; got: {}, want: {}..={}",
                got,
                SEQ_MIN,
                seq,
            )),
        }? {
            return Ok(());
        }
    }
    Err(anyhow!("no ping reply received"))
}

/// Trait that can send ICMP echo requests, and receive and validate replies.
#[async_trait]
pub trait Ping {
    /// Returns true if the address is reachable, false otherwise.
    async fn ping(&self, interface_name: &str, addr: SocketAddr) -> bool;
}

pub struct Pinger;

#[async_trait]
impl Ping for Pinger {
    async fn ping(&self, interface_name: &str, addr: SocketAddr) -> bool {
        let r = match addr {
            SocketAddr::V4(addr_v4) => ping::<ping::Ipv4>(interface_name, addr_v4).await,
            SocketAddr::V6(addr_v6) => ping::<ping::Ipv6>(interface_name, addr_v6).await,
        };
        match r {
            Ok(()) => true,
            Err(e) => {
                log::warn!("error while pinging {}: {}", addr, e);
                false
            }
        }
    }
}
