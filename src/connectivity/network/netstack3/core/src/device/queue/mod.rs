// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Device queues.

mod fifo;

use alloc::collections::VecDeque;

use derivative::Derivative;
use packet::ParseBuffer;

use crate::{
    device::{Device, DeviceIdContext},
    sync::Mutex,
};

const MAX_RX_QUEUED_PACKETS: usize = 10000;
const MAX_BATCH_SIZE: usize = 100;

#[derive(Debug, PartialEq, Eq)]
pub(crate) struct ReceiveQueueFullError<T>(pub T);

#[derive(Debug, PartialEq, Eq)]
enum EnqueuedRxQueueResult {
    QueueWasPreviouslyEmpty,
    QueuePreviouslyHadPackets,
}

/// The state used to hold a queue of received packets to be handled at a later
/// time.
#[derive(Derivative)]
#[derivative(Default(bound = ""))]
#[cfg_attr(test, derive(Debug, PartialEq, Eq))]
pub(crate) struct ReceiveQueueState<Meta, Buffer> {
    queue: fifo::Fifo<Meta, Buffer>,
}

/// The non-synchonized context for the receive queue.
pub(crate) trait ReceiveQueueNonSyncContext<D: Device, DeviceId> {
    /// Wakes up RX task.
    fn wake_rx_task(&mut self, device_id: DeviceId);
}

/// The state used to dequeue and handle packets from the RX device queue.
pub(crate) struct ReceiveDequeueState<Meta, Buffer> {
    dequed_packets: VecDeque<(Meta, Buffer)>,
}

impl<Meta, Buffer> Default for ReceiveDequeueState<Meta, Buffer> {
    fn default() -> ReceiveDequeueState<Meta, Buffer> {
        ReceiveDequeueState {
            /// Make sure we can dequeue up to `MAX_BATCH_SIZE` packets without
            /// needing to reallocate.
            dequed_packets: VecDeque::with_capacity(MAX_BATCH_SIZE),
        }
    }
}

#[derive(Derivative)]
#[derivative(Default(bound = ""))]
pub(crate) struct ReceiveQueue<Meta, Buffer> {
    /// The state for dequeued packets that will be handled.
    ///
    /// See `queue` for lock ordering.
    pub(crate) deque: Mutex<ReceiveDequeueState<Meta, Buffer>>,
    /// A queue of received packets protected by a lock.
    ///
    /// Lock ordering: `deque` must be locked before `queue` is locked when both
    /// are needed at the same time.
    pub(crate) queue: Mutex<ReceiveQueueState<Meta, Buffer>>,
}

/// The execution context for a receive queue.
pub(crate) trait ReceiveQueueContext<D: Device, C>: DeviceIdContext<D> {
    /// Metadata associated with an RX packet.
    ///
    /// E.g. loopback may hold the packet's IP version in the metadata (since
    /// loopback has no headers that may be used to identify the packet type).
    type Meta;

    /// The type of buffer holding an RX packet.
    type Buffer: ParseBuffer;

    /// Calls the function with the RX queue state.
    fn with_receive_queue_mut<O, F: FnOnce(&mut ReceiveQueueState<Self::Meta, Self::Buffer>) -> O>(
        &mut self,
        device_id: Self::DeviceId,
        cb: F,
    ) -> O;

    /// Handle a received packet.
    fn handle_packet(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        meta: Self::Meta,
        buf: Self::Buffer,
    );
}

pub(crate) trait ReceiveDequeContext<D: Device, C>: ReceiveQueueContext<D, C> {
    type ReceiveQueueCtx: ReceiveQueueContext<
        D,
        C,
        Meta = Self::Meta,
        Buffer = Self::Buffer,
        DeviceId = Self::DeviceId,
    >;

    /// Calls the function with the RX deque state and the RX queue context.
    fn with_dequed_packets_and_rx_queue_ctx<
        O,
        F: FnOnce(&mut ReceiveDequeueState<Self::Meta, Self::Buffer>, &mut Self::ReceiveQueueCtx) -> O,
    >(
        &mut self,
        device_id: Self::DeviceId,
        cb: F,
    ) -> O;
}

#[cfg_attr(test, derive(Debug))]
enum DequeuedRxQueueResult {
    MorePacketsStillQueued,
    NoMorePacketsLeft,
}

/// An implementation of a receive queue.
pub(crate) trait ReceiveQueueHandler<D: Device, C>: DeviceIdContext<D> {
    /// Metadata associated with an RX packet.
    type Meta;

