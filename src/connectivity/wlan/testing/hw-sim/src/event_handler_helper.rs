// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{create_rx_info, send_scan_complete, send_scan_result, BeaconInfo},
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_tap as wlantap,
    std::collections::hash_map::HashMap,
    wlan_common::{
        buffer_reader::BufferReader,
        channel::Channel,
        mac::{FrameControl, FrameType, Msdu, MsduIterator},
    },
};

/// An Action is a unary function with no return value. It can be passed to the various `on_`-
/// prefixed functions to perform a behavior.
pub trait Action<T> {
    fn run(&mut self, args: &T);
}

// A no-op action.
pub struct NoAction;

impl<T> Action<T> for NoAction {
    fn run(&mut self, _arg: &T) {}
}

// Trivial implementation so anonymous functions can be substitutible for Action<T>.
impl<T, F: FnMut(&T)> Action<T> for F {
    fn run(&mut self, arg: &T) {
        self(&arg)
    }
}

/// A Sequence chains multiple Actions together sequentially.
pub struct Sequence<'a, T> {
    actions: Vec<Box<dyn Action<T> + 'a>>,
}

impl<'a, T> Sequence<'a, T> {
    pub fn start() -> Self {
        Self { actions: vec![] }
    }

    pub fn then(mut self, action: impl Action<T> + 'a) -> Self {
        self.actions.push(Box::new(action));
        self
    }
}

impl<'a, T> Action<T> for Sequence<'a, T> {
    fn run(&mut self, arg: &T) {
        for action in self.actions.iter_mut() {
            action.run(&arg)
        }
    }
}

/// MatchChannel provides functions to register dispatches to different actions based on the
/// WlanChannel found in SetChannelArgs, as well as a fallthrough case.
pub struct MatchChannel<'a> {
    actions: HashMap<fidl_common::WlanChannel, Box<dyn Action<wlantap::SetChannelArgs> + 'a>>,
    fallthrough_action: Box<dyn Action<wlantap::SetChannelArgs> + 'a>,
}

impl<'a> MatchChannel<'a> {
    pub fn new() -> Self {
        Self { actions: HashMap::new(), fallthrough_action: Box::new(NoAction) }
    }

    /// Registers an action for a channel.
    pub fn on_channel(
        mut self,
        channel: fidl_common::WlanChannel,
        action: impl Action<wlantap::SetChannelArgs> + 'a,
    ) -> Self {
        self.actions.insert(channel, Box::new(action));
        self
    }

    /// Registers an action for a primary channel number.
    pub fn on_primary(
        self,
        primary: u8,
        action: impl Action<wlantap::SetChannelArgs> + 'a,
    ) -> Self {
        self.on_channel(
            fidl_common::WlanChannel {
                primary: primary,
                cbw: fidl_common::ChannelBandwidth::Cbw20,
                secondary80: 0,
            },
            action,
        )
    }

    /// Registers a fallthrough action if none of the registered channels match.
    pub fn on_fallthrough(mut self, action: impl Action<wlantap::SetChannelArgs> + 'a) -> Self {
        self.fallthrough_action = Box::new(action);
        self
    }
}

impl<'a> Action<wlantap::SetChannelArgs> for MatchChannel<'a> {
    fn run(&mut self, args: &wlantap::SetChannelArgs) {
        if let Some(action) = self.actions.get_mut(&args.channel) {
            action.run(&args)
        } else {
            self.fallthrough_action.run(&args)
        }
    }
}

pub struct ScanResults<'a> {
    phy: &'a wlantap::WlantapPhyProxy,
    inner: Vec<BeaconInfo<'a>>,
}

impl<'a> ScanResults<'a> {
    pub fn new(phy: &'a wlantap::WlantapPhyProxy, scan_results: Vec<BeaconInfo<'a>>) -> Self {
        Self { phy, inner: scan_results }
    }
}

impl<'a> Action<wlantap::StartScanArgs> for ScanResults<'a> {
    fn run(&mut self, args: &wlantap::StartScanArgs) {
        for beacon_info in &self.inner {
            send_scan_result(&self.phy, &beacon_info);
        }
        send_scan_complete(args.scan_id, 0, &self.phy).unwrap();
    }
}

pub struct ForEachMsdu<A>(A);

impl<A: for<'b> Action<Msdu<&'b [u8]>>> Action<Vec<u8>> for ForEachMsdu<A> {
    fn run(&mut self, data: &Vec<u8>) {
        for msdu in MsduIterator::from_raw_data_frame(&data[..], false)
            .expect("reading msdu from data frame")
        {
            self.0.run(&msdu)
        }
    }
}

/// MatchTx matches frames by frame type.
pub struct MatchTx<'a> {
    frame_type_actions: HashMap<FrameType, Box<dyn Action<Vec<u8>> + 'a>>,
}

impl<'a> MatchTx<'a> {
    pub fn new() -> Self {
        Self { frame_type_actions: HashMap::new() }
    }

