// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async::{self as fasync, PacketReceiver, ReceiverRegistration},
    fuchsia_zircon::{self as zx},
    futures::{channel::mpsc, Stream, StreamExt, TryStreamExt},
    std::{
        convert::TryInto,
        pin::Pin,
        task::{Context, Poll},
    },
    thiserror::Error,
};

// Virtio 1.0 Section 4.1.4.4: notify_off_multiplier is combined with the
// queue_notify_off to derive the Queue Notify address within a BAR for a
// virtqueue:
//
//      cap.offset + queue_notify_off * notify_off_multiplier
//
// Virtio 1.0 Section 4.1.4.4.1: The device MUST either present
// notify_off_multiplier as an even power of 2, or present
// notify_off_multiplier as 0.
//
// By using a multiplier of 4, we use sequential 4b words to notify, ex:
//
//      cap.offset + 0  -> Notify Queue 0
//      cap.offset + 4  -> Notify Queue 1
//      ...
//      cap.offset + 4n -> Notify Queue n
const QUEUE_NOTIFY_MULTIPLIER: usize = 4;

#[derive(Error, Debug, PartialEq, Eq)]
pub enum BellError {
    #[error("Received unexpected packet {0:?}")]
    UnexpectedPacket(zx::Packet),
    #[error("Trap address {0:?} did not map to a queue")]
    BadAddress(zx::GPAddr),
}

#[derive(Debug, Eq, PartialEq)]
enum Packet {
    Bell(zx::GPAddr),
    Other(zx::Packet),
}

// Forwards incoming port packets into a channel.
#[derive(Debug)]
pub struct PortForwarder {
    channel: mpsc::UnboundedSender<Packet>,
}

impl PacketReceiver for PortForwarder {
    fn receive_packet(&self, packet: zx::Packet) {
        let packet = if let zx::PacketContents::GuestBell(bell) = packet.contents() {
            Packet::Bell(bell.addr())
        } else {
            Packet::Other(packet)
        };
        // An unbounded channel should never be full and this PacketReceiver should have been
        // de-registered if the receiver side of this channel were to have gone away, and so no
        // errors should be possible. Even if an error does occur we have no mechanism to return it,
        // so we just unwrap.
        self.channel.unbounded_send(packet).unwrap();
    }
}

/// Wrapper for receiving bell traps from the guest.
///
/// A bell trap from the guest is a signal that a particular virtqueue needs to be processed. Bell
/// traps are delivered by the kernel as packets on a port. This wrapper registers itself on the
/// current executors port and provides an asynchronous [`Stream`] of queues that have been
/// notified.
#[derive(Debug)]
pub struct GuestBellTrap<T = ReceiverRegistration<PortForwarder>> {
    _registration: T,
    channel: mpsc::UnboundedReceiver<Packet>,
    base: zx::GPAddr,
    num_queues: u16,
}

impl GuestBellTrap {
    /// Construct a [`GuestBellTrap`] for the provided guest range.
    ///
    /// If a device is using bell traps then the trap information is in the [`StartInfo`]
    /// (fidl_fuchsia_virtualization_hardware::StartInfo). A reference to the [`zx::Guest`] is only
    /// needed temporarily to register the trap range.
    ///
    /// Note that traps cannot be unregistered and creating a second [`GuestBellTrap`] for the same
    /// range, even after dropping the first one, will fail.
    pub fn new(guest: &zx::Guest, base: zx::GPAddr, len: usize) -> Result<Self, zx::Status> {
        let (tx, rx) = mpsc::unbounded();
        let registration = fasync::EHandle::local()
            .register_receiver(std::sync::Arc::new(PortForwarder { channel: tx }));
        guest.set_trap_bell(base, len, registration.port(), registration.key())?;
        Self::with_registration(base, len, rx, registration)
    }
}

impl<T> GuestBellTrap<T> {
    fn with_registration(
        base: zx::GPAddr,
        len: usize,
        rx: mpsc::UnboundedReceiver<Packet>,
        registration: T,
    ) -> Result<Self, zx::Status> {
        // Ensure base is aligned to the queue multiplier.
        if (base.0 % QUEUE_NOTIFY_MULTIPLIER) != 0 {
            return Err(zx::Status::INVALID_ARGS);
        }
        let num_queues = (len / QUEUE_NOTIFY_MULTIPLIER) as u16;
        if num_queues as usize * QUEUE_NOTIFY_MULTIPLIER != len {
            return Err(zx::Status::INVALID_ARGS);
        }
        // Needs to be at least one queue.
        if num_queues == 0 {
            return Err(zx::Status::INVALID_ARGS);
        }
        Ok(GuestBellTrap { _registration: registration, channel: rx, base, num_queues })
    }

    /// Convert a guest address to a queue.
    ///
    /// Returns a none if the provided `addr` is not within the trap range. Otherwise if a queue is
    /// returned the caller still needs to validate that it is for a queue that was actually
    /// configured and exists.
    pub fn queue_for_addr(&self, addr: zx::GPAddr) -> Option<u16> {
        let queue =
            ((addr.0.checked_sub(self.base.0)?) / QUEUE_NOTIFY_MULTIPLIER).try_into().ok()?;
        if queue >= self.num_queues {
            None
        } else {
            Some(queue)
        }
    }
}