    /// Handle any queued RX packets.
    fn handle_queued_rx_packets(&mut self, ctx: &mut C, device_id: Self::DeviceId);
}

/// An implementation of a receive queue, with a buffer.
pub(crate) trait BufferReceiveQueueHandler<D: Device, B: ParseBuffer, C>:
    ReceiveQueueHandler<D, C>
{
    /// Queues a packet for reception.
    ///
    /// # Errors
    ///
    /// Returns an error with the metadata and body if the queue is full.
    fn queue_rx_packet(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        meta: Self::Meta,
        body: B,
    ) -> Result<(), ReceiveQueueFullError<(Self::Meta, B)>>;
}

impl<D: Device, C: ReceiveQueueNonSyncContext<D, SC::DeviceId>, SC: ReceiveDequeContext<D, C>>
    ReceiveQueueHandler<D, C> for SC
{
    type Meta = SC::Meta;

    fn handle_queued_rx_packets(&mut self, ctx: &mut C, device_id: SC::DeviceId) {
        self.with_dequed_packets_and_rx_queue_ctx(
            device_id,
            |ReceiveDequeueState { dequed_packets }, rx_queue_ctx| {
                assert_eq!(
                    dequed_packets.len(),
                    0,
                    "should not keep dequeued packets across calls to this fn"
                );

                let ret = rx_queue_ctx.with_receive_queue_mut(
                    device_id,
                    |ReceiveQueueState { queue }| {
                        queue.dequeue_packets_into(dequed_packets, MAX_BATCH_SIZE)
                    },
                );

                while let Some((meta, p)) = dequed_packets.pop_front() {
                    rx_queue_ctx.handle_packet(ctx, device_id, meta, p);
                }

                match ret {
                    DequeuedRxQueueResult::MorePacketsStillQueued => ctx.wake_rx_task(device_id),
                    DequeuedRxQueueResult::NoMorePacketsLeft => {
                        // There are no more packets left after the batch we
                        // just handled. When the next RX packet gets enqueued,
                        // the RX task will be woken up again.
                    }
                }
            },
        )
    }
}

