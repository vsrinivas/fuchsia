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
    BlanketCoreContext, BufferDispatcher, Ctx, EventDispatcher,
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
    C: BlanketCoreContext,
    A: IpAddress,
    S: Serializer<Buffer = B>,
>(
    ctx: &mut Ctx<D, C>,
    _local_addr: SpecifiedAddr<A>,
    body: S,
) -> Result<(), S> {
    let frame = body
        .serialize_vec_outer()
        .map_err(|(_err, s): (SerializeError<Never>, _)| s)?
        .map_a(|b| Buf::new(b.as_ref().to_vec(), ..))
        .into_inner();

    crate::ip::receive_ip_packet::<_, _, _, A::Version>(
        ctx,
        DeviceIdInner::Loopback.into(),
        FrameDestination::Unicast,
        frame,
    );
    Ok(())
}

/// Gets the MTU associated with this device.
pub(super) fn get_mtu<D: EventDispatcher, C: BlanketCoreContext>(ctx: &Ctx<D, C>) -> u32 {
    ctx.state.device.loopback.as_ref().unwrap().link.mtu
}

#[cfg(test)]
mod tests {
    use alloc::vec::Vec;

    use net_types::ip::{AddrSubnet, Ipv4, Ipv6};

    use crate::{
        device::DeviceId,
        error::NotFoundError,
        ip::device::state::{AssignedAddress, IpDeviceState, IpDeviceStateIpExt},
        testutil::{DummyCtx, DummyEventDispatcherBuilder, DummyEventDispatcherConfig, TestIpExt},
        BlanketCoreContext, Ctx, EventDispatcher,
    };

    #[test]
    fn test_loopback_methods() {
        const MTU: u32 = 66;
        let mut ctx = DummyEventDispatcherBuilder::default().build();
        let device =
            crate::add_loopback_device(&mut ctx, MTU).expect("error adding loopback device");
        crate::device::testutil::enable_device(&mut ctx, device);

        assert_eq!(crate::ip::IpDeviceContext::<Ipv4, _>::get_mtu(&ctx, device), MTU);
        assert_eq!(crate::ip::IpDeviceContext::<Ipv6, _>::get_mtu(&ctx, device), MTU);

        fn test<
            I: TestIpExt + IpDeviceStateIpExt<C::Instant>,
            D: EventDispatcher,
            C: BlanketCoreContext,
        >(
            ctx: &mut Ctx<D, C>,
            device: DeviceId,
            get_ip_state: for<'a> fn(&'a Ctx<D, C>, DeviceId) -> &'a IpDeviceState<C::Instant, I>,
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

        test::<Ipv4, _, _>(
            &mut ctx,
            device,
            crate::ip::device::get_ipv4_device_state::<(), DummyCtx>,
        );
        test::<Ipv6, _, _>(
            &mut ctx,
            device,
            crate::ip::device::get_ipv6_device_state::<(), DummyCtx>,
        );
    }
}
