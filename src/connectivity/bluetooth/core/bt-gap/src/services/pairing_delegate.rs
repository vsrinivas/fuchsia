// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_control as fctrl,
    fidl_fuchsia_bluetooth_sys::{
        PairingDelegateOnPairingRequestResponder,
        PairingDelegateRequest::{self, *},
        PairingDelegateRequestStream, PairingKeypress, PairingMethod,
    },
    fuchsia_bluetooth::{
        bt_fidl_status,
        types::{Peer, PeerId},
    },
    fuchsia_syslog::fx_log_warn,
    futures::{Future, TryStreamExt},
    std::convert::TryFrom,
};

use crate::host_dispatcher::HostDispatcher;

// Number of concurrent requests allowed to the pairing delegate at a single time
const MAX_CONCURRENT_REQUESTS: usize = 100;

pub fn start_pairing_delegate(
    hd: HostDispatcher,
    stream: PairingDelegateRequestStream,
) -> impl Future<Output = fidl::Result<()>> {
    stream.try_for_each_concurrent(MAX_CONCURRENT_REQUESTS, move |event| {
        handler(hd.pairing_delegate(), event)
    })
}

async fn handler(
    pd: Option<fctrl::PairingDelegateProxy>,
    event: PairingDelegateRequest,
) -> fidl::Result<()> {
    match pd {
        Some(pd) => match event {
            OnPairingRequest { peer, method, displayed_passkey, responder } => {
                let peer = match Peer::try_from(peer) {
                    Ok(p) => p,
                    Err(e) => {
                        fx_log_warn!("Received malformed peer from bt-host: {}", e);
                        return Ok(());
                    }
                };
                compat::relay_control_pairing_request(
                    pd,
                    peer,
                    method,
                    displayed_passkey,
                    responder,
                )
                .await
            }
            OnPairingComplete { id, success, control_handle: _ } => {
                compat::relay_control_pairing_complete(pd, id.into(), success)
            }
            OnRemoteKeypress { id, keypress, control_handle: _ } => {
                compat::relay_control_remote_keypress(pd, id.into(), keypress)
            }
        },
        None => match event {
            OnPairingRequest { peer: _, method: _, displayed_passkey: _, responder } => {
                fx_log_warn!("Rejected pairing due to no upstream pairing delegate");
                let _ = responder.send(false, 0);
                Ok(())
            }
            OnPairingComplete { id, .. } => {
                fx_log_warn!(
                    "Unhandled OnPairingComplete for device '{:?}': No PairingDelegate",
                    id
                );
                Ok(())
            }
            OnRemoteKeypress { id, .. } => {
                fx_log_warn!(
                    "Unhandled OnRemoteKeypress for device '{:?}': No PairingDelegate",
                    id
                );
                Ok(())
            }
        },
    }
}

// The functions below relay sys.PairingDelegate events from bt-host to a control.PairingDelegate.
mod compat {
    use super::*;

    fn method_to_control(method: PairingMethod) -> fctrl::PairingMethod {
        match method {
            PairingMethod::Consent => fctrl::PairingMethod::Consent,
            PairingMethod::PasskeyDisplay => fctrl::PairingMethod::PasskeyDisplay,
            PairingMethod::PasskeyComparison => fctrl::PairingMethod::PasskeyComparison,
            PairingMethod::PasskeyEntry => fctrl::PairingMethod::PasskeyEntry,
        }
    }

    fn keypress_to_control(keypress: PairingKeypress) -> fctrl::PairingKeypressType {
        match keypress {
            PairingKeypress::DigitEntered => fctrl::PairingKeypressType::DigitEntered,
            PairingKeypress::DigitErased => fctrl::PairingKeypressType::DigitErased,
            PairingKeypress::PasskeyCleared => fctrl::PairingKeypressType::PasskeyCleared,
            PairingKeypress::PasskeyEntered => fctrl::PairingKeypressType::PasskeyEntered,
        }
    }

    pub(crate) async fn relay_control_pairing_request(
        pd: fctrl::PairingDelegateProxy,
        peer: Peer,
        method: PairingMethod,
        displayed_passkey: u32,
        responder: PairingDelegateOnPairingRequestResponder,
    ) -> fidl::Result<()> {
        let passkey = match method {
            PairingMethod::PasskeyDisplay | PairingMethod::PasskeyComparison => {
                Some(format!("{:0>6}", displayed_passkey))
            }
            _ => None,
        };
        let mut device: fidl_fuchsia_bluetooth_control::RemoteDevice = peer.into();
        let (accept, passkey) = pd
            .on_pairing_request(
                &mut device,
                method_to_control(method),
                passkey.as_ref().map(String::as_str),
            )
            .await?;

        // Reject the pairing if a malformed passkey value was provided.
        let passkey = passkey.map(|p| p.parse::<u32>()).unwrap_or(Ok(0));
        let (accept, passkey) = match passkey {
            Ok(p) => (accept, p),
            Err(e) => {
                fx_log_warn!("malformed passkey: {}", e);
                (false, 0)
            }
        };

        let _ = responder.send(accept, passkey);
        Ok(())
    }

    pub(crate) fn relay_control_pairing_complete(
        pd: fctrl::PairingDelegateProxy,
        id: PeerId,
        success: bool,
    ) -> fidl::Result<()> {
        let mut status =
            if success { bt_fidl_status!() } else { bt_fidl_status!(Failed, "failed to pair") };
        pd.on_pairing_complete(&format!("{}", id), &mut status)
    }

    pub(crate) fn relay_control_remote_keypress(
        pd: fctrl::PairingDelegateProxy,
        id: PeerId,
        keypress: PairingKeypress,
    ) -> fidl::Result<()> {
        pd.on_remote_keypress(&format!("{}", id), keypress_to_control(keypress))
    }
}
