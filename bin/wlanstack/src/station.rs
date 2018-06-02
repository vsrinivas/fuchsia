// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]
#![allow(unused_variables)]

use fidl;
use wlan_sme::{client, Station, MlmeRequest, MlmeStream};
use wlan_sme::client::{DiscoveryError, DiscoveryResult, DiscoveredEss};
use fidl_mlme::MlmeProxy;
use fidl_sme::{self, ClientSme, ScanTransaction};
use std::sync::{Arc, Mutex};
use failure;
use futures::{future, prelude::*, stream};
use futures::channel::mpsc;
use async;
use zx;

struct ClientTokens;

impl client::Tokens for ClientTokens {
    type ScanToken = fidl_sme::ScanTransactionControlHandle;
}

type Client = client::ClientSme<ClientTokens>;

pub fn serve_client_sme(proxy: MlmeProxy, new_fidl_clients: mpsc::UnboundedReceiver<async::Channel>)
    -> impl Future<Item = (), Error = failure::Error>
{
    let (client, mlme_stream, user_stream) = Client::new();
    let client_arc = Arc::new(Mutex::new(client));
    // A future that handles MLME interactions
    let mlme_sme_fut = serve_mlme_sme(proxy, client_arc.clone(), mlme_stream);
    // A future that forwards user events from the station to connected FIDL clients
    let sme_fidl_fut = serve_client_sme_fidl(client_arc, new_fidl_clients, user_stream)
        .map(|x| x.never_into::<()>());
    mlme_sme_fut.select(sme_fidl_fut)
        .map(|_| ())
        .map_err(|e| e.either(|(x, _)| x.context("MLME<->SME future").into(),
                              |(x, _)| x.context("SME<->FIDL future").into()))
}

fn serve_client_sme_fidl(client_arc: Arc<Mutex<Client>>,
                         new_fidl_clients: mpsc::UnboundedReceiver<async::Channel>,
                         user_stream: client::UserStream<ClientTokens>)
    -> impl Future<Item = Never, Error = failure::Error>
{
    let sme_to_fidl = serve_user_stream(user_stream)
        // Map 'Never' to 'fidl::Error'
        .map_err(|e| e.never_into())
        .and_then(|()| Err(format_err!("SME->FIDL future unexpectedly finished")));
    // A future that handles requests from FIDL clients
    let fidl_to_sme = new_fidl_clients
        .map_err(|e| e.never_into())
        .chain(stream::once(Err(format_err!("new FIDL client stream unexpectedly ended"))))
        .for_each_concurrent(move |channel| {
            new_client_service(client_arc.clone(), channel).recover(
                |e| eprintln!("Error handling a FIDL request from user: {:?}", e)
            )
        })
        .and_then(|_| Err(format_err!("FIDL->SME future unexpectedly finished")));
    sme_to_fidl.join(fidl_to_sme).map(|x: (Never, Never)| x.0)
}

// The returned future successfully terminates when MLME closes the channel
fn serve_mlme_sme<S: Station>(proxy: MlmeProxy, station: Arc<Mutex<S>>, mlme_stream: MlmeStream)
     -> impl Future<Item = (), Error = failure::Error>
{
    let sme_to_mlme_fut = proxy.take_event_stream().for_each(move |e| {
        station.lock().unwrap().on_mlme_event(e);
        Ok(())
    }).map(|_| ()).err_into::<failure::Error>();
    let mlme_to_sme_fut = mlme_stream
        // Map 'Never' to 'fidl::Error'
        .map_err(|e| e.never_into())
        .for_each(move |e| {
            match e {
                MlmeRequest::Scan(mut req) => proxy.scan_req(&mut req),
                MlmeRequest::Join(mut req) => proxy.join_req(&mut req),
                MlmeRequest::Authenticate(mut req) => proxy.authenticate_req(&mut req),
                MlmeRequest::Associate(mut req) => proxy.associate_req(&mut req),
                MlmeRequest::Deauthenticate(mut req) => proxy.deauthenticate_req(&mut req),
            }
        })
        .then(|r| match r {
            Ok(_) => Err(format_err!("SME->MLME sender future unexpectedly finished")),
            Err(fidl::Error::ClientWrite(status)) => {
                if status == zx::Status::PEER_CLOSED {
                    // Don't treat closed channel as error; terminate the future peacefully instead
                    Ok(())
                } else {
                    Err(fidl::Error::ClientWrite(status).into())
                }
            }
            Err(other) => Err(other.into()),
        });
    // Select, not join: terminate as soon as one of the futures terminates
    sme_to_mlme_fut.select(mlme_to_sme_fut)
        .map(|_| ())
        .map_err(|e| e.either(|(x, _)| x.context("MLME->SME").into(),
                              |(x, _)| x.context("SME->MLME").into()))
}

fn new_client_service(client_arc: Arc<Mutex<Client>>, channel: async::Channel)
    -> impl Future<Item = (), Error = ::fidl::Error>
{
    fidl_sme::ClientSmeImpl {
        state: client_arc.clone(),
        on_open: |_, _| {
            future::ok(())
        },
        scan: |state, _req, txn, c| {
            match new_scan_transaction(txn.into_channel()) {
                Ok(token) => {
                    state.lock().unwrap().on_scan_command(token);
                },
                Err(e) => {
                    eprintln!("Error starting a scan transaction: {:?}", e);
                }
            }
            future::ok(())
        },
    }.serve(channel)
}


fn new_scan_transaction(channel: zx::Channel)
    -> Result<fidl_sme::ScanTransactionControlHandle, failure::Error>
{
    let local = async::Channel::from_channel(channel)?;
    let server_future = fidl_sme::ScanTransactionImpl {
        state: (),
        on_open: |_, _| {
            future::ok(())
        }
    }.serve(local);
    Ok(server_future.control_handle())
}

fn serve_user_stream(stream: client::UserStream<ClientTokens>)
    -> impl Future<Item = (), Error = Never>
{
    stream
        .for_each(|e| {
            match e {
                client::UserEvent::ScanFinished{ token, result } => {
                    send_scan_results(token, result).unwrap_or_else(|e| {
                        eprintln!("Error sending scan results to user: {:?}", e);
                    })
                }
            }
            Ok(())
        })
        .map(|_| ())
}

fn send_scan_results(token: fidl_sme::ScanTransactionControlHandle,
                     result: DiscoveryResult)
    -> Result<(), fidl::Error>
{
    match result {
        Ok(ess_list) => {
            let mut results = ess_list.into_iter().map(convert_scan_result).collect::<Vec<_>>();
            token.send_on_result(&mut results.iter_mut())?;
            token.send_on_finished()?;
        },
        Err(e) => {
            token.send_on_error(&mut fidl_sme::ScanError {
                code: match &e {
                    DiscoveryError::NotSupported => fidl_sme::ScanErrorCode::NotSupported,
                },
                message: e.to_string()
            })?;
        }
    }
    Ok(())
}

fn convert_scan_result(ess: DiscoveredEss) -> fidl_sme::ScanResult {
    fidl_sme::ScanResult {
        bssid: ess.best_bss,
        ssid: ess.ssid,
    }
}