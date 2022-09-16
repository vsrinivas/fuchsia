// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(armansito): Remove this once a server channel can be killed using a Controller
#![allow(unreachable_code)]

use anyhow::{format_err, Context, Error};
use fidl::{endpoints, endpoints::Proxy};
use fidl_fuchsia_bluetooth_gatt2::ClientMarker;
use fidl_fuchsia_bluetooth_le::{
    CentralProxy, ConnectionMarker, ConnectionOptions, ScanResultWatcherProxy,
};
use fuchsia_bluetooth::{
    error::Error as BTError,
    types::{le::Peer, PeerId, Uuid},
};
use futures::{future::FutureExt, pin_mut, select};
use parking_lot::RwLock;
use std::{convert::TryFrom, sync::Arc};

use crate::gatt::repl::start_gatt_loop;

pub type CentralStatePtr = Arc<RwLock<CentralState>>;

pub struct CentralState {
    // If `Some(n)`, stop scanning and close the delegate handle after n more scan results.
    pub remaining_scan_results: Option<u64>,

    // If true, attempt to connect to the first scan result.
    pub connect: bool,

    // The proxy that we use to perform LE central requests.
    svc: CentralProxy,
}

impl CentralState {
    pub fn new(proxy: CentralProxy) -> CentralStatePtr {
        Arc::new(RwLock::new(CentralState {
            remaining_scan_results: None,
            connect: false,
            svc: proxy,
        }))
    }

    pub fn get_svc(&self) -> &CentralProxy {
        &self.svc
    }

    /// If the remaining_scan_results is specified, decrement until it reaches 0.
    /// Scanning should continue if the user is not attempted to connect to a device
    /// and there are remaining scans left to run.
    ///
    /// return: true if scanning should continue
    ///         false if scanning should stop
    pub fn decrement_scan_count(&mut self) -> bool {
        self.remaining_scan_results = self.remaining_scan_results.map(
            // decrement until n is 0
            |n| if n > 0 { n - 1 } else { n },
        );
        // scanning should continue if connection will not be attempted and
        // there are remaining scan results
        !(self.connect || self.remaining_scan_results.iter().any(|&n| n == 0))
    }
}

/// Watch for scan results from the given `result_watcher`. If `state.connect`, then try to connect
/// to the first connectable peer and stop scanning. Returns when `state.remaining_scan_results`
/// results have been received, or after the connected peer disconnects (whichever happens first).
pub async fn watch_scan_results(
    state: CentralStatePtr,
    result_watcher: ScanResultWatcherProxy,
) -> Result<(), Error> {
    if result_watcher.is_closed() {
        return Err(format_err!("ScanResultWatcherProxy closed"));
    }

    loop {
        let fidl_peers: Vec<fidl_fuchsia_bluetooth_le::Peer> = result_watcher
            .watch()
            .await
            .map_err(|e| BTError::new(&format!("ScanResultWatcherProxy error: {}", e)))?;

        for fidl_peer in fidl_peers {
            let peer = Peer::try_from(fidl_peer)?;
            eprintln!(" {}", peer);

            if state.write().decrement_scan_count() {
                continue;
            }

            if state.read().connect && peer.connectable {
                // connect_peripheral will log errors, so the result can be ignored.
                // TODO(fxbug.dev/108816): Use Central.Connect instead of deprecated Central.ConnectPeripheral.
                let _ = connect(&state, peer.id, None).await;
            }

            return Ok(());
        }
    }
}

/// Attempts to connect to the peripheral with the given `peer_id` and begins the GATT REPL if this succeeds.
/// If `service_uuid` is specified, limit GATT service discovery to services with the indicated UUID.
pub async fn connect(
    state: &CentralStatePtr,
    peer_id: PeerId,
    service_uuid: Option<Uuid>,
) -> Result<(), Error> {
    let (conn_proxy, conn_server) = endpoints::create_proxy::<ConnectionMarker>()
        .context("Failed to create Connection endpoints")?;

    let conn_opts = ConnectionOptions {
        bondable_mode: Some(true),
        service_filter: service_uuid.map(Into::into),
        ..ConnectionOptions::EMPTY
    };

    state
        .read()
        .svc
        .connect(&mut peer_id.into(), conn_opts, conn_server)
        .context("Failed to connect")?;

    let (gatt_proxy, gatt_server) = endpoints::create_proxy::<ClientMarker>()
        .context("Failed to create GATT Client endpoints")?;
    conn_proxy.request_gatt_client(gatt_server).context("GATT client request failed")?;

    let mut conn_closed_fut = conn_proxy.on_closed().fuse();
    let gatt_loop_fut = start_gatt_loop(gatt_proxy).fuse();
    pin_mut!(gatt_loop_fut);
    select! {
        result = gatt_loop_fut => result,
        _ = conn_closed_fut => {
            println!("connection closed");
            Ok(())
        },
    }
}
