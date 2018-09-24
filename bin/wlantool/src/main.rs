// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api, arbitrary_self_types, pin)]
#![deny(warnings)]

use failure::{bail, format_err, Error, ResultExt};
use fidl::endpoints;
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
            Opt::Ap(cmd) => await!(do_ap(cmd, wlan_svc)),
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
        },
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
        },
        opts::IfaceCmd::List => {
            let response = await!(wlan_svc.list_ifaces()).context("error getting response")?;
            println!("response: {:?}", response);
        },
        opts::IfaceCmd::Stats { iface_id } => {
            let ids = match iface_id {
                Some(id) => vec![id],
                None => {
                  let response = await!(wlan_svc.list_ifaces()).context("error listing ifaces")?;
                  response.ifaces.into_iter().map(|iface| iface.iface_id).collect()
                }
            };

            for iface_id in ids {
                let (status, resp) = await!(wlan_svc.get_iface_stats(iface_id))
                                      .context("error getting stats for iface")?;
                match status {
                    zx::sys::ZX_OK => {
                        match resp {
                            // TODO(eyw): Implement fmt::Display
                            Some(r) => println!("Iface {}: {:#?}", iface_id, r),
                            None => println!("Iface {} returns empty stats resonse", iface_id),
                        }
                    },
                    status => println!("error getting stats for Iface {}: {}", iface_id, status),
                }
            }
        },
    }
    Ok(())
}


fn parse_phy_str(phy_str: Option<String>) -> Result<(bool, fidl_sme::Phy), Error> {
    let phy_str = match phy_str {
        None => return Ok((false, fidl_sme::Phy::Ht)),
        Some(x) => x.to_uppercase(),
    };
    let phy = match phy_str.as_str() {
        "HR" => fidl_sme::Phy::Hr,
        "ERP" => fidl_sme::Phy::Erp,
        "HT" => fidl_sme::Phy::Ht,
        "VHT" => fidl_sme::Phy::Vht,
        "HEW" => fidl_sme::Phy::Hew,
        other => bail!("unknown phy {}", other),
    };
    Ok((true, phy))
}

fn parse_cbw_str(cbw_str: Option<String>) -> Result<(bool, fidl_sme::Cbw), Error> {
    let cbw_str = match cbw_str {
        None => return Ok((false, fidl_sme::Cbw::Cbw20)),
        Some(x) => x.to_uppercase(),
    };
    let cbw = match cbw_str.as_str() {
        "20" => fidl_sme::Cbw::Cbw20,
        "40" => fidl_sme::Cbw::Cbw40,
        "80" => fidl_sme::Cbw::Cbw80,
        "160" => fidl_sme::Cbw::Cbw160,
        "80P80" => fidl_sme::Cbw::Cbw80P80,
        other => bail!("unknown cbw {}", other),
    };
    Ok((true, cbw))
}

async fn do_client(cmd: opts::ClientCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    match cmd {
        opts::ClientCmd::Scan { iface_id } => {
            let sme = await!(get_client_sme(wlan_svc, iface_id))?;
            let (local, remote) = endpoints::create_proxy()?;
            let mut req = fidl_sme::ScanRequest { timeout: 10 };
            sme.scan(&mut req, remote).context("error sending scan request")?;
            await!(handle_scan_transaction(local))
        }
        opts::ClientCmd::Connect { iface_id, ssid, password, phy_str, cbw_str } => {
            let sme = await!(get_client_sme(wlan_svc, iface_id))?;
            let (local, remote) = endpoints::create_proxy()?;
            let (override_phy, phy) = parse_phy_str(phy_str)?;
            let (override_cbw, cbw) = parse_cbw_str(cbw_str)?;
            let mut req = fidl_sme::ConnectRequest {
                ssid: ssid.as_bytes().to_vec(),
                password: password.unwrap_or(String::new()).as_bytes().to_vec(),
                params: fidl_sme::ConnectPhyParams {
                    override_phy,
                    phy,
                    override_cbw,
                    cbw,
                },
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

async fn do_ap(cmd: opts::ApCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    match cmd {
        opts::ApCmd::Start { iface_id, ssid, password, channel } => {
            let sme = await!(get_ap_sme(wlan_svc, iface_id))?;
            let mut config = fidl_sme::ApConfig {
                ssid: ssid.as_bytes().to_vec(),
                password: password.map_or(vec![], |p| p.as_bytes().to_vec()),
                channel
            };
            let r = await!(sme.start(&mut config));
            println!("{:?}", r);
        },
        opts::ApCmd::Stop { iface_id } => {
            let sme = await!(get_ap_sme(wlan_svc, iface_id))?;
            await!(sme.stop());
        }
    }
    Ok(())
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
    println!("BSSID              dBm     Chan Protected SSID");
}

fn is_ascii(v: &Vec<u8>) -> bool {
   for val in v {
     if val > &0x7e { return false; }
   }
   return true;
}

fn is_printable_ascii(v: &Vec<u8>) ->bool {
   for val in v {
     if val < &0x20 || val > &0x7e { return false; }
   }
   return true;
}

fn print_scan_result(ess: fidl_sme::EssInfo) {
    let is_ascii = is_ascii(&ess.best_bss.ssid);
    let is_ascii_print = is_printable_ascii(&ess.best_bss.ssid);
    let is_utf8 =  String::from_utf8(ess.best_bss.ssid.clone()).is_ok();
    let is_hex = !is_utf8 || (is_ascii && !is_ascii_print);

    let ssid_str;
    if is_hex {
        ssid_str = format!("({:X?})", &*ess.best_bss.ssid);
    } else {
        ssid_str = format!("\"{}\"", String::from_utf8_lossy(&ess.best_bss.ssid));
    }

    println!("{} {:4} {:8} {:9} {}",
        Bssid(ess.best_bss.bssid),
        ess.best_bss.rx_dbm,
        ess.best_bss.channel,
        if ess.best_bss.protected { "Y" } else { "N" },
        ssid_str);
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
    let (proxy, remote) = endpoints::create_proxy()?;
    let status = await!(wlan_svc.get_client_sme(iface_id, remote)).context("error sending GetClientSme request")?;
    if status == zx::sys::ZX_OK {
        Ok(proxy)
    } else {
        Err(format_err!("Invalid interface id {}", iface_id))
    }
}

async fn get_ap_sme(wlan_svc: WlanSvc, iface_id: u16)
    -> Result<fidl_sme::ApSmeProxy, Error>
{
    let (proxy, remote) = endpoints::create_proxy()?;
    let status = await!(wlan_svc.get_ap_sme(iface_id, remote)).context("error sending GetApSme request")?;
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
