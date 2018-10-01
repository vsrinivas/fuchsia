// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::bail,
    fidl_fuchsia_wlan_mlme::{MlmeEventStream, MlmeProxy},
    fidl_fuchsia_wlan_sme as fidl_sme,
    futures::{
        channel::mpsc,
        prelude::*,
        select,
    },
    log::error,
    pin_utils::pin_mut,
    std::{
        marker::Unpin,
        sync::{Arc, Mutex},
    },
    wlan_sme::mesh::{self as mesh_sme, UserEvent},
    crate::{
        future_util::ConcurrentTasks,
        Never,
        stats_scheduler::StatsRequest,
    },
};

struct Tokens;

impl mesh_sme::Tokens for Tokens { }

pub type Endpoint = fidl::endpoints::ServerEnd<fidl_sme::MeshSmeMarker>;
type Sme = mesh_sme::MeshSme<Tokens>;

pub async fn serve<S>(proxy: MlmeProxy,
                      event_stream: MlmeEventStream,
                      new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
                      stats_requests: S)
                      -> Result<(), failure::Error>
    where S: Stream<Item = StatsRequest> + Send + Unpin
{
    let (sme, mlme_stream, user_stream) = Sme::new();
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
                    mut user_stream: mesh_sme::UserStream<Tokens>)
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
        UserEvent::_Dummy(_) => unimplemented!(),
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
        error!("Error serving a FIDL client of Mesh SME: {}", e);
    }
}

async fn handle_fidl_request(_sme: Arc<Mutex<Sme>>, _request: fidl_sme::MeshSmeRequest)
    -> Result<(), ::fidl::Error>
{
    Ok(())
}
