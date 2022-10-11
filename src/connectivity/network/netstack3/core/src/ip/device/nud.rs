// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Neighbor unreachability detection.

use alloc::{
    collections::{
        hash_map::{Entry, HashMap},
        VecDeque,
    },
    vec::Vec,
};
use core::{fmt::Debug, hash::Hash, marker::PhantomData, num::NonZeroU8};

use assert_matches::assert_matches;
use derivative::Derivative;
use net_types::{ip::Ip, SpecifiedAddr};
use packet::{Buf, BufferMut, Serializer};
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

const MAX_PENDING_FRAMES: usize = 10;

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
    Incomplete { transmit_counter: Option<NonZeroU8>, pending_frames: VecDeque<Buf<Vec<u8>>> },
    Complete { link_address: LinkAddr },
}

impl<LinkAddr> DynamicNeighborState<LinkAddr> {
    fn new_incomplete_with_pending_frame(remaining_tries: u8, frame: Buf<Vec<u8>>) -> Self {
        DynamicNeighborState::Incomplete {
            transmit_counter: NonZeroU8::new(remaining_tries),
            pending_frames: [frame].into(),
        }
    }
}

#[cfg(test)]
pub(crate) mod testutil {
    use super::*;

    pub(crate) fn assert_dynamic_neighbor_with_addr<
        I: Ip,
        D: LinkDevice,
        C: NonSyncNudContext<I, D, SC::DeviceId>,
        SC: NudContext<I, D, C>,
    >(
        sync_ctx: &mut SC,
        device_id: SC::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        expected_link_addr: D::Address,
    ) {
        sync_ctx.with_nud_state_mut(&device_id, |NudState { neighbors }| {
            assert_matches!(
                neighbors.get(&neighbor),
                Some(
                    NeighborState::Dynamic(DynamicNeighborState::Complete { link_address })
                ) => {
                    assert_eq!(link_address, &expected_link_addr)
                }
            )
        })
    }

    pub(crate) fn assert_neighbor_unknown<
        I: Ip,
        D: LinkDevice,
        C: NonSyncNudContext<I, D, SC::DeviceId>,
        SC: NudContext<I, D, C>,
    >(
        sync_ctx: &mut SC,
        device_id: SC::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
    ) {
        sync_ctx.with_nud_state_mut(&device_id, |NudState { neighbors }| {
            assert_matches!(neighbors.get(&neighbor), None)
        })
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

/// The non-synchronized context for NUD.
pub(crate) trait NonSyncNudContext<I: Ip, D: LinkDevice, DeviceId>:
    TimerContext<NudTimerId<I, D, DeviceId>>
{
}

impl<I: Ip, D: LinkDevice, DeviceId, C: TimerContext<NudTimerId<I, D, DeviceId>>>
    NonSyncNudContext<I, D, DeviceId> for C
{
}

/// The execution context for NUD for a link device.
pub(crate) trait NudContext<I: Ip, D: LinkDevice, C: NonSyncNudContext<I, D, Self::DeviceId>>:
    DeviceIdContext<D>
{
    /// Returns the amount of time between neighbor probe/solicitation messages.
    fn retrans_timer(&self, device_id: &Self::DeviceId) -> NonZeroDuration;

    /// Calls the function with a mutable reference to the NUD state.
    fn with_nud_state_mut<O, F: FnOnce(&mut NudState<I, D::Address>) -> O>(
        &mut self,
        device_id: &Self::DeviceId,
        cb: F,
    ) -> O;

    /// Sends a neighbor probe/solicitation message.
    fn send_neighbor_solicitation(
        &mut self,
        ctx: &mut C,
        device_id: &Self::DeviceId,
        lookup_addr: SpecifiedAddr<I::Addr>,
    );
}

/// The execution context for NUD for a link device, with a buffer.
pub(crate) trait BufferNudContext<
    B: BufferMut,
    I: Ip,
    D: LinkDevice,
    C: NonSyncNudContext<I, D, Self::DeviceId>,
>: NudContext<I, D, C>
{
    /// Send an IP frame to the neighbor with the specified link address.
    fn send_ip_packet_to_neighbor_link_addr<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        device_id: &Self::DeviceId,
        neighbor_link_addr: D::Address,
        body: S,
    ) -> Result<(), S>;
}

/// An implementation of NUD for the IP layer.
pub(crate) trait NudIpHandler<I: Ip, C>: IpDeviceIdContext<I> {
    /// Handles an incoming neighbor probe message.
    ///
    /// For IPv6, this can be an NDP Neighbor Solicitation or an NDP Router
    /// Advertisement message.
    fn handle_neighbor_probe(
        &mut self,
        ctx: &mut C,
        device_id: &Self::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_addr: &[u8],
    );

