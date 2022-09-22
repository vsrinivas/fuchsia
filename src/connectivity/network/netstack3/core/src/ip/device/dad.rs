// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Duplicate Address Detection.

use core::{num::NonZeroU8, time::Duration};

use net_types::{
    ip::{Ipv6, Ipv6Addr},
    MulticastAddr, UnicastAddr, Witness as _,
};
use packet_formats::icmp::ndp::NeighborSolicitation;

use crate::{
    context::{EventContext, TimerContext},
    ip::{device::state::AddressState, IpDeviceIdContext},
};

/// A timer ID for duplicate address detection.
#[derive(Copy, Clone, Eq, PartialEq, Debug, Hash)]
pub(crate) struct DadTimerId<DeviceId> {
    pub(crate) device_id: DeviceId,
    pub(crate) addr: UnicastAddr<Ipv6Addr>,
}

/// The IP device context provided to DAD.
pub(super) trait Ipv6DeviceDadContext<C>: IpDeviceIdContext<Ipv6> {
    /// Calls the callback function with a mutable reference to the address's
    /// state and the NDP retransmission timer configured on the device if the
    /// address exists on the interface.
    fn with_address_state_and_retrans_timer<
        O,
        F: FnOnce(Option<(&mut AddressState, Duration)>) -> O,
    >(
        &mut self,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
        cb: F,
    ) -> O;
}

/// The IP layer context provided to DAD.
pub(super) trait Ipv6LayerDadContext<C>: IpDeviceIdContext<Ipv6> {
    /// Sends an NDP Neighbor Solicitation message for DAD to the local-link.
    ///
    /// The message will be sent with the unspecified (all-zeroes) source
    /// address.
    fn send_dad_packet(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        dst_ip: MulticastAddr<Ipv6Addr>,
        message: NeighborSolicitation,
    ) -> Result<(), ()>;
}

#[derive(Debug, Eq, Hash, PartialEq)]
/// Events generated by duplicate address detection.
pub enum DadEvent<DeviceId> {
    /// Duplicate address detection completed and the address is assigned.
    AddressAssigned {
        /// Device the address belongs to.
        device: DeviceId,
        /// The address that moved to the assigned state.
        addr: UnicastAddr<Ipv6Addr>,
    },
}

/// The non-synchronized execution context for DAD.
pub(super) trait DadNonSyncContext<DeviceId>:
    TimerContext<DadTimerId<DeviceId>> + EventContext<DadEvent<DeviceId>>
{
}
impl<DeviceId, C: TimerContext<DadTimerId<DeviceId>> + EventContext<DadEvent<DeviceId>>>
    DadNonSyncContext<DeviceId> for C
{
}

/// The execution context for DAD.
pub(super) trait DadContext<C: DadNonSyncContext<Self::DeviceId>>:
    Ipv6DeviceDadContext<C> + Ipv6LayerDadContext<C>
{
}

impl<C: DadNonSyncContext<SC::DeviceId>, SC: Ipv6DeviceDadContext<C> + Ipv6LayerDadContext<C>>
    DadContext<C> for SC
{
}

/// An implementation for Duplicate Address Detection.
pub(crate) trait DadHandler<C>: IpDeviceIdContext<Ipv6> {
    /// Do duplicate address detection.
    ///
    /// # Panics
    ///
    /// Panics if tentative state for the address is not found.
    fn do_duplicate_address_detection(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    );

    /// Stops duplicate address detection.
    ///
    /// Does nothing if DAD is not being performed on the address.
    fn stop_duplicate_address_detection(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    );

    /// Handles a timer.
    // TODO(https://fxbug.dev/101399): Replace this with a `TimerHandler` bound.
    fn handle_timer(
        &mut self,
        ctx: &mut C,
        DadTimerId { device_id, addr }: DadTimerId<Self::DeviceId>,
    ) {
        self.do_duplicate_address_detection(ctx, device_id, addr)
    }
}

