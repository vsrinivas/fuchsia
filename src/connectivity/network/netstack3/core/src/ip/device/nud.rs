// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Neighbor unreachability detection.

use alloc::{
    collections::hash_map::{Entry, HashMap},
    vec::Vec,
};
use core::{fmt::Debug, hash::Hash, marker::PhantomData, num::NonZeroU8};

use assert_matches::assert_matches;
use derivative::Derivative;
use net_types::{ip::Ip, SpecifiedAddr};
use packet_formats::utils::NonZeroDuration;

use crate::{
    context::{TimerContext, TimerHandler},
    device::{link::LinkDevice, DeviceIdContext},
    ip::IpDeviceIdContext,
};

/// The maximum number of multicast solicitations as defined in [RFC 4861
/// section 10].
///
/// [RFC 4861 section 10]: https://tools.ietf.org/html/rfc4861#section-10
const MAX_MULTICAST_SOLICIT: u8 = 3;

/// The type of message with a dynamic neighbor update.
#[derive(Copy, Clone)]
pub(crate) enum DynamicNeighborUpdateSource {
    /// Indicates an update from a neighbor probe message.
    ///
    /// E.g. NDP Neighbor Solicitation.
    Probe,

    /// Indicates an update from a neighbor confirmation message.
    ///
    /// E.g. NDP Neighbor Advertisement.
    Confirmation,
}

#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
enum NeighborState<LinkAddr> {
    Dynamic(DynamicNeighborState<LinkAddr>),
    Static(LinkAddr),
}

#[derive(Debug)]
#[cfg_attr(test, derive(PartialEq, Eq))]
enum DynamicNeighborState<LinkAddr> {
    Incomplete { transmit_counter: Option<NonZeroU8> },
    Complete { link_address: LinkAddr },
}

impl<LinkAddr> DynamicNeighborState<LinkAddr> {
    fn new_incomplete(remaining_tries: u8) -> Self {
        DynamicNeighborState::Incomplete { transmit_counter: NonZeroU8::new(remaining_tries) }
    }
}

#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub(crate) struct NudTimerId<I: Ip, D: LinkDevice, DeviceId> {
    device_id: DeviceId,
    lookup_addr: SpecifiedAddr<I::Addr>,
    _marker: PhantomData<D>,
}

#[derive(Derivative)]
#[derivative(Default(bound = ""))]
#[cfg_attr(test, derive(Debug, PartialEq, Eq))]
pub(crate) struct NudState<I: Ip, LinkAddr> {
    neighbors: HashMap<SpecifiedAddr<I::Addr>, NeighborState<LinkAddr>>,
}

pub(crate) trait NonSyncNudContext<I: Ip, D: LinkDevice, DeviceId>:
    TimerContext<NudTimerId<I, D, DeviceId>>
{
}

impl<I: Ip, D: LinkDevice, DeviceId, C: TimerContext<NudTimerId<I, D, DeviceId>>>
    NonSyncNudContext<I, D, DeviceId> for C
{
}

pub(crate) trait NudContext<I: Ip, D: LinkDevice, C: NonSyncNudContext<I, D, Self::DeviceId>>:
    DeviceIdContext<D>
{
    /// Returns the amount of time between neighbor probe/solicitation messages.
    fn retrans_timer(&self, device_id: Self::DeviceId) -> NonZeroDuration;

    /// Returns a mutable reference to the NUD state.
    fn get_state_mut(&mut self, device_id: Self::DeviceId) -> &mut NudState<I, D::Address>;

    /// Sends a neighbor probe/solicitation message.
    fn send_neighbor_solicitation(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        lookup_addr: SpecifiedAddr<I::Addr>,
    );

    /// Notifies device layer that the link-layer address for the neighbor in
    /// `address` has been resolved to `link_address`.
    ///
    /// Implementers may use this signal to dispatch any packets that were
    /// queued waiting for address resolution.
    fn address_resolved(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        address: SpecifiedAddr<I::Addr>,
        link_address: D::Address,
    );

    /// Notifies the device layer that the link-layer address resolution for the
    /// neighbor in `address` failed.
    fn address_resolution_failed(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        address: SpecifiedAddr<I::Addr>,
    );
}

pub(crate) trait NudIpHandler<I: Ip, C>: IpDeviceIdContext<I> {
    /// Handles an incoming neighbor probe message.
    ///
    /// For IPv6, this can be an NDP Neighbor Solicitation or an NDP Router
    /// Advertisement message.
    fn handle_neighbor_probe(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_addr: &[u8],
    );

