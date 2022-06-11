// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! IPv6 Router Solicitation as defined by [RFC 4861 section 6.3.7].
//!
//! [RFC 4861 section 6.3.7]: https://datatracker.ietf.org/doc/html/rfc4861#section-6.3.7

use core::{num::NonZeroU8, time::Duration};

use net_types::ip::Ipv6;
use packet::{EmptyBuf, InnerPacketBuilder as _, Serializer};
use packet_formats::icmp::ndp::{
    options::NdpOptionBuilder, OptionSequenceBuilder, RouterSolicitation,
};
use rand::Rng as _;

use crate::{
    context::{RngContext, TimerContext},
    ip::IpDeviceIdContext,
};

/// Amount of time to wait after sending `MAX_RTR_SOLICITATIONS` Router
/// Solicitation messages before determining that there are no routers on the
/// link for the purpose of IPv6 Stateless Address Autoconfiguration if no
/// Router Advertisement messages have been received as defined in [RFC 4861
/// section 10].
///
/// This parameter is also used when a host sends its initial Router
/// Solicitation message, as per [RFC 4861 section 6.3.7]. Before a node sends
/// an initial solicitation, it SHOULD delay the transmission for a random
/// amount of time between 0 and `MAX_RTR_SOLICITATION_DELAY`. This serves to
/// alleviate congestion when many hosts start up on a link at the same time.
///
/// [RFC 4861 section 10]: https://tools.ietf.org/html/rfc4861#section-10
/// [RFC 4861 section 6.3.7]: https://tools.ietf.org/html/rfc4861#section-6.3.7
pub(crate) const MAX_RTR_SOLICITATION_DELAY: Duration = Duration::from_secs(1);

/// Minimum duration between router solicitation messages as defined in [RFC
/// 4861 section 10].
///
/// [RFC 4861 section 10]: https://tools.ietf.org/html/rfc4861#section-10
pub(crate) const RTR_SOLICITATION_INTERVAL: Duration = Duration::from_secs(4);

#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) struct RsTimerId<DeviceId> {
    pub(crate) device_id: DeviceId,
}

/// The IP device context provided to RS.
pub(super) trait Ipv6DeviceRsContext<C>: IpDeviceIdContext<Ipv6> {
    /// Gets the maximum number of router solicitations to send when
    /// performing router solicitation.
    fn get_max_router_solicitations(&self, device_id: Self::DeviceId) -> Option<NonZeroU8>;

    /// Gets a mutable reference to the remaining number of router
    /// solicitations.
    fn get_router_soliciations_remaining_mut(
        &mut self,
        device_id: Self::DeviceId,
    ) -> &mut Option<NonZeroU8>;

    /// Gets the device's link-layer address bytes, if the device supports
    /// link-layer addressing.
    fn get_link_layer_addr_bytes(&self, device_id: Self::DeviceId) -> Option<&[u8]>;
}

/// The IP layer context provided to RS.
pub(super) trait Ipv6LayerRsContext<C>: IpDeviceIdContext<Ipv6> {
    /// Sends an NDP Router Solicitation to the local-link.
    fn send_rs_packet<S: Serializer<Buffer = EmptyBuf>>(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        message: RouterSolicitation,
        body: S,
    ) -> Result<(), S>;
}

/// The execution context for RS
pub(super) trait RsContext<C>:
    Ipv6DeviceRsContext<C>
    + Ipv6LayerRsContext<C>
    + TimerContext<RsTimerId<Self::DeviceId>>
    + RngContext
{
}

impl<
        C,
        SC: Ipv6DeviceRsContext<C>
            + Ipv6LayerRsContext<C>
            + TimerContext<RsTimerId<SC::DeviceId>>
            + RngContext,
    > RsContext<C> for SC
{
}

/// An implementation of Router Solicitation.
pub(crate) trait RsHandler<C>: IpDeviceIdContext<Ipv6> {
    /// Starts router solicitation.
    fn start_router_solicitation(&mut self, ctx: &mut C, device_id: Self::DeviceId);

    /// Stops router solicitation.
    ///
    /// Does nothing if router solicitaiton is not being performed
    fn stop_router_solicitation(&mut self, ctx: &mut C, device_id: Self::DeviceId);

    /// Handles a timer.
    // TODO: Replace this with a `TimerHandler` bound.
    fn handle_timer(&mut self, ctx: &mut C, id: RsTimerId<Self::DeviceId>);
}