    pub fn on_frame_type(
        mut self,
        frame_type: FrameType,
        action: impl Action<Vec<u8>> + 'a,
    ) -> Self {
        self.frame_type_actions.insert(frame_type, Box::new(action));
        self
    }

    pub fn on_mgmt(self, action: impl Action<Vec<u8>> + 'a) -> Self {
        self.on_frame_type(FrameType::MGMT, action)
    }

    pub fn on_data(self, action: impl Action<Vec<u8>> + 'a) -> Self {
        self.on_frame_type(FrameType::DATA, action)
    }

    pub fn on_msdu(self, action: impl for<'b> Action<Msdu<&'b [u8]>> + 'a) -> Self {
        self.on_data(ForEachMsdu(action))
    }

    pub fn on_ctrl(self, action: impl Action<Vec<u8>> + 'a) -> Self {
        self.on_frame_type(FrameType::CTRL, action)
    }
}

impl<'a> Action<wlantap::TxArgs> for MatchTx<'a> {
    fn run(&mut self, args: &wlantap::TxArgs) {
        let reader = BufferReader::new(&args.packet.data[..]);
        let fc = FrameControl(reader.peek_value().unwrap());

        if let Some(action) = self.frame_type_actions.get_mut(&fc.frame_type()) {
            action.run(&args.packet.data)
        }
    }
}

/// Rx forwards packets to the Rx queue of a device.
pub struct Rx<'a> {
    proxy: &'a wlantap::WlantapPhyProxy,
    channel: Channel,
}

impl<'a> Rx<'a> {
    pub fn send(proxy: &'a wlantap::WlantapPhyProxy, channel: Channel) -> Self {
        Self { proxy, channel }
    }
}

impl<'a> Action<wlantap::TxArgs> for Rx<'a> {
    fn run(&mut self, args: &wlantap::TxArgs) {
        let frame = &args.packet.data;
        self.proxy.rx(0, frame, &mut create_rx_info(&self.channel, 0)).expect("rx");
    }
}

/// EventHandlerBuilder builds a action that can be passed to
/// `TestHelper::run_until_complete_or_timeout`.
pub struct EventHandlerBuilder<'a> {
    set_channel_action: Box<dyn Action<wlantap::SetChannelArgs> + 'a>,
    start_scan_action: Box<dyn Action<wlantap::StartScanArgs> + 'a>,
    tx_action: Box<dyn Action<wlantap::TxArgs> + 'a>,
    phy_event_action: Box<dyn Action<wlantap::WlantapPhyEvent> + 'a>,
    debug_name: Option<String>,
}

impl<'a> EventHandlerBuilder<'a> {
    pub fn new() -> Self {
        Self {
            set_channel_action: Box::new(NoAction),
            start_scan_action: Box::new(NoAction),
            tx_action: Box::new(NoAction),
            phy_event_action: Box::new(NoAction),
            debug_name: None,
        }
    }

    /// Sets the action for SetChannel events. Only one may be registered.
    pub fn on_set_channel(mut self, action: impl Action<wlantap::SetChannelArgs> + 'a) -> Self {
        self.set_channel_action = Box::new(action);
        self
    }

    pub fn on_start_scan(mut self, action: impl Action<wlantap::StartScanArgs> + 'a) -> Self {
        self.start_scan_action = Box::new(action);
        self
    }

    pub fn on_tx(mut self, action: impl Action<wlantap::TxArgs> + 'a) -> Self {
        self.tx_action = Box::new(action);
        self
    }

    /// Sets the action for all PHY events. Only one may be registered.
    ///
    /// The action registered here will be called even if another action has already been called:
    /// e.g. if a SetChannel event was received, both the on_set_channel and on_phy_event actions
    /// will be called.
    pub fn on_phy_event(mut self, action: impl Action<wlantap::WlantapPhyEvent> + 'a) -> Self {
        self.phy_event_action = Box::new(action);
        self
    }

    // TODO(fxbug.dev/91118) - Added to help investigate hw-sim test. Remove later
    pub fn on_debug_name(mut self, debug_name: &str) -> Self {
        self.debug_name = Some(debug_name.to_string());
        self
    }

    pub fn build(mut self) -> impl FnMut(wlantap::WlantapPhyEvent) + 'a {
        move |event| {
            match event {
                wlantap::WlantapPhyEvent::SetChannel { ref args } => {
                    self.set_channel_action.run(&args)
                }

                wlantap::WlantapPhyEvent::Tx { ref args } => {
                    // TODO(fxbug.dev/91118) - Added to help investigate hw-sim test. Remove later
                    if let Some(debug_name) = &self.debug_name {
                        tracing::info!(
                            "[{}] - Tx({} bytes): {:X?}",
                            debug_name,
                            args.packet.data.len(),
                            args.packet.data
                        );
                    }
                    self.tx_action.run(&args)
                }

                wlantap::WlantapPhyEvent::StartScan { ref args } => {
                    self.start_scan_action.run(&args)
                }

                _ => {}
            }
            self.phy_event_action.run(&event)
        }
    }
}
