// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod opt;
mod stats;
mod store;
#[cfg(test)]
mod tests;

use anyhow::{format_err, Error};
use futures::future::try_join;
use futures::prelude::*;
use log::{debug, error, info, Level, LevelFilter, Metadata, Record};
use std::sync::{Arc, Mutex};
use structopt::StructOpt;
use zerocopy::{AsBytes, LayoutVerified};

use fuchsia_async as fasync;
use fuchsia_async::TimeoutExt;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;

use fidl::endpoints::create_endpoints;
use fidl_fuchsia_net_ext::IpAddress;
use fidl_fuchsia_net_icmp::{
    EchoPacket, EchoSocketConfig, EchoSocketEvent, EchoSocketMarker, EchoSocketProxy,
    ProviderMarker,
};

use crate::opt::Opt;
use crate::stats::Stats;
use crate::store::SequenceStore;

struct ConsoleLogger {
    level: Level,
}

impl ConsoleLogger {
    fn new() -> ConsoleLogger {
        let Opt { verbose, .. } = Opt::from_args();
        let level = if verbose { Level::Debug } else { Level::Info };
        ConsoleLogger { level }
    }
}

impl log::Log for ConsoleLogger {
    fn enabled(&self, metadata: &Metadata<'_>) -> bool {
        metadata.level() <= self.level
    }

    fn log(&self, record: &Record) {
        if self.enabled(record.metadata()) {
            println!("{}", record.args());
        }
    }

    fn flush(&self) {}
}

#[fasync::run_singlethreaded]
async fn main() {
    log::set_boxed_logger(Box::new(ConsoleLogger::new()))
        .map(|_| log::set_max_level(LevelFilter::Debug))
        .expect("Failed to initialize logger");

    ::std::process::exit(match run_app().await {
        Ok(_) => ExitReason::Reachable.into(),
        Err(e) => {
            error!("{}", e.err);
            e.code.into()
        }
    });
}

/// Reason for the application exiting.
enum ExitReason {
    /// Host is reachabile
    Reachable,
    /// Host is unreachable
    Unreachable,
    /// Error occured which does not infer reachability to the host
    ReachabilityUnknown,
}

impl Into<i32> for ExitReason {
    fn into(self) -> i32 {
        match self {
            Self::Reachable => 0,
            Self::Unreachable => 1,
            Self::ReachabilityUnknown => 2,
        }
    }
}

/// An Error with an exit code.
struct ErrorWithCode {
    /// The original error returned by the application, kept for debugging.
    err: Error,

    /// Exit code of the application.
    code: ExitReason,
}

/// Run the ping application.
async fn run_app() -> Result<(), ErrorWithCode> {
    let config =
        get_config().map_err(|err| ErrorWithCode { err, code: ExitReason::ReachabilityUnknown })?;
    let socket = open_socket(config)
        .await
        .map_err(|err| ErrorWithCode { err, code: ExitReason::ReachabilityUnknown })?;
    let store = Arc::new(Mutex::new(SequenceStore::new()));
    let stats = Arc::new(Stats::new());

    let Opt { remote_addr, packet_size, count, deadline, .. } = Opt::from_args();

    let fut = try_join(
        send_requests(socket.clone(), store.clone(), Arc::clone(&stats), packet_size),
        watch_replies(socket, store, Arc::clone(&stats), packet_size),
    )
    .map_ok(|_| ())
    .map_err(|err| ErrorWithCode { err, code: ExitReason::ReachabilityUnknown });

    let res = match (count, deadline) {
        (Some(_), Some(deadline)) => {
            fut.on_timeout(fasync::Time::after(zx::Duration::from_seconds(deadline)), || {
                return Err(ErrorWithCode {
                    err: format_err!("Timeout after {} seconds", deadline),
                    code: ExitReason::Unreachable,
                });
            })
            .await
        }
        (None, Some(deadline)) => {
            fut.on_timeout(fasync::Time::after(zx::Duration::from_seconds(deadline)), || Ok(()))
                .await
        }
        _ => fut.await,
    };

    let _ = res?;
    let ret = stats
        .has_received_replies()
        .map(|success| {
            if success {
                Ok(())
            } else {
                Err(ErrorWithCode {
                    err: format_err!("Did not receive any ICMP echo replies"),
                    code: ExitReason::Unreachable,
                })
            }
        })
        .map_err(|err| ErrorWithCode { err, code: ExitReason::ReachabilityUnknown })?;

    info!("");
    info!("--- {} ping statistics ---", remote_addr);
    info!("{}", stats);
    ret
}

/// Parse the configuration for an ICMP echo socket using the command-line arguments passed in.
fn get_config() -> Result<EchoSocketConfig, Error> {
    let Opt { local_addr, remote_addr, packet_size, count, deadline, interval, .. } =
        Opt::from_args();

    let local = match local_addr {
        Some(addr) => Some(
            IpAddress(
                addr.parse().map_err(|e| format_err!("Failed to parse local address: {}", e))?,
            )
            .into(),
        ),
        None => None,
    };

    let remote = IpAddress(
        remote_addr.parse().map_err(|e| format_err!("Failed to parse remote address: {}", e))?,
    );

    if interval < 200 {
        return Err(format_err!("Cannot flood; minimum interval allowed is 200ms."));
    }

    if let Some(l) = local {
        debug!("Using local address {:?}", l);
    }
    if let Some(d) = deadline {
        if d == 0 {
            return Err(format_err!(
                "Bad deadline for packets to transmit; deadline cannot be zero."
            ));
        }
        debug!("Timeout set to {} seconds", d);
    }
    if let Some(c) = count {
        if c == 0 {
            return Err(format_err!("Bad number of packets to transmit; count cannot be zero."));
        }
        debug!("Will send {} ICMP echo requests", c);
    }
    debug!("Sending ICMP echo requests every {} milliseconds...", interval);

    info!("PING {} ({}) {}({}) bytes of data.", remote, remote, packet_size, packet_size + 28);
    Ok(EchoSocketConfig { local, remote: Some(remote.into()) })
}

