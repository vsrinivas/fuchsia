// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{create_rx_info, send_beacon},
    fidl_fuchsia_wlan_common::{Cbw, WlanChan},
    fidl_fuchsia_wlan_tap as wlantap,
    std::collections::hash_map::HashMap,
    wlan_common::{
        bss::Protection,
        buffer_reader::BufferReader,
        mac::{Bssid, FrameControl, FrameType, Msdu, MsduIterator},
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
/// WlanChan found in SetChannelArgs, as well as a fallthrough case.
pub struct MatchChannel<'a> {
    actions: HashMap<WlanChan, Box<dyn Action<wlantap::SetChannelArgs> + 'a>>,
    fallthrough_action: Box<dyn Action<wlantap::SetChannelArgs> + 'a>,
}

impl<'a> MatchChannel<'a> {
    pub fn new() -> Self {
        Self { actions: HashMap::new(), fallthrough_action: Box::new(NoAction) }
    }

    /// Registers an action for a channel.
    pub fn on_channel(
        mut self,
        channel: WlanChan,
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
        self.on_channel(WlanChan { primary: primary, cbw: Cbw::Cbw20, secondary80: 0 }, action)
    }

    /// Registers a fallthrough action if none of the registered channels match.
    pub fn on_fallthrough(mut self, action: impl Action<wlantap::SetChannelArgs> + 'a) -> Self {
        self.fallthrough_action = Box::new(action);
        self
    }
}

impl<'a> Action<wlantap::SetChannelArgs> for MatchChannel<'a> {
    fn run(&mut self, args: &wlantap::SetChannelArgs) {
        if let Some(action) = self.actions.get_mut(&args.chan) {
            action.run(&args)
        } else {
            self.fallthrough_action.run(&args)
        }
    }
}

/// Beacon builds a action that can be passed to on_set_channel (or MatchChannel::on_channel) to
///  send a beacon with the provided BSSID + SSID + protection.
pub struct Beacon<'a> {
    phy: &'a wlantap::WlantapPhyProxy,
    bssid: Bssid,
    ssid: Vec<u8>,
    protection: Protection,
    rssi_dbm: i8,
}

impl<'a> Beacon<'a> {
    pub fn send(phy: &'a wlantap::WlantapPhyProxy) -> Self {
        Self {
            phy: phy,
            bssid: Bssid([1; 6]),
            ssid: vec![],
            protection: Protection::Open,
            rssi_dbm: 0,
        }
    }

    pub fn bssid(self, bssid: Bssid) -> Self {
        Self { bssid, ..self }
    }

    pub fn ssid(self, ssid: Vec<u8>) -> Self {
        Self { ssid, ..self }
    }

    pub fn protection(self, protection: Protection) -> Self {
        Self { protection, ..self }
    }

    pub fn rssi(self, rssi_dbm: i8) -> Self {
        Self { rssi_dbm, ..self }
    }
}

impl<'a> Action<wlantap::SetChannelArgs> for Beacon<'a> {
    fn run(&mut self, args: &wlantap::SetChannelArgs) {
        send_beacon(
            &mut vec![],
            &args.chan,
            &self.bssid,
            &self.ssid,
            &self.protection,
            &self.phy,
            self.rssi_dbm,
        )
        .unwrap();
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
    channel: WlanChan,
}

impl<'a> Rx<'a> {
    pub fn send(proxy: &'a wlantap::WlantapPhyProxy, channel: WlanChan) -> Self {
        Self { proxy, channel }
    }
}

impl<'a> Action<wlantap::TxArgs> for Rx<'a> {
    fn run(&mut self, args: &wlantap::TxArgs) {
        let frame = args.packet.data.clone();
        let mut frame_iter = frame.into_iter();
        self.proxy.rx(0, &mut frame_iter, &mut create_rx_info(&self.channel, 0)).expect("rx");
    }
}

/// EventHandlerBuilder builds a action that can be passed to
/// `TestHelper::run_until_complete_or_timeout`.
pub struct EventHandlerBuilder<'a> {
    set_channel_action: Box<dyn Action<wlantap::SetChannelArgs> + 'a>,
    tx_action: Box<dyn Action<wlantap::TxArgs> + 'a>,
    phy_event_action: Box<dyn Action<wlantap::WlantapPhyEvent> + 'a>,
}

impl<'a> EventHandlerBuilder<'a> {
    pub fn new() -> Self {
        Self {
            set_channel_action: Box::new(NoAction),
            tx_action: Box::new(NoAction),
            phy_event_action: Box::new(NoAction),
        }
    }

    /// Sets the action for SetChannel events. Only one may be registered.
    pub fn on_set_channel(mut self, action: impl Action<wlantap::SetChannelArgs> + 'a) -> Self {
        self.set_channel_action = Box::new(action);
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

    pub fn build(mut self) -> impl FnMut(wlantap::WlantapPhyEvent) + 'a {
        move |event| {
            match event {
                wlantap::WlantapPhyEvent::SetChannel { ref args } => {
                    self.set_channel_action.run(&args)
                }

                wlantap::WlantapPhyEvent::Tx { ref args } => self.tx_action.run(&args),

                _ => {}
            }

            self.phy_event_action.run(&event)
        }
    }
}
