// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::bail;
use fidl_fuchsia_wlan_mlme::{MlmeEventStream, MlmeProxy};
use fidl_fuchsia_wlan_sme as fidl_sme;
use futures::{select, Stream};
use futures::channel::{oneshot, mpsc};
use futures::prelude::*;
use log::error;
use pin_utils::pin_mut;
use std::marker::Unpin;
use std::sync::{Arc, Mutex};
use wlan_sme::{ap::{self as ap_sme, UserEvent}, DeviceInfo};

use crate::future_util::ConcurrentTasks;
use crate::Never;
use crate::stats_scheduler::StatsRequest;

struct Tokens;

impl ap_sme::Tokens for Tokens {
    type StartToken = oneshot::Sender<ap_sme::StartResult>;
    type StopToken = oneshot::Sender<()>;
}

pub type Endpoint = fidl::endpoints::ServerEnd<fidl_sme::ApSmeMarker>;
type Sme = ap_sme::ApSme<Tokens>;

pub async fn serve<S>(proxy: MlmeProxy,
                      device_info: DeviceInfo,
                      event_stream: MlmeEventStream,
                      new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
                      stats_requests: S)
    -> Result<(), failure::Error>
    where S: Stream<Item = StatsRequest> + Send + Unpin
{
    let (sme, mlme_stream, user_stream) = Sme::new(device_info);
    let sme = Arc::new(Mutex::new(sme));
    let mlme_sme = super::serve_mlme_sme(
        proxy, event_stream, Arc::clone(&sme), mlme_stream, stats_requests);
    let sme_fidl = serve_fidl(sme, new_fidl_clients, user_stream);
    pin_mut!(mlme_sme);
    pin_mut!(sme_fidl);
    select! {
        mlme_sme => mlme_sme?,
        sme_fidl => sme_fidl?.into_any(),
    }
    Ok(())
}

async fn serve_fidl(sme: Arc<Mutex<Sme>>,
                    mut new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
                    mut user_stream: ap_sme::UserStream<Tokens>)
    -> Result<Never, failure::Error>
{
    let mut fidl_clients = ConcurrentTasks::new();
    loop {
        let mut user_event = user_stream.next();
        let mut new_fidl_client = new_fidl_clients.next();
        select! {
            user_event => match user_event {
                Some(e) => handle_user_event(e),
                None => bail!("Stream of events from SME unexpectedly ended"),
            },
            new_fidl_client => match new_fidl_client {
                Some(c) => fidl_clients.add(serve_fidl_endpoint(Arc::clone(&sme), c)),
                None => bail!("New FIDL client stream unexpectedly ended"),
            },
            fidl_clients => {},
        }
    }
}

fn handle_user_event(e: UserEvent<Tokens>) {
    match e {
        UserEvent::StartComplete { token, result } => token.send(result).unwrap_or_else(|_| ()),
        UserEvent::StopComplete { token } => token.send(()).unwrap_or_else(|_| ()),
    }
}

async fn serve_fidl_endpoint(sme: Arc<Mutex<Sme>>, endpoint: Endpoint) {
    const MAX_CONCURRENT_REQUESTS: usize = 1000;
    let stream = match endpoint.into_stream() {
        Ok(s) => s,
        Err(e) => {
            error!("Failed to create a stream from a zircon channel: {}", e);
            return;
        }
    };
    let r = await!(stream.try_for_each_concurrent(MAX_CONCURRENT_REQUESTS, move |request| {
        handle_fidl_request(Arc::clone(&sme), request)
    }));
    if let Err(e) = r {
        error!("Error serving a FIDL client of AP SME: {}", e);
    }
}

async fn handle_fidl_request(sme: Arc<Mutex<Sme>>, request: fidl_sme::ApSmeRequest)
    -> Result<(), ::fidl::Error>
{
    match request {
        fidl_sme::ApSmeRequest::Start { config, responder } => {
            let r = await!(start(&sme, config));
            responder.send(r)?;
        },
        fidl_sme::ApSmeRequest::Stop { responder } => {
            await!(stop(&sme));
            responder.send()?;
        }
    }
    Ok(())
}

async fn start(sme: &Arc<Mutex<Sme>>, config: fidl_sme::ApConfig) -> fidl_sme::StartApResultCode {
    let (sender, receiver) = oneshot::channel();
    let sme_config = ap_sme::Config {
        ssid: config.ssid,
        password: config.password,
        channel: config.channel,
    };
    sme.lock().unwrap().on_start_command(sme_config, sender);
    let r = await!(receiver).unwrap_or_else(|_| {
        error!("Responder for AP Start command was dropped without sending a response");
        ap_sme::StartResult::InternalError
    });
    convert_start_result_code(r)
}

fn convert_start_result_code(r: ap_sme::StartResult) -> fidl_sme::StartApResultCode {
    match r {
        ap_sme::StartResult::Success => fidl_sme::StartApResultCode::Success,
        ap_sme::StartResult::AlreadyStarted => fidl_sme::StartApResultCode::AlreadyStarted,
        ap_sme::StartResult::InternalError => fidl_sme::StartApResultCode::InternalError
    }
}

async fn stop(sme: &Arc<Mutex<Sme>>) {
    let (sender, receiver) = oneshot::channel();
    sme.lock().unwrap().on_stop_command(sender);
    await!(receiver).unwrap_or_else(|_| {
        error!("Responder for AP Stop command was dropped without sending a response");
    })
}
