// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api, arbitrary_self_types, pin)]
#![deny(warnings)]

use failure::{format_err, Error, ResultExt};
use fidl::endpoints2;
use fidl_fuchsia_wlan_device_service::{self as wlan_service, DeviceServiceMarker,
                                       DeviceServiceProxy};
use fidl_fuchsia_wlan_sme::{self as fidl_sme, ConnectResultCode, ConnectTransactionEvent,
                            ScanTransactionEvent};
use fuchsia_app::client::connect_to_service;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::prelude::*;
use std::fmt;
use structopt::StructOpt;

mod opts;
use crate::opts::*;

type WlanSvc = DeviceServiceProxy;

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    println!("{:?}", opt);

    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let wlan_svc =
        connect_to_service::<DeviceServiceMarker>().context("failed to connect to device service")?;

    let fut = async {
        match opt {
            Opt::Phy(cmd) => await!(do_phy(cmd, wlan_svc)),
            Opt::Iface(cmd) => await!(do_iface(cmd, wlan_svc)),
            Opt::Client(cmd) => await!(do_client(cmd, wlan_svc)),
        }
    };
    exec.run_singlethreaded(fut)
}

async fn do_phy(cmd: opts::PhyCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    match cmd {
        opts::PhyCmd::List => {
            // TODO(tkilbourn): add timeouts to prevent hanging commands
            let response = await!(wlan_svc.list_phys()).context("error getting response")?;
            println!("response: {:?}", response);
        }
        opts::PhyCmd::Query { phy_id } => {
            let mut req = wlan_service::QueryPhyRequest { phy_id };
            let response = await!(wlan_svc.query_phy(&mut req)).context("error querying phy")?;
            println!("response: {:?}", response);
        }
    }
    Ok(())
}

async fn do_iface(cmd: opts::IfaceCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    match cmd {
        opts::IfaceCmd::New { phy_id, role } => {
            let mut req = wlan_service::CreateIfaceRequest {
                phy_id: phy_id,
                role: role.into(),
            };

            let response = await!(wlan_svc.create_iface(&mut req)).context("error getting response")?;
            println!("response: {:?}", response);
        }
        opts::IfaceCmd::Delete { phy_id, iface_id } => {
            let mut req = wlan_service::DestroyIfaceRequest {
                phy_id: phy_id,
                iface_id: iface_id,
            };

            let response = await!(wlan_svc.destroy_iface(&mut req)).context("error destroying iface")?;
            match zx::Status::ok(response) {
                Ok(()) => println!("destroyed iface {:?}", iface_id),
                Err(s) => println!("error destroying iface: {:?}", s),
            }
        }
        opts::IfaceCmd::List => {
            let response = await!(wlan_svc.list_ifaces()).context("error getting response")?;
            println!("response: {:?}", response);
        }
    }
    Ok(())
}

async fn do_client(cmd: opts::ClientCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    match cmd {
        opts::ClientCmd::Scan { iface_id } => {
            let sme = await!(get_client_sme(wlan_svc, iface_id))?;
            let (local, remote) = endpoints2::create_endpoints()?;
            let mut req = fidl_sme::ScanRequest { timeout: 10 };
            sme.scan(&mut req, remote).context("error sending scan request")?;
            await!(handle_scan_transaction(local))
        }
        opts::ClientCmd::Connect { iface_id, ssid, password } => {
            let sme = await!(get_client_sme(wlan_svc, iface_id))?;
            let (local, remote) = endpoints2::create_endpoints()?;
            let mut req = fidl_sme::ConnectRequest {
                ssid: ssid.as_bytes().to_vec(),
                password: password.as_bytes().to_vec(),
            };
            sme.connect(&mut req, Some(remote)).context("error sending connect request")?;
            await!(handle_connect_transaction(local))
        }
        opts::ClientCmd::Disconnect { iface_id } => {
            let sme = await!(get_client_sme(wlan_svc, iface_id))?;
            await!(sme.disconnect())
                .map_err(|e| format_err!("error sending disconnect request: {}", e))
        },
        opts::ClientCmd::Status { iface_id } => {
            let sme = await!(get_client_sme(wlan_svc, iface_id))?;
            let st = await!(sme.status())?;
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
        }
    }
}

struct Bssid([u8; 6]);

impl fmt::Display for Bssid {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        write!(f, "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
               self.0[0], self.0[1], self.0[2], self.0[3], self.0[4], self.0[5])
    }
}

async fn handle_scan_transaction(scan_txn: fidl_sme::ScanTransactionProxy) -> Result<(), Error> {
    let mut printed_header = false;
    let mut events = scan_txn.take_event_stream();
    while let Some(evt) = await!(events.try_next()).context("failed to fetch all results before the channel was closed")? {
        match evt {
            ScanTransactionEvent::OnResult { aps } => {
                if !printed_header {
                    print_scan_header();
                    printed_header = true;
                }
                for ap in aps {
                    print_scan_result(ap);
                }
            }
            ScanTransactionEvent::OnFinished { } => break,
            ScanTransactionEvent::OnError { error } => {
                eprintln!("Error: {}", error.message);
                break;
            },
        }
    }
    Ok(())
}

fn print_scan_header() {
    println!("BSSID             dBm  Channel Protected SSID");
}

fn print_scan_result(ess: fidl_sme::EssInfo) {
    println!("{} {:4} {:7} {:9} {}",
        Bssid(ess.best_bss.bssid),
        ess.best_bss.rx_dbm,
        ess.best_bss.channel,
        if ess.best_bss.protected { "Y" } else { "N" },
        String::from_utf8_lossy(&ess.best_bss.ssid));
}

async fn handle_connect_transaction(connect_txn: fidl_sme::ConnectTransactionProxy)
    -> Result<(), Error>
{
    let mut events = connect_txn.take_event_stream();
    while let Some(evt) = await!(events.try_next()).context("failed to receive connect result before the channel was closed")? {
        match evt {
            ConnectTransactionEvent::OnFinished { code } => {
                match code {
                    ConnectResultCode::Success => println!("Connected successfully"),
                    ConnectResultCode::Canceled =>
                        eprintln!("Connecting was canceled or superseded by another command"),
                    ConnectResultCode::Failed => eprintln!("Failed to connect to network"),
                    ConnectResultCode::BadCredentials =>
                        eprintln!("Failed to connect to network; bad credentials"),
                }
                break;
            }
        }
    }
    Ok(())
}

async fn get_client_sme(wlan_svc: WlanSvc, iface_id: u16)
    -> Result<fidl_sme::ClientSmeProxy, Error>
{
    let (proxy, remote) = endpoints2::create_endpoints()?;
    let status = await!(wlan_svc.get_client_sme(iface_id, remote)).context("error sending GetClientSme request")?;
    if status == zx::sys::ZX_OK {
        Ok(proxy)
    } else {
        Err(format_err!("Invalid interface id {}", iface_id))
    }
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
