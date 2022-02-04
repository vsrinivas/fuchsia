// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The loopback device.

use alloc::vec::Vec;
use core::convert::Infallible as Never;
use net_types::{
    ip::{AddrSubnet, AddrSubnetEither, IpAddr, IpAddress},
    SpecifiedAddr, Witness as _,
};
use packet::{Buf, BufferMut, SerializeError, Serializer};

use crate::{
    device::{DeviceIdInner, FrameDestination},
    error::NotFoundError,
    ip::device::state::{
        AddrConfig, AddressError, AddressState, AssignedAddress as _, IpDeviceState,
        IpDeviceStateIpExt, Ipv6AddressEntry, Ipv6DeviceConfiguration, Ipv6DeviceState,
    },
    BufferDispatcher, Ctx, EventDispatcher,
};

#[derive(Copy, Clone, Eq, PartialEq, Hash)]
pub(crate) struct LoopbackDeviceId;

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

pub(super) fn add_ip_addr_subnet<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Ctx<D>,
    addr_sub: AddrSubnet<A>,
) -> Result<(), AddressError> {
    fn inner<Instant, I: IpDeviceStateIpExt<Instant>>(
        ip_state: &mut IpDeviceState<Instant, I>,
        addr: I::AssignedAddress,
    ) -> Result<(), AddressError> {
        if ip_state.iter_addrs().any(|a| a.addr() == addr.addr()) {
            return Err(AddressError::AlreadyExists);
        }

        ip_state.add_addr(addr);
        Ok(())
    }

    let state = &mut ctx.state.device.loopback.as_mut().unwrap().device;
    match addr_sub.into() {
        AddrSubnetEither::V4(addr_sub) => inner(&mut state.ip.ipv4.ip_state, addr_sub),
        AddrSubnetEither::V6(addr_sub) => {
            let Ipv6DeviceState {
                ref mut ip_state,
                config: Ipv6DeviceConfiguration { dad_transmits },
            } = state.ip.ipv6;
            assert_eq!(
                dad_transmits, None,
                "TODO(https://fxbug.dev/72378): loopback does not handle DAD yet"
            );

            let addr_sub = addr_sub.to_unicast();
            inner(
                ip_state,
                Ipv6AddressEntry::new(addr_sub, AddressState::Assigned, AddrConfig::Manual),
            )
        }
    }
}

pub(super) fn del_ip_addr<D: EventDispatcher, A: IpAddress>(
    ctx: &mut Ctx<D>,
    addr: &SpecifiedAddr<A>,
) -> Result<(), NotFoundError> {
    let state = &mut ctx.state.device.loopback.as_mut().unwrap().device;
    match addr.get().to_ip_addr() {
        IpAddr::V4(addr) => state.ip.ipv4.ip_state.remove_addr(&addr),
        IpAddr::V6(addr) => state.ip.ipv6.ip_state.remove_addr(&addr),
    }
}

pub(super) fn set_ipv6_configuration<D: EventDispatcher>(
    ctx: &mut Ctx<D>,
    config: Ipv6DeviceConfiguration,
) {
    ctx.state.device.loopback.as_mut().unwrap().device.ip.ipv6.config = config;
}

/// Gets the MTU associated with this device.
pub(super) fn get_mtu<D: EventDispatcher>(ctx: &Ctx<D>) -> u32 {
    ctx.state.device.loopback.as_ref().unwrap().device.link.mtu
}

#[cfg(test)]
mod tests {
    use crate::{
        context::InstantContext,
        device::DeviceId,
        error::NotFoundError,
        ip::device::state::{AssignedAddress, IpDeviceState, IpDeviceStateIpExt},
        testutil::{
            DummyEventDispatcher, DummyEventDispatcherBuilder, DummyEventDispatcherConfig,
            TestIpExt,
        },
        Ctx, EventDispatcher,
    };
    use alloc::vec::Vec;
    use net_types::ip::AddrSubnet;
    use net_types::ip::{Ipv4, Ipv6};

    #[test]
    fn test_loopback_methods() {
        const MTU: u32 = 66;
        let mut ctx = DummyEventDispatcherBuilder::default().build::<DummyEventDispatcher>();
        let device = ctx.state.add_loopback_device(MTU).expect("error adding loopback device");
        crate::device::initialize_device(&mut ctx, device);

        assert_eq!(crate::device::get_mtu(&ctx, device), MTU);

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

        test::<Ipv4, _>(
            &mut ctx,
            device,
            crate::device::get_ipv4_device_state::<DummyEventDispatcher>,
        );
        test::<Ipv6, _>(
            &mut ctx,
            device,
            crate::device::get_ipv6_device_state::<DummyEventDispatcher>,
        );
    }
}
