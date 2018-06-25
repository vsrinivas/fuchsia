// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

#[macro_use]
extern crate clap;
#[macro_use]
extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_wlan_device as wlan;
extern crate fidl_fuchsia_wlan_device_service as wlan_service;
extern crate fidl_fuchsia_wlan_sme as fidl_sme;
extern crate fuchsia_app as component;
#[macro_use]
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
#[macro_use]
extern crate structopt;

use component::client::connect_to_service;
use failure::{Error, Fail, ResultExt};
use fidl::endpoints2;
use fidl_sme::{ConnectResultCode, ConnectTransactionEvent, ScanTransactionEvent};
use futures::prelude::*;
use std::fmt;
use structopt::StructOpt;
use wlan_service::{DeviceServiceMarker, DeviceServiceProxy};

mod opts;
use opts::*;

type WlanSvc = DeviceServiceProxy;

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    println!("{:?}", opt);

    let mut exec = async::Executor::new().context("error creating event loop")?;
    let wlan_svc =
        connect_to_service::<DeviceServiceMarker>().context("failed to connect to device service")?;

    match opt {
        Opt::Phy(cmd) => exec.run_singlethreaded(do_phy(cmd, wlan_svc)),
        Opt::Iface(cmd) => exec.run_singlethreaded(do_iface(cmd, wlan_svc)),
        Opt::Client(cmd) => exec.run_singlethreaded(do_client(cmd, wlan_svc)),
    }
}

fn do_phy(cmd: opts::PhyCmd, wlan_svc: WlanSvc) -> impl Future<Item = (), Error = Error> {
    match cmd {
        opts::PhyCmd::List => {
            // TODO(tkilbourn): add timeouts to prevent hanging commands
            wlan_svc
                .list_phys()
                .map_err(|e| e.context("error getting response").into())
                .and_then(|response| {
                    println!("response: {:?}", response);
                    Ok(())
                }).left_future()
        },
        opts::PhyCmd::Query { phy_id } => {
            let mut req = wlan_service::QueryPhyRequest { phy_id };
            wlan_svc
                .query_phy(&mut req)
                .map_err(|e| e.context("error querying phy").into())
                .and_then(|response| {
                    println!("response: {:?}", response);
                    Ok(())
                }).right_future()
        }
    }
}

fn do_iface(cmd: opts::IfaceCmd, wlan_svc: WlanSvc) -> impl Future<Item = (), Error = Error> {
    many_futures!(IfaceFut, [ New, Delete, List ]);
    match cmd {
        opts::IfaceCmd::New { phy_id, role } => IfaceFut::New({
            let mut req = wlan_service::CreateIfaceRequest {
                phy_id: phy_id,
                role: role.into(),
            };

            wlan_svc
                .create_iface(&mut req)
                .map_err(|e| e.context("error getting response").into())
                .and_then(|response| {
                    println!("response: {:?}", response);
                    Ok(())
                })
        }),
        opts::IfaceCmd::Delete { phy_id, iface_id } => IfaceFut::Delete({
            let mut req = wlan_service::DestroyIfaceRequest {
                phy_id: phy_id,
                iface_id: iface_id,
            };

            wlan_svc
                .destroy_iface(&mut req)
                .map(move |status| match zx::Status::ok(status) {
                    Ok(()) => println!("destroyed iface {:?}", iface_id),
                    Err(s) => println!("error destroying iface: {:?}", s),
                })
                .map_err(|e| e.context("error destroying iface").into())
                .into_future()
        }),
        opts::IfaceCmd::List => IfaceFut::List({
            wlan_svc.list_ifaces()
                .map_err(|e| e.context("error getting response").into())
                .and_then(|response| {
                    println!("response: {:?}", response);
                    Ok(())
                })
        }),
    }
}

