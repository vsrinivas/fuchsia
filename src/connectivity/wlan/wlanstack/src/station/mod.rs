// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod ap;
pub mod client;
pub mod mesh;

use anyhow::format_err;
use fidl_fuchsia_wlan_mlme::{MlmeEventStream, MlmeProxy};
use futures::prelude::*;
use futures::select;
use std::marker::Unpin;
use std::sync::{Arc, Mutex};
use wlan_common::timer::{self, TimeEntry};
use wlan_sme::{MlmeRequest, MlmeStream, Station};

// The returned future successfully terminates when MLME closes the channel
async fn serve_mlme_sme<STA, TS>(
    proxy: MlmeProxy,
    mut event_stream: MlmeEventStream,
    station: Arc<Mutex<STA>>,
    mut mlme_stream: MlmeStream,
    time_stream: TS,
) -> Result<(), anyhow::Error>
where
    STA: Station,
    TS: Stream<Item = TimeEntry<<STA as wlan_sme::Station>::Event>> + Unpin,
{
    let mut timeout_stream = timer::make_async_timed_event_stream(time_stream).fuse();

    loop {
        select! {
            // Fuse rationale: any `none`s in the MLME stream should result in
            // bailing immediately, so we don't need to track if we've seen a
            // `None` or not and can `fuse` directly in the `select` call.
            mlme_event = event_stream.next().fuse() => match mlme_event {
                Some(Ok(mlme_event)) => station.lock().unwrap().on_mlme_event(mlme_event),
                Some(Err(ref e)) if e.is_closed() => return Ok(()),
                None => return Ok(()),
                Some(Err(e)) => return Err(format_err!("Error reading an event from MLME channel: {}", e)),
            },
            mlme_req = mlme_stream.next().fuse() => match mlme_req {
                Some(req) => match forward_mlme_request(req, &proxy) {
                    Ok(()) => {},
                    Err(ref e) if e.is_closed() => return Ok(()),
                    Err(e) => return Err(format_err!("Error forwarding a request from SME to MLME: {}", e)),
                },
                None => return Err(format_err!("Stream of requests from SME to MLME has ended unexpectedly")),
            },
            timeout = timeout_stream.next() => match timeout {
                Some(timed_event) => station.lock().unwrap().on_timeout(timed_event),
                None => return Err(format_err!("SME timer stream has ended unexpectedly")),
            },
        }
    }
}

fn forward_mlme_request(req: MlmeRequest, proxy: &MlmeProxy) -> Result<(), fidl::Error> {
    match req {
        MlmeRequest::Scan(mut req) => proxy.start_scan(&mut req),
        MlmeRequest::Join(mut req) => proxy.join_req(&mut req),
        MlmeRequest::Authenticate(mut req) => proxy.authenticate_req(&mut req),
        MlmeRequest::AuthResponse(mut resp) => proxy.authenticate_resp(&mut resp),
        MlmeRequest::Associate(mut req) => proxy.associate_req(&mut req),
        MlmeRequest::AssocResponse(mut resp) => proxy.associate_resp(&mut resp),
        MlmeRequest::Deauthenticate(mut req) => proxy.deauthenticate_req(&mut req),
        MlmeRequest::Eapol(mut req) => proxy.eapol_req(&mut req),
        MlmeRequest::SetKeys(mut req) => proxy.set_keys_req(&mut req),
        MlmeRequest::SetCtrlPort(mut req) => proxy.set_controlled_port(&mut req),
        MlmeRequest::Start(mut req) => proxy.start_req(&mut req),
        MlmeRequest::Stop(mut req) => proxy.stop_req(&mut req),
        MlmeRequest::SendMpOpenAction(mut req) => proxy.send_mp_open_action(&mut req),
        MlmeRequest::SendMpConfirmAction(mut req) => proxy.send_mp_confirm_action(&mut req),
        MlmeRequest::MeshPeeringEstablished(mut req) => proxy.mesh_peering_established(&mut req),
        MlmeRequest::SaeHandshakeResp(mut resp) => proxy.sae_handshake_resp(&mut resp),
        MlmeRequest::SaeFrameTx(mut frame) => proxy.sae_frame_tx(&mut frame),
        MlmeRequest::WmmStatusReq => proxy.wmm_status_req(),
        MlmeRequest::FinalizeAssociation(mut cap) => proxy.finalize_association_req(&mut cap),
    }
}
