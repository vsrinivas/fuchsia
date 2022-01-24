// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::ReadValue;
use fidl::{HandleRef, Signals};
use fidl_fuchsia_overnet_protocol::{SignalUpdate, Signals as WireSignals};
use fuchsia_async::OnSignals;
use fuchsia_zircon_status as zx_status;
use futures::FutureExt;
use std::task::{Context, Poll};

const POLLED_SIGNALS: Signals = Signals::from_bits_truncate(
    Signals::USER_0.bits()
        | Signals::USER_1.bits()
        | Signals::USER_2.bits()
        | Signals::USER_3.bits()
        | Signals::USER_4.bits()
        | Signals::USER_5.bits()
        | Signals::USER_6.bits()
        | Signals::USER_7.bits(),
);

#[derive(Default)]
pub(crate) struct Collector {
    on_signals: Option<OnSignals<'static>>,
}

impl Collector {
    pub(crate) fn after_read<'a, 'b>(
        &mut self,
        ctx: &mut Context<'a>,
        hdl: HandleRef<'b>,
        read_result: Poll<Result<(), zx_status::Status>>,
    ) -> Poll<Result<ReadValue, zx_status::Status>> {
        match read_result {
            Poll::Ready(Ok(())) => Poll::Ready(Ok(ReadValue::Message)),
            Poll::Ready(Err(e)) => Poll::Ready(Err(e)),
            Poll::Pending => {
                let mut on_signals = self
                    .on_signals
                    .take()
                    .unwrap_or_else(|| OnSignals::new(&hdl, POLLED_SIGNALS).extend_lifetime());
                match on_signals.poll_unpin(ctx) {
                    Poll::Ready(Ok(signals)) => {
                        hdl.signal(signals, Signals::empty())?;
                        Poll::Ready(Ok(ReadValue::SignalUpdate(SignalUpdate {
                            assert_signals: Some(to_wire_signals(signals)),
                            ..SignalUpdate::EMPTY
                        })))
                    }
                    Poll::Ready(Err(e)) => Poll::Ready(Err(e)),
                    Poll::Pending => {
                        self.on_signals = Some(on_signals);
                        Poll::Pending
                    }
                }
            }
        }
    }
}

fn to_wire_signals(signals: Signals) -> WireSignals {
    let mut out = WireSignals::empty();
    if signals.contains(Signals::USER_0) {
        out.insert(WireSignals::USER_0);
    }
    if signals.contains(Signals::USER_1) {
        out.insert(WireSignals::USER_1);
    }
    if signals.contains(Signals::USER_2) {
        out.insert(WireSignals::USER_2);
    }
    if signals.contains(Signals::USER_3) {
        out.insert(WireSignals::USER_3);
    }
    if signals.contains(Signals::USER_4) {
        out.insert(WireSignals::USER_4);
    }
    if signals.contains(Signals::USER_5) {
        out.insert(WireSignals::USER_5);
    }
    if signals.contains(Signals::USER_6) {
        out.insert(WireSignals::USER_6);
    }
    if signals.contains(Signals::USER_7) {
        out.insert(WireSignals::USER_7);
    }
    out
}

pub(crate) fn from_wire_signals(signals: WireSignals) -> Signals {
    let mut out = Signals::empty();
    if signals.contains(WireSignals::USER_0) {
        out.insert(Signals::USER_0);
    }
    if signals.contains(WireSignals::USER_1) {
        out.insert(Signals::USER_1);
    }
    if signals.contains(WireSignals::USER_2) {
        out.insert(Signals::USER_2);
    }
    if signals.contains(WireSignals::USER_3) {
        out.insert(Signals::USER_3);
    }
    if signals.contains(WireSignals::USER_4) {
        out.insert(Signals::USER_4);
    }
    if signals.contains(WireSignals::USER_5) {
        out.insert(Signals::USER_5);
    }
    if signals.contains(WireSignals::USER_6) {
        out.insert(Signals::USER_6);
    }
    if signals.contains(WireSignals::USER_7) {
        out.insert(Signals::USER_7);
    }
    out
}
