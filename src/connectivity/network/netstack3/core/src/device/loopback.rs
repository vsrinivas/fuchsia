// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The loopback device.

use alloc::vec::Vec;
use core::{
    convert::Infallible as Never,
    fmt::{self, Debug, Display, Formatter},
    hash::{Hash, Hasher},
};

use derivative::Derivative;
use net_types::{
    ip::{Ip as _, IpAddress, IpVersion, Ipv4, Ipv6},
    SpecifiedAddr,
};
use packet::{Buf, BufferMut, SerializeError, Serializer};

use crate::{
    device::{
        queue::{
            BufferReceiveQueueHandler, ReceiveDequeContext, ReceiveDequeueState, ReceiveQueue,
            ReceiveQueueContext, ReceiveQueueFullError, ReceiveQueueNonSyncContext,
            ReceiveQueueState,
        },
        state::IpLinkDeviceState,
        with_loopback_state, Device, DeviceIdContext, DeviceLayerEventDispatcher, FrameDestination,
    },
    sync::WeakReferenceCounted,
    DeviceId, Instant, NonSyncContext, SyncCtx,
};

#[derive(Derivative)]
#[derivative(Clone(bound = ""))]
pub(crate) struct LoopbackDeviceId<I: Instant>(
    pub(super) WeakReferenceCounted<IpLinkDeviceState<I, LoopbackDeviceState>>,
);

impl<I: Instant> PartialEq for LoopbackDeviceId<I> {
    fn eq(&self, LoopbackDeviceId(other): &LoopbackDeviceId<I>) -> bool {
        let LoopbackDeviceId(me) = self;
        me.ptr_eq(other)
    }
}

impl<I: Instant> Eq for LoopbackDeviceId<I> {}

impl<I: Instant> Hash for LoopbackDeviceId<I> {
    fn hash<H: Hasher>(&self, state: &mut H) {
        let LoopbackDeviceId(me) = self;
        me.as_ptr().hash(state)
    }
}

impl<I: Instant> Debug for LoopbackDeviceId<I> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        let device: DeviceId<I> = self.clone().into();
        write!(f, "{:?}", device)
    }
}

impl<I: Instant> Display for LoopbackDeviceId<I> {
    fn fmt(&self, f: &mut Formatter<'_>) -> fmt::Result {
        let device: DeviceId<I> = self.clone().into();
        write!(f, "{}", device)
    }
}

#[derive(Copy, Clone)]
pub(super) enum LoopbackDevice {}

impl Device for LoopbackDevice {}

impl<NonSyncCtx: NonSyncContext> DeviceIdContext<LoopbackDevice> for &'_ SyncCtx<NonSyncCtx> {
    type DeviceId = LoopbackDeviceId<NonSyncCtx::Instant>;
}

pub(super) struct LoopbackDeviceState {
    mtu: u32,
    rx_queue: ReceiveQueue<IpVersion, Buf<Vec<u8>>>,
}

impl LoopbackDeviceState {
    pub(super) fn new(mtu: u32) -> LoopbackDeviceState {
        LoopbackDeviceState { mtu, rx_queue: Default::default() }
    }
}

pub(super) fn send_ip_frame<
    B: BufferMut,
    NonSyncCtx: NonSyncContext,
    A: IpAddress,
    S: Serializer<Buffer = B>,
>(
    mut sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    device_id: &LoopbackDeviceId<NonSyncCtx::Instant>,
    _local_addr: SpecifiedAddr<A>,
    body: S,
) -> Result<(), S> {
    let frame = body
        .serialize_vec_outer()
        .map_err(|(_err, s): (SerializeError<Never>, _)| s)?
        .map_a(|b| Buf::new(b.as_ref().to_vec(), ..))
        .into_inner();

    // Never handle packets synchronously with the send path - always queue
    // the packet to be received by the loopback device into a queue which
    // a dedicated RX task will kick to handle the queued packet.
    //
    // This is done so that a socket lock may be held while sending a packet
    // which may need to be delivered to the sending socket itself. Without
    // this decoupling of RX/TX paths, sending a packet while holding onto
    // the socket lock will result in a deadlock.
    match BufferReceiveQueueHandler::queue_rx_packet(
        &mut sync_ctx,
        ctx,
        device_id,
        A::Version::VERSION,
        frame,
    ) {
        Ok(()) => {}
        Err(ReceiveQueueFullError((_ip_version, _frame))) => {
            // RX queue is full - there is nothing further we can do here.
            log::error!("dropped RX frame on loopback device due to full RX queue")
        }
    }

    Ok(())
}

/// Gets the MTU associated with this device.
pub(super) fn get_mtu<NonSyncCtx: NonSyncContext>(
    ctx: &SyncCtx<NonSyncCtx>,
    device_id: &LoopbackDeviceId<NonSyncCtx::Instant>,
) -> u32 {
    with_loopback_state(ctx, device_id, |state| state.link.mtu)
}

impl<C: NonSyncContext> ReceiveQueueNonSyncContext<LoopbackDevice, LoopbackDeviceId<C::Instant>>
    for C
{
    fn wake_rx_task(&mut self, device_id: LoopbackDeviceId<C::Instant>) {
        DeviceLayerEventDispatcher::wake_rx_task(self, &device_id.into())
    }
}

