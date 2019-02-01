// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]
#![deny(warnings)]

use failure::{bail, format_err, Error, ResultExt};
use fidl::endpoints;
use fidl_fuchsia_wlan_device_service::{
    self as wlan_service, DeviceServiceMarker, DeviceServiceProxy,
};
use fidl_fuchsia_wlan_minstrel::Peer;
use fidl_fuchsia_wlan_sme::{
    self as fidl_sme, ConnectResultCode, ConnectTransactionEvent, ScanTransactionEvent,
};
use fuchsia_app::client::connect_to_service;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::prelude::*;
use std::fmt;
use std::str::FromStr;
use structopt::StructOpt;

mod opts;
use crate::opts::*;

const SCAN_REQUEST_TIMEOUT_SEC: u8 = 10;

type WlanSvc = DeviceServiceProxy;

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    println!("{:?}", opt);

    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let wlan_svc = connect_to_service::<DeviceServiceMarker>()
        .context("failed to connect to device service")?;

    let fut = async {
        match opt {
            Opt::Phy(cmd) => await!(do_phy(cmd, wlan_svc)),
            Opt::Iface(cmd) => await!(do_iface(cmd, wlan_svc)),
            Opt::Client(cmd) => await!(do_client(cmd, wlan_svc)),
            Opt::Ap(cmd) => await!(do_ap(cmd, wlan_svc)),
            Opt::Mesh(cmd) => await!(do_mesh(cmd, wlan_svc)),
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
            let mut req = wlan_service::CreateIfaceRequest { phy_id: phy_id, role: role.into() };

            let response =
                await!(wlan_svc.create_iface(&mut req)).context("error getting response")?;
            println!("response: {:?}", response);
        }
        opts::IfaceCmd::Delete { phy_id, iface_id } => {
            let mut req = wlan_service::DestroyIfaceRequest { phy_id: phy_id, iface_id: iface_id };

            let response =
                await!(wlan_svc.destroy_iface(&mut req)).context("error destroying iface")?;
            match zx::Status::ok(response) {
                Ok(()) => println!("destroyed iface {:?}", iface_id),
                Err(s) => println!("error destroying iface: {:?}", s),
            }
        }
        opts::IfaceCmd::List => {
            let response = await!(wlan_svc.list_ifaces()).context("error getting response")?;
            println!("response: {:?}", response);
        }
        opts::IfaceCmd::Query { iface_id } => {
            let response =
                await!(wlan_svc.query_iface(iface_id)).context("error querying iface")?;
            println!("response: {:?}", response);
        }
        opts::IfaceCmd::Stats { iface_id } => {
            let ids = await!(get_iface_ids(wlan_svc.clone(), iface_id))?;

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
                    }
                    status => println!("error getting stats for Iface {}: {}", iface_id, status),
                }
            }
        }
        opts::IfaceCmd::Minstrel(cmd) => match cmd {
            opts::MinstrelCmd::List { iface_id } => {
                let ids = await!(get_iface_ids(wlan_svc.clone(), iface_id))?;
                for id in ids {
                    if let Ok(peers) = await!(list_minstrel_peers(wlan_svc.clone(), id)) {
                        if peers.is_empty() {
                            continue;
                        }
                        println!("iface {} has {} peers:", id, peers.len());
                        for peer in peers {
                            println!("{}", peer);
                        }
                    }
                }
            }
            opts::MinstrelCmd::Show { iface_id, peer_addr } => {
                let peer_addr = match peer_addr {
                    Some(s) => Some(s.parse()?),
                    None => None,
                };
                let ids = await!(get_iface_ids(wlan_svc.clone(), iface_id))?;
                for id in ids {
                    if let Err(e) =
                        await!(show_minstrel_peer_for_iface(wlan_svc.clone(), id, peer_addr))
                    {
                        println!(
                            "querying peer(s) {} on iface {} returned an error: {}",
                            peer_addr.unwrap_or(MacAddr([0; 6])),
                            id,
                            e
                        );
                    }
                }
            }
        },
    }
    Ok(())
}

