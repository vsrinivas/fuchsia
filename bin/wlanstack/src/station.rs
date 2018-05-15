// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(dead_code)]

use wlan_sme::{client, Station, MlmeRequest, MlmeStream};
use fidl_mlme::{self, MlmeProxy};
use fidl_wlan_service::{self, Wlan};
use std::sync::{Arc, Mutex};
use futures::{future, prelude::*};
use async;

struct ClientTokens;

impl client::Tokens for ClientTokens {
    type ScanToken = fidl_wlan_service::WlanScanResponder;
}

type ClientSme = client::ClientSme<ClientTokens>;

pub fn serve_client<T>(proxy: MlmeProxy, new_fidl_clients: T)
    -> impl Future<Item = (), Error = ::fidl::Error>
    where T: Stream<Item = async::Channel, Error = ::fidl::Error>
{
    let (client, mlme_stream, user_stream) = ClientSme::new();
    let client_arc = Arc::new(Mutex::new(client));

    // A future that handles MLME interactions
    let station_server = serve_station(proxy, client_arc.clone(), mlme_stream);
    // A future that forwards user events from the station to connected FIDL clients
    let user_stream_server = serve_user_stream(user_stream)
        // Map 'Never' to 'fidl::Error'
        .map_err(|_| panic!("'Never' should never happen"));
    // A future that handles requests from FIDL clients
    let wlan_server = new_fidl_clients.for_each_concurrent(move |channel| {
        new_wlan_service(client_arc.clone(), channel).recover(
            |e| eprintln!("Error handling a FIDL request from user: {:?}", e)
        )
    });
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

fn new_wlan_service(client_arc: Arc<Mutex<ClientSme>>, channel: async::Channel)
    -> impl Future<Item = (), Error = ::fidl::Error>
{
    // TODO(gbonik): Use a proper "Client Iface" FIDL interface instead of WlanService
    fidl_wlan_service::WlanImpl {
        state: client_arc.clone(),
        on_open: |_, _| {
            future::ok(())
        },
        scan: |state, _req, resp| {
            state.lock().unwrap().on_scan_command(resp);
            future::ok(())
        },
        connect: |state, req, _resp| {
            state.lock().unwrap().on_connect_command(req.ssid);
            future::ok(())
        },
        disconnect: |_state, _resp| {
            future::ok(())
        },
        status: |_state, _resp| {
            future::ok(())
        },
        start_bss: |_state, _req, _resp| {
            future::ok(())
        },
        stop_bss: |_state, _resp| {
            future::ok(())
        }
    }.serve(channel)
}

fn serve_user_stream(stream: client::UserStream<ClientTokens>)
    -> impl Future<Item = (), Error = Never>
{
    stream
        .for_each(|e| {
            match e {
                client::UserEvent::ScanFinished{ token, result } => {
                    token.send(&mut convert_scan_result(result)).unwrap_or_else(|e| {
                        // TODO(gbonik): stop serving the channel?
                        eprintln!("Error sending scan results to user: {:?}", e);
                    })
                }
            }
            Ok(())
        })
        .map(|_| ())
}

fn convert_scan_result(_sc: fidl_mlme::ScanConfirm) -> fidl_wlan_service::ScanResult {
    // TODO(gbonik): actually convert the result. Also figure out the appropriate place
    // to hold this logic.
    fidl_wlan_service::ScanResult {
        error: fidl_wlan_service::Error {
            code: fidl_wlan_service::ErrCode::Ok,
            description: "".to_string(),
        },
        aps: Some(vec![])
    }
}