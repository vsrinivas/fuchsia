// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use ess_store::{EssStore, SavedEss};
use failure;
use fidl::endpoints2::create_endpoints;
use fidl_sme;
use future_util::retry_until;
use futures::{prelude::*, channel::oneshot, channel::mpsc, future::Either, stream};
use state_machine::{self, IntoStateExt};
use std::sync::Arc;
use zx::prelude::*;

const AUTO_CONNECT_RETRY_SECONDS: u64 = 10;
const AUTO_CONNECT_SCAN_TIMEOUT_SECONDS: u8 = 20;

#[derive(Clone)]
pub struct Client {
    req_sender: mpsc::UnboundedSender<ManualRequest>,
}

impl Client {
    pub fn connect(&self, request: ConnectRequest) -> Result<(), failure::Error> {
        self.req_sender.unbounded_send(ManualRequest::Connect(request))
            .map_err(|_| format_err!("Station does not exist anymore"))
    }
}

pub struct ConnectRequest {
    pub ssid: Vec<u8>,
    pub password: Vec<u8>,
    pub responder: oneshot::Sender<fidl_sme::ConnectResultCode>,
}

enum ManualRequest {
    Connect(ConnectRequest),
}

pub fn new_client(iface_id: u16,
                  sme: fidl_sme::ClientSmeProxy,
                  ess_store: Arc<EssStore>)
    -> (Client, impl Future<Item = (), Error = Never>)
{
    let (req_sender, req_receiver) = mpsc::unbounded();
    let sme_event_stream = sme.take_event_stream();
    let services = Services {
        sme,
        ess_store: Arc::clone(&ess_store)
    };
    let state_machine = auto_connect_state(services, req_receiver.next()).into_future()
        .map(Never::never_into::<()>)
        .recover::<Never, _>(move |e| eprintln!("wlancfg: Client station state machine \
                    for iface {} terminated with an error: {}", iface_id, e));
    let removal_watcher = sme_event_stream.for_each(|_| Ok(()))
        .map(move |_| println!("wlancfg: Client station removed (iface {})", iface_id))
        .recover::<Never, _>(move |e|
            println!("wlancfg: Removing client station (iface {}) because of an error: {}", iface_id, e));
    let fut = state_machine.select(removal_watcher)
        .map(|_| ())
        .recover(|_| ());
    let client = Client { req_sender };
    (client, fut)
}

type State = state_machine::State<failure::Error>;
type NextReqFut = stream::StreamFuture<mpsc::UnboundedReceiver<ManualRequest>>;

#[derive(Clone)]
struct Services {
    sme: fidl_sme::ClientSmeProxy,
    ess_store: Arc<EssStore>,
}

fn auto_connect_state(services: Services, next_req: NextReqFut) -> State {
    println!("wlancfg: Starting auto-connect loop");
    auto_connect(services.clone()).select(next_req)
        .map_err(|e| e.either(|(left, _)| left,
                              |((right, _), _)| right.never_into()))
        .and_then(move |r| match r {
            Either::Left((_ssid, next_req)) => Ok(connected_state(services, next_req)),
            Either::Right(((req, req_stream), _))
                => handle_manual_request(services, req, req_stream)
        })
        .into_state()
}

fn handle_manual_request(services: Services,
                         req: Option<ManualRequest>,
                         req_stream: mpsc::UnboundedReceiver<ManualRequest>)
    -> Result<State, failure::Error>
{
    match req {
        Some(ManualRequest::Connect(req)) => {
            Ok(manual_connect_state(services, req_stream.next(), req))
        },
        None => bail!("The stream of user requests ended unexpectedly")
    }
}

fn auto_connect(services: Services)
    -> impl Future<Item = Vec<u8>, Error = failure::Error>
{
    retry_until(AUTO_CONNECT_RETRY_SECONDS.seconds(),
        move || attempt_auto_connect(services.clone()))
}

fn attempt_auto_connect(services: Services)
    -> impl Future<Item = Option<Vec<u8>>, Error = failure::Error>
{
    start_scan_txn(&services.sme)
        .into_future()
        .and_then(fetch_scan_results)
        .and_then(move |results| {
            let saved_networks = {
                let services = services.clone();
                results.into_iter()
                    .filter_map(move |ess| {
                        services.ess_store.lookup(&ess.best_bss.ssid)
                            .map(|saved_ess| (ess.best_bss.ssid, saved_ess))
                    })
            };
            stream::iter_ok(saved_networks)
                .skip_while(move |(ssid, saved_ess)| {
                    connect_to_saved_network(&services.sme, ssid, saved_ess)
                        .map(|connected| !connected)
                })
                .next()
                .map_err(|(e, _stream)| e)
        })
        .map(|(item, _)| item.map(|(ssid, _)| ssid))
}

fn connect_to_saved_network(sme: &fidl_sme::ClientSmeProxy, ssid: &[u8], saved_ess: &SavedEss)
    -> impl Future<Item = bool, Error = failure::Error>
{
    let ssid_str = String::from_utf8_lossy(ssid).into_owned();
    println!("wlancfg: Auto-connecting to '{}'", ssid_str);
    start_connect_txn(sme, &ssid, &saved_ess.password)
        .into_future()
        .and_then(wait_until_connected)
        .map(move |r| match r {
            fidl_sme::ConnectResultCode::Success => {
                println!("wlancfg: Auto-connected to '{}'", ssid_str);
                true
            },
            other => {
                println!("wlancfg: Failed to auto-connect to '{}': {:?}", ssid_str, other);
                false
            },
        })
}

fn manual_connect_state(services: Services, next_req: NextReqFut, req: ConnectRequest) -> State {
    println!("wlancfg: Connecting to '{}' because of a manual request from the user",
        String::from_utf8_lossy(&req.ssid));
    services.ess_store.store(req.ssid.clone(), SavedEss {
        password: req.password.clone()
    }).unwrap_or_else(|e| eprintln!("wlancfg: Failed to store network password: {}", e));

    let connect_fut = start_connect_txn(&services.sme, &req.ssid, &req.password)
        .into_future()
        .and_then(wait_until_connected);

    connect_fut.select(next_req)
        .map_err(|e| e.either(|(left, _)| left,
                              |((right, _), _)| right.never_into()))
        .and_then(move |r| match r {
            Either::Left((error_code, next_req)) => {
                req.responder.send(error_code).unwrap_or_else(|_| ());
                Ok(match error_code {
                    fidl_sme::ConnectResultCode::Success => {
                        println!("wlancfg: Successfully connected to '{}'", String::from_utf8_lossy(&req.ssid));
                        connected_state(services, next_req)
                    },
                    other => {
                        println!("wlancfg: Failed to connect to '{}': {:?}",
                                 String::from_utf8_lossy(&req.ssid), other);
                        auto_connect_state(services, next_req)
                    }
                })
            },
            Either::Right(((new_req, req_stream), _coonect_fut)) => {
                req.responder.send(fidl_sme::ConnectResultCode::Canceled).unwrap_or_else(|_| ());
                handle_manual_request(services, new_req, req_stream)
            }
        })
        .into_state()
}

fn connected_state(services: Services, next_req: NextReqFut) -> State {
    // TODO(gbonik): monitor connection status and jump back to auto-connect state when disconnected
    next_req
        .map_err(|(e, _stream)| e.never_into())
        .and_then(|(req, req_stream)| {
            handle_manual_request(services, req, req_stream)
        }).into_state()
}

fn start_scan_txn(sme: &fidl_sme::ClientSmeProxy)
    -> Result<fidl_sme::ScanTransactionProxy, failure::Error>
{
    let (scan_txn, remote) = create_endpoints()?;
    let mut req = fidl_sme::ScanRequest {
        timeout: AUTO_CONNECT_SCAN_TIMEOUT_SECONDS,
    };
    sme.scan(&mut req, remote)?;
    Ok(scan_txn)
}

fn start_connect_txn(sme: &fidl_sme::ClientSmeProxy, ssid: &[u8], password: &[u8])
    -> Result<fidl_sme::ConnectTransactionProxy, failure::Error>
{
    let (connect_txn, remote) = create_endpoints()?;
    let mut req = fidl_sme::ConnectRequest { ssid: ssid.to_vec(), password: password.to_vec() };
    sme.connect(&mut req, Some(remote))?;
    Ok(connect_txn)
}

fn wait_until_connected(txn: fidl_sme::ConnectTransactionProxy)
    -> impl Future<Item = fidl_sme::ConnectResultCode, Error = failure::Error>
{
    txn.take_event_stream()
        .filter_map(|e| Ok(match e {
            fidl_sme::ConnectTransactionEvent::OnFinished{ code } => Some(code),
        }))
        .next()
        .map_err(|(e, _stream)| e.into())
        .and_then(|(code, _stream)| code.ok_or_else(||
            format_err!("Server closed the ConnectTransaction channel before sending a response"))
        )
}

fn fetch_scan_results(txn: fidl_sme::ScanTransactionProxy)
    -> impl Future<Item = Vec<fidl_sme::EssInfo>, Error = failure::Error>
{
    txn.take_event_stream().fold(Vec::new(), |mut old_aps, event| {
        match event {
            fidl_sme::ScanTransactionEvent::OnResult { aps } => {
                old_aps.extend(aps);
                Ok(old_aps)
            },
            fidl_sme::ScanTransactionEvent::OnFinished { } => Ok(old_aps),
            fidl_sme::ScanTransactionEvent::OnError { error } => {
                eprintln!("wlancfg: Scanning failed with error: {:?}", error);
                Ok(old_aps)
            }
        }
    }).err_into()
}