impl<C, SC: RsContext<C>> RsHandler<C> for SC {
    fn start_router_solicitation(&mut self, _ctx: &mut C, device_id: Self::DeviceId) {
        let max_router_solicitations = self.get_max_router_solicitations(device_id);
        *self.get_router_soliciations_remaining_mut(device_id) = max_router_solicitations;

        match max_router_solicitations {
            None => {}
            Some(_) => {
                // As per RFC 4861 section 6.3.7, delay the first transmission for a
                // random amount of time between 0 and `MAX_RTR_SOLICITATION_DELAY` to
                // alleviate congestion when many hosts start up on a link at the same
                // time.
                let delay =
                    self.rng_mut().gen_range(Duration::new(0, 0)..MAX_RTR_SOLICITATION_DELAY);
                assert_eq!(self.schedule_timer(delay, RsTimerId { device_id },), None);
            }
        }
    }

    fn stop_router_solicitation(&mut self, _ctx: &mut C, device_id: Self::DeviceId) {
        let _: Option<SC::Instant> = self.cancel_timer(RsTimerId { device_id });
    }

    fn handle_timer(&mut self, ctx: &mut C, RsTimerId { device_id }: RsTimerId<SC::DeviceId>) {
        do_router_solicitation(self, ctx, device_id)
    }
}

/// Solicit routers once and schedule next message.
fn do_router_solicitation<C, SC: RsContext<C>>(
    sync_ctx: &mut SC,
    ctx: &mut C,
    device_id: SC::DeviceId,
) {
    let src_ll = sync_ctx.get_link_layer_addr_bytes(device_id).map(|a| a.to_vec());

    // TODO(https://fxbug.dev/85055): Either panic or guarantee that this error
    // can't happen statically.
    let _: Result<(), _> = sync_ctx.send_rs_packet(
        ctx,
        device_id,
        RouterSolicitation::default(),
        OptionSequenceBuilder::new(
            src_ll.as_ref().map(AsRef::as_ref).map(NdpOptionBuilder::SourceLinkLayerAddress).iter(),
        )
        .into_serializer(),
    );

    let remaining = sync_ctx.get_router_soliciations_remaining_mut(device_id);
    *remaining = NonZeroU8::new(
        remaining
            .expect("should only send a router solicitations when at least one is remaining")
            .get()
            - 1,
    );
    match *remaining {
        None => {}
        Some(NonZeroU8 { .. }) => {
            assert_eq!(
                sync_ctx.schedule_timer(RTR_SOLICITATION_INTERVAL, RsTimerId { device_id },),
                None
            );
        }
    }
}

#[cfg(test)]
mod tests {
    use alloc::vec;
    use packet_formats::icmp::ndp::{options::NdpOption, Options};
    use test_case::test_case;

    use super::*;
    use crate::{
        context::{
            testutil::{DummyCtx, DummySyncCtx, DummyTimerCtxExt as _},
            FrameContext as _, InstantContext as _,
        },
        ip::DummyDeviceId,
    };