impl<C: NonSyncContext> ReceiveQueueContext<LoopbackDevice, C> for &'_ SyncCtx<C> {
    type Meta = IpVersion;
    type Buffer = Buf<Vec<u8>>;

    fn with_receive_queue_mut<
        O,
        F: FnOnce(&mut ReceiveQueueState<Self::Meta, Self::Buffer>) -> O,
    >(
        &mut self,
        device_id: &LoopbackDeviceId<C::Instant>,
        cb: F,
    ) -> O {
        with_loopback_state(self, device_id, |state| {
            let x = cb(&mut state.link.rx_queue.queue.lock());
            x
        })
    }

    fn handle_packet(
        &mut self,
        ctx: &mut C,
        device_id: &LoopbackDeviceId<C::Instant>,
        meta: IpVersion,
        buf: Buf<Vec<u8>>,
    ) {
        match meta {
            IpVersion::V4 => crate::ip::receive_ip_packet::<_, _, Ipv4>(
                self,
                ctx,
                &device_id.clone().into(),
                FrameDestination::Unicast,
                buf,
            ),
            IpVersion::V6 => crate::ip::receive_ip_packet::<_, _, Ipv6>(
                self,
                ctx,
                &device_id.clone().into(),
                FrameDestination::Unicast,
                buf,
            ),
        }
    }
}

impl<C: NonSyncContext> ReceiveDequeContext<LoopbackDevice, C> for &'_ SyncCtx<C> {
    type ReceiveQueueCtx = Self;

    fn with_dequed_packets_and_rx_queue_ctx<
        O,
        F: FnOnce(&mut ReceiveDequeueState<IpVersion, Buf<Vec<u8>>>, &mut Self::ReceiveQueueCtx) -> O,
    >(
        &mut self,
        device_id: &LoopbackDeviceId<C::Instant>,
        cb: F,
    ) -> O {
        with_loopback_state(self, device_id, |state| {
            let x = cb(&mut state.link.rx_queue.deque.lock(), self);
            x
        })
    }
}

#[cfg(test)]
mod tests {
    use alloc::vec::Vec;

    use net_types::{
        ip::{AddrSubnet, Ipv4, Ipv6},
        SpecifiedAddr,
    };

    use crate::{
        device::DeviceId,
        error::NotFoundError,
        ip::device::state::{AssignedAddress, IpDeviceStateIpExt},
        testutil::{FakeEventDispatcherConfig, TestIpExt},
        Ctx, NonSyncContext, SyncCtx,
    };

    #[test]
    fn test_loopback_methods() {
        const MTU: u32 = 66;
        let Ctx { sync_ctx, mut non_sync_ctx } = crate::testutil::FakeCtx::default();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_loopback_device(&mut sync_ctx, &mut non_sync_ctx, MTU)
            .expect("error adding loopback device");
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, &device);

        assert_eq!(crate::ip::IpDeviceContext::<Ipv4, _>::get_mtu(&sync_ctx, &device), MTU);
        assert_eq!(crate::ip::IpDeviceContext::<Ipv6, _>::get_mtu(&sync_ctx, &device), MTU);

        fn test<I: TestIpExt + IpDeviceStateIpExt, NonSyncCtx: NonSyncContext>(
            sync_ctx: &mut &SyncCtx<NonSyncCtx>,
            ctx: &mut NonSyncCtx,
            device: &DeviceId<NonSyncCtx::Instant>,
            get_addrs: fn(
                &mut &SyncCtx<NonSyncCtx>,
                &DeviceId<NonSyncCtx::Instant>,
            ) -> Vec<SpecifiedAddr<I::Addr>>,
        ) {
            assert_eq!(get_addrs(sync_ctx, device), []);

            let FakeEventDispatcherConfig {
                subnet,
                local_ip,
                local_mac: _,
                remote_ip: _,
                remote_mac: _,
            } = I::FAKE_CONFIG;
            let addr = AddrSubnet::from_witness(local_ip, subnet.prefix())
                .expect("error creating AddrSubnet");
            assert_eq!(crate::device::add_ip_addr_subnet(sync_ctx, ctx, device, addr), Ok(()));
            let addr = addr.addr();
            assert_eq!(&get_addrs(sync_ctx, device)[..], [addr]);

            assert_eq!(crate::device::del_ip_addr(sync_ctx, ctx, device, &addr), Ok(()));
            assert_eq!(get_addrs(sync_ctx, device), []);

            assert_eq!(
                crate::device::del_ip_addr(sync_ctx, ctx, device, &addr),
                Err(NotFoundError)
            );
        }

        test::<Ipv4, _>(&mut sync_ctx, &mut non_sync_ctx, &device, |sync_ctx, device| {
            crate::ip::device::IpDeviceContext::<Ipv4, _>::with_ip_device_state(
                sync_ctx,
                device,
                |state| state.ip_state.iter_addrs().map(AssignedAddress::addr).collect::<Vec<_>>(),
            )
        });
        test::<Ipv6, _>(&mut sync_ctx, &mut non_sync_ctx, &device, |sync_ctx, device| {
            crate::ip::device::IpDeviceContext::<Ipv6, _>::with_ip_device_state(
                sync_ctx,
                device,
                |state| state.ip_state.iter_addrs().map(AssignedAddress::addr).collect::<Vec<_>>(),
            )
        });
    }
}
