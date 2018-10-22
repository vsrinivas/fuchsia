// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod ap;
pub mod client;
pub mod mesh;

use failure::{bail, format_err};
use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEvent, MlmeEventStream, MlmeProxy};
use fidl_fuchsia_wlan_stats::IfaceStats;
use futures::channel::mpsc;
use futures::prelude::*;
use futures::select;
use log::warn;
use pin_utils::pin_mut;
use std::marker::Unpin;
use std::sync::{Arc, Mutex};
use wlan_sme::{Station, MlmeRequest, MlmeStream};

use crate::fidl_util::is_peer_closed;
use crate::Never;
use crate::stats_scheduler::StatsRequest;

// The returned future successfully terminates when MLME closes the channel
async fn serve_mlme_sme<STA, SRS>(proxy: MlmeProxy, mut event_stream: MlmeEventStream,
                                  station: Arc<Mutex<STA>>, mut mlme_stream: MlmeStream,
                                  stats_requests: SRS)
    -> Result<(), failure::Error>
    where STA: Station,
          SRS: Stream<Item = StatsRequest> + Unpin
{
    let (mut stats_sender, stats_receiver) = mpsc::channel(1);
    let stats_fut = serve_stats(proxy.clone(), stats_requests, stats_receiver);
    pin_mut!(stats_fut);
    loop {
        let mut mlme_event = event_stream.next();
        let mut mlme_req = mlme_stream.next();
        select! {
            mlme_event => match mlme_event {
                // Handle the stats response separately since it is SME-independent
                Some(Ok(MlmeEvent::StatsQueryResp{ resp })) => handle_stats_resp(&mut stats_sender, resp)?,
                Some(Ok(other)) => station.lock().unwrap().on_mlme_event(other),
                Some(Err(ref e)) if is_peer_closed(e) => return Ok(()),
                None => return Ok(()),
                Some(Err(e)) => bail!("Error reading an event from MLME channel: {}", e),
            },
            mlme_req => match mlme_req {
                Some(req) => match forward_mlme_request(req, &proxy) {
                    Ok(()) => {},
                    Err(ref e) if is_peer_closed(e) => return Ok(()),
                    Err(e) => bail!("Error forwarding a request from SME to MLME: {}", e),
                },
                None => bail!("Stream of requests from SME to MLME has ended unexpectedly"),
            },
            stats_fut => stats_fut?.into_any(),
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
        MlmeRequest::Start(mut req) => proxy.start_req(&mut req),
        MlmeRequest::Stop(mut req) => proxy.stop_req(&mut req),
    }
}

fn handle_stats_resp(stats_sender: &mut mpsc::Sender<IfaceStats>,
                     resp: fidl_mlme::StatsQueryResponse) -> Result<(), failure::Error> {
    stats_sender.try_send(resp.stats).or_else(|e| {
        if e.is_full() {
            // We only expect one response from MLME per each request, so the bounded
            // queue of size 1 should always suffice.
            warn!("Received an extra GetStatsResp from MLME, discarding");
            Ok(())
        } else {
            Err(format_err!("Failed to send a message to stats future"))
        }
    })
}

async fn serve_stats<S>(proxy: MlmeProxy, mut stats_requests: S,
                        mut responses: mpsc::Receiver<IfaceStats>)
    -> Result<Never, failure::Error>
    where S: Stream<Item = StatsRequest> + Unpin
{
    while let Some(req) = await!(stats_requests.next()) {
        proxy.stats_query_req().map_err(
                |e| format_err!("Failed to send a StatsReq to MLME: {}", e))?;
        match await!(responses.next()) {
            Some(response) => req.reply(response),
            None => bail!("Stream of stats responses has ended unexpectedly"),
        };
    }
    Err(format_err!("Stream of stats requests has ended unexpectedly"))
}
