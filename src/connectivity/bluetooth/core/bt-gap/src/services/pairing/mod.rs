// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl::endpoints::Proxy,
    fidl_fuchsia_bluetooth_control as fctrl,
    fidl_fuchsia_bluetooth_sys::{self as sys, PairingKeypress, PairingMethod},
    fuchsia_bluetooth::{
        bt_fidl_status,
        types::{Peer, PeerId},
    },
    futures::{
        future::{Either::*, Future, TryFutureExt},
        stream::StreamExt,
    },
};

pub mod pairing_dispatcher;
pub mod pairing_requests;

/// Abstraction over the fuchsia.bluetooth.control and fuchsia.bluetooth.system interfaces, which
/// each have their own pairing delegate definition.
/// TODO(fxbug.dev/48051): Once control is fully deprecated and removed, we can remove this.
///
/// This type is structurally equivalent to (it implements the same set of methods as)
/// sys::PairingDelegateProxy, for easy migration.
#[derive(Clone)]
pub enum PairingDelegate {
    Control(fctrl::PairingDelegateProxy),
    Sys(sys::PairingDelegateProxy),
}

impl PairingDelegate {
    pub fn is_closed(&self) -> bool {
        match self {
            PairingDelegate::Control(delegate) => delegate.is_closed(),
            PairingDelegate::Sys(delegate) => delegate.is_closed(),
        }
    }

    pub fn when_done(&self) -> impl Future<Output = ()> {
        match self {
            PairingDelegate::Sys(sys) => Left(sys.take_event_stream().map(|_| ()).collect()),
            PairingDelegate::Control(control) => {
                Right(control.take_event_stream().map(|_| ()).collect())
            }
        }
    }

    // from sys::PairingDelegateProxy
    pub fn on_pairing_complete(&self, peer: PeerId, success: bool) -> fidl::Result<()> {
        match self {
            PairingDelegate::Sys(sys) => sys.on_pairing_complete(&mut peer.into(), success),
            PairingDelegate::Control(control) => {
                let mut status = if success {
                    bt_fidl_status!()
                } else {
                    bt_fidl_status!(Failed, "failed to pair")
                };
                control.on_pairing_complete(&peer.to_string(), &mut status)
            }
        }
    }
    // from sys::PairingDelegateProxy
    pub fn on_pairing_request(
        &self,
        peer: Peer,
        method: PairingMethod,
        passkey: u32,
    ) -> impl Future<Output = fidl::Result<(bool, u32)>> {
        match self {
            PairingDelegate::Sys(sys) => {
                Left(sys.on_pairing_request((&peer).into(), method, passkey))
            }
            PairingDelegate::Control(control) => Right(
                control
                    .on_pairing_request(
                        &mut (&peer).into(),
                        compat::method_to_control(method),
                        Some(&passkey.to_string()),
                    )
                    .map_ok(|(success, passkey)| {
                        (success, passkey.and_then(|s| s.parse::<u32>().ok()).unwrap_or(0))
                    }),
            ),
        }
    }
    // from sys::PairingDelegateProxy
    pub fn on_remote_keypress(&self, peer: PeerId, keypress: PairingKeypress) -> fidl::Result<()> {
        match self {
            PairingDelegate::Sys(sys) => sys.on_remote_keypress(&mut peer.into(), keypress),
            PairingDelegate::Control(control) => {
                control.on_remote_keypress(&peer.to_string(), compat::keypress_to_control(keypress))
            }
        }
    }
}

// Convert types from fuchsia.bluetooth.sys to equivalent types from fuchsia.bluetooth.control
mod compat {
    use super::*;

    pub fn method_to_control(method: PairingMethod) -> fctrl::PairingMethod {
        match method {
            PairingMethod::Consent => fctrl::PairingMethod::Consent,
            PairingMethod::PasskeyDisplay => fctrl::PairingMethod::PasskeyDisplay,
            PairingMethod::PasskeyComparison => fctrl::PairingMethod::PasskeyComparison,
            PairingMethod::PasskeyEntry => fctrl::PairingMethod::PasskeyEntry,
        }
    }

    pub fn keypress_to_control(keypress: PairingKeypress) -> fctrl::PairingKeypressType {
        match keypress {
            PairingKeypress::DigitEntered => fctrl::PairingKeypressType::DigitEntered,
            PairingKeypress::DigitErased => fctrl::PairingKeypressType::DigitErased,
            PairingKeypress::PasskeyCleared => fctrl::PairingKeypressType::PasskeyCleared,
            PairingKeypress::PasskeyEntered => fctrl::PairingKeypressType::PasskeyEntered,
        }
    }
}
