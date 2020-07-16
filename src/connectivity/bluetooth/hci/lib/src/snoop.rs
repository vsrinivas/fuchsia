// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::log::*;
use fuchsia_async as fasync;

#[cfg_attr(test, derive(Debug))]
pub struct SnoopFlags(u8);

#[cfg(test)]
impl PartialEq<u8> for SnoopFlags {
    fn eq(&self, other: &u8) -> bool {
        self.0 == *other
    }
}

/// Internal const fn used to generate the flags value that is prepended to snoop packets.
const fn flags(hci_type: u8, is_received: bool) -> SnoopFlags {
    // Host -> Controller bitflag index
    const SNOOP_FLAG_RECV_BIT_IDX: usize = 2;

    SnoopFlags(hci_type | ((is_received as u8) << SNOOP_FLAG_RECV_BIT_IDX))
}

// TODO: Add buffering to store packets until there is a snoop channel to send them to.
#[derive(Default)]
pub(crate) struct Snoop {
    pub channel: Option<fasync::Channel>,
}

impl Snoop {
    // These constant values are defined as C preprocessor macros in
    // //zircon/system/public/zircon/device/bt-hci.h.
    // TODO (fxbug.dev/49211): Define constants in a single place that can be used across the stack.
    pub const OUTGOING_CMD: SnoopFlags = flags(0, false);
    pub const INCOMING_EVT: SnoopFlags = flags(1, true);
    pub const OUTGOING_ACL: SnoopFlags = flags(2, false);
    pub const INCOMING_ACL: SnoopFlags = flags(2, true);
    #[allow(unused)] // unused until SCO is supported
    pub const OUTGOING_SCO: SnoopFlags = flags(3, false);
    #[allow(unused)] // unused until SCO is supported
    pub const INCOMING_SCO: SnoopFlags = flags(3, true);

    /// Write to the snoop log if it exists.
    pub fn write(&mut self, flags: SnoopFlags, buffer: &[u8]) {
        if let Some(chan) = &mut self.channel {
            let mut snoop_packet = Vec::with_capacity(buffer.len() + 1);
            snoop_packet.push(flags.0);
            snoop_packet.extend_from_slice(buffer);
            if let Some(e) = chan.write(&snoop_packet[..], &mut Vec::new()).err() {
                bt_log_warn!("failed to write to snoop channel {} -- closing", e);
                self.channel = None;
            }
        }
    }

    /// Check if the channel is currently bound and open.
    pub fn is_bound(&self) -> bool {
        self.channel.as_ref().map(|c| !c.is_closed()).unwrap_or(false)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use fuchsia_zircon::{self as zx, DurationNum};

    #[test]
    fn snoop_flags_output() {
        assert_eq!(Snoop::OUTGOING_CMD, 0b000);
        assert_eq!(Snoop::OUTGOING_ACL, 0b010);
        assert_eq!(Snoop::OUTGOING_SCO, 0b011);
        assert_eq!(Snoop::INCOMING_EVT, 0b101);
        assert_eq!(Snoop::INCOMING_ACL, 0b110);
        assert_eq!(Snoop::INCOMING_SCO, 0b111);
    }

    #[fasync::run_until_stalled(test)]
    async fn write_to_snoop_success() {
        let (tx, rx) = zx::Channel::create().unwrap();
        let (tx, rx) = (
            fasync::Channel::from_channel(tx).unwrap(),
            fasync::Channel::from_channel(rx).unwrap(),
        );
        let mut snoop = Snoop { channel: Some(tx) };

        // send a packet with some flags
        let packet = &[1, 2, 3];
        snoop.write(Snoop::INCOMING_EVT, packet);

        // receive that packet on the other end with the flags prepended
        let mut buf = zx::MessageBuf::new();
        rx.recv_msg(&mut buf).await.unwrap();
        assert_eq!(Snoop::INCOMING_EVT, buf.bytes()[0]);
        assert_eq!(&buf.bytes()[1..], packet);
    }

    #[fasync::run_until_stalled(test)]
    async fn write_to_snoop_failure() {
        let (tx, rx) = zx::Channel::create().unwrap();
        let tx = fasync::Channel::from_channel(tx).unwrap();
        let rx = fasync::Channel::from_channel(rx).unwrap();

        let mut snoop = Snoop { channel: Some(tx) };

        // close receiving end of the channel
        drop(rx);

        // send a packet with some
        let packet = &[1, 2, 3];
        snoop.write(Snoop::INCOMING_EVT, packet);

        // channel removed from snoop on failure to send
        assert!(snoop.channel.is_none());
    }

    #[fasync::run_until_stalled(test)]
    async fn write_to_snoop_no_channel() {
        let mut snoop = Snoop { channel: None };
        let packet = &[1, 2, 3];
        snoop.write(Snoop::INCOMING_EVT, packet);
        assert!(snoop.channel.is_none());
    }

    #[fasync::run_until_stalled(test)]
    async fn channel_is_bound() {
        let mut snoop = Snoop { channel: None };
        assert!(!snoop.is_bound());

        let (tx, _rx) = zx::Channel::create().unwrap();
        let tx = fasync::Channel::from_channel(tx).unwrap();

        snoop.channel = Some(tx);
        assert!(snoop.is_bound());
    }

    // intentionally run_singlethreaded to allow a 1 nanosecond
    // sleep in the body of the function without stalling.
    #[fasync::run_singlethreaded(test)]
    async fn handle_snoop_channel_close() {
        let (tx, rx) = zx::Channel::create().unwrap();
        let tx = fasync::Channel::from_channel(tx).unwrap();

        let snoop = Snoop { channel: Some(tx) };
        assert!(snoop.is_bound());

        drop(rx);

        // Sleep to allow is_closed notification to propagate to
        // the the tx end of the channel.
        fasync::Timer::new(fasync::Time::after(1.nanos())).await;

        assert!(!snoop.is_bound());
    }
}