/// Open an ICMP echo socket.
async fn open_socket(config: EchoSocketConfig) -> Result<EchoSocketProxy, Error> {
    let provider = connect_to_service::<ProviderMarker>()
        .map_err(|e| format_err!("Failed to connect to the ICMP Provider service: {}", e))?;

    debug!("Connected to fuchsia.net.icmp.Provider service");

    let (socket_client, socket_server) = create_endpoints::<EchoSocketMarker>()
        .map_err(|e| format_err!("Failed to create channel for ICMP echo socket: {}", e))?;
    let socket = socket_client
        .into_proxy()
        .map_err(|e| format_err!("Failed to create proxy to ICMP echo socket: {}", e))?;

    debug!("Opening ICMP echo socket...");

    provider
        .open_echo_socket(config, socket_server)
        .map_err(|e| format_err!("Failed to open ICMP echo socket: {}", e))?;

    // Wait until an OnOpen event is received before doing anything with the socket. This is done
    // to circumvent inaccurate first packet latency due to the kernel queuing FIDL requests before
    // the socket is ready to accept requests.
    let mut event_stream = socket.take_event_stream();
    while let Some(evt) = event_stream.next().await {
        match evt {
            Ok(EchoSocketEvent::OnOpen_ { s }) => {
                let status = zx::Status::from_raw(s);
                match status {
                    zx::Status::OK => debug!("ICMP echo socket successfully opened"),
                    zx::Status::INVALID_ARGS => {
                        return Err(format_err!("Passed invalid arguments to ICMP echo socket"))
                    }
                    _ => {
                        return Err(format_err!(
                            "Received unknown status code from `EchoSocket.OnOpen`: {}",
                            status
                        ))
                    }
                }
                break;
            }
            Err(e) => return Err(format_err!("Socket error: {:?}", e)),
        }
    }

    Ok(socket)
}

/// Send ICMP echo requests.
async fn send_requests(
    socket: EchoSocketProxy,
    store: Arc<Mutex<SequenceStore>>,
    stats: Arc<Stats>,
    packet_size: u16,
) -> Result<(), Error> {
    let Opt { interval, count, .. } = Opt::from_args();

    let mut interval = fasync::Interval::new(zx::Duration::from_millis(interval));

    while let Some(_) = interval.next().await {
        let (num, time) = store
            .lock()
            .map_err(|e| format_err!("Sequence store mutex is poisoned: {}", e))?
            .take()?;

        debug!("Sending ICMP echo request w/ icmp_seq={}", num);

        let payload = if packet_size < 8 {
            vec![0u8; packet_size as usize]
        } else {
            let padding = vec![0u8; (packet_size - 8) as usize];
            [time.into_nanos().as_bytes(), &padding].concat()
        };

        socket
            .send_request(&mut EchoPacket { sequence_num: num, payload })
            .map_err(|e| format_err!("Failed to send ICMP echo request: {}", e))?;

        let requests = stats.inc_request_count()?;
        if count.map_or(false, |c| c == requests) {
            break;
        }
    }

    Ok(())
}

/// Watch for ICMP echo replies.
async fn watch_replies(
    socket: EchoSocketProxy,
    store: Arc<Mutex<SequenceStore>>,
    stats: Arc<Stats>,
    packet_size: u16,
) -> Result<(), Error> {
    let Opt { count, remote_addr, .. } = Opt::from_args();
    loop {
        let EchoPacket { payload, sequence_num } = match socket.watch().await {
            Ok(Ok(packet)) => packet,
            Ok(Err(e)) => {
                let status = zx::Status::from_raw(e);
                return Err(format_err!("Error sending ICMP echo request: {}", status));
            }
            Err(e) => return Err(format_err!("FIDL error during watch: {:?}", e)),
        };

        if payload.len() != packet_size as usize {
            return Err(format_err!("Validation error: ICMP payload sizes do not match"));
        }

        let time = if payload.len() >= 8 {
            LayoutVerified::new(payload[..8].as_ref())
                .map(|t: LayoutVerified<_, i64>| zx::Duration::from_nanos(*t))
        } else {
            None
        };

        let duration = store
            .lock()
            .map_err(|e| format_err!("Sequence store mutex is poisoned: {}", e))?
            .give(sequence_num, time);

        let info = format!(
            "{} bytes from {}: icmp_seq={} ttl=64",
            packet_size + 8,
            remote_addr,
            sequence_num
        );

        fn calc_latency(dur: Option<zx::Duration>) -> String {
            dur.map_or("".to_string(), |d| {
                let time = (d.into_nanos() as f64) / 1_000_000.0;
                format!(" time={:.3} ms", time)
            })
        }

        match duration {
            Ok(dur) => {
                info!("{}{}", info, calc_latency(dur));
                let replies = stats.inc_reply_count(dur)?;
                if count.map_or(false, |c| c == replies) {
                    break;
                }
            }
            Err(store::GiveError::Duplicate(duration)) => {
                info!("{}{} [DUPLICATE]", info, calc_latency(duration));
            }
            Err(store::GiveError::OutOfOrder(duration)) => {
                info!("{}{} [OUT OF ORDER]", info, calc_latency(duration));
            }
            Err(store::GiveError::DoesNotExist(duration)) => {
                info!("{}{} [DOES NOT EXIST]", info, calc_latency(duration));
            }
        }
    }
    Ok(())
}
