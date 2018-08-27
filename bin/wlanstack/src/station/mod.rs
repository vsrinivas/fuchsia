// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod client;

use fuchsia_async::temp::TempFutureExt;
use failure::{format_err, ResultExt};
use fidl_fuchsia_wlan_mlme::{self as fidl_mlme, MlmeEvent, MlmeEventStream, MlmeProxy};
use fidl_fuchsia_wlan_stats::IfaceStats;
use futures::channel::mpsc;
use futures::prelude::*;
use log::{log, warn};
use std::sync::{Arc, Mutex};
use wlan_sme::{Station, MlmeRequest, MlmeStream};

use crate::fidl_util::is_peer_closed;
use crate::Never;
use crate::stats_scheduler::StatsRequest;

// The returned future successfully terminates when MLME closes the channel
fn serve_mlme_sme<STA, SRS>(proxy: MlmeProxy, event_stream: MlmeEventStream,
                            station: Arc<Mutex<STA>>, mlme_stream: MlmeStream,
                            stats_requests: SRS)
    -> impl Future<Output = Result<(), failure::Error>>
    where STA: Station,
          SRS: Stream<Item = StatsRequest>
{
    let (mut stats_sender, stats_fut) = serve_stats(proxy.clone(), stats_requests);
    let mlme_to_sme_fut = event_stream
        .err_into::<failure::Error>()
        .try_for_each(move |e| future::ready(match e {
            // Handle the stats response separately since it is SME-independent
            MlmeEvent::StatsQueryResp{ resp } => handle_stats_resp(&mut stats_sender, resp),
            other => Ok(station.lock().unwrap().on_mlme_event(other))
        }))
        .map_ok(|_| ());
    let sme_to_mlme_fut = mlme_stream
        .map(Ok)
        .try_for_each(move |e| {
            future::ready(match e {
                MlmeRequest::Scan(mut req) => proxy.start_scan(&mut req),
                MlmeRequest::Join(mut req) => proxy.join_req(&mut req),
                MlmeRequest::Authenticate(mut req) => proxy.authenticate_req(&mut req),
                MlmeRequest::Associate(mut req) => proxy.associate_req(&mut req),
                MlmeRequest::Deauthenticate(mut req) => proxy.deauthenticate_req(&mut req),
                MlmeRequest::Eapol(mut req) => proxy.eapol_req(&mut req),
                MlmeRequest::SetKeys(mut req) => proxy.set_keys_req(&mut req),
            })
        })
        .then(|r| future::ready(match r {
            Ok(_) => Err(format_err!("SME->MLME sender future unexpectedly finished")),
            Err(ref e) if is_peer_closed(e) => {
                // Don't treat closed channel as error; terminate the future peacefully instead
                Ok(())
            },
            Err(other) => Err(other.into()),
        }));
    // Select, not join: terminate as soon as one of the futures terminates
    let mlme_sme_fut = mlme_to_sme_fut.select(sme_to_mlme_fut)
        .map(|e| e.either(|x| Ok(x.context("MLME->SME")?),
                          |x| Ok(x.context("SME->MLME")?)));
    // select3() would be nice
    mlme_sme_fut.select(stats_fut)
        .map(|e| e.either(|x| x,
                          |x| Ok(x.context("Stats server")?.into_any())))
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

fn serve_stats<S>(proxy: MlmeProxy, stats_requests: S)
    -> (mpsc::Sender<IfaceStats>, impl Future<Output = Result<Never, failure::Error>>)
    where S: Stream<Item = StatsRequest>
{
    let (sender, receiver) = mpsc::channel(1);
    let fut = stats_requests
        .map(move |req| proxy.stats_query_req().map(move |_| req))
        .zip(receiver)
        // Future<Output = (Result<T, E>, IfaceStats)> to
        // Future<Output = Result<(T, IfaceStats), E>>
        .map(|(req, stats)| req.map(move |req| (req, stats)))
        .try_for_each(|(req, stats)| {
            req.reply(stats);
            future::ready(Ok(()))
        })
        .err_into::<failure::Error>()
        .and_then(|_|
            future::ready(Err(format_err!("Stats server future unexpectedly finished"))));
    (sender, fut)
}
