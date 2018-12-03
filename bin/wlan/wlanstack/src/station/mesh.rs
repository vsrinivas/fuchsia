// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::bail,
    fidl_fuchsia_wlan_mlme::{MlmeEventStream, MlmeProxy},
    fidl_fuchsia_wlan_sme as fidl_sme,
    futures::{
        channel::{mpsc, oneshot},
        Poll,
        prelude::*,
        select,
        stream::{self, FuturesUnordered},
    },
    log::error,
    pin_utils::pin_mut,
    std::{
        marker::Unpin,
        sync::{Arc, Mutex},
    },
    wlan_sme::{
        DeviceInfo,
        mesh::{self as mesh_sme, UserEvent},
        timer::TimeEntry,
    },
    crate::{
        Never,
        stats_scheduler::StatsRequest,
    },
};

struct Tokens;

impl mesh_sme::Tokens for Tokens {
    type JoinToken = oneshot::Sender<mesh_sme::JoinMeshResult>;
}

pub type Endpoint = fidl::endpoints::ServerEnd<fidl_sme::MeshSmeMarker>;
type Sme = mesh_sme::MeshSme<Tokens>;

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
    let time_stream = stream::poll_fn::<TimeEntry<()>, _>(|_| Poll::Pending);
    let mlme_sme = super::serve_mlme_sme(
        proxy, event_stream, Arc::clone(&sme), mlme_stream, stats_requests, time_stream);
    let sme_fidl = serve_fidl(sme, new_fidl_clients, user_stream);
    pin_mut!(mlme_sme);
    pin_mut!(sme_fidl);
    select! {
        mlme_sme = mlme_sme.fuse() => mlme_sme?,
        sme_fidl = sme_fidl.fuse() => sme_fidl?.into_any(),
    }
    Ok(())
}

async fn serve_fidl(sme: Arc<Mutex<Sme>>,
                    new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
                    user_stream: mesh_sme::UserStream<Tokens>)
    -> Result<Never, failure::Error>
{
    let mut fidl_clients = FuturesUnordered::new();
    let mut user_stream = user_stream.fuse();
    let mut new_fidl_clients = new_fidl_clients.fuse();
    loop {
        select! {
            user_event = user_stream.next() => match user_event {
                Some(e) => handle_user_event(e),
                None => bail!("Stream of events from SME unexpectedly ended"),
            },
            new_fidl_client = new_fidl_clients.next() => match new_fidl_client {
                Some(c) => fidl_clients.push(serve_fidl_endpoint(Arc::clone(&sme), c)),
                None => bail!("New FIDL client stream unexpectedly ended"),
            },
            // Drive clients towards completion
            _ = fidl_clients.next() => {},
        }
    }
}

fn handle_user_event(e: UserEvent<Tokens>) {
    match e {
        UserEvent::JoinMeshFinished { token, result } => token.send(result).unwrap_or_else(|_| ()),
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

async fn handle_fidl_request(sme: Arc<Mutex<Sme>>, request: fidl_sme::MeshSmeRequest)
    -> Result<(), ::fidl::Error>
{
    match request {
        fidl_sme::MeshSmeRequest::Join { config, responder } => {
            let code = await!(join_mesh(sme, config));
            responder.send(code)
        },
    }
}

async fn join_mesh(sme: Arc<Mutex<Sme>>, config: fidl_sme::MeshConfig)
    -> fidl_sme::JoinMeshResultCode
{
    let (sender, receiver) = oneshot::channel();
    sme.lock().unwrap().on_join_command(sender, mesh_sme::Config {
        mesh_id: config.mesh_id,
        channel: config.channel,
    });
    let r = await!(receiver).unwrap_or_else(|_| {
        error!("Responder for Join Mesh command was dropped without sending a response");
        mesh_sme::JoinMeshResult::Error
    });
    convert_join_mesh_result(r)
}

fn convert_join_mesh_result(result: mesh_sme::JoinMeshResult) -> fidl_sme::JoinMeshResultCode {
    match result {
        mesh_sme::JoinMeshResult::Success => fidl_sme::JoinMeshResultCode::Success,
        mesh_sme::JoinMeshResult::Error => fidl_sme::JoinMeshResultCode::InternalError,
    }
}