    /// Handles an incoming neighbor confirmation message.
    ///
    /// For IPv6, this can be an NDP Neighbor Advertisement.
    fn handle_neighbor_confirmation(
        &mut self,
        ctx: &mut C,
        device_id: &Self::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_addr: &[u8],
    );

    /// Clears the neighbor table.
    fn flush_neighbor_table(&mut self, ctx: &mut C, device_id: &Self::DeviceId);
}

/// An implementation of NUD for a link device.
pub(crate) trait NudHandler<I: Ip, D: LinkDevice, C>: DeviceIdContext<D> {
    /// Sets a dynamic neighbor's entry state to the specified values in
    /// response to the source packet.
    fn set_dynamic_neighbor(
        &mut self,
        ctx: &mut C,
        device_id: &Self::DeviceId,
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
        device_id: &Self::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_addr: D::Address,
    );

    /// Clears the neighbor table.
    fn flush(&mut self, ctx: &mut C, device_id: &Self::DeviceId);
}

/// An implementation of NUD for a link device, with a buffer.
pub(crate) trait BufferNudHandler<B: BufferMut, I: Ip, D: LinkDevice, C>:
    NudHandler<I, D, C>
{
    /// Send an IP packet to the neighbor.
    ///
    /// If the neighbor's link address is not known, link address resolution
    /// is performed.
    fn send_ip_packet_to_neighbor<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        device_id: &Self::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        body: S,
    ) -> Result<(), S>;
}

