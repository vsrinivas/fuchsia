// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::send_beacon,
    fidl_fuchsia_wlan_common::{Cbw, WlanChan},
    fidl_fuchsia_wlan_tap as wlantap,
    std::collections::hash_map::HashMap,
    wlan_common::bss::Protection,
    wlan_common::mac::Bssid,
};

/// An Action is a unary function with no return value. It can be passed to the various `on_`-
/// prefixed functions to perform a behavior.
pub trait Action<T> {
    fn run(&mut self, args: &T);
}

// A no-op action.
pub struct NoAction;

impl<T> Action<T> for NoAction {
    fn run(&mut self, _args: &T) {}
}

// Trivial implementation so anonymous functions can be substitutible for Action<T>.
impl<T, F: FnMut(&T) + Sized> Action<T> for F {
    fn run(&mut self, args: &T) {
        self(&args)
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
}

impl<'a> Beacon<'a> {
    pub fn send(phy: &'a wlantap::WlantapPhyProxy) -> Self {
        Self { phy: phy, bssid: Bssid([1; 6]), ssid: vec![], protection: Protection::Open }
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
}

impl<'a> Action<wlantap::SetChannelArgs> for Beacon<'a> {
    fn run(&mut self, args: &wlantap::SetChannelArgs) {
        send_beacon(&mut vec![], &args.chan, &self.bssid, &self.ssid, &self.protection, &self.phy)
            .unwrap();
    }
}

/// EventHandlerBuilder builds a action that can be passed to
/// `TestHelper::run_until_complete_or_timeout`.
pub struct EventHandlerBuilder<'a> {
    set_channel_action: Box<dyn Action<wlantap::SetChannelArgs> + 'a>,
    phy_event_action: Box<dyn Action<wlantap::WlantapPhyEvent> + 'a>,
}

impl<'a> EventHandlerBuilder<'a> {
    pub fn new() -> Self {
        Self { set_channel_action: Box::new(NoAction), phy_event_action: Box::new(NoAction) }
    }

    /// Sets the action for SetChannel events. Only one may be registered.
    pub fn on_set_channel(mut self, action: impl Action<wlantap::SetChannelArgs> + 'a) -> Self {
        self.set_channel_action = Box::new(action);
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

                _ => {}
            }

            self.phy_event_action.run(&event)
        }
    }
}
