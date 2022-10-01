// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! FiFo device queue.

use alloc::collections::VecDeque;

use derivative::Derivative;
use packet::ParseBuffer;

use crate::device::queue::{
    DequeuedRxQueueResult, EnqueuedRxQueueResult, ReceiveQueueFullError, MAX_RX_QUEUED_PACKETS,
};

/// A FiFo (First In, First Out) queue of packets.
///
/// If the queue is full, no new packets will be accepted.
#[derive(Derivative)]
#[derivative(Default(bound = ""))]
#[cfg_attr(test, derive(Debug, PartialEq, Eq))]
pub(super) struct Fifo<Meta, Buffer> {
    packets: VecDeque<(Meta, Buffer)>,
}

impl<Meta, Buffer> Fifo<Meta, Buffer> {
    /// Dequeues packets from this queue and pushes them to the back of the
    /// sink.
    ///
    /// Note that this method takes an explicit `max_batch_size` argument
    /// because the `VecDeque`'s capacity (via `VecDequeue::capacity`) may be
    /// larger than some specified maximum batch size. Note that
    /// [`VecDeque::with_capcity`] may allocate more capacity than specified.
    pub(super) fn dequeue_packets_into(
        &mut self,
        sink: &mut VecDeque<(Meta, Buffer)>,
        max_batch_size: usize,
    ) -> DequeuedRxQueueResult {
        for _ in 0..max_batch_size {
            match self.packets.pop_front() {
                Some(p) => sink.push_back(p),
                // No more packets.
                None => break,
            }
        }

        if self.packets.is_empty() {
            DequeuedRxQueueResult::NoMorePacketsLeft
        } else {
            DequeuedRxQueueResult::MorePacketsStillQueued
        }
    }
}

impl<Meta, Buffer: ParseBuffer> Fifo<Meta, Buffer> {
    /// Attempts to add the RX packet to the queue.
    pub(super) fn queue_rx_packet(
        &mut self,
        meta: Meta,
        body: Buffer,
    ) -> Result<EnqueuedRxQueueResult, ReceiveQueueFullError<(Meta, Buffer)>> {
        let Self { packets } = self;

        let len = packets.len();
        if len == MAX_RX_QUEUED_PACKETS {
            return Err(ReceiveQueueFullError((meta, body)));
        }

        packets.push_back((meta, body));

        Ok(if len == 0 {
            EnqueuedRxQueueResult::QueueWasPreviouslyEmpty
        } else {
            EnqueuedRxQueueResult::QueuePreviouslyHadPackets
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use packet::Buf;

    #[test]
    fn max_mackets() {
        let mut fifo = Fifo::default();

        let mut res = Ok(EnqueuedRxQueueResult::QueueWasPreviouslyEmpty);
        for i in 0..MAX_RX_QUEUED_PACKETS {
            let body = Buf::new([i as u8], ..);
            assert_eq!(fifo.queue_rx_packet((), body), res);

            // The result we expect after the first packet is enqueued.
            res = Ok(EnqueuedRxQueueResult::QueuePreviouslyHadPackets);
        }

        let packets = (0..MAX_RX_QUEUED_PACKETS)
            .into_iter()
            .map(|i| ((), Buf::new([i as u8], ..)))
            .collect::<VecDeque<_>>();
        assert_eq!(fifo.packets, packets);

        let body = Buf::new([131], ..);
        assert_eq!(fifo.queue_rx_packet((), body.clone()), Err(ReceiveQueueFullError(((), body))));
        assert_eq!(fifo.packets, packets);
    }
}