    /// Handles an incoming neighbor confirmation message.
    ///
    /// For IPv6, this can be an NDP Neighbor Advertisement.
    fn handle_neighbor_confirmation(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_addr: &[u8],
    );

    /// Clears the neighbor table.
    fn flush_neighbor_table(&mut self, ctx: &mut C, device_id: Self::DeviceId);
}

pub(crate) trait NudHandler<I: Ip, D: LinkDevice, C>: DeviceIdContext<D> {
    /// Sets a dynamic neighbor's entry state to the specified values in
    /// response to the source packet.
    fn set_dynamic_neighbor(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_addr: D::Address,
        source: DynamicNeighborUpdateSource,
    );

    /// Sets a static neighbor entry for the neighbor.
    ///
    /// If no entry exists, a new one may be created. If an entry already
    /// exists, it will be updated with the provided link address and set
    /// to be a static entry.
    ///
    /// Dynamic updates for the neighbor will be ignored for static entries.
    fn set_static_neighbor(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_addr: D::Address,
    );

    /// Look up the link layer address.
    ///
    /// Begins the address resolution process if the link layer address for
    /// `lookup_addr` is not already known.
    fn lookup(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        lookup_addr: SpecifiedAddr<I::Addr>,
    ) -> Option<D::Address>;

    /// Clears the neighbor tabe.
    fn flush(&mut self, ctx: &mut C, device_id: Self::DeviceId);
}

impl<I: Ip, D: LinkDevice, C: NonSyncNudContext<I, D, SC::DeviceId>, SC: NudContext<I, D, C>>
    TimerHandler<C, NudTimerId<I, D, SC::DeviceId>> for SC
{
    fn handle_timer(
        &mut self,
        ctx: &mut C,
        NudTimerId { device_id, lookup_addr, _marker }: NudTimerId<I, D, SC::DeviceId>,
    ) {
        let NudState { neighbors } = self.get_state_mut(device_id);

        let transmit_counter =
            match neighbors.get_mut(&lookup_addr).expect("timer fired for invalid entry") {
                NeighborState::Dynamic(DynamicNeighborState::Incomplete { transmit_counter }) => {
                    transmit_counter
                }
                NeighborState::Static(_)
                | NeighborState::Dynamic(DynamicNeighborState::Complete { link_address: _ }) => {
                    unreachable!("timer should only fire for incomplete entry")
                }
            };

        match transmit_counter {
            Some(c) => {
                *transmit_counter = NonZeroU8::new(c.get() - 1);
                solicit_neighbor(self, ctx, device_id, lookup_addr)
            }
            None => {
                // Failed to complete neighbor resolution and no more probes to
                // send.
                assert_matches!(
                    neighbors.remove(&lookup_addr),
                    Some(e) => {
                        let _: NeighborState<_> = e;
                    }
                );

                self.address_resolution_failed(ctx, device_id, lookup_addr)
            }
        }
    }
}

fn solicit_neighbor<
    I: Ip,
    D: LinkDevice,
    C: NonSyncNudContext<I, D, SC::DeviceId>,
    SC: NudContext<I, D, C>,
>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
    lookup_addr: SpecifiedAddr<I::Addr>,
) {
    sync_ctx.send_neighbor_solicitation(ctx, device_id, lookup_addr);

    let retrans_timer = sync_ctx.retrans_timer(device_id);
    let _: Option<C::Instant> = ctx.schedule_timer(
        retrans_timer.get(),
        NudTimerId { device_id, lookup_addr, _marker: PhantomData },
    );
}