fn do_client(cmd: opts::ClientCmd, wlan_svc: WlanSvc) -> impl Future<Item = (), Error = Error> {
    many_futures!(ClientFut, [ Scan, Connect, Status ]);
    match cmd {
        opts::ClientCmd::Scan { iface_id } => ClientFut::Scan(get_client_sme(wlan_svc, iface_id)
            .and_then(|sme| {
                let (local, remote) = endpoints2::create_endpoints()?;
                let mut req = fidl_sme::ScanRequest {
                    timeout: 10
                };
                sme.scan(&mut req, remote)
                    .map_err(|e| e.context("error sending scan request"))?;
                Ok(local)
            })
            .and_then(handle_scan_transaction)
            .err_into()),
        opts::ClientCmd::Connect { iface_id, ssid } => ClientFut::Connect(get_client_sme(wlan_svc, iface_id)
            .and_then(move |sme| {
                let (local, remote) = endpoints2::create_endpoints()?;
                let mut req = fidl_sme::ConnectRequest {
                    ssid: ssid.as_bytes().to_vec()
                };
                sme.connect(&mut req, Some(remote))
                    .map_err(|e| e.context("error sending connect request"))?;
                Ok(local)
            })
            .and_then(handle_connect_transaction)
            .err_into()),
        opts::ClientCmd::Status { iface_id } => ClientFut::Status(get_client_sme(wlan_svc, iface_id)
            .and_then(|sme| sme.status().err_into())
            .and_then(|st| {
                match st.connected_to {
                    Some(bss) => {
                        println!("Connected to '{}' (bssid {})",
                                 String::from_utf8_lossy(&bss.ssid), Bssid(bss.bssid));
                    },
                    None => println!("Not connected to a network"),
                }
                if !st.connecting_to_ssid.is_empty() {
                    println!("Connecting to '{}'", String::from_utf8_lossy(&st.connecting_to_ssid));
                }
                Ok(())
            }))
    }
}

struct Bssid([u8; 6]);

impl fmt::Display for Bssid {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        write!(f, "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
               self.0[0], self.0[1], self.0[2], self.0[3], self.0[4], self.0[5])
    }
}

fn handle_scan_transaction(scan_txn: fidl_sme::ScanTransactionProxy)
    -> impl Future<Item = (), Error = Error>
{
    scan_txn.take_event_stream()
        .map(|e| {
            match e {
                ScanTransactionEvent::OnResult { aps } => {
                    for ap in aps {
                        println!("{}", String::from_utf8_lossy(&ap.ssid));
                    }
                    false
                },
                ScanTransactionEvent::OnFinished { } => true,
                ScanTransactionEvent::OnError { error } => {
                    eprintln!("Error: {}", error.message);
                    true
                },
            }
        })
        .fold(false, |_prev, done| Ok(done))
        .err_into::<Error>()
        .and_then(|done| {
            if !done {
                bail!("Failed to fetch all results before the channel was closed");
            }
            Ok(())
        })
}

fn handle_connect_transaction(connect_txn: fidl_sme::ConnectTransactionProxy)
    -> impl Future<Item = (), Error = Error>
{
    connect_txn.take_event_stream()
        .map(|e| {
            match e {
                ConnectTransactionEvent::OnFinished { code } => {
                    match code {
                        ConnectResultCode::Success => println!("Connected successfully"),
                        ConnectResultCode::Canceled =>
                            eprintln!("Connecting was canceled or superseded by another command"),
                        ConnectResultCode::Failed => eprintln!("Failed to connect to network"),
                    }
                    true
                },
            }
        })
        .fold(false, |_prev, done| Ok(done))
        .err_into::<Error>()
        .and_then(|done| {
            if !done {
                bail!("Failed to receiver a connect result before the channel was closed");
            }
            Ok(())
        })
}

fn get_client_sme(wlan_svc: WlanSvc, iface_id: u16)
    -> impl Future<Item = fidl_sme::ClientSmeProxy, Error = Error>
{
    endpoints2::create_endpoints()
        .into_future()
        .map_err(|e| e.into())
        .and_then(move |(proxy, remote)| {
            wlan_svc.get_client_sme(iface_id, remote)
                .map_err(|e| e.context("error sending GetClientSme request").into())
                .and_then(move |status| {
                    if status == zx::sys::ZX_OK {
                        Ok(proxy)
                    } else {
                        Err(format_err!("Invalid interface id {}", iface_id))
                    }
                })
        })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn format_bssid() {
        assert_eq!("01:02:03:ab:cd:ef",
               format!("{}", Bssid([ 0x01, 0x02, 0x03, 0xab, 0xcd, 0xef])));
    }
}