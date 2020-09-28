// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Error};
use fuchsia_zircon as zx;
use std::fmt;
use std::sync::{Mutex, PoisonError};
use std::vec::Vec;

/// `Stats` centralizes all statistical data for transmit of ICMP echo requests and receival of
/// ICMP echo replies. Print a `Stats` object to see all available statistics.
pub struct Stats {
    tx: Mutex<TransmitStats>,
    rx: Mutex<ReceiveStats>,
}

struct TransmitStats {
    requests_sent: u64,
}

struct ReceiveStats {
    replies_received: u64,
    latencies: Vec<zx::Duration>,
}

impl Stats {
    /// Create new running statistics for transmit and receival of ICMP echo messages.
    pub fn new() -> Self {
        Stats {
            tx: Mutex::new(TransmitStats { requests_sent: 0 }),
            rx: Mutex::new(ReceiveStats { replies_received: 0, latencies: Vec::new() }),
        }
    }

    /// Increment the request count. Returns the new count.
    pub fn inc_request_count(&self) -> Result<u64, Error> {
        let mut tx = self
            .tx
            .lock()
            .map_err(|e: PoisonError<_>| format_err!("Transmit stats has been poisoned: {}", e))?;
        tx.requests_sent += 1;
        Ok(tx.requests_sent)
    }

    /// Increment the reply count. Returns the new count.
    pub fn inc_reply_count(&self, latency: Option<zx::Duration>) -> Result<u64, Error> {
        let mut rx = self
            .rx
            .lock()
            .map_err(|e: PoisonError<_>| format_err!("Receive stats has been poisoned: {}", e))?;
        rx.replies_received += 1;
        if let Some(l) = latency {
            rx.latencies.push(l);
        }
        Ok(rx.replies_received)
    }

    /// Check if any replies have been received.
    pub fn has_received_replies(&self) -> Result<bool, Error> {
        let rx = self
            .rx
            .lock()
            .map_err(|e: PoisonError<_>| format_err!("Receive stats has been poisoned: {}", e))?;
        Ok(rx.replies_received != 0)
    }
}

impl fmt::Display for Stats {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        let requests = self.tx.lock().map_err(|_| fmt::Error)?.requests_sent;
        let rx = &self.rx.lock().map_err(|_| fmt::Error)?;
        let replies = rx.replies_received;

        let loss = if requests != 0 {
            ((requests - replies) as f64) / (requests as f64) * 100.0
        } else {
            0.0
        };

        writeln!(
            f,
            "{} packets transmitted, {} received, {:.0}% packet loss",
            requests, replies, loss
        )?;

        if replies as usize != rx.latencies.len() {
            // Not enough space in the ICMP payload for a timestamp
            return Ok(());
        }

        let sum: i64 = rx.latencies.iter().map(|d| d.into_micros()).sum();
        let avg: f64 = sum as f64 / replies as f64;
        let min: Option<i64> = rx.latencies.iter().map(|d| d.into_micros()).min();
        let max: Option<i64> = rx.latencies.iter().map(|d| d.into_micros()).max();

        match (min, max) {
            (Some(min), Some(max)) => write!(
                f,
                "rtt min/avg/max = {:.3}/{:.3}/{:.3} ms",
                min as f64 / 1000.0,
                avg as f64 / 1000.0,
                max as f64 / 1000.0
            ),
            _ => write!(f, ""),
        }
    }
}