impl<I: Ip, D: LinkDevice, C: NonSyncNudContext<I, D, SC::DeviceId>, SC: NudContext<I, D, C>>
    NudHandler<I, D, C> for SC
{
    fn lookup(
        &mut self,
        ctx: &mut C,
        device_id: SC::DeviceId,
        lookup_addr: SpecifiedAddr<I::Addr>,
    ) -> Option<D::Address> {
        let NudState { neighbors } = self.get_state_mut(device_id);

        let entry = match neighbors.entry(lookup_addr) {
            Entry::Vacant(e) => {
                let _: &mut NeighborState<_> = e.insert(NeighborState::Dynamic(
                    DynamicNeighborState::new_incomplete(MAX_MULTICAST_SOLICIT - 1),
                ));
                solicit_neighbor(self, ctx, device_id, lookup_addr);
                return None;
            }
            Entry::Occupied(e) => e.into_mut(),
        };

        match entry {
            NeighborState::Dynamic(DynamicNeighborState::Incomplete { transmit_counter: _ }) => {
                None
            }
            NeighborState::Static(link_address)
            | NeighborState::Dynamic(DynamicNeighborState::Complete { link_address }) => {
                Some(link_address.clone())
            }
        }
    }

    fn set_dynamic_neighbor(
        &mut self,
        ctx: &mut C,
        device_id: SC::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_address: D::Address,
        source: DynamicNeighborUpdateSource,
    ) {
        let NudState { neighbors } = self.get_state_mut(device_id);
        match neighbors.entry(neighbor) {
            Entry::Vacant(e) => match source {
                DynamicNeighborUpdateSource::Probe => {
                    let _: &mut NeighborState<_> =
                        e.insert(NeighborState::Dynamic(DynamicNeighborState::Complete {
                            link_address,
                        }));
                }
                DynamicNeighborUpdateSource::Confirmation => {}
            },
            Entry::Occupied(e) => match e.into_mut() {
                NeighborState::Dynamic(e) => {
                    let prev =
                        core::mem::replace(e, DynamicNeighborState::Complete { link_address });
                    let _: Option<C::Instant> = ctx.cancel_timer(NudTimerId {
                        device_id,
                        lookup_addr: neighbor,
                        _marker: PhantomData,
                    });

                    match prev {
                        DynamicNeighborState::Incomplete { transmit_counter: _ } => {
                            self.address_resolved(ctx, device_id, neighbor, link_address)
                        }
                        DynamicNeighborState::Complete { link_address: _ } => {}
                    }
                }
                NeighborState::Static(_) => {}
            },
        }
    }

    fn set_static_neighbor(
        &mut self,
        ctx: &mut C,
        device_id: SC::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_address: D::Address,
    ) {
        let NudState { neighbors } = self.get_state_mut(device_id);
        match neighbors.insert(neighbor, NeighborState::Static(link_address)) {
            Some(NeighborState::Dynamic(DynamicNeighborState::Incomplete {
                transmit_counter: _,
            })) => self.address_resolved(ctx, device_id, neighbor, link_address),
            None
            | Some(NeighborState::Static(_))
            | Some(NeighborState::Dynamic(DynamicNeighborState::Complete { link_address: _ })) => {}
        }
    }

    fn flush(&mut self, ctx: &mut C, device_id: Self::DeviceId) {
        let NudState { neighbors } = self.get_state_mut(device_id);

        let mut previously_incomplete = Vec::new();

        neighbors.retain(|neighbor, state| {
            match state {
                NeighborState::Dynamic(state) => {
                    match state {
                        DynamicNeighborState::Incomplete { transmit_counter: _ } => {
                            previously_incomplete.push(*neighbor);
                        }
                        DynamicNeighborState::Complete { link_address: _ } => {}
                    }

                    // Only flush dynamic entries.
                    false
                }
                NeighborState::Static(_) => true,
            }
        });

        previously_incomplete.into_iter().for_each(|neighbor| {
            assert_ne!(
                ctx.cancel_timer(NudTimerId {
                    device_id,
                    lookup_addr: neighbor,
                    _marker: PhantomData,
                }),
                None,
                "previously incomplete entry for {} should have had a timer",
                neighbor
            );

            self.address_resolution_failed(ctx, device_id, neighbor);
        });
    }
}

#[cfg(test)]
mod tests {
    use alloc::{vec, vec::Vec};
    use core::{convert::TryInto as _, num::NonZeroU64};

    use net_declare::{net_ip_v4, net_ip_v6};
    use net_types::{
        ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
        UnicastAddr, Witness as _,
    };
    use packet::{Buf, InnerPacketBuilder as _, Serializer as _};
    use packet_formats::{
        icmp::{
            ndp::{
                options::NdpOptionBuilder, NeighborAdvertisement, NeighborSolicitation,
                OptionSequenceBuilder, RouterAdvertisement,
            },
            IcmpPacketBuilder, IcmpUnusedCode,
        },
        ip::Ipv6Proto,
        ipv6::Ipv6PacketBuilder,
        testutil::parse_icmp_packet_in_ip_packet_in_ethernet_frame,
    };
    use specialize_ip_macro::ip_test;

    use super::*;
    use crate::{
        context::{
            testutil::{DummyCtx, DummyNonSyncCtx, DummySyncCtx, DummyTimerCtxExt as _},
            FrameContext as _, InstantContext as _,
        },
        device::link::testutil::{DummyLinkAddress, DummyLinkDevice, DummyLinkDeviceId},
        ip::{
            device::update_ipv6_configuration, icmp::REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
            receive_ipv6_packet, FrameDestination,
        },
        testutil::{DummyEventDispatcherConfig, TestIpExt as _},
    };