impl<D: Device, C: ReceiveQueueNonSyncContext<D, SC::DeviceId>, SC: ReceiveDequeContext<D, C>>
    BufferReceiveQueueHandler<D, SC::Buffer, C> for SC
{
    fn queue_rx_packet(
        &mut self,
        ctx: &mut C,
        device_id: SC::DeviceId,
        meta: SC::Meta,
        body: SC::Buffer,
    ) -> Result<(), ReceiveQueueFullError<(Self::Meta, SC::Buffer)>> {
        self.with_receive_queue_mut(device_id, |ReceiveQueueState { queue }| {
            queue.queue_rx_packet(meta, body).map(|res| match res {
                EnqueuedRxQueueResult::QueueWasPreviouslyEmpty => ctx.wake_rx_task(device_id),
                EnqueuedRxQueueResult::QueuePreviouslyHadPackets => {
                    // We have already woken up the RX task when the queue was
                    // previously empty so there is no need to do it again.
                }
            })
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use alloc::{vec, vec::Vec};

    use packet::Buf;

    use crate::{
        context::testutil::{DummyCtx, DummyNonSyncCtx, DummySyncCtx},
        device::link::testutil::{DummyLinkDevice, DummyLinkDeviceId},
    };

    #[derive(Default)]
    struct MockRxQueueState {
        queue: ReceiveQueueState<(), Buf<Vec<u8>>>,
        handled_packets: Vec<Buf<Vec<u8>>>,
    }

    #[derive(Default)]
    struct MockRxQueueNonSyncCtxState {
        woken_rx_tasks: Vec<DummyLinkDeviceId>,
    }

    type MockSyncCtx = DummySyncCtx<MockRxQueueState, (), DummyLinkDeviceId>;
    type MockNonSyncCtx = DummyNonSyncCtx<(), (), MockRxQueueNonSyncCtxState>;

    impl ReceiveQueueNonSyncContext<DummyLinkDevice, DummyLinkDeviceId> for MockNonSyncCtx {
        fn wake_rx_task(&mut self, device_id: DummyLinkDeviceId) {
            self.state_mut().woken_rx_tasks.push(device_id)
        }
    }

    impl ReceiveQueueContext<DummyLinkDevice, MockNonSyncCtx> for MockSyncCtx {
        type Meta = ();
        type Buffer = Buf<Vec<u8>>;

        fn with_receive_queue_mut<O, F: FnOnce(&mut ReceiveQueueState<(), Buf<Vec<u8>>>) -> O>(
            &mut self,
            DummyLinkDeviceId: DummyLinkDeviceId,
            cb: F,
        ) -> O {
            cb(&mut self.get_mut().queue)
        }

        fn handle_packet(
            &mut self,
            _ctx: &mut MockNonSyncCtx,
            DummyLinkDeviceId: DummyLinkDeviceId,
            (): (),
            buf: Buf<Vec<u8>>,
        ) {
            self.get_mut().handled_packets.push(buf)
        }
    }

    impl ReceiveDequeContext<DummyLinkDevice, MockNonSyncCtx> for MockSyncCtx {
        type ReceiveQueueCtx = Self;

        fn with_dequed_packets_and_rx_queue_ctx<
            O,
            F: FnOnce(
                &mut ReceiveDequeueState<Self::Meta, Self::Buffer>,
                &mut Self::ReceiveQueueCtx,
            ) -> O,
        >(
            &mut self,
            DummyLinkDeviceId: DummyLinkDeviceId,
            cb: F,
        ) -> O {
            cb(&mut ReceiveDequeueState::default(), self)
        }
    }

    #[test]
    fn queue_and_dequeue() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockSyncCtx::default());

        for _ in 0..2 {
            for i in 0..MAX_RX_QUEUED_PACKETS {
                let body = Buf::new(vec![i as u8], ..);
                assert_eq!(
                    BufferReceiveQueueHandler::queue_rx_packet(
                        &mut sync_ctx,
                        &mut non_sync_ctx,
                        DummyLinkDeviceId,
                        (),
                        body
                    ),
                    Ok(())
                );
                // We should only ever be woken up once when the first packet
                // was enqueued.
                assert_eq!(non_sync_ctx.state().woken_rx_tasks, [DummyLinkDeviceId]);
            }

            let body = Buf::new(vec![131], ..);
            assert_eq!(
                BufferReceiveQueueHandler::queue_rx_packet(
                    &mut sync_ctx,
                    &mut non_sync_ctx,
                    DummyLinkDeviceId,
                    (),
                    body.clone(),
                ),
                Err(ReceiveQueueFullError(((), body)))
            );
            // We should only ever be woken up once when the first packet
            // was enqueued.
            assert_eq!(
                core::mem::take(&mut non_sync_ctx.state_mut().woken_rx_tasks),
                [DummyLinkDeviceId]
            );

            assert!(MAX_RX_QUEUED_PACKETS > MAX_BATCH_SIZE);
            for i in (0..(MAX_RX_QUEUED_PACKETS - MAX_BATCH_SIZE)).step_by(MAX_BATCH_SIZE) {
                ReceiveQueueHandler::handle_queued_rx_packets(
                    &mut sync_ctx,
                    &mut non_sync_ctx,
                    DummyLinkDeviceId,
                );
                assert_eq!(
                    core::mem::take(&mut sync_ctx.get_mut().handled_packets),
                    (i..i + MAX_BATCH_SIZE)
                        .into_iter()
                        .map(|i| Buf::new(vec![i as u8], ..))
                        .collect::<Vec<_>>()
                );
                // We should get a wake up signal when packets remain after
                // handling a batch of RX packets.
                assert_eq!(
                    core::mem::take(&mut non_sync_ctx.state_mut().woken_rx_tasks),
                    [DummyLinkDeviceId]
                );
            }

            ReceiveQueueHandler::handle_queued_rx_packets(
                &mut sync_ctx,
                &mut non_sync_ctx,
                DummyLinkDeviceId,
            );
            assert_eq!(
                core::mem::take(&mut sync_ctx.get_mut().handled_packets),
                (MAX_BATCH_SIZE * (MAX_RX_QUEUED_PACKETS / MAX_BATCH_SIZE - 1)
                    ..MAX_RX_QUEUED_PACKETS)
                    .into_iter()
                    .map(|i| Buf::new(vec![i as u8], ..))
                    .collect::<Vec<_>>()
            );
            // Should not have woken up the RX task since the queue should be
            // empty.
            assert_eq!(core::mem::take(&mut non_sync_ctx.state_mut().woken_rx_tasks), []);

            // The queue should now be empty so the next iteration of queueing
            // `MAX_RX_QUEUED_PACKETS` packets should succeed.
        }
    }
}