impl<C: DadNonSyncContext<SC::DeviceId>, SC: DadContext<C>> DadHandler<C> for SC {
    fn do_duplicate_address_detection(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    ) {
        let send_msg = self.with_address_state_and_retrans_timer(device_id, addr, |state| {
            let (state, retrans_timer) = state.unwrap_or_else(|| panic!("expected address to exist; addr={}", addr));

            let remaining = match state {
                AddressState::Tentative { dad_transmits_remaining } => dad_transmits_remaining,
                AddressState::Assigned => {
                    panic!("expected address to be tentative; addr={}", addr)
                }
            };

            match remaining {
                None => {
                    *state = AddressState::Assigned;
                    ctx.on_event(DadEvent::AddressAssigned { device: device_id, addr });
                    false
                }
                Some(non_zero_remaining) => {
                    *remaining = NonZeroU8::new(non_zero_remaining.get() - 1);

                    // Per RFC 4862 section 5.1,
                    //
                    //   DupAddrDetectTransmits ...
                    //      Autoconfiguration also assumes the presence of the variable
                    //      RetransTimer as defined in [RFC4861]. For autoconfiguration
                    //      purposes, RetransTimer specifies the delay between
                    //      consecutive Neighbor Solicitation transmissions performed
                    //      during Duplicate Address Detection (if
                    //      DupAddrDetectTransmits is greater than 1), as well as the
                    //      time a node waits after sending the last Neighbor
                    //      Solicitation before ending the Duplicate Address Detection
                    //      process.
                    assert_eq!(
                        ctx.schedule_timer(retrans_timer, DadTimerId { device_id, addr }),
                        None,
                        "Should not have a DAD timer set when performing DAD work; addr={}, device_id={}",
                        addr,
                        device_id
                    );

                    true
                }
            }
        });

        if !send_msg {
            return;
        }

        // Do not include the source link-layer option when the NS
        // message as DAD messages are sent with the unspecified source
        // address which must not hold a source link-layer option.
        //
        // As per RFC 4861 section 4.3,
        //
        //   Possible options:
        //
        //      Source link-layer address
        //           The link-layer address for the sender. MUST NOT be
        //           included when the source IP address is the
        //           unspecified address. Otherwise, on link layers
        //           that have addresses this option MUST be included in
        //           multicast solicitations and SHOULD be included in
        //           unicast solicitations.
        //
        // TODO(https://fxbug.dev/85055): Either panic or guarantee that this error
        // can't happen statically.
        let dst_ip = addr.to_solicited_node_address();
        let _: Result<(), _> =
            self.send_dad_packet(ctx, device_id, dst_ip, NeighborSolicitation::new(addr.get()));
    }

    fn stop_duplicate_address_detection(
        &mut self,
        ctx: &mut C,
        device_id: Self::DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    ) {
        let _: Option<C::Instant> = ctx.cancel_timer(DadTimerId { device_id, addr });
    }
}

#[cfg(test)]
mod tests {
    use packet::EmptyBuf;
    use packet_formats::icmp::ndp::Options;

    use super::*;
    use crate::{
        context::{
            testutil::{DummyCtx, DummyNonSyncCtx, DummySyncCtx, DummyTimerCtxExt as _},
            FrameContext as _, InstantContext as _,
        },
        ip::testutil::DummyDeviceId,
    };

    struct MockDadContext {
        addr: UnicastAddr<Ipv6Addr>,
        state: AddressState,
        retrans_timer: Duration,
    }

    #[derive(Debug)]
    struct DadMessageMeta {
        dst_ip: MulticastAddr<Ipv6Addr>,
        message: NeighborSolicitation,
    }

    type MockNonSyncCtx = DummyNonSyncCtx<DadTimerId<DummyDeviceId>, DadEvent<DummyDeviceId>, ()>;

    type MockCtx = DummySyncCtx<MockDadContext, DadMessageMeta, DummyDeviceId>;

    impl Ipv6DeviceDadContext<MockNonSyncCtx> for MockCtx {
        fn with_address_state_and_retrans_timer<
            O,
            F: FnOnce(Option<(&mut AddressState, Duration)>) -> O,
        >(
            &mut self,
            DummyDeviceId: DummyDeviceId,
            request_addr: UnicastAddr<Ipv6Addr>,
            cb: F,
        ) -> O {
            let MockDadContext { addr, state, retrans_timer } = self.get_mut();
            cb((*addr == request_addr).then(|| (state, *retrans_timer)))
        }
    }

    impl Ipv6LayerDadContext<MockNonSyncCtx> for MockCtx {
        fn send_dad_packet(
            &mut self,
            ctx: &mut MockNonSyncCtx,
            DummyDeviceId: DummyDeviceId,
            dst_ip: MulticastAddr<Ipv6Addr>,
            message: NeighborSolicitation,
        ) -> Result<(), ()> {
            self.send_frame(ctx, DadMessageMeta { dst_ip, message }, EmptyBuf)
                .map_err(|EmptyBuf| ())
        }
    }

    const DAD_ADDRESS: UnicastAddr<Ipv6Addr> =
        unsafe { UnicastAddr::new_unchecked(Ipv6Addr::new([0xa, 0, 0, 0, 0, 0, 0, 1])) };
    const OTHER_ADDRESS: UnicastAddr<Ipv6Addr> =
        unsafe { UnicastAddr::new_unchecked(Ipv6Addr::new([0xa, 0, 0, 0, 0, 0, 0, 2])) };