async fn do_client(cmd: opts::ClientCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    match cmd {
        opts::ClientCmd::Scan { iface_id, scan_type } => {
            let sme = await!(get_client_sme(wlan_svc, iface_id))?;
            let (local, remote) = endpoints::create_proxy()?;
            let mut req = fidl_sme::ScanRequest {
                timeout: SCAN_REQUEST_TIMEOUT_SEC,
                scan_type: scan_type.into(),
            };
            sme.scan(&mut req, remote).context("error sending scan request")?;
            await!(handle_scan_transaction(local))
        }
        opts::ClientCmd::Connect { iface_id, ssid, password, phy, cbw, scan_type } => {
            let sme = await!(get_client_sme(wlan_svc, iface_id))?;
            let (local, remote) = endpoints::create_proxy()?;
            let mut req = fidl_sme::ConnectRequest {
                ssid: ssid.as_bytes().to_vec(),
                password: password.unwrap_or(String::new()).as_bytes().to_vec(),
                radio_cfg: fidl_sme::RadioConfig {
                    override_phy: phy.is_some(),
                    phy: phy.unwrap_or(PhyArg::Vht).into(),
                    override_cbw: cbw.is_some(),
                    cbw: cbw.unwrap_or(CbwArg::Cbw80).into(),
                    override_primary_chan: false,
                    primary_chan: 0,
                },
                scan_type: scan_type.into(),
            };
            sme.connect(&mut req, Some(remote)).context("error sending connect request")?;
            await!(handle_connect_transaction(local))
        }
        opts::ClientCmd::Disconnect { iface_id } => {
            let sme = await!(get_client_sme(wlan_svc, iface_id))?;
            await!(sme.disconnect())
                .map_err(|e| format_err!("error sending disconnect request: {}", e))
        }
        opts::ClientCmd::Status { iface_id } => {
            let sme = await!(get_client_sme(wlan_svc, iface_id))?;
            let st = await!(sme.status())?;
            match st.connected_to {
                Some(bss) => {
                    println!(
                        "Connected to '{}' (bssid {})",
                        String::from_utf8_lossy(&bss.ssid),
                        MacAddr(bss.bssid)
                    );
                }
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
                channel,
            };
            let r = await!(sme.start(&mut config))?;
            match r {
                fidl_sme::StartApResultCode::InvalidArguments => {
                    println!("{:?}: Channel {:?} is invalid", r, config.channel);
                }
                fidl_sme::StartApResultCode::DfsUnsupported => {
                    println!(
                        "{:?}: The specified role does not support DFS channel {:?}",
                        r, config.channel
                    );
                }
                _ => {
                    println!("{:?}", r);
                }
            }
        }
        opts::ApCmd::Stop { iface_id } => {
            let sme = await!(get_ap_sme(wlan_svc, iface_id))?;
            let r = await!(sme.stop());
            println!("{:?}", r);
        }
    }
    Ok(())
}

async fn do_mesh(cmd: opts::MeshCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    match cmd {
        opts::MeshCmd::Join { iface_id, mesh_id, channel } => {
            let sme = await!(get_mesh_sme(wlan_svc, iface_id))?;
            let mut config = fidl_sme::MeshConfig { mesh_id: mesh_id.as_bytes().to_vec(), channel };
            let r = await!(sme.join(&mut config))?;
            match r {
                fidl_sme::JoinMeshResultCode::InvalidArguments => {
                    println!("{:?}: Channel {:?} is invalid", r, config.channel);
                }
                fidl_sme::JoinMeshResultCode::DfsUnsupported => {
                    println!(
                        "{:?}: The specified role does not support DFS channel {:?}",
                        r, config.channel
                    );
                }
                _ => {
                    println!("{:?}", r);
                }
            }
        }
        opts::MeshCmd::Leave { iface_id } => {
            let sme = await!(get_mesh_sme(wlan_svc, iface_id))?;
            let r = await!(sme.leave());
            println!("{:?}", r);
        }
    }
    Ok(())
}

#[derive(Debug, Clone, Copy, PartialEq)]
struct MacAddr([u8; 6]);

impl fmt::Display for MacAddr {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        write!(
            f,
            "{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
            self.0[0], self.0[1], self.0[2], self.0[3], self.0[4], self.0[5]
        )
    }
}

impl FromStr for MacAddr {
    type Err = Error;

    fn from_str(s: &str) -> Result<Self, Self::Err> {
        let mut bytes = [0; 6];
        let mut index = 0;

        for octet in s.split(|c| c == ':' || c == '-') {
            if index == 6 {
                bail!("Too many octets");
            }
            bytes[index] = u8::from_str_radix(octet, 16)?;
            index += 1;
        }

        if index != 6 {
            bail!("Too few octets");
        }
        Ok(MacAddr(bytes))
    }
}