    struct MockNudContext<I: Ip, LinkAddr> {
        retrans_timer: NonZeroDuration,
        nud: NudState<I, LinkAddr>,
        resolved: Vec<(SpecifiedAddr<I::Addr>, DummyLinkAddress)>,
        failed: Vec<SpecifiedAddr<I::Addr>>,
    }

    #[derive(Debug, PartialEq, Eq)]
    struct MockNudMessageMeta<I: Ip> {
        lookup_addr: SpecifiedAddr<I::Addr>,
    }

    type MockCtx<I> =
        DummySyncCtx<MockNudContext<I, DummyLinkAddress>, MockNudMessageMeta<I>, DummyLinkDeviceId>;

    type MockNonSyncCtx<I> =
        DummyNonSyncCtx<NudTimerId<I, DummyLinkDevice, DummyLinkDeviceId>, (), ()>;

    impl<I: Ip> NudContext<I, DummyLinkDevice, MockNonSyncCtx<I>> for MockCtx<I> {
        fn retrans_timer(&self, DummyLinkDeviceId: DummyLinkDeviceId) -> NonZeroDuration {
            self.get_ref().retrans_timer
        }

        fn get_state_mut(
            &mut self,
            DummyLinkDeviceId: DummyLinkDeviceId,
        ) -> &mut NudState<I, DummyLinkAddress> {
            &mut self.get_mut().nud
        }

        fn send_neighbor_solicitation(
            &mut self,
            ctx: &mut MockNonSyncCtx<I>,
            DummyLinkDeviceId: DummyLinkDeviceId,
            lookup_addr: SpecifiedAddr<I::Addr>,
        ) {
            self.send_frame(ctx, MockNudMessageMeta { lookup_addr }, Buf::new(Vec::new(), ..))
                .unwrap()
        }

        fn address_resolved(
            &mut self,
            _ctx: &mut MockNonSyncCtx<I>,
            DummyLinkDeviceId: DummyLinkDeviceId,
            address: SpecifiedAddr<I::Addr>,
            link_address: DummyLinkAddress,
        ) {
            self.get_mut().resolved.push((address, link_address))
        }

        fn address_resolution_failed(
            &mut self,
            _ctx: &mut MockNonSyncCtx<I>,
            DummyLinkDeviceId: DummyLinkDeviceId,
            address: SpecifiedAddr<I::Addr>,
        ) {
            self.get_mut().failed.push(address)
        }
    }

    const ONE_SECOND: NonZeroDuration =
        NonZeroDuration::from_nonzero_secs(const_unwrap::const_unwrap_option(NonZeroU64::new(1)));