    struct MockRsContext<'a> {
        max_router_solicitations: Option<NonZeroU8>,
        router_soliciations_remaining: Option<NonZeroU8>,
        link_layer_bytes: Option<&'a [u8]>,
    }

    #[derive(Debug, PartialEq)]
    struct RsMessageMeta {
        message: RouterSolicitation,
    }

    type MockCtx<'a> =
        DummySyncCtx<MockRsContext<'a>, RsTimerId<DummyDeviceId>, RsMessageMeta, (), DummyDeviceId>;

    impl<'a> Ipv6DeviceRsContext<()> for MockCtx<'a> {
        fn get_max_router_solicitations(&self, DummyDeviceId: DummyDeviceId) -> Option<NonZeroU8> {
            let MockRsContext {
                max_router_solicitations,
                router_soliciations_remaining: _,
                link_layer_bytes: _,
            } = self.get_ref();
            *max_router_solicitations
        }

        fn get_router_soliciations_remaining_mut(
            &mut self,
            DummyDeviceId: DummyDeviceId,
        ) -> &mut Option<NonZeroU8> {
            let MockRsContext {
                max_router_solicitations: _,
                router_soliciations_remaining,
                link_layer_bytes: _,
            } = self.get_mut();
            router_soliciations_remaining
        }

        fn get_link_layer_addr_bytes(&self, DummyDeviceId: DummyDeviceId) -> Option<&[u8]> {
            let MockRsContext {
                max_router_solicitations: _,
                router_soliciations_remaining: _,
                link_layer_bytes,
            } = self.get_ref();
            *link_layer_bytes
        }
    }

    impl<'a> Ipv6LayerRsContext<()> for MockCtx<'a> {
        fn send_rs_packet<S: Serializer<Buffer = EmptyBuf>>(
            &mut self,
            _ctx: &mut (),
            DummyDeviceId: DummyDeviceId,
            message: RouterSolicitation,
            body: S,
        ) -> Result<(), S> {
            self.send_frame(&mut (), RsMessageMeta { message }, body)
        }
    }

    const RS_TIMER_ID: RsTimerId<DummyDeviceId> = RsTimerId { device_id: DummyDeviceId };

    #[test]
    fn stop_router_solicitation() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::with_state(MockRsContext {
                max_router_solicitations: NonZeroU8::new(1),
                router_soliciations_remaining: None,
                link_layer_bytes: None,
            }));
        RsHandler::start_router_solicitation(&mut sync_ctx, &mut non_sync_ctx, DummyDeviceId);

        let now = sync_ctx.now();
        sync_ctx
            .timer_ctx()
            .assert_timers_installed([(RS_TIMER_ID, now..=now + MAX_RTR_SOLICITATION_DELAY)]);

        RsHandler::stop_router_solicitation(&mut sync_ctx, &mut non_sync_ctx, DummyDeviceId);
        sync_ctx.timer_ctx().assert_no_timers_installed();

        assert_eq!(sync_ctx.frames(), &[][..]);
    }

    #[test_case(0, None; "disabled")]
    #[test_case(1, None; "once_without_source_link_layer_option")]
    #[test_case(1, Some((&[1, 2, 3, 4, 5, 6], 0)); "once_with_mac_address_source_link_layer_option")]
    #[test_case(1, Some((&[1, 2, 3, 4, 5], 1)); "once_with_short_address_source_link_layer_option")]
    #[test_case(1, Some((&[1, 2, 3, 4, 5, 6, 7], 7)); "once_with_long_address_source_link_layer_option")]
    fn perform_router_solicitation(
        max_router_solicitations: u8,
        link_layer_bytes: Option<(&[u8], u8)>,
    ) {
        // NDP options have lengths in 8 byte increments so the option may be
        // padded if the address does not fit cleanly in the option.
        let (link_layer_bytes, expected_pad_bytes) =
            link_layer_bytes.map_or((None, 0), |(a, b)| (Some(a), b));

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::with_state(MockRsContext {
                max_router_solicitations: NonZeroU8::new(max_router_solicitations),
                router_soliciations_remaining: None,
                link_layer_bytes,
            }));
        RsHandler::start_router_solicitation(&mut sync_ctx, &mut non_sync_ctx, DummyDeviceId);

        assert_eq!(sync_ctx.frames(), &[][..]);

        let mut duration = MAX_RTR_SOLICITATION_DELAY;
        for i in 0..max_router_solicitations {
            assert_eq!(
                *sync_ctx.get_router_soliciations_remaining_mut(DummyDeviceId),
                NonZeroU8::new(max_router_solicitations - i)
            );
            let now = sync_ctx.now();
            sync_ctx.timer_ctx().assert_timers_installed([(RS_TIMER_ID, now..=now + duration)]);

            assert_eq!(
                sync_ctx.trigger_next_timer(&mut non_sync_ctx, RsHandler::handle_timer),
                Some(RS_TIMER_ID)
            );
            let frames = sync_ctx.frames();
            assert_eq!(frames.len(), usize::from(i + 1), "frames = {:?}", frames);
            let (RsMessageMeta { message }, frame) =
                frames.last().expect("should have transmitted a frame");
            assert_eq!(*message, RouterSolicitation::default());
            let options = Options::parse(&frame[..]).expect("parse NDP options");
            let sll_bytes = options.iter().find_map(|o| match o {
                NdpOption::SourceLinkLayerAddress(a) => Some(a),
                o => panic!("unexpected NDP option = {:?}", o),
            });

            match (sll_bytes, link_layer_bytes) {
                (Some(sll_bytes), Some(link_layer_bytes)) => {
                    assert_eq!(&sll_bytes[..link_layer_bytes.len()], link_layer_bytes);
                    assert_eq!(
                        sll_bytes[link_layer_bytes.len()..],
                        vec![0; expected_pad_bytes.into()]
                    );
                }
                (None, None) => {}
                (sll_bytes, link_layer_bytes) => panic!(
                    "got sll_bytes = {:?}, want = {:?} with {} padding bytes",
                    sll_bytes, link_layer_bytes, expected_pad_bytes
                ),
            }

            duration = RTR_SOLICITATION_INTERVAL;
        }

        sync_ctx.timer_ctx().assert_no_timers_installed();
        assert_eq!(*sync_ctx.get_router_soliciations_remaining_mut(DummyDeviceId), None);
        let frames = sync_ctx.frames();
        assert_eq!(frames.len(), usize::from(max_router_solicitations), "frames = {:?}", frames);
    }
}
