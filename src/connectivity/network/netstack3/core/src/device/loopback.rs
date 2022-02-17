// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The loopback device.

use alloc::vec::Vec;
use core::convert::Infallible as Never;

use net_types::{ip::IpAddress, SpecifiedAddr};
use packet::{Buf, BufferMut, SerializeError, Serializer};

use crate::{
    device::{DeviceIdInner, FrameDestination},
    BufferDispatcher, Ctx, EventDispatcher,
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
    D: BufferDispatcher<Buf<Vec<u8>>>,
    A: IpAddress,
    S: Serializer<Buffer = B>,
>(
    ctx: &mut Ctx<D>,
    _local_addr: SpecifiedAddr<A>,
    body: S,
) -> Result<(), S> {
    let frame = body
        .serialize_vec_outer()
        .map_err(|(_err, s): (SerializeError<Never>, _)| s)?
        .map_a(|b| Buf::new(b.as_ref().to_vec(), ..))
        .into_inner();

    crate::ip::receive_ip_packet::<_, _, A::Version>(
        ctx,
        DeviceIdInner::Loopback.into(),
        FrameDestination::Unicast,
        frame,
    );
    Ok(())
}

/// Gets the MTU associated with this device.
pub(super) fn get_mtu<D: EventDispatcher>(ctx: &Ctx<D>) -> u32 {
    ctx.state.device.loopback.as_ref().unwrap().device.link.mtu
}

#[cfg(test)]
mod tests {
    use alloc::vec::Vec;

    use net_types::ip::{AddrSubnet, Ipv4, Ipv6};

    use crate::{
        context::InstantContext,
        device::DeviceId,
        error::NotFoundError,
        ip::device::state::{AssignedAddress, IpDeviceState, IpDeviceStateIpExt},
        testutil::{DummyCtx, DummyEventDispatcherBuilder, DummyEventDispatcherConfig, TestIpExt},
        Ctx, EventDispatcher,
    };

    #[test]
    fn test_loopback_methods() {
        const MTU: u32 = 66;
        let mut ctx = DummyEventDispatcherBuilder::default().build();
        let device = ctx.state.add_loopback_device(MTU).expect("error adding loopback device");
        crate::device::initialize_device(&mut ctx, device);

        assert_eq!(crate::ip::device::get_mtu::<Ipv4, _>(&ctx, device), MTU);
        assert_eq!(crate::ip::device::get_mtu::<Ipv6, _>(&ctx, device), MTU);

        fn test<
            I: TestIpExt + IpDeviceStateIpExt<D::Instant>,
            D: EventDispatcher + InstantContext,
        >(
            ctx: &mut Ctx<D>,
            device: DeviceId,
            get_ip_state: fn(&Ctx<D>, DeviceId) -> &IpDeviceState<D::Instant, I>,
        ) {
            assert_eq!(
                &get_ip_state(ctx, device)
                    .iter_addrs()
                    .map(AssignedAddress::addr)
                    .collect::<Vec<_>>()[..],
                []
            );

            let DummyEventDispatcherConfig {
                subnet,
                local_ip,
                local_mac: _,
                remote_ip: _,
                remote_mac: _,
            } = I::DUMMY_CONFIG;
            let addr = AddrSubnet::from_witness(local_ip, subnet.prefix())
                .expect("error creating AddrSubnet");
            assert_eq!(crate::device::add_ip_addr_subnet(ctx, device, addr,), Ok(()));
            let addr = addr.addr();
            assert_eq!(
                &get_ip_state(ctx, device)
                    .iter_addrs()
                    .map(AssignedAddress::addr)
                    .collect::<Vec<_>>()[..],
                [addr]
            );

            assert_eq!(crate::device::del_ip_addr(ctx, device, &addr,), Ok(()));
            assert_eq!(
                &get_ip_state(ctx, device)
                    .iter_addrs()
                    .map(AssignedAddress::addr)
                    .collect::<Vec<_>>()[..],
                []
            );

            assert_eq!(crate::device::del_ip_addr(ctx, device, &addr,), Err(NotFoundError));
        }

        test::<Ipv4, _>(&mut ctx, device, crate::ip::device::get_ipv4_device_state::<DummyCtx>);
        test::<Ipv6, _>(&mut ctx, device, crate::ip::device::get_ipv6_device_state::<DummyCtx>);
    }
}
