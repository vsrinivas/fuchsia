// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(dead_code, unused_imports, unused_macros)]

use core::time::Duration;
use std::convert::TryInto as _;

use crate::{testutil::DummyEventDispatcher, Ctx, DeviceId, TimerId};
use arbitrary::{Arbitrary, Unstructured};
use net_declare::net_mac;
use net_types::UnicastAddr;
use packet::serialize::Buf;

fn initialize_logging() {
    #[cfg(fuzz_logging)]
    {
        static LOGGER_ONCE: core::sync::atomic::AtomicBool =
            core::sync::atomic::AtomicBool::new(true);

        // This function gets called on every fuzz iteration, but we only need to set up logging the
        // first time.
        if LOGGER_ONCE.swap(false, core::sync::atomic::Ordering::AcqRel) {
            fuchsia_syslog::init().expect("couldn't initialize logging");
            log::info!("trace logs enabled");
            fuchsia_syslog::set_severity(fuchsia_syslog::levels::TRACE);
        };
    }
}

/// Wrapper around Duration that limits the range of possible values. This keeps the fuzzer
/// from generating Duration values that, when added up, would cause overflow.
struct SmallDuration(Duration);

impl Arbitrary for SmallDuration {
    fn arbitrary(u: &mut Unstructured<'_>) -> arbitrary::Result<Self> {
        // The maximum time increment to advance by in a single step.
        const MAX_DURATION: Duration = Duration::from_secs(60 * 60);

        let max = MAX_DURATION.as_nanos().try_into().unwrap();
        let nanos = u.int_in_range::<u64>(0..=max)?;
        Ok(Self(Duration::from_nanos(nanos)))
    }
}

#[derive(Arbitrary)]
enum FuzzAction {
    ReceiveFrame(Vec<u8>),
    AdvanceTime(SmallDuration),
}

#[derive(Arbitrary)]
pub(crate) struct FuzzInput {
    actions: Vec<FuzzAction>,
}

fn dispatch(ctx: &mut Ctx<DummyEventDispatcher>, device_id: DeviceId, action: FuzzAction) {
    use FuzzAction::*;
    match action {
        ReceiveFrame(frame) => crate::receive_frame(ctx, device_id, Buf::new(frame, ..)),
        AdvanceTime(SmallDuration(duration)) => {
            let _: Vec<TimerId> = crate::testutil::run_for(ctx, duration);
        }
    }
}

#[::fuzz::fuzz]
pub(crate) fn single_device_arbitrary_packets(input: FuzzInput) {
    initialize_logging();
    let mut ctx = Ctx::with_default_state(DummyEventDispatcher::default());
    let FuzzInput { actions } = input;
    let device_id = ctx
        .state
        .add_ethernet_device(UnicastAddr::new(net_mac!("10:20:30:40:50:60")).unwrap(), 1500);
    crate::initialize_device(&mut ctx, device_id);
    actions.into_iter().for_each(|action| dispatch(&mut ctx, device_id, action));
}