impl<I: Ip, D: LinkDevice, C: NonSyncNudContext<I, D, SC::DeviceId>, SC: NudContext<I, D, C>>
    TimerHandler<C, NudTimerId<I, D, SC::DeviceId>> for SC
{
    fn handle_timer(
        &mut self,
        ctx: &mut C,
        NudTimerId { device_id, lookup_addr, _marker }: NudTimerId<I, D, SC::DeviceId>,
    ) {
        let do_solicit = self.with_nud_state_mut(&device_id, |NudState { neighbors }| {
            let transmit_counter = match neighbors
                .get_mut(&lookup_addr)
                .expect("timer fired for invalid entry")
            {
                NeighborState::Dynamic(DynamicNeighborState::Incomplete {
                    transmit_counter,
                    pending_frames: _,
                }) => transmit_counter,
                NeighborState::Static(_)
                | NeighborState::Dynamic(DynamicNeighborState::Complete { link_address: _ }) => {
                    unreachable!("timer should only fire for incomplete entry")
                }
            };

            match transmit_counter {
                Some(c) => {
                    *transmit_counter = NonZeroU8::new(c.get() - 1);
                    true
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
                    false
                }
            }
        });

        if do_solicit {
            solicit_neighbor(self, ctx, &device_id, lookup_addr)
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
    device_id: &SC::DeviceId,
    lookup_addr: SpecifiedAddr<I::Addr>,
) {
    sync_ctx.send_neighbor_solicitation(ctx, device_id, lookup_addr);

    let retrans_timer = sync_ctx.retrans_timer(device_id);
    assert_eq!(
        ctx.schedule_timer(
            retrans_timer.get(),
            NudTimerId { device_id: device_id.clone(), lookup_addr, _marker: PhantomData },
        ),
        None
    );
}

impl<
        I: Ip,
        D: LinkDevice,
        C: NonSyncNudContext<I, D, SC::DeviceId>,
        SC: BufferNudContext<Buf<Vec<u8>>, I, D, C>,
    > NudHandler<I, D, C> for SC
{
    fn set_dynamic_neighbor(
        &mut self,
        ctx: &mut C,
        device_id: &SC::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_address: D::Address,
        source: DynamicNeighborUpdateSource,
    ) {
        let pending_frames = self.with_nud_state_mut(device_id, |NudState { neighbors }| {
            match neighbors.entry(neighbor) {
                Entry::Vacant(e) => {
                    match source {
                        DynamicNeighborUpdateSource::Probe => {
                            let _: &mut NeighborState<_> =
                                e.insert(NeighborState::Dynamic(DynamicNeighborState::Complete {
                                    link_address,
                                }));
                        }
                        DynamicNeighborUpdateSource::Confirmation => {}
                    }

                    None
                }
                Entry::Occupied(e) => match e.into_mut() {
                    NeighborState::Dynamic(e) => {
                        match core::mem::replace(e, DynamicNeighborState::Complete { link_address })
                        {
                            DynamicNeighborState::Incomplete {
                                transmit_counter: _,
                                pending_frames,
                            } => {
                                assert_ne!(
                                    ctx.cancel_timer(NudTimerId {
                                        device_id: device_id.clone(),
                                        lookup_addr: neighbor,
                                        _marker: PhantomData,
                                    }),
                                    None,
                                    "previously incomplete entry for {} should have had a timer",
                                    neighbor
                                );

                                Some(pending_frames)
                            }
                            DynamicNeighborState::Complete { link_address: _ } => None,
                        }
                    }
                    NeighborState::Static(_) => None,
                },
            }
        });

        for body in pending_frames.into_iter().flatten() {
            let _: Result<(), _> =
                self.send_ip_packet_to_neighbor_link_addr(ctx, device_id, link_address, body);
        }
    }

    fn set_static_neighbor(
        &mut self,
        ctx: &mut C,
        device_id: &SC::DeviceId,
        neighbor: SpecifiedAddr<I::Addr>,
        link_address: D::Address,
    ) {
        let pending_frames = self.with_nud_state_mut(device_id, |NudState { neighbors }| {
            match neighbors.insert(neighbor, NeighborState::Static(link_address)) {
                Some(NeighborState::Dynamic(DynamicNeighborState::Incomplete {
                    transmit_counter: _,
                    pending_frames,
                })) => {
                    assert_ne!(
                        ctx.cancel_timer(NudTimerId {
                            device_id: device_id.clone(),
                            lookup_addr: neighbor,
                            _marker: PhantomData,
                        }),
                        None,
                        "previously incomplete entry for {} should have had a timer",
                        neighbor
                    );

                    Some(pending_frames)
                }
                None
                | Some(NeighborState::Static(_))
                | Some(NeighborState::Dynamic(DynamicNeighborState::Complete {
                    link_address: _,
                })) => None,
            }
        });

        for body in pending_frames.into_iter().flatten() {
            let _: Result<(), _> =
                self.send_ip_packet_to_neighbor_link_addr(ctx, device_id, link_address, body);
        }
    }

    fn flush(&mut self, ctx: &mut C, device_id: &Self::DeviceId) {
        let previously_incomplete = self.with_nud_state_mut(device_id, |NudState { neighbors }| {
            let mut previously_incomplete = Vec::new();

            neighbors.retain(|neighbor, state| {
                match state {
                    NeighborState::Dynamic(state) => {
                        match state {
                            DynamicNeighborState::Incomplete {
                                transmit_counter: _,
                                pending_frames: _,
                            } => {
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

            previously_incomplete
        });

        previously_incomplete.into_iter().for_each(|neighbor| {
            assert_ne!(
                ctx.cancel_timer(NudTimerId {
                    device_id: device_id.clone(),
                    lookup_addr: neighbor,
                    _marker: PhantomData,
                }),
                None,
                "previously incomplete entry for {} should have had a timer",
                neighbor
            );
        });
    }
}

impl<
        B: BufferMut,
        I: Ip,
        D: LinkDevice,
        C: NonSyncNudContext<I, D, SC::DeviceId>,
        SC: BufferNudContext<B, I, D, C> + BufferNudContext<Buf<Vec<u8>>, I, D, C>,
    > BufferNudHandler<B, I, D, C> for SC
{
    fn send_ip_packet_to_neighbor<S: Serializer<Buffer = B>>(
        &mut self,
        ctx: &mut C,
        device_id: &Self::DeviceId,
        lookup_addr: SpecifiedAddr<I::Addr>,
        body: S,
    ) -> Result<(), S> {
        enum Action<A, S> {
            Nothing,
            DoSolicit,
            SendPacketToLinkAddr(A, S),
        }

        let action = self.with_nud_state_mut(device_id, |NudState { neighbors }| {
            match neighbors.entry(lookup_addr) {
                Entry::Vacant(e) => {
                    let _: &mut NeighborState<_> = e.insert(NeighborState::Dynamic(
                        DynamicNeighborState::new_incomplete_with_pending_frame(
                            MAX_MULTICAST_SOLICIT - 1,
                            body.serialize_vec_outer()
                                .map_err(|(_err, s)| s)?
                                .map_a(|b| Buf::new(b.as_ref().to_vec(), ..))
                                .into_inner(),
                        ),
                    ));

                    Ok(Action::DoSolicit)
                }
                Entry::Occupied(e) => match e.into_mut() {
                    NeighborState::Dynamic(DynamicNeighborState::Incomplete {
                        transmit_counter: _,
                        pending_frames,
                    }) => {
                        // We don't accept new packets when the queue is full
                        // because earlier packets are more likely to initiate
                        // connections whereas later packets are more likely to
                        // carry data. E.g. A TCP SYN/SYN-ACK is likely to appear
                        // before a TCP segment with data and dropping the
                        // SYN/SYN-ACK may result in the TCP peer not processing the
                        // segment with data since the segment completing the
                        // handshake has not been received and handled yet.
                        if pending_frames.len() < MAX_PENDING_FRAMES {
                            pending_frames.push_back(
                                body.serialize_vec_outer()
                                    .map_err(|(_err, s)| s)?
                                    .map_a(|b| Buf::new(b.as_ref().to_vec(), ..))
                                    .into_inner(),
                            );
                        }

                        Ok(Action::Nothing)
                    }
                    NeighborState::Dynamic(DynamicNeighborState::Complete { link_address })
                    | NeighborState::Static(link_address) => {
                        Ok(Action::SendPacketToLinkAddr(*link_address, body))
                    }
                },
            }
        })?;

        match action {
            Action::Nothing => Ok(()),
            Action::DoSolicit => Ok(solicit_neighbor(self, ctx, device_id, lookup_addr)),
            Action::SendPacketToLinkAddr(link_addr, body) => {
                self.send_ip_packet_to_neighbor_link_addr(ctx, device_id, link_addr, body)
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use alloc::{vec, vec::Vec};
    use core::{convert::TryInto as _, num::NonZeroU64};

    use ip_test_macro::ip_test;
    use net_declare::{net_ip_v4, net_ip_v6};
    use net_types::{
        ip::{Ipv4, Ipv4Addr, Ipv6, Ipv6Addr},
        UnicastAddr, Witness as _,
    };
    use packet::{Buf, InnerPacketBuilder as _, Serializer as _};
    use packet_formats::{
        ethernet::{testutil::ETHERNET_MIN_BODY_LEN_NO_TAG, EtherType},
        icmp::{
            ndp::{
                options::NdpOptionBuilder, NeighborAdvertisement, NeighborSolicitation,
                OptionSequenceBuilder, RouterAdvertisement,
            },
            IcmpPacketBuilder, IcmpUnusedCode,
        },
        ip::Ipv6Proto,
        ipv6::Ipv6PacketBuilder,
        testutil::{parse_ethernet_frame, parse_icmp_packet_in_ip_packet_in_ethernet_frame},
    };
    use test_case::test_case;

    use super::*;
    use crate::{
        context::{
            testutil::{
                handle_timer_helper_with_sc_ref_mut, DummyCtx, DummyNonSyncCtx, DummySyncCtx,
                DummyTimerCtxExt as _,
            },
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
    }

    #[derive(Debug, PartialEq, Eq)]
    enum MockNudMessageMeta<I: Ip> {
        NeighborSolicitation { lookup_addr: SpecifiedAddr<I::Addr> },
        IpFrame { dst_link_address: DummyLinkAddress },
    }

    type MockCtx<I> =
        DummySyncCtx<MockNudContext<I, DummyLinkAddress>, MockNudMessageMeta<I>, DummyLinkDeviceId>;

    type MockNonSyncCtx<I> =
        DummyNonSyncCtx<NudTimerId<I, DummyLinkDevice, DummyLinkDeviceId>, (), ()>;

    impl<I: Ip> NudContext<I, DummyLinkDevice, MockNonSyncCtx<I>> for MockCtx<I> {
        fn retrans_timer(&self, &DummyLinkDeviceId: &DummyLinkDeviceId) -> NonZeroDuration {
            self.get_ref().retrans_timer
        }

        fn with_nud_state_mut<O, F: FnOnce(&mut NudState<I, DummyLinkAddress>) -> O>(
            &mut self,
            &DummyLinkDeviceId: &DummyLinkDeviceId,
            cb: F,
        ) -> O {
            cb(&mut self.get_mut().nud)
        }

        fn send_neighbor_solicitation(
            &mut self,
            ctx: &mut MockNonSyncCtx<I>,
            &DummyLinkDeviceId: &DummyLinkDeviceId,
            lookup_addr: SpecifiedAddr<I::Addr>,
        ) {
            self.send_frame(
                ctx,
                MockNudMessageMeta::NeighborSolicitation { lookup_addr },
                Buf::new(Vec::new(), ..),
            )
            .unwrap()
        }
    }

    impl<B: BufferMut, I: Ip> BufferNudContext<B, I, DummyLinkDevice, MockNonSyncCtx<I>>
        for MockCtx<I>
    {
        fn send_ip_packet_to_neighbor_link_addr<S: Serializer<Buffer = B>>(
            &mut self,
            ctx: &mut MockNonSyncCtx<I>,
            _device_id: &DummyLinkDeviceId,
            dst_link_address: DummyLinkAddress,
            body: S,
        ) -> Result<(), S> {
            self.send_frame(ctx, MockNudMessageMeta::IpFrame { dst_link_address }, body)
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
            sync_ctx.get_ref().nud.neighbors.get(&lookup_addr),
            Some(
                NeighborState::Dynamic(DynamicNeighborState::Complete { link_address })
                | NeighborState::Static(link_address)
            ) => {
                assert_eq!(link_address, &expected_link_addr)
            }
        );
        ctx.timer_ctx().assert_no_timers_installed();
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
            }));

        let link_addr = DummyLinkAddress([1]);
        NudHandler::set_dynamic_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &DummyLinkDeviceId,
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
    #[test_case(true; "set_with_dynamic")]
    #[test_case(false; "set_with_static")]
    fn pending_frames<I: Ip + TestIpExt>(dynamic: bool) {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::<I>::with_state(MockNudContext {
                retrans_timer: ONE_SECOND,
                nud: Default::default(),
            }));
        assert_eq!(sync_ctx.take_frames(), []);

        // Send up to the maximum number of pending frames to some neighbor
        // which requires resolution. This should cause all frames to be queued
        // pending resolution completion.
        const MAX_PENDING_FRAMES_U8: u8 = MAX_PENDING_FRAMES as u8;
        let expected_pending_frames = (0..MAX_PENDING_FRAMES_U8)
            .into_iter()
            .map(|i| Buf::new(vec![i], ..))
            .collect::<VecDeque<_>>();

        for body in expected_pending_frames.iter() {
            assert_eq!(
                BufferNudHandler::send_ip_packet_to_neighbor(
                    &mut sync_ctx,
                    &mut non_sync_ctx,
                    &DummyLinkDeviceId,
                    I::LOOKUP_ADDR1,
                    body.clone()
                ),
                Ok(())
            );
        }
        // Should have only sent out a single neighbor probe message.
        assert_eq!(
            sync_ctx.take_frames(),
            [(
                MockNudMessageMeta::NeighborSolicitation { lookup_addr: I::LOOKUP_ADDR1 },
                Vec::new()
            )]
        );
        assert_matches!(
            sync_ctx.get_ref().nud.neighbors.get(&I::LOOKUP_ADDR1),
            Some(NeighborState::Dynamic(DynamicNeighborState::Incomplete {
                transmit_counter: _,
                pending_frames
            })) => {
                assert_eq!(pending_frames, &expected_pending_frames);
            }
        );

        // The next frame should be dropped.
        assert_eq!(
            BufferNudHandler::send_ip_packet_to_neighbor(
                &mut sync_ctx,
                &mut non_sync_ctx,
                &DummyLinkDeviceId,
                I::LOOKUP_ADDR1,
                Buf::new([123], ..),
            ),
            Ok(())
        );
        assert_eq!(sync_ctx.take_frames(), []);
        assert_matches!(
            sync_ctx.get_ref().nud.neighbors.get(&I::LOOKUP_ADDR1),
            Some(NeighborState::Dynamic(DynamicNeighborState::Incomplete {
                transmit_counter: _,
                pending_frames
            })) => {
                assert_eq!(pending_frames, &expected_pending_frames);
            }
        );

        // Completing resolution should result in all queued packets to be sent.
        if dynamic {
            NudHandler::set_dynamic_neighbor(
                &mut sync_ctx,
                &mut non_sync_ctx,
                &DummyLinkDeviceId,
                I::LOOKUP_ADDR1,
                LINK_ADDR1,
                DynamicNeighborUpdateSource::Confirmation,
            );
        } else {
            NudHandler::set_static_neighbor(
                &mut sync_ctx,
                &mut non_sync_ctx,
                &DummyLinkDeviceId,
                I::LOOKUP_ADDR1,
                LINK_ADDR1,
            );
        }
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(
            sync_ctx.take_frames(),
            expected_pending_frames
                .into_iter()
                .map(|p| (
                    MockNudMessageMeta::IpFrame { dst_link_address: LINK_ADDR1 },
                    p.as_ref().to_vec()
                ))
                .collect::<Vec<_>>()
        );
    }

    #[ip_test]
    fn static_neighbor<I: Ip + TestIpExt>() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::<I>::with_state(MockNudContext {
                retrans_timer: ONE_SECOND,
                nud: Default::default(),
            }));

        NudHandler::set_static_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &DummyLinkDeviceId,
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
            &DummyLinkDeviceId,
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
            }));

        NudHandler::set_dynamic_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &DummyLinkDeviceId,
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
            &DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            LINK_ADDR2,
            DynamicNeighborUpdateSource::Probe,
        );
        check_lookup_has(&mut sync_ctx, &mut non_sync_ctx, I::LOOKUP_ADDR1, LINK_ADDR2);
        assert_eq!(sync_ctx.take_frames(), []);

        // A static entry may overwrite a dynamic entry.
        NudHandler::set_static_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            LINK_ADDR3,
        );
        check_lookup_has(&mut sync_ctx, &mut non_sync_ctx, I::LOOKUP_ADDR1, LINK_ADDR3);
        assert_eq!(sync_ctx.take_frames(), []);
    }

    #[ip_test]
    fn send_solicitation_on_lookup<I: Ip + TestIpExt>() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::<I>::with_state(MockNudContext {
                retrans_timer: ONE_SECOND,
                nud: Default::default(),
            }));
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.take_frames(), []);

        let mut pending_frames = VecDeque::new();
        let mut send_ip_packet_to_neighbor =
            |sync_ctx: &mut MockCtx<I>, non_sync_ctx: &mut MockNonSyncCtx<I>, body: u8| {
                let body = [body];
                assert_eq!(
                    BufferNudHandler::send_ip_packet_to_neighbor(
                        sync_ctx,
                        non_sync_ctx,
                        &DummyLinkDeviceId,
                        I::LOOKUP_ADDR1,
                        Buf::new(body, ..),
                    ),
                    Ok(())
                );

                pending_frames.push_back(Buf::new(body.to_vec(), ..));

                let MockNudContext { retrans_timer: _, nud } = sync_ctx.get_ref();
                assert_eq!(
                    nud.neighbors,
                    HashMap::from([(
                        I::LOOKUP_ADDR1,
                        NeighborState::Dynamic(DynamicNeighborState::Incomplete {
                            transmit_counter: NonZeroU8::new(MAX_MULTICAST_SOLICIT - 1),
                            pending_frames: pending_frames.clone(),
                        }),
                    )])
                );
                non_sync_ctx.timer_ctx().assert_timers_installed([(
                    NudTimerId {
                        device_id: DummyLinkDeviceId,
                        lookup_addr: I::LOOKUP_ADDR1,
                        _marker: PhantomData,
                    },
                    non_sync_ctx.now() + ONE_SECOND.get(),
                )]);
            };

        send_ip_packet_to_neighbor(&mut sync_ctx, &mut non_sync_ctx, 1);
        assert_eq!(
            sync_ctx.take_frames(),
            [(
                MockNudMessageMeta::NeighborSolicitation { lookup_addr: I::LOOKUP_ADDR1 },
                Vec::new()
            )]
        );

        send_ip_packet_to_neighbor(&mut sync_ctx, &mut non_sync_ctx, 2);
        assert_eq!(sync_ctx.take_frames(), []);

        // Complete link resolution.
        NudHandler::set_dynamic_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            LINK_ADDR1,
            DynamicNeighborUpdateSource::Confirmation,
        );
        check_lookup_has(&mut sync_ctx, &mut non_sync_ctx, I::LOOKUP_ADDR1, LINK_ADDR1);

        let MockNudContext { retrans_timer: _, nud } = sync_ctx.get_ref();
        assert_eq!(
            nud.neighbors,
            HashMap::from([(
                I::LOOKUP_ADDR1,
                NeighborState::Dynamic(DynamicNeighborState::Complete { link_address: LINK_ADDR1 }),
            )])
        );
        assert_eq!(
            sync_ctx.take_frames(),
            pending_frames
                .into_iter()
                .map(|f| (
                    MockNudMessageMeta::IpFrame { dst_link_address: LINK_ADDR1 },
                    f.as_ref().to_vec(),
                ))
                .collect::<Vec<_>>()
        );
    }

    #[ip_test]
    fn solicitation_failure<I: Ip + TestIpExt>() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::<I>::with_state(MockNudContext {
                retrans_timer: ONE_SECOND,
                nud: Default::default(),
            }));
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.take_frames(), []);

        let body = [1];
        let pending_frames = VecDeque::from([Buf::new(body.to_vec(), ..)]);
        assert_eq!(
            BufferNudHandler::send_ip_packet_to_neighbor(
                &mut sync_ctx,
                &mut non_sync_ctx,
                &DummyLinkDeviceId,
                I::LOOKUP_ADDR1,
                Buf::new(body, ..),
            ),
            Ok(())
        );

        let timer_id = NudTimerId {
            device_id: DummyLinkDeviceId,
            lookup_addr: I::LOOKUP_ADDR1,
            _marker: PhantomData,
        };
        for i in 1..=MAX_MULTICAST_SOLICIT {
            let MockNudContext { retrans_timer, nud } = sync_ctx.get_ref();
            let retrans_timer = retrans_timer.get();

            assert_eq!(
                nud.neighbors,
                HashMap::from([(
                    I::LOOKUP_ADDR1,
                    NeighborState::Dynamic(DynamicNeighborState::Incomplete {
                        transmit_counter: NonZeroU8::new(MAX_MULTICAST_SOLICIT - i),
                        pending_frames: pending_frames.clone(),
                    }),
                )])
            );

            non_sync_ctx
                .timer_ctx()
                .assert_timers_installed([(timer_id, non_sync_ctx.now() + ONE_SECOND.get())]);
            assert_eq!(
                sync_ctx.take_frames(),
                [(
                    MockNudMessageMeta::NeighborSolicitation { lookup_addr: I::LOOKUP_ADDR1 },
                    Vec::new()
                )]
            );

            assert_eq!(
                non_sync_ctx.trigger_timers_for(
                    retrans_timer,
                    handle_timer_helper_with_sc_ref_mut(&mut sync_ctx, TimerHandler::handle_timer),
                ),
                [timer_id]
            );
        }

        let MockNudContext { retrans_timer: _, nud } = sync_ctx.get_ref();
        assert_eq!(nud.neighbors, HashMap::new());
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.take_frames(), []);
    }

    #[ip_test]
    fn flush_entries<I: Ip + TestIpExt>() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::<I>::with_state(MockNudContext {
                retrans_timer: ONE_SECOND,
                nud: Default::default(),
            }));
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(sync_ctx.take_frames(), []);

        NudHandler::set_static_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &DummyLinkDeviceId,
            I::LOOKUP_ADDR1,
            LINK_ADDR1,
        );
        NudHandler::set_dynamic_neighbor(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &DummyLinkDeviceId,
            I::LOOKUP_ADDR2,
            LINK_ADDR2,
            DynamicNeighborUpdateSource::Probe,
        );
        let body = [3];
        let pending_frames = VecDeque::from([Buf::new(body.to_vec(), ..)]);
        assert_eq!(
            BufferNudHandler::send_ip_packet_to_neighbor(
                &mut sync_ctx,
                &mut non_sync_ctx,
                &DummyLinkDeviceId,
                I::LOOKUP_ADDR3,
                Buf::new(body, ..),
            ),
            Ok(())
        );

        let MockNudContext { retrans_timer: _, nud } = sync_ctx.get_ref();
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
                    NeighborState::Dynamic(DynamicNeighborState::Incomplete {
                        transmit_counter: NonZeroU8::new(MAX_MULTICAST_SOLICIT - 1),
                        pending_frames: pending_frames,
                    }),
                ),
            ])
        );
        non_sync_ctx.timer_ctx().assert_timers_installed([(
            NudTimerId {
                device_id: DummyLinkDeviceId,
                lookup_addr: I::LOOKUP_ADDR3,
                _marker: PhantomData,
            },
            non_sync_ctx.now() + ONE_SECOND.get(),
        )]);

        // Flushing the table should clear all dynamic entries and timers.
        NudHandler::flush(&mut sync_ctx, &mut non_sync_ctx, &DummyLinkDeviceId);
        let MockNudContext { retrans_timer: _, nud } = sync_ctx.get_ref();
        assert_eq!(
            nud.neighbors,
            HashMap::from([(I::LOOKUP_ADDR1, NeighborState::Static(LINK_ADDR1),),])
        );
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }

    fn assert_neighbors<
        I: Ip,
        D: LinkDevice,
        C: NonSyncNudContext<I, D, SC::DeviceId>,
        SC: NudContext<I, D, C>,
    >(
        sync_ctx: &mut SC,
        device_id: &SC::DeviceId,
        expected: HashMap<SpecifiedAddr<I::Addr>, NeighborState<D::Address>>,
    ) {
        sync_ctx.with_nud_state_mut(device_id, |NudState { neighbors }| {
            assert_eq!(*neighbors, expected)
        })
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

        let crate::testutil::DummyCtx { sync_ctx, mut non_sync_ctx } =
            crate::testutil::DummyCtx::default();
        let mut sync_ctx = &sync_ctx;
        let device_id =
            sync_ctx.state.device.add_ethernet_device(local_mac, Ipv6::MINIMUM_LINK_MTU.into());
        crate::ip::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
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
            &device_id,
            FrameDestination::Multicast,
            ra_packet_buf(&[][..]),
        );
        let link_device_id = device_id.clone().try_into().unwrap();
        assert_neighbors::<Ipv6, _, _, _>(&mut sync_ctx, &link_device_id, Default::default());

        // RA with a source link layer option should create a new entry.
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            FrameDestination::Multicast,
            ra_packet_buf(&options[..]),
        );
        assert_neighbors::<Ipv6, _, _, _>(
            &mut sync_ctx,
            &link_device_id,
            HashMap::from([(
                {
                    let src_ip: UnicastAddr<_> = src_ip.into_addr();
                    src_ip.into_specified()
                },
                NeighborState::Dynamic(DynamicNeighborState::Complete {
                    link_address: remote_mac.get(),
                }),
            )]),
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

        let crate::testutil::DummyCtx { sync_ctx, mut non_sync_ctx } =
            crate::testutil::DummyCtx::default();
        let mut sync_ctx = &sync_ctx;
        let device_id =
            sync_ctx.state.device.add_ethernet_device(local_mac, Ipv6::MINIMUM_LINK_MTU.into());
        crate::ip::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
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
            &device_id,
            FrameDestination::Multicast,
            na_packet_buf(false, false),
        );
        let link_device_id = device_id.clone().try_into().unwrap();
        assert_neighbors::<Ipv6, _, _, _>(&mut sync_ctx, &link_device_id, Default::default());
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            FrameDestination::Multicast,
            na_packet_buf(true, true),
        );
        assert_neighbors::<Ipv6, _, _, _>(&mut sync_ctx, &link_device_id, Default::default());

        assert_eq!(non_sync_ctx.take_frames(), []);

        // Trigger a neighbor solicitation to be sent.
        let body = [u8::MAX];
        let pending_frames = VecDeque::from([Buf::new(body.to_vec(), ..)]);
        assert_matches!(
            BufferNudHandler::<_, Ipv6, _, _>::send_ip_packet_to_neighbor(
                &mut sync_ctx,
                &mut non_sync_ctx,
                &link_device_id,
                neighbor_ip.into_specified(),
                Buf::new(body, ..),
            ),
            Ok(())
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
        assert_neighbors::<Ipv6, _, _, _>(
            &mut sync_ctx,
            &link_device_id,
            HashMap::from([(
                neighbor_ip.into_specified(),
                NeighborState::Dynamic(DynamicNeighborState::Incomplete {
                    transmit_counter: NonZeroU8::new(MAX_MULTICAST_SOLICIT - 1),
                    pending_frames: pending_frames,
                }),
            )]),
        );

        // A Neighbor advertisement should now update the entry.
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            FrameDestination::Multicast,
            na_packet_buf(true, true),
        );
        assert_neighbors::<Ipv6, _, _, _>(
            &mut sync_ctx,
            &link_device_id,
            HashMap::from([(
                neighbor_ip.into_specified(),
                NeighborState::Dynamic(DynamicNeighborState::Complete {
                    link_address: remote_mac.get(),
                }),
            )]),
        );
        assert_matches!(
            &non_sync_ctx.take_frames()[..],
            [(got_device_id, got_frame)] => {
                assert_eq!(got_device_id, &device_id);

                let (payload, src_mac, dst_mac, ether_type) = parse_ethernet_frame(got_frame)
                    .unwrap();
                assert_eq!(src_mac, local_mac.get());
                assert_eq!(dst_mac, remote_mac.get());
                assert_eq!(ether_type, Some(EtherType::Ipv6));
                assert_eq!(payload, {
                    let mut expected_body = [0; ETHERNET_MIN_BODY_LEN_NO_TAG];
                    expected_body[..body.len()].copy_from_slice(&body);
                    expected_body
                });
            }
        );

        // Disabling the device should clear the neighbor table.
        update_ipv6_configuration(&mut sync_ctx, &mut non_sync_ctx, &device_id, |config| {
            config.ip_config.ip_enabled = false;
        });
        assert_neighbors::<Ipv6, _, _, _>(&mut sync_ctx, &link_device_id, HashMap::new());
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }
}