    #[test]
    #[should_panic(expected = "expected address to exist")]
    fn panic_unknown_address() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::with_state(MockDadContext {
                addr: DAD_ADDRESS,
                state: AddressState::Tentative { dad_transmits_remaining: None },
                retrans_timer: Duration::default(),
            }));
        DadHandler::do_duplicate_address_detection(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            OTHER_ADDRESS,
        );
    }

    #[test]
    #[should_panic(expected = "expected address to be tentative")]
    fn panic_non_tentative_address() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::with_state(MockDadContext {
                addr: DAD_ADDRESS,
                state: AddressState::Assigned,
                retrans_timer: Duration::default(),
            }));
        DadHandler::do_duplicate_address_detection(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            DAD_ADDRESS,
        );
    }

    #[test]
    fn dad_disabled() {
        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::with_state(MockDadContext {
                addr: DAD_ADDRESS,
                state: AddressState::Tentative { dad_transmits_remaining: None },
                retrans_timer: Duration::default(),
            }));
        DadHandler::do_duplicate_address_detection(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            DAD_ADDRESS,
        );
        let MockDadContext { addr: _, state, retrans_timer: _ } = sync_ctx.get_ref();
        assert_eq!(*state, AddressState::Assigned);
        assert_eq!(
            non_sync_ctx.take_events(),
            &[DadEvent::AddressAssigned { device: DummyDeviceId, addr: DAD_ADDRESS }][..]
        );
    }

    const DAD_TIMER_ID: DadTimerId<DummyDeviceId> =
        DadTimerId { addr: DAD_ADDRESS, device_id: DummyDeviceId };

    fn check_dad(
        sync_ctx: &MockCtx,
        non_sync_ctx: &MockNonSyncCtx,
        frames_len: usize,
        dad_transmits_remaining: Option<NonZeroU8>,
        retrans_timer: Duration,
    ) {
        let MockDadContext { addr: _, state, retrans_timer: _ } = sync_ctx.get_ref();
        assert_eq!(*state, AddressState::Tentative { dad_transmits_remaining });
        let frames = sync_ctx.frames();
        assert_eq!(frames.len(), frames_len, "frames = {:?}", frames);
        let (DadMessageMeta { dst_ip, message }, frame) =
            frames.last().expect("should have transmitted a frame");

        assert_eq!(*dst_ip, DAD_ADDRESS.to_solicited_node_address());
        assert_eq!(*message, NeighborSolicitation::new(DAD_ADDRESS.get()));

        let options = Options::parse(&frame[..]).expect("parse NDP options");
        assert_eq!(options.iter().count(), 0);
        non_sync_ctx
            .timer_ctx()
            .assert_timers_installed([(DAD_TIMER_ID, non_sync_ctx.now() + retrans_timer)]);
    }

    #[test]
    fn perform_dad() {
        const DAD_TRANSMITS_REQUIRED: u8 = 2;
        const RETRANS_TIMER: Duration = Duration::from_secs(1);

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::with_state(MockDadContext {
                addr: DAD_ADDRESS,
                state: AddressState::Tentative {
                    dad_transmits_remaining: NonZeroU8::new(DAD_TRANSMITS_REQUIRED),
                },
                retrans_timer: RETRANS_TIMER,
            }));
        DadHandler::do_duplicate_address_detection(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            DAD_ADDRESS,
        );

        for count in 0..=1u8 {
            check_dad(
                &sync_ctx,
                &non_sync_ctx,
                usize::from(count + 1),
                NonZeroU8::new(DAD_TRANSMITS_REQUIRED - count - 1),
                RETRANS_TIMER,
            );
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&mut sync_ctx, DadHandler::handle_timer),
                Some(DAD_TIMER_ID)
            );
        }
        let MockDadContext { addr: _, state, retrans_timer: _ } = sync_ctx.get_ref();
        assert_eq!(*state, AddressState::Assigned);
        assert_eq!(
            non_sync_ctx.take_events(),
            &[DadEvent::AddressAssigned { device: DummyDeviceId, addr: DAD_ADDRESS }][..]
        );
    }

    #[test]
    fn stop_dad() {
        const DAD_TRANSMITS_REQUIRED: u8 = 2;
        const RETRANS_TIMER: Duration = Duration::from_secs(2);

        let DummyCtx { mut sync_ctx, mut non_sync_ctx } =
            DummyCtx::with_sync_ctx(MockCtx::with_state(MockDadContext {
                addr: DAD_ADDRESS,
                state: AddressState::Tentative {
                    dad_transmits_remaining: NonZeroU8::new(DAD_TRANSMITS_REQUIRED),
                },
                retrans_timer: RETRANS_TIMER,
            }));
        DadHandler::do_duplicate_address_detection(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            DAD_ADDRESS,
        );
        check_dad(
            &sync_ctx,
            &non_sync_ctx,
            1,
            NonZeroU8::new(DAD_TRANSMITS_REQUIRED - 1),
            RETRANS_TIMER,
        );

        DadHandler::stop_duplicate_address_detection(
            &mut sync_ctx,
            &mut non_sync_ctx,
            DummyDeviceId,
            DAD_ADDRESS,
        );
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }
}