async fn handle_scan_transaction(scan_txn: fidl_sme::ScanTransactionProxy) -> Result<(), Error> {
    let mut printed_header = false;
    let mut events = scan_txn.take_event_stream();
    while let Some(evt) = await!(events.try_next())
        .context("failed to fetch all results before the channel was closed")?
    {
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
            ScanTransactionEvent::OnFinished {} => break,
            ScanTransactionEvent::OnError { error } => {
                eprintln!("Error: {}", error.message);
                break;
            }
        }
    }
    Ok(())
}

fn print_scan_header() {
    println!("BSSID              dBm     Chan Protected SSID");
}

fn is_ascii(v: &Vec<u8>) -> bool {
    for val in v {
        if val > &0x7e {
            return false;
        }
    }
    return true;
}

fn is_printable_ascii(v: &Vec<u8>) -> bool {
    for val in v {
        if val < &0x20 || val > &0x7e {
            return false;
        }
    }
    return true;
}

fn print_scan_result(ess: fidl_sme::EssInfo) {
    let is_ascii = is_ascii(&ess.best_bss.ssid);
    let is_ascii_print = is_printable_ascii(&ess.best_bss.ssid);
    let is_utf8 = String::from_utf8(ess.best_bss.ssid.clone()).is_ok();
    let is_hex = !is_utf8 || (is_ascii && !is_ascii_print);

    let ssid_str;
    if is_hex {
        ssid_str = format!("({:X?})", &*ess.best_bss.ssid);
    } else {
        ssid_str = format!("\"{}\"", String::from_utf8_lossy(&ess.best_bss.ssid));
    }

    println!(
        "{} {:4} {:8} {:9} {}",
        MacAddr(ess.best_bss.bssid),
        ess.best_bss.rx_dbm,
        ess.best_bss.channel,
        if ess.best_bss.protected { "Y" } else { "N" },
        ssid_str
    );
}

async fn handle_connect_transaction(
    connect_txn: fidl_sme::ConnectTransactionProxy,
) -> Result<(), Error> {
    let mut events = connect_txn.take_event_stream();
    while let Some(evt) = await!(events.try_next())
        .context("failed to receive connect result before the channel was closed")?
    {
        match evt {
            ConnectTransactionEvent::OnFinished { code } => {
                match code {
                    ConnectResultCode::Success => println!("Connected successfully"),
                    ConnectResultCode::Canceled => {
                        eprintln!("Connecting was canceled or superseded by another command")
                    }
                    ConnectResultCode::Failed => eprintln!("Failed to connect to network"),
                    ConnectResultCode::BadCredentials => {
                        eprintln!("Failed to connect to network; bad credentials")
                    }
                }
                break;
            }
        }
    }
    Ok(())
}

async fn get_client_sme(
    wlan_svc: WlanSvc,
    iface_id: u16,
) -> Result<fidl_sme::ClientSmeProxy, Error> {
    let (proxy, remote) = endpoints::create_proxy()?;
    let status = await!(wlan_svc.get_client_sme(iface_id, remote))
        .context("error sending GetClientSme request")?;
    if status == zx::sys::ZX_OK {
        Ok(proxy)
    } else {
        Err(format_err!("Invalid interface id {}", iface_id))
    }
}

async fn get_ap_sme(wlan_svc: WlanSvc, iface_id: u16) -> Result<fidl_sme::ApSmeProxy, Error> {
    let (proxy, remote) = endpoints::create_proxy()?;
    let status =
        await!(wlan_svc.get_ap_sme(iface_id, remote)).context("error sending GetApSme request")?;
    if status == zx::sys::ZX_OK {
        Ok(proxy)
    } else {
        Err(format_err!("Invalid interface id {}", iface_id))
    }
}

async fn get_mesh_sme(wlan_svc: WlanSvc, iface_id: u16) -> Result<fidl_sme::MeshSmeProxy, Error> {
    let (proxy, remote) = endpoints::create_proxy()?;
    let status = await!(wlan_svc.get_mesh_sme(iface_id, remote))
        .context("error sending GetMeshSme request")?;
    if status == zx::sys::ZX_OK {
        Ok(proxy)
    } else {
        Err(format_err!("Invalid interface id {}", iface_id))
    }
}

async fn get_iface_ids(wlan_svc: WlanSvc, iface_id: Option<u16>) -> Result<Vec<u16>, Error> {
    match iface_id {
        Some(id) => Ok(vec![id]),
        None => {
            let response = await!(wlan_svc.list_ifaces()).context("error listing ifaces")?;
            Ok(response.ifaces.into_iter().map(|iface| iface.iface_id).collect())
        }
    }
}