impl<T: Unpin> GuestBellTrap<T> {
    /// Consume all traps by notifying the provided [`Device`](crate::Device).
    ///
    /// This method will only yield a value if there is an error, either due to the stream ending or
    /// an invalid queue.
    pub async fn complete<'a, N>(
        self,
        device: &crate::Device<'a, N>,
    ) -> Result<(), crate::DeviceError> {
        self.err_into()
            .try_for_each(|queue| futures::future::ready(device.notify_queue(queue as u16)))
            .await
    }

    /// [`complete`] a [`GuestBellTrap`] or block forever.
    ///
    /// Bell traps are not always provided to a device and this provides a unified way of
    /// interacting with them. It will either run [`complete`] on the provided bell trap, or block
    /// permanently. In this way it is similar to [`complete`] in that it only resolves to a value
    /// on error, and the absence of a bell trap is not considered an error.
    pub async fn complete_or_pending<'a, N>(
        maybe_trap: Option<Self>,
        device: &crate::Device<'a, N>,
    ) -> Result<(), crate::DeviceError> {
        match maybe_trap {
            Some(bell) => bell.complete(device).await,
            None => futures::future::pending().await,
        }
    }
}

impl<T: Unpin> Stream for GuestBellTrap<T> {
    type Item = Result<u16, BellError>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.channel.poll_next_unpin(cx).map(|maybe_packet| {
            // We do not expect our channel to get closed and for this to be a None, but if it is
            // there is no choice but to propagate it up to have this stream get closed.
            let packet = maybe_packet?;
            match packet {
                Packet::Bell(addr) => {
                    Some(self.queue_for_addr(addr).ok_or(BellError::BadAddress(addr)))
                }
                Packet::Other(packet) => Some(Err(BellError::UnexpectedPacket(packet))),
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::FutureExt;
    #[test]
    fn trap_size() {
        // Base must be aligned.
        assert_eq!(
            GuestBellTrap::with_registration(zx::GPAddr(3), 4, mpsc::unbounded().1, ()).err(),
            Some(zx::Status::INVALID_ARGS)
        );
        assert_eq!(
            GuestBellTrap::with_registration(zx::GPAddr(1), 4, mpsc::unbounded().1, ()).err(),
            Some(zx::Status::INVALID_ARGS)
        );

        // Length may not be zero.
        assert_eq!(
            GuestBellTrap::with_registration(zx::GPAddr(8), 0, mpsc::unbounded().1, ()).err(),
            Some(zx::Status::INVALID_ARGS)
        );

        // Length must be a multiple.
        assert_eq!(
            GuestBellTrap::with_registration(zx::GPAddr(8), 1, mpsc::unbounded().1, ()).err(),
            Some(zx::Status::INVALID_ARGS)
        );
        assert_eq!(
            GuestBellTrap::with_registration(zx::GPAddr(8), 3, mpsc::unbounded().1, ()).err(),
            Some(zx::Status::INVALID_ARGS)
        );
        assert_eq!(
            GuestBellTrap::with_registration(zx::GPAddr(8), 9, mpsc::unbounded().1, ()).err(),
            Some(zx::Status::INVALID_ARGS)
        );
        assert_eq!(
            GuestBellTrap::with_registration(zx::GPAddr(8), 42, mpsc::unbounded().1, ()).err(),
            Some(zx::Status::INVALID_ARGS)
        );

        assert!(
            GuestBellTrap::with_registration(zx::GPAddr(64), 12, mpsc::unbounded().1, ()).is_ok()
        );
    }

    #[test]
    fn queue_conversion() {
        let bell =
            GuestBellTrap::with_registration(zx::GPAddr(80), 12, mpsc::unbounded().1, ()).unwrap();

        // Too low to be in the range.
        assert_eq!(bell.queue_for_addr(zx::GPAddr(79)), None);
        assert_eq!(bell.queue_for_addr(zx::GPAddr(76)), None);

        // Any access in the range should map to the queue.
        assert_eq!(bell.queue_for_addr(zx::GPAddr(80)), Some(0));
        assert_eq!(bell.queue_for_addr(zx::GPAddr(81)), Some(0));
        assert_eq!(bell.queue_for_addr(zx::GPAddr(83)), Some(0));

        // All queues should map.
        assert_eq!(bell.queue_for_addr(zx::GPAddr(84)), Some(1));
        assert_eq!(bell.queue_for_addr(zx::GPAddr(88)), Some(2));
        assert_eq!(bell.queue_for_addr(zx::GPAddr(91)), Some(2));

        // Too high to be in the range.
        assert_eq!(bell.queue_for_addr(zx::GPAddr(92)), None);
        assert_eq!(bell.queue_for_addr(zx::GPAddr(94)), None);
        assert_eq!(bell.queue_for_addr(zx::GPAddr(128)), None);
    }

    #[fasync::run_until_stalled(test)]
    async fn packet_stream() {
        let (tx, rx) = mpsc::unbounded();

        let bell = GuestBellTrap::with_registration(zx::GPAddr(64), 12, rx, ()).unwrap();

        // Put some valid and invalid packets in.
        tx.unbounded_send(Packet::Bell(zx::GPAddr(64))).unwrap();
        tx.unbounded_send(Packet::Bell(zx::GPAddr(68))).unwrap();
        tx.unbounded_send(Packet::Bell(zx::GPAddr(100))).unwrap();

        let mut stream = bell.peekable();
        // There should be items waiting.
        assert!(Pin::new(&mut stream).peek().now_or_never().is_some());

        // Read off the valid and invalid items.
        assert_eq!(stream.next().await, Some(Ok(0)));
        assert_eq!(stream.next().await, Some(Ok(1)));
        assert_eq!(stream.next().await, Some(Err(BellError::BadAddress(zx::GPAddr(100)))));

        // Should be nothing else waiting.
        assert!(Pin::new(&mut stream).peek().now_or_never().is_none());
    }
}
