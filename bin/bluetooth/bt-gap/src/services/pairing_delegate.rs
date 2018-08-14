// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::host_dispatcher::HostDispatcher;
use fidl;
use fidl::endpoints2::RequestStream;
use fidl_fuchsia_bluetooth_control::{PairingDelegateRequest, PairingDelegateRequestStream};
use fuchsia_async::{self as fasync,
                    temp::Either::{Left, Right}};
use fuchsia_syslog::{fx_log, fx_log_warn};
use futures::future;
use futures::{Future, TryFutureExt, TryStreamExt};
use parking_lot::RwLock;
use std::sync::Arc;

// Number of concurrent requests allowed to the pairing delegate
// at a single time
const MAX_CONCURRENT: usize = 100;

pub fn start_pairing_delegate(
    hd: Arc<RwLock<HostDispatcher>>, channel: fasync::Channel,
) -> impl Future<Output = Result<(), fidl::Error>> {
    let stream = PairingDelegateRequestStream::from_channel(channel);
    let hd = hd.clone();
    stream.try_for_each_concurrent(MAX_CONCURRENT, move |evt| match evt {
        PairingDelegateRequest::OnPairingRequest {
            mut device,
            method,
            displayed_passkey,
            responder,
        } => {
            let pd = hd.read().pairing_delegate.clone();
            let passkey_ref = displayed_passkey.as_ref().map(|x| &**x);
            Left(Left(
                match pd {
                    Some(pd) => Left(pd.on_pairing_request(&mut device, method, passkey_ref)),
                    None => {
                        fx_log_warn!("Rejected pairing due to no upstream pairing delegate");
                        Right(future::ready(Ok((false, None))))
                    }
                }.and_then(move |(status, passkey)| {
                    future::ready(Ok(
                        responder.send(status, passkey.as_ref().map(String::as_str))
                    ))
                }).map_ok(|_| ()),
            ))
        }
        PairingDelegateRequest::OnPairingComplete {
            device_id,
            mut status,
            control_handle: _,
        } => {
            let pd = hd.read().pairing_delegate.clone();
            if let Some(pd) = pd {
                let res = pd.on_pairing_complete(device_id.as_str(), &mut status);
                if res.is_err() {
                    fx_log_warn!("Failed to propagate pairing cancelled upstream");
                }
            }
            Left(Right(future::ready(Ok(()))))
        }
        PairingDelegateRequest::OnRemoteKeypress {
            device_id,
            keypress,
            control_handle: _,
        } => {
            let pd = hd.read().pairing_delegate.clone();
            if let Some(pd) = pd {
                let res = pd.on_remote_keypress(device_id.as_str(), keypress);
                if res.is_err() {
                    fx_log_warn!("Failed to propagate pairing cancelled upstream");
                }
            }
            Right(future::ready(Ok(())))
        }
    })
}