async fn list_minstrel_peers(wlan_svc: WlanSvc, iface_id: u16) -> Result<Vec<MacAddr>, Error> {
    let (status, resp) = await!(wlan_svc.get_minstrel_list(iface_id))
        .context(format!("Error getting minstrel peer list iface {}", iface_id))?;
    if status == zx::sys::ZX_OK {
        Ok(resp
            .peers
            .into_iter()
            .map(|v| {
                let mut arr = [0u8; 6];
                arr.copy_from_slice(v.as_slice());
                MacAddr(arr)
            })
            .collect())
    } else {
        println!("Error getting minstrel peer list from iface {}: {}", iface_id, status);
        Ok(vec![])
    }
}

async fn show_minstrel_peer_for_iface(
    wlan_svc: WlanSvc,
    id: u16,
    peer_addr: Option<MacAddr>,
) -> Result<(), Error> {
    let peer_addrs = await!(get_peer_addrs(wlan_svc.clone(), id, peer_addr))?;
    let mut first_peer = true;
    for mut peer_addr in peer_addrs {
        let (status, resp) = await!(wlan_svc.get_minstrel_stats(id, &mut peer_addr.0))
            .context(format!("Error getting minstrel stats from peer {}", peer_addr))?;
        if status != zx::sys::ZX_OK {
            println!(
                "error getting minstrel stats for {} from iface {}: {}",
                peer_addr, id, status
            );
        } else if let Some(peer) = resp {
            if first_peer {
                println!("iface {}", id);
                first_peer = false;
            }
            print_minstrel_stats(peer);
        }
    }
    Ok(())
}

async fn get_peer_addrs(
    wlan_svc: WlanSvc,
    iface_id: u16,
    peer_addr: Option<MacAddr>,
) -> Result<Vec<MacAddr>, Error> {
    match peer_addr {
        Some(addr) => Ok(vec![addr]),
        None => await!(list_minstrel_peers(wlan_svc, iface_id)),
    }
}

fn print_minstrel_stats(mut peer: Box<Peer>) {
    let total_attempts: f64 = peer.entries.iter().map(|e| e.attempts_total as f64).sum();
    let total_success: f64 = peer.entries.iter().map(|e| e.success_total as f64).sum();
    println!(
        "{}, max_tp: {}, max_probability: {}, attempts/success: {:.6}, probes: {}",
        MacAddr(peer.mac_addr),
        peer.max_tp,
        peer.max_probability,
        total_attempts / total_success,
        peer.probes
    );
    println!(
        "     TxVector                            succ_c   att_c  succ_t   att_t \
         probability throughput probes probe_cycles_skipped"
    );
    peer.entries.sort_by(|l, r| l.tx_vector_idx.cmp(&r.tx_vector_idx));
    for e in peer.entries {
        println!(
            "{}{} {:<36} {:7} {:7} {:7} {:7} {:11.4} {:10.3} {:6} {:20}",
            if e.tx_vector_idx == peer.max_tp { "T" } else { " " },
            if e.tx_vector_idx == peer.max_probability { "P" } else { " " },
            e.tx_vec_desc,
            e.success_cur,
            e.attempts_cur,
            e.success_total,
            e.attempts_total,
            e.probability * 100.0,
            e.cur_tp,
            e.probes_total,
            e.probe_cycles_skipped,
        );
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn format_bssid() {
        assert_eq!(
            "01:02:03:ab:cd:ef",
            format!("{}", MacAddr([0x01, 0x02, 0x03, 0xab, 0xcd, 0xef]))
        );
    }

    #[test]
    fn mac_addr_from_str() {
        assert_eq!(
            MacAddr::from_str("01:02:03:ab:cd:ef").unwrap(),
            MacAddr([0x01, 0x02, 0x03, 0xab, 0xcd, 0xef])
        );
        assert_eq!(
            MacAddr::from_str("01:02-03:ab-cd:ef").unwrap(),
            MacAddr([0x01, 0x02, 0x03, 0xab, 0xcd, 0xef])
        );
        assert!(MacAddr::from_str("01:02:03:ab:cd").is_err());
        assert!(MacAddr::from_str("01:02:03:04:05:06:07").is_err());
        assert!(MacAddr::from_str("01:02:gg:gg:gg:gg").is_err());
    }
}