    fn check_lookup_has<I: Ip>(
        sync_ctx: &mut MockCtx<I>,
        ctx: &mut MockNonSyncCtx<I>,
        lookup_addr: SpecifiedAddr<I::Addr>,
        expected_link_addr: DummyLinkAddress,
    ) {
        assert_matches!(
            NudHandler::lookup(sync_ctx, ctx, DummyLinkDeviceId, lookup_addr),
            Some(got) => assert_eq!(got, expected_link_addr)
        );
        ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.take_frames(), []);
    }

    trait TestIpExt: Ip {
        const LOOKUP_ADDR1: SpecifiedAddr<Self::Addr>;
        const LOOKUP_ADDR2: SpecifiedAddr<Self::Addr>;
        const LOOKUP_ADDR3: SpecifiedAddr<Self::Addr>;
    }

    impl TestIpExt for Ipv4 {
        // Safe because the address is non-zero.
        const LOOKUP_ADDR1: SpecifiedAddr<Ipv4Addr> =
            unsafe { SpecifiedAddr::new_unchecked(net_ip_v4!("192.168.0.1")) };
        const LOOKUP_ADDR2: SpecifiedAddr<Ipv4Addr> =
            unsafe { SpecifiedAddr::new_unchecked(net_ip_v4!("192.168.0.2")) };
        const LOOKUP_ADDR3: SpecifiedAddr<Ipv4Addr> =
            unsafe { SpecifiedAddr::new_unchecked(net_ip_v4!("192.168.0.3")) };
    }

    impl TestIpExt for Ipv6 {
        // Safe because the address is non-zero.
        const LOOKUP_ADDR1: SpecifiedAddr<Ipv6Addr> =
            unsafe { SpecifiedAddr::new_unchecked(net_ip_v6!("fe80::1")) };
        const LOOKUP_ADDR2: SpecifiedAddr<Ipv6Addr> =
            unsafe { SpecifiedAddr::new_unchecked(net_ip_v6!("fe80::2")) };
        const LOOKUP_ADDR3: SpecifiedAddr<Ipv6Addr> =
            unsafe { SpecifiedAddr::new_unchecked(net_ip_v6!("fe80::3")) };
    }

    #[ip_test]
    fn comfirmation_should_not_create_entry<I: Ip + TestIpExt>() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::<I>::with_state(MockNudContext {
                retrans_timer: ONE_SECOND,
                nud: Default::default(),
                resolved: Default::default(),
                failed: Default::default(),
            }));

        let link_addr = DummyLinkAddress([1]);
        NudHandler::set_dynamic_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            link_addr,
            DynamicNeighborUpdateSource::Confirmation,
        );
        assert_eq!(sync_ctx.get_ref().nud, Default::default());
    }

    const LINK_ADDR1: DummyLinkAddress = DummyLinkAddress([1]);
    const LINK_ADDR2: DummyLinkAddress = DummyLinkAddress([2]);
    const LINK_ADDR3: DummyLinkAddress = DummyLinkAddress([3]);

    #[ip_test]
    fn static_neighbor<I: Ip + TestIpExt>() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::<I>::with_state(MockNudContext {
                retrans_timer: ONE_SECOND,
                nud: Default::default(),
                resolved: Default::default(),
                failed: Default::default(),
            }));

        NudHandler::set_static_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            LINK_ADDR1,
        );
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.take_frames(), []);
        check_lookup_has(&mut sync_ctx, &mut non_sync_ctx, I::LOOKUP_ADDR1, LINK_ADDR1);

        // Dynamic entries should not overwrite static entries.
        NudHandler::set_dynamic_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            LINK_ADDR2,
            DynamicNeighborUpdateSource::Probe,
        );
        check_lookup_has(&mut sync_ctx, &mut non_sync_ctx, I::LOOKUP_ADDR1, LINK_ADDR1);
    }

    #[ip_test]
    fn dynamic_neighbor<I: Ip + TestIpExt>() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::<I>::with_state(MockNudContext {
                retrans_timer: ONE_SECOND,
                nud: Default::default(),
                resolved: Default::default(),
                failed: Default::default(),
            }));

        NudHandler::set_dynamic_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            LINK_ADDR1,
            DynamicNeighborUpdateSource::Probe,
        );
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.take_frames(), []);
        check_lookup_has(&mut sync_ctx, &mut non_sync_ctx, I::LOOKUP_ADDR1, LINK_ADDR1);

        // Dynamic entries may be overwritten by new dynamic entries.
        NudHandler::set_dynamic_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            LINK_ADDR2,
            DynamicNeighborUpdateSource::Probe,
        );
        check_lookup_has(&mut sync_ctx, &mut non_sync_ctx, I::LOOKUP_ADDR1, LINK_ADDR2);

        // A static entry may overwrite a dynamic entry.
        NudHandler::set_static_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            LINK_ADDR3,
        );
        check_lookup_has(&mut sync_ctx, &mut non_sync_ctx, I::LOOKUP_ADDR1, LINK_ADDR3);
    }

    #[ip_test]
    fn send_solicitation_on_lookup<I: Ip + TestIpExt>() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::<I>::with_state(MockNudContext {
                retrans_timer: ONE_SECOND,
                nud: Default::default(),
                resolved: Default::default(),
                failed: Default::default(),
            }));
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.take_frames(), []);

        assert_matches!(
            NudHandler::lookup(
                &mut sync_ctx,
                &mut non_sync_ctx,
                DummyLinkDeviceId,
                I::LOOKUP_ADDR1
            ),
            None
        );
        let MockNudContext { retrans_timer: _, nud, resolved, failed } = sync_ctx.get_ref();
        assert_eq!(
            nud.neighbors,
            HashMap::from([(
                I::LOOKUP_ADDR1,
                NeighborState::Dynamic(DynamicNeighborState::new_incomplete(
                    MAX_MULTICAST_SOLICIT - 1
                )),
            )])
        );
        assert_eq!(resolved, &[]);
        assert_eq!(failed, &[]);
        non_sync_ctx.timer_ctx().assert_timers_installed([(
            NudTimerId {
                device_id: DummyLinkDeviceId,
                lookup_addr: I::LOOKUP_ADDR1,
                _marker: PhantomData,
            },
            non_sync_ctx.now() + ONE_SECOND.get(),
        )]);
        assert_eq!(
            sync_ctx.take_frames(),
            [(MockNudMessageMeta { lookup_addr: I::LOOKUP_ADDR1 }, Vec::new())]
        );

        // Calling lookup immediately again does not trigger a new NUD message.
        assert_matches!(
            NudHandler::lookup(
                &mut sync_ctx,
                &mut non_sync_ctx,
                DummyLinkDeviceId,
                I::LOOKUP_ADDR1
            ),
            None
        );
        let MockNudContext { retrans_timer: _, nud, resolved, failed } = sync_ctx.get_ref();
        assert_eq!(
            nud.neighbors,
            HashMap::from([(
                I::LOOKUP_ADDR1,
                NeighborState::Dynamic(DynamicNeighborState::new_incomplete(
                    MAX_MULTICAST_SOLICIT - 1
                )),
            )])
        );
        assert_eq!(resolved, &[]);
        assert_eq!(failed, &[]);
        non_sync_ctx.timer_ctx().assert_timers_installed([(
            NudTimerId {
                device_id: DummyLinkDeviceId,
                lookup_addr: I::LOOKUP_ADDR1,
                _marker: PhantomData,
            },
            non_sync_ctx.now() + ONE_SECOND.get(),
        )]);
        assert_eq!(sync_ctx.take_frames(), []);

        // Complete link resolution.
        NudHandler::set_dynamic_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            LINK_ADDR1,
            DynamicNeighborUpdateSource::Confirmation,
        );
        check_lookup_has(&mut sync_ctx, &mut non_sync_ctx, I::LOOKUP_ADDR1, LINK_ADDR1);

        let MockNudContext { retrans_timer: _, nud, resolved, failed } = sync_ctx.get_ref();
        assert_eq!(
            nud.neighbors,
            HashMap::from([(
                I::LOOKUP_ADDR1,
                NeighborState::Dynamic(DynamicNeighborState::Complete { link_address: LINK_ADDR1 }),
            )])
        );
        assert_eq!(resolved, &[(I::LOOKUP_ADDR1, LINK_ADDR1)]);
        assert_eq!(failed, &[]);
    }

    #[ip_test]
    fn solicitation_failure<I: Ip + TestIpExt>() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::<I>::with_state(MockNudContext {
                retrans_timer: ONE_SECOND,
                nud: Default::default(),
                resolved: Default::default(),
                failed: Default::default(),
            }));
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.take_frames(), []);

        assert_matches!(
            NudHandler::lookup(
                &mut sync_ctx,
                &mut non_sync_ctx,
                DummyLinkDeviceId,
                I::LOOKUP_ADDR1
            ),
            None
        );

        let timer_id = NudTimerId {
            device_id: DummyLinkDeviceId,
            lookup_addr: I::LOOKUP_ADDR1,
            _marker: PhantomData,
        };
        for i in 1..=MAX_MULTICAST_SOLICIT {
            let MockNudContext { retrans_timer, nud, resolved, failed } = sync_ctx.get_ref();
            let retrans_timer = retrans_timer.get();

            assert_eq!(
                nud.neighbors,
                HashMap::from([(
                    I::LOOKUP_ADDR1,
                    NeighborState::Dynamic(DynamicNeighborState::new_incomplete(
                        MAX_MULTICAST_SOLICIT - i
                    )),
                )])
            );
            assert_eq!(resolved, &[]);
            assert_eq!(failed, &[]);

            non_sync_ctx
                .timer_ctx()
                .assert_timers_installed([(timer_id, non_sync_ctx.now() + ONE_SECOND.get())]);
            assert_eq!(
                sync_ctx.take_frames(),
                [(MockNudMessageMeta { lookup_addr: I::LOOKUP_ADDR1 }, Vec::new())]
            );

            assert_eq!(
                non_sync_ctx.trigger_timers_for(
                    &mut sync_ctx,
                    retrans_timer,
                    TimerHandler::handle_timer
                ),
                [timer_id]
            );
        }

        let MockNudContext { retrans_timer: _, nud, resolved, failed } = sync_ctx.get_ref();
        assert_eq!(nud.neighbors, HashMap::new());
        assert_eq!(resolved, &[]);
        assert_eq!(failed, &[I::LOOKUP_ADDR1]);
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.take_frames(), []);
    }

    #[ip_test]
    fn flush_entries<I: Ip + TestIpExt>() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::<I>::with_state(MockNudContext {
                retrans_timer: ONE_SECOND,
                nud: Default::default(),
                resolved: Default::default(),
                failed: Default::default(),
            }));
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.take_frames(), []);

        NudHandler::set_static_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            LINK_ADDR1,
        );
        NudHandler::set_dynamic_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyLinkDeviceId,
            I::LOOKUP_ADDR2,
            LINK_ADDR2,
            DynamicNeighborUpdateSource::Probe,
        );
        assert_matches!(
            NudHandler::lookup(
                &mut sync_ctx,
                &mut non_sync_ctx,
                DummyLinkDeviceId,
                I::LOOKUP_ADDR3
            ),
            None
        );
        let MockNudContext { retrans_timer: _, nud, resolved, failed } = sync_ctx.get_ref();
        assert_eq!(
            nud.neighbors,
            HashMap::from([
                (I::LOOKUP_ADDR1, NeighborState::Static(LINK_ADDR1),),
                (
                    I::LOOKUP_ADDR2,
                    NeighborState::Dynamic(DynamicNeighborState::Complete {
                        link_address: LINK_ADDR2
                    }),
                ),
                (
                    I::LOOKUP_ADDR3,
                    NeighborState::Dynamic(DynamicNeighborState::new_incomplete(
                        MAX_MULTICAST_SOLICIT - 1
                    )),
                ),
            ])
        );
        assert_eq!(resolved, &[]);
        assert_eq!(failed, &[]);
        non_sync_ctx.timer_ctx().assert_timers_installed([(
            NudTimerId {
                device_id: DummyLinkDeviceId,
                lookup_addr: I::LOOKUP_ADDR3,
                _marker: PhantomData,
            },
            non_sync_ctx.now() + ONE_SECOND.get(),
        )]);

        // Flushing the table should clear all dynamic entries and timers.
        NudHandler::flush(&mut sync_ctx, &mut non_sync_ctx, DummyLinkDeviceId);
        let MockNudContext { retrans_timer: _, nud, resolved, failed } = sync_ctx.get_ref();
        assert_eq!(
            nud.neighbors,
            HashMap::from([(I::LOOKUP_ADDR1, NeighborState::Static(LINK_ADDR1),),])
        );
        assert_eq!(resolved, &[]);
        assert_eq!(failed, &[I::LOOKUP_ADDR3]);
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }

    #[test]
    fn router_advertisement_with_source_link_layer_option_should_add_neighbor() {
        let DummyEventDispatcherConfig {
            local_mac,
            remote_mac,
            local_ip: _,
            remote_ip: _,
            subnet: _,
        } = Ipv6::DUMMY_CONFIG;

        let crate::testutil::DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            crate::testutil::DummyCtx::default();
        let device_id =
            sync_ctx.state.device.add_ethernet_device(local_mac, Ipv6::MINIMUM_LINK_MTU.into());
        crate::ip::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            |config| {
                config.ip_config.ip_enabled = true;
            },
        );

        let remote_mac_bytes = remote_mac.bytes();
        let options = vec![NdpOptionBuilder::SourceLinkLayerAddress(&remote_mac_bytes[..])];

        let src_ip = remote_mac.to_ipv6_link_local().addr();
        let dst_ip = Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get();
        let ra_packet_buf = |options: &[NdpOptionBuilder<'_>]| {
            OptionSequenceBuilder::new(options.iter())
                .into_serializer()
                .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                    src_ip,
                    dst_ip,
                    IcmpUnusedCode,
                    RouterAdvertisement::new(0, false, false, 0, 0, 0),
                ))
                .encapsulate(Ipv6PacketBuilder::new(
                    src_ip,
                    dst_ip,
                    REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
                    Ipv6Proto::Icmpv6,
                ))
                .serialize_vec_outer()
                .unwrap()
                .unwrap_b()
        };

        // First receive a Router Advertisement without the source link layer
        // and make sure no new neighbor gets added.
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            FrameDestination::Multicast,
            ra_packet_buf(&[][..]),
        );
        let link_device_id = device_id.try_into().unwrap();
        assert_eq!(
            NudContext::<Ipv6, _, _>::get_state_mut(&mut sync_ctx, link_device_id).neighbors,
            Default::default()
        );

        // RA with a source link layer option should create a new entry.
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            FrameDestination::Multicast,
            ra_packet_buf(&options[..]),
        );
        assert_eq!(
            NudContext::<Ipv6, _, _>::get_state_mut(&mut sync_ctx, link_device_id).neighbors,
            HashMap::from([(
                {
                    let src_ip: UnicastAddr<_> = src_ip.into_addr();
                    src_ip.into_specified()
                },
                NeighborState::Dynamic(DynamicNeighborState::Complete {
                    link_address: remote_mac.get()
                })
            )])
        );
    }

    #[test]
    fn ipv6_integration() {
        let DummyEventDispatcherConfig {
            local_mac,
            remote_mac,
            local_ip: _,
            remote_ip: _,
            subnet: _,
        } = Ipv6::DUMMY_CONFIG;

        let crate::testutil::DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            crate::testutil::DummyCtx::default();
        let device_id =
            sync_ctx.state.device.add_ethernet_device(local_mac, Ipv6::MINIMUM_LINK_MTU.into());
        crate::ip::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            |config| {
                config.ip_config.ip_enabled = true;
            },
        );

        let remote_mac_bytes = remote_mac.bytes();

        let neighbor_ip = remote_mac.to_ipv6_link_local().addr();
        let neighbor_ip: UnicastAddr<_> = neighbor_ip.into_addr();
        let dst_ip = Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get();
        let na_packet_buf = |solicited_flag, override_flag| {
            let options = [NdpOptionBuilder::TargetLinkLayerAddress(&remote_mac_bytes[..])];
            OptionSequenceBuilder::new(options.iter())
                .into_serializer()
                .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                    neighbor_ip,
                    dst_ip,
                    IcmpUnusedCode,
                    NeighborAdvertisement::new(
                        false, /* router_flag */
                        solicited_flag,
                        override_flag,
                        neighbor_ip.get(),
                    ),
                ))
                .encapsulate(Ipv6PacketBuilder::new(
                    neighbor_ip,
                    dst_ip,
                    REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
                    Ipv6Proto::Icmpv6,
                ))
                .serialize_vec_outer()
                .unwrap()
                .unwrap_b()
        };

        // NeighborAdvertisements should not create a new entry even if
        // the advertisement has both the solicited and override flag set.
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            FrameDestination::Multicast,
            na_packet_buf(false, false),
        );
        let link_device_id = device_id.try_into().unwrap();
        assert_eq!(
            NudContext::<Ipv6, _, _>::get_state_mut(&mut sync_ctx, link_device_id).neighbors,
            Default::default()
        );
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            FrameDestination::Multicast,
            na_packet_buf(true, true),
        );
        assert_eq!(
            NudContext::<Ipv6, _, _>::get_state_mut(&mut sync_ctx, link_device_id).neighbors,
            Default::default()
        );

        assert_eq!(non_sync_ctx.take_frames(), []);

        // Trigger a neighbor solicitation to be sent.
        assert_matches!(
            NudHandler::<Ipv6, _, _>::lookup(
                &mut sync_ctx,
                &mut non_sync_ctx,
                link_device_id,
                neighbor_ip.into_specified()
            ),
            None
        );
        assert_matches!(
            &non_sync_ctx.take_frames()[..],
            [(got_device_id, got_frame)] => {
                assert_eq!(got_device_id, &device_id);

                let (src_mac, dst_mac, got_src_ip, got_dst_ip, ttl, message, code) = parse_icmp_packet_in_ip_packet_in_ethernet_frame::<
                    Ipv6,
                    _,
                    NeighborSolicitation,
                    _,
                >(got_frame, |_| {})
                    .unwrap();
                let target = neighbor_ip;
                let snmc = target.to_solicited_node_address();
                assert_eq!(src_mac, local_mac.get());
                assert_eq!(dst_mac, snmc.into());
                assert_eq!(got_src_ip, local_mac.to_ipv6_link_local().addr().into());
                assert_eq!(got_dst_ip, snmc.get());
                assert_eq!(ttl, 255);
                assert_eq!(message.target_address(), &target.get());
                assert_eq!(code, IcmpUnusedCode);
            }
        );
        assert_eq!(
            NudContext::<Ipv6, _, _>::get_state_mut(&mut sync_ctx, link_device_id).neighbors,
            HashMap::from([(
                neighbor_ip.into_specified(),
                NeighborState::Dynamic(DynamicNeighborState::new_incomplete(
                    MAX_MULTICAST_SOLICIT - 1
                )),
            )])
        );

        // A Neighbor advertisement should now update the entry.
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            device_id,
            FrameDestination::Multicast,
            na_packet_buf(true, true),
        );
        assert_eq!(
            NudContext::<Ipv6, _, _>::get_state_mut(&mut sync_ctx, link_device_id).neighbors,
            HashMap::from([(
                neighbor_ip.into_specified(),
                NeighborState::Dynamic(DynamicNeighborState::Complete {
                    link_address: remote_mac.get()
                })
            )])
        );

        // Disabling the device should clear the neighbor table.
        update_ipv6_configuration(&mut sync_ctx, &mut non_sync_ctx, device_id, |config| {
            config.ip_config.ip_enabled = false;
        });
        assert_eq!(
            NudContext::<Ipv6, _, _>::get_state_mut(&mut sync_ctx, link_device_id).neighbors,
            HashMap::new()
        );
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }
}
