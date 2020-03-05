// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_control as fctrl,
    fidl_fuchsia_bluetooth_sys::{
        self as sys, PairingDelegateOnPairingRequestResponder, PairingDelegateProxy,
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

use crate::host_dispatcher::{HostDispatcher, PairingDelegate};

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

async fn handler(pd: Option<PairingDelegate>, event: PairingDelegateRequest) -> fidl::Result<()> {
    match pd {
        Some(PairingDelegate::Sys(pd)) => match event {
            OnPairingRequest { peer, method, displayed_passkey, responder } => {
                let peer = match Peer::try_from(peer) {
                    Ok(p) => p,
                    Err(e) => {
                        fx_log_warn!("Received malformed peer from bt-host: {}", e);
                        return Ok(());
                    }
                };
                on_pairing_request(pd, peer, method, displayed_passkey, responder).await
            }
            OnPairingComplete { id, success, control_handle: _ } => {
                on_pairing_complete(pd, id.into(), success)
            }
            OnRemoteKeypress { id, keypress, control_handle: _ } => {
                on_keypress(pd, id.into(), keypress)
            }
        },
        // TODO(fxb/36378) - DEPRECATED
        // Handle an upstream using the deprecated fuchsia.bluetooth.control.PairingDelegate protocol
        Some(PairingDelegate::Control(pd)) => match event {
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

async fn on_pairing_request(
    pd: PairingDelegateProxy,
    peer: Peer,
    method: sys::PairingMethod,
    displayed_passkey: u32,
    responder: PairingDelegateOnPairingRequestResponder,
) -> fidl::Result<()> {
    let (status, passkey) =
        pd.on_pairing_request((&peer).into(), method, displayed_passkey).await?;
    let _ = responder.send(status, passkey);
    Ok(())
}

fn on_pairing_complete(pd: PairingDelegateProxy, id: PeerId, success: bool) -> fidl::Result<()> {
    if let Err(e) = pd.on_pairing_complete(&mut id.into(), success) {
        fx_log_warn!("Failed to propagate pairing cancelled upstream: {}", e);
    };
    Ok(())
}

fn on_keypress(
    pd: PairingDelegateProxy,
    id: PeerId,
    key: sys::PairingKeypress,
) -> fidl::Result<()> {
    if let Err(e) = pd.on_remote_keypress(&mut id.into(), key) {
        fx_log_warn!("Failed to propagate pairing upstream: {}", e);
    };
    Ok(())
}

#[cfg(test)]
mod test {
    use super::*;
    use {
        anyhow::Error,
        fidl_fuchsia_bluetooth::Appearance,
        fuchsia_async as fasync,
        fuchsia_bluetooth::types::Address,
        futures::future::{self, BoxFuture, FutureExt},
        matches::assert_matches,
    };

    fn simple_test_peer(id: PeerId) -> Peer {
        Peer {
            id: id.into(),
            address: Address::Public([1, 2, 3, 4, 5, 6]),
            technology: sys::TechnologyType::LowEnergy,
            name: Some("Peer Name".into()),
            appearance: Some(Appearance::Watch),
            device_class: None,
            rssi: None,
            tx_power: None,
            connected: false,
            bonded: false,
            services: vec![],
        }
    }

    // No upstream delegate causes request to fail
    #[fasync::run_singlethreaded(test)]
    async fn test_no_delegate_rejects() -> Result<(), anyhow::Error> {
        let (proxy, mut requests) =
            fidl::endpoints::create_proxy_and_stream::<sys::PairingDelegateMarker>()?;

        let peer = simple_test_peer(PeerId(0));

        // Make the request
        let result_fut = proxy.on_pairing_request((&peer).into(), PairingMethod::Consent, 0);

        let handler_response =
            handler(None, requests.try_next().await?.expect("We expect a request")).await;
        assert_eq!(handler_response.map_err(|e| e.to_string()), Ok(()));

        let result = result_fut.await.map(|(success, _)| success).map_err(|e| e.to_string());
        // We should be rejected
        assert_eq!(result, Ok(false));
        Ok(())
    }

    // Upstream error causes dropped stream (i.e. handler returns err)
    #[fasync::run_singlethreaded(test)]
    async fn test_bad_delegate_drops_stream() -> Result<(), Error> {
        let (proxy, requests) =
            fidl::endpoints::create_proxy_and_stream::<sys::PairingDelegateMarker>()?;

        let peer = simple_test_peer(PeerId(0));

        let make_request = async {
            let result = proxy.on_pairing_request((&peer).into(), PairingMethod::Consent, 0).await;
            // Our channel should have been closed as the responder was dropped
            assert_matches!(result, Err(fidl::Error::ClientChannelClosed(_)));
        };

        let (pd, run_upstream) = delegate_from_handler(|_req| future::ok(()))?;
        let run_upstream = async move {
            assert_eq!(run_upstream.await.map_err(|e| e.to_string()), Ok(()));
        };

        let run_handler = async move {
            assert!(run_pairing_delegate(Some(pd), requests).await.is_err());
        };

        // Wait on all three tasks.
        // All three tasks should terminate, as the handler will terminate, and then the upstream
        // will close as the upstream channel is dropped
        future::join3(make_request, run_handler, run_upstream).await;
        Ok(())
    }

    // Create a pairing delegate from a given handler function
    fn delegate_from_handler<Fut>(
        handler: impl Fn(PairingDelegateRequest) -> Fut + Send + Sync + 'static,
    ) -> Result<(PairingDelegate, BoxFuture<'static, Result<(), fidl::Error>>), Error>
    where
        Fut: Future<Output = Result<(), fidl::Error>> + Send + Sync + 'static,
    {
        let (proxy, requests) =
            fidl::endpoints::create_proxy_and_stream::<sys::PairingDelegateMarker>()?;
        Ok((PairingDelegate::Sys(proxy), requests.try_for_each(handler).boxed()))
    }

    pub fn run_pairing_delegate(
        pd: Option<PairingDelegate>,
        stream: PairingDelegateRequestStream,
    ) -> impl Future<Output = fidl::Result<()>> {
        stream.try_for_each_concurrent(MAX_CONCURRENT_REQUESTS, move |event| {
            handler(pd.clone(), event)
        })
    }

    // Successful response from Upstream reaches the downstream request
    #[fasync::run_singlethreaded(test)]
    async fn test_success_notifies_responder() -> Result<(), Error> {
        let (proxy, requests) =
            fidl::endpoints::create_proxy_and_stream::<sys::PairingDelegateMarker>()?;

        let peer = simple_test_peer(PeerId(0));
        let passkey = 42;

        let make_request = async move {
            let result = proxy.on_pairing_request((&peer).into(), PairingMethod::Consent, 0).await;
            // Our channel should have been closed as the responder was dropped
            let _passkey = passkey;
            assert_matches!(result, Ok((true, _passkey)));
        };

        let (pd, run_upstream) = delegate_from_handler(move |req| async move {
            if let OnPairingRequest { peer: _, method: _, displayed_passkey: _, responder } = req {
                assert!(responder.send(true, passkey).is_ok());
            }
            Ok(())
        })?;
        let run_upstream = async move {
            assert_eq!(run_upstream.await.map_err(|e| e.to_string()), Ok(()));
        };

        let run_handler = async move {
            assert!(run_pairing_delegate(Some(pd), requests).await.is_ok());
        };

        // Wait on all three tasks.
        // All three tasks should terminate, as the handler will terminate, and then the upstream
        // will close as the upstream channel is dropped
        future::join3(make_request, run_handler, run_upstream).await;
        Ok(())
    }
}
