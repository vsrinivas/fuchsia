// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The loopback device.

use core::convert::Infallible as Never;

use net_types::{ip::IpAddress, SpecifiedAddr};
use packet::{Buf, BufferMut, SerializeError, Serializer};

use crate::{
    device::{DeviceIdInner, FrameDestination},
    NonSyncContext, SyncCtx,
};

pub(super) struct LoopbackDeviceState {
    mtu: u32,
}

impl LoopbackDeviceState {
    pub(super) fn new(mtu: u32) -> LoopbackDeviceState {
        LoopbackDeviceState { mtu }
    }
}

pub(super) fn send_ip_frame<
    B: BufferMut,
    NonSyncCtx: NonSyncContext,
    A: IpAddress,
    S: Serializer<Buffer = B>,
>(
    sync_ctx: &SyncCtx<NonSyncCtx>,
    ctx: &mut NonSyncCtx,
    _local_addr: SpecifiedAddr<A>,
    body: S,
) -> Result<(), S> {
    let frame = body
        .serialize_vec_outer()
        .map_err(|(_err, s): (SerializeError<Never>, _)| s)?
        .map_a(|b| Buf::new(b.as_ref().to_vec(), ..))
        .into_inner();

    crate::ip::receive_ip_packet::<_, _, A::Version>(
        sync_ctx,
        ctx,
        DeviceIdInner::Loopback.into(),
        FrameDestination::Unicast,
        frame,
    );
    Ok(())
}

/// Gets the MTU associated with this device.
pub(super) fn get_mtu<NonSyncCtx: NonSyncContext>(ctx: &SyncCtx<NonSyncCtx>) -> u32 {
    ctx.state.device.devices.read().loopback.as_ref().unwrap().link.mtu
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
        testutil::{DummyEventDispatcherBuilder, DummyEventDispatcherConfig, TestIpExt},
        Ctx, NonSyncContext, SyncCtx,
    };

    #[test]
    fn test_loopback_methods() {
        const MTU: u32 = 66;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_loopback_device(&mut sync_ctx, &mut non_sync_ctx, MTU)
            .expect("error adding loopback device");
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, device);

        assert_eq!(crate::ip::IpDeviceContext::<Ipv4, _>::get_mtu(&sync_ctx, device), MTU);
        assert_eq!(crate::ip::IpDeviceContext::<Ipv6, _>::get_mtu(&sync_ctx, device), MTU);

        fn test<
            I: TestIpExt + IpDeviceStateIpExt<NonSyncCtx::Instant>,
            NonSyncCtx: NonSyncContext,
        >(
            sync_ctx: &mut &SyncCtx<NonSyncCtx>,
            ctx: &mut NonSyncCtx,
            device: DeviceId,
            get_addrs: fn(&mut &SyncCtx<NonSyncCtx>, DeviceId) -> Vec<SpecifiedAddr<I::Addr>>,
        ) {
            assert_eq!(get_addrs(sync_ctx, device), []);

            let DummyEventDispatcherConfig {
                subnet,
                local_ip,
                local_mac: _,
                remote_ip: _,
                remote_mac: _,
            } = I::DUMMY_CONFIG;
            let addr = AddrSubnet::from_witness(local_ip, subnet.prefix())
                .expect("error creating AddrSubnet");
            assert_eq!(crate::device::add_ip_addr_subnet(sync_ctx, ctx, device, addr,), Ok(()));
            let addr = addr.addr();
            assert_eq!(&get_addrs(sync_ctx, device)[..], [addr]);

            assert_eq!(crate::device::del_ip_addr(sync_ctx, ctx, device, &addr,), Ok(()));
            assert_eq!(get_addrs(sync_ctx, device), []);

            assert_eq!(
                crate::device::del_ip_addr(sync_ctx, ctx, device, &addr,),
                Err(NotFoundError)
            );
        }

        test::<Ipv4, _>(&mut sync_ctx, &mut non_sync_ctx, device, |sync_ctx, device| {
            crate::ip::device::IpDeviceContext::<Ipv4, _>::with_ip_device_state(
                sync_ctx,
                device,
                |state| state.ip_state.iter_addrs().map(AssignedAddress::addr).collect::<Vec<_>>(),
            )
        });
        test::<Ipv6, _>(&mut sync_ctx, &mut non_sync_ctx, device, |sync_ctx, device| {
            crate::ip::device::IpDeviceContext::<Ipv6, _>::with_ip_device_state(
                sync_ctx,
                device,
                |state| state.ip_state.iter_addrs().map(AssignedAddress::addr).collect::<Vec<_>>(),
            )
        });
    }
}
