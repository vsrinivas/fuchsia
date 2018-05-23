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
use futures::{future, prelude::*};
use futures::channel::mpsc;
use async;
use zx;

struct ClientTokens;

impl client::Tokens for ClientTokens {
    type ScanToken = fidl_sme::ScanTransactionControlHandle;
}

type Client = client::ClientSme<ClientTokens>;

pub fn serve_client_sme(proxy: MlmeProxy, new_fidl_clients: mpsc::UnboundedReceiver<async::Channel>)
    -> impl Future<Item = (), Error = ::fidl::Error>
{
    let (client, mlme_stream, user_stream) = Client::new();
    let client_arc = Arc::new(Mutex::new(client));

    // A future that handles MLME interactions
    let station_server = serve_station(proxy, client_arc.clone(), mlme_stream);
    // A future that forwards user events from the station to connected FIDL clients
    let user_stream_server = serve_user_stream(user_stream)
        // Map 'Never' to 'fidl::Error'
        .map_err(|_| panic!("'Never' should never happen"));
    // A future that handles requests from FIDL clients
    let wlan_server = new_fidl_clients.for_each_concurrent(move |channel| {
        new_client_service(client_arc.clone(), channel).recover(
            |e| eprintln!("Error handling a FIDL request from user: {:?}", e)
        )
    }).map_err(|e| e.never_into());
    station_server.join3(user_stream_server, wlan_server).map(|_| ())
}

fn serve_station<S: Station>(proxy: MlmeProxy, station: Arc<Mutex<S>>, mlme_stream: MlmeStream)
     -> impl Future<Item = (), Error = ::fidl::Error>
{
    let event_handler = proxy.take_event_stream().for_each(move |e| {
        station.lock().unwrap().on_mlme_event(e);
        Ok(())
    });
    let mlme_sender = mlme_stream
        // Map 'Never' to 'fidl::Error'
        .map_err(|_| panic!("'Never' should never happen"))
        .for_each(move |e| {
            match e {
                MlmeRequest::Scan(mut req) => proxy.scan_req(&mut req),
                MlmeRequest::Join(mut req) => proxy.join_req(&mut req),
                MlmeRequest::Authenticate(mut req) => proxy.authenticate_req(&mut req),
                MlmeRequest::Associate(mut req) => proxy.associate_req(&mut req),
                MlmeRequest::Deauthenticate(mut req) => proxy.deauthenticate_req(&mut req),
            }
        });
    event_handler.join(mlme_sender).map(|_| ())
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