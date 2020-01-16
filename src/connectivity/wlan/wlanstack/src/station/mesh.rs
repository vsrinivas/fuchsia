// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::stats_scheduler::StatsRequest,
    anyhow::format_err,
    fidl_fuchsia_wlan_mesh as fidl_mesh, fidl_fuchsia_wlan_mlme as fidl_mlme,
    fidl_fuchsia_wlan_mlme::{MlmeEventStream, MlmeProxy},
    fidl_fuchsia_wlan_sme as fidl_sme,
    futures::{
        channel::mpsc,
        prelude::*,
        select,
        stream::{self, FuturesUnordered},
    },
    log::error,
    pin_utils::pin_mut,
    std::{
        marker::Unpin,
        sync::{Arc, Mutex},
        task::Poll,
    },
    void::Void,
    wlan_sme::{mesh as mesh_sme, timer::TimeEntry},
};

pub type Endpoint = fidl::endpoints::ServerEnd<fidl_sme::MeshSmeMarker>;
type Sme = mesh_sme::MeshSme;

pub async fn serve<S>(
    proxy: MlmeProxy,
    device_info: fidl_mlme::DeviceInfo,
    event_stream: MlmeEventStream,
    new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
    stats_requests: S,
) -> Result<(), anyhow::Error>
where
    S: Stream<Item = StatsRequest> + Send + Unpin,
{
    let (sme, mlme_stream) = Sme::new(device_info);
    let sme = Arc::new(Mutex::new(sme));
    let time_stream = stream::poll_fn::<TimeEntry<()>, _>(|_| Poll::Pending);
    let mlme_sme = super::serve_mlme_sme(
        proxy.clone(),
        event_stream,
        Arc::clone(&sme),
        mlme_stream,
        stats_requests,
        time_stream,
    );
    let sme_fidl = serve_fidl(&sme, new_fidl_clients, &proxy);
    pin_mut!(mlme_sme);
    pin_mut!(sme_fidl);
    select! {
        mlme_sme = mlme_sme.fuse() => mlme_sme?,
        sme_fidl = sme_fidl.fuse() => match sme_fidl? {},
    }
    Ok(())
}

async fn serve_fidl<'a>(
    sme: &'a Mutex<Sme>,
    new_fidl_clients: mpsc::UnboundedReceiver<Endpoint>,
    proxy: &'a MlmeProxy,
) -> Result<Void, anyhow::Error> {
    let mut fidl_clients = FuturesUnordered::new();
    let mut new_fidl_clients = new_fidl_clients.fuse();
    loop {
        select! {
            new_fidl_client = new_fidl_clients.next() => match new_fidl_client {
                Some(c) => fidl_clients.push(serve_fidl_endpoint(proxy, sme, c)),
                None => return Err(format_err!("New FIDL client stream unexpectedly ended")),
            },
            // Drive clients towards completion
            _ = fidl_clients.next() => {},
        }
    }
}

async fn serve_fidl_endpoint<'a>(proxy: &'a MlmeProxy, sme: &'a Mutex<Sme>, endpoint: Endpoint) {
    const MAX_CONCURRENT_REQUESTS: usize = 1000;
    let stream = match endpoint.into_stream() {
        Ok(s) => s,
        Err(e) => {
            error!("Failed to create a stream from a zircon channel: {}", e);
            return;
        }
    };
    let r = stream
        .try_for_each_concurrent(MAX_CONCURRENT_REQUESTS, move |request| {
            handle_fidl_request(proxy, &sme, request)
        })
        .await;
    if let Err(e) = r {
        error!("Error serving a FIDL client of Mesh SME: {}", e);
    }
}

async fn handle_fidl_request<'a>(
    proxy: &'a MlmeProxy,
    sme: &'a Mutex<Sme>,
    request: fidl_sme::MeshSmeRequest,
) -> Result<(), ::fidl::Error> {
    match request {
        fidl_sme::MeshSmeRequest::Join { config, responder } => {
            let code = join_mesh(sme, config).await;
            responder.send(code)
        }
        fidl_sme::MeshSmeRequest::Leave { responder } => {
            let code = leave_mesh(sme).await;
            responder.send(code)
        }
        fidl_sme::MeshSmeRequest::GetMeshPathTable { responder } => {
            let (code, mut table) = get_mesh_path_table(proxy, sme).await;
            responder.send(code, &mut table)
        }
    }
}

async fn get_mesh_path_table<'a>(
    proxy: &'a MlmeProxy,
    _sme: &'a Mutex<Sme>,
) -> (fidl_sme::GetMeshPathTableResultCode, fidl_mesh::MeshPathTable) {
    let mut dummy = fidl_mlme::GetMeshPathTableRequest { dummy: 0 };
    let table = proxy.get_mesh_path_table_req(&mut dummy).await;
    match table {
        Ok(tbl) => (fidl_sme::GetMeshPathTableResultCode::Success, tbl),
        Err(err) => {
            error!("Error received in getting mesh path table from MLME: {}", err);
            (
                fidl_sme::GetMeshPathTableResultCode::InternalError,
                fidl_mesh::MeshPathTable { paths: Vec::new() },
            )
        }
    }
}

async fn join_mesh(sme: &Mutex<Sme>, config: fidl_sme::MeshConfig) -> fidl_sme::JoinMeshResultCode {
    let receiver = sme
        .lock()
        .unwrap()
        .on_join_command(mesh_sme::Config { mesh_id: config.mesh_id, channel: config.channel });
    let r = receiver.await.unwrap_or_else(|_| {
        error!("Responder for Join Mesh command was dropped without sending a response");
        mesh_sme::JoinMeshResult::InternalError
    });
    convert_join_mesh_result(r)
}

fn convert_join_mesh_result(result: mesh_sme::JoinMeshResult) -> fidl_sme::JoinMeshResultCode {
    match result {
        mesh_sme::JoinMeshResult::Success => fidl_sme::JoinMeshResultCode::Success,
        mesh_sme::JoinMeshResult::Canceled => fidl_sme::JoinMeshResultCode::Canceled,
        mesh_sme::JoinMeshResult::InternalError => fidl_sme::JoinMeshResultCode::InternalError,
        mesh_sme::JoinMeshResult::InvalidArguments => {
            fidl_sme::JoinMeshResultCode::InvalidArguments
        }
        mesh_sme::JoinMeshResult::DfsUnsupported => fidl_sme::JoinMeshResultCode::DfsUnsupported,
    }
}

async fn leave_mesh(sme: &Mutex<Sme>) -> fidl_sme::LeaveMeshResultCode {
    let receiver = sme.lock().unwrap().on_leave_command();
    let r = receiver.await.unwrap_or_else(|_| {
        error!("Responder for Leave Mesh command was dropped without sending a response");
        mesh_sme::LeaveMeshResult::InternalError
    });
    convert_leave_mesh_result(r)
}

fn convert_leave_mesh_result(result: mesh_sme::LeaveMeshResult) -> fidl_sme::LeaveMeshResultCode {
    match result {
        mesh_sme::LeaveMeshResult::Success => fidl_sme::LeaveMeshResultCode::Success,
        mesh_sme::LeaveMeshResult::InternalError => fidl_sme::LeaveMeshResultCode::InternalError,
    }
}
