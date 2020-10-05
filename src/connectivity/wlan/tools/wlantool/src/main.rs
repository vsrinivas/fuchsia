// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints;
use fidl_fuchsia_wlan_device::MacRole;
use fidl_fuchsia_wlan_device_service::{
    self as wlan_service, DeviceServiceMarker, DeviceServiceProxy, QueryIfaceResponse,
};
use fidl_fuchsia_wlan_minstrel::Peer;
use fidl_fuchsia_wlan_sme::{
    self as fidl_sme, ConnectResultCode, ConnectTransactionEvent, ScanTransactionEvent,
};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_service;
use fuchsia_zircon as zx;
use futures::prelude::*;
use hex::{FromHex, ToHex};
use itertools::Itertools;
use std::fmt;
use std::str::FromStr;
use structopt::StructOpt;
use wlan_common::{
    channel::{Cbw, Phy},
    ie::SSID_MAX_LEN,
    RadioConfig, StationMode,
};
use wlan_rsn::psk;

mod opts;
use crate::opts::*;

type WlanSvc = DeviceServiceProxy;

fn main() -> Result<(), Error> {
    let opt = Opt::from_args();
    println!("{:?}", opt);

    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let wlan_svc = connect_to_service::<DeviceServiceMarker>()
        .context("failed to `connect` to device service")?;

    let fut = async {
        match opt {
            Opt::Phy(cmd) => do_phy(cmd, wlan_svc).await,
            Opt::Iface(cmd) => do_iface(cmd, wlan_svc).await,
            Opt::Client(opts::ClientCmd::Connect(cmd)) | Opt::Connect(cmd) => {
                do_client_connect(cmd, wlan_svc).await
            }
            Opt::Client(opts::ClientCmd::Disconnect(cmd)) | Opt::Disconnect(cmd) => {
                do_client_disconnect(cmd, wlan_svc).await
            }
            Opt::Client(opts::ClientCmd::Scan(cmd)) | Opt::Scan(cmd) => {
                do_client_scan(cmd, wlan_svc).await
            }
            Opt::Ap(cmd) => do_ap(cmd, wlan_svc).await,
            Opt::Mesh(cmd) => do_mesh(cmd, wlan_svc).await,
            Opt::Rsn(cmd) => do_rsn(cmd).await,
            Opt::Status(cmd) => do_status(cmd, wlan_svc).await,
        }
    };
    exec.run_singlethreaded(fut)
}

async fn do_phy(cmd: opts::PhyCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    match cmd {
        opts::PhyCmd::List => {
            // TODO(tkilbourn): add timeouts to prevent hanging commands
            let response = wlan_svc.list_phys().await.context("error getting response")?;
            println!("response: {:?}", response);
        }
        opts::PhyCmd::Query { phy_id } => {
            let mut req = wlan_service::QueryPhyRequest { phy_id };
            let response = wlan_svc.query_phy(&mut req).await.context("error querying phy")?;
            println!("response: {:?}", response);
        }
        opts::PhyCmd::GetCountry { phy_id } => {
            let result = wlan_svc.get_country(phy_id).await.context("error getting country")?;
            match result {
                Ok(country) => {
                    println!("response: \"{}\"", std::str::from_utf8(&country.alpha2[..])?);
                }
                Err(status) => {
                    println!("response: Failed with status {:?}", zx::Status::from_raw(status));
                }
            }
        }
        opts::PhyCmd::SetCountry { phy_id, country } => {
            if !is_valid_country_str(&country) {
                return Err(format_err!(
                    "Country string [{}] looks invalid: Should be 2 ASCII characters",
                    country
                ));
            }

            let mut alpha2 = [0u8; 2];
            alpha2.copy_from_slice(country.as_bytes());
            let mut req = wlan_service::SetCountryRequest { phy_id, alpha2 };
            let response = wlan_svc.set_country(&mut req).await.context("error setting country")?;
            println!("response: {:?}", zx::Status::from_raw(response));
        }
        opts::PhyCmd::ClearCountry { phy_id } => {
            let mut req = wlan_service::ClearCountryRequest { phy_id };
            let response =
                wlan_svc.clear_country(&mut req).await.context("error clearing country")?;
            println!("response: {:?}", zx::Status::from_raw(response));
        }
    }
    Ok(())
}

fn is_valid_country_str(country: &String) -> bool {
    country.len() == 2 && country.chars().all(|x| x.is_ascii())
}

async fn do_iface(cmd: opts::IfaceCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    match cmd {
        opts::IfaceCmd::New { phy_id, role, mac_addr } => {
            let mac_addr: Option<Vec<u8>> = match mac_addr {
                Some(s) => Some(s.parse::<MacAddr>()?.0.to_vec()),
                None => None,
            };

            let mut req = wlan_service::CreateIfaceRequest { phy_id, role: role.into(), mac_addr };

            let response =
                wlan_svc.create_iface(&mut req).await.context("error getting response")?;
            println!("response: {:?}", response);
        }
        opts::IfaceCmd::Delete { iface_id } => {
            let mut req = wlan_service::DestroyIfaceRequest { iface_id };

            let response =
                wlan_svc.destroy_iface(&mut req).await.context("error destroying iface")?;
            match zx::Status::ok(response) {
                Ok(()) => println!("destroyed iface {:?}", iface_id),
                Err(s) => println!("error destroying iface: {:?}", s),
            }
        }
        opts::IfaceCmd::List => {
            let response = wlan_svc.list_ifaces().await.context("error getting response")?;
            println!("response: {:?}", response);
        }
        opts::IfaceCmd::Query { iface_id } => {
            let (status, response) =
                wlan_svc.query_iface(iface_id).await.context("error querying iface")?;
            match status {
                zx::sys::ZX_OK => {
                    let response_str = match response {
                        Some(response) => format_iface_query_response(*response),
                        None => format!("Iface {} returns empty query response", iface_id),
                    };
                    println!("response: {}", response_str)
                }
                status => println!("error querying Iface {}: {}", iface_id, status),
            }
        }
        opts::IfaceCmd::Stats { iface_id } => {
            let ids = get_iface_ids(wlan_svc.clone(), iface_id).await?;

            for iface_id in ids {
                let (status, resp) = wlan_svc
                    .get_iface_stats(iface_id)
                    .await
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
                let ids = get_iface_ids(wlan_svc.clone(), iface_id).await?;
                for id in ids {
                    if let Ok(peers) = list_minstrel_peers(wlan_svc.clone(), id).await {
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
                let ids = get_iface_ids(wlan_svc.clone(), iface_id).await?;
                for id in ids {
                    if let Err(e) =
                        show_minstrel_peer_for_iface(wlan_svc.clone(), id, peer_addr).await
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
        opts::IfaceCmd::Status(cmd) => do_status(cmd, wlan_svc).await?,
    }
    Ok(())
}

async fn do_client_connect(cmd: opts::ClientConnectCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    let opts::ClientConnectCmd { iface_id, ssid, password, psk, phy, cbw, scan_type } = cmd;
    if ssid.len() > SSID_MAX_LEN {
        return Err(format_err!(
            "SSID is too long ({} bytes). Max is {}",
            ssid.len(),
            SSID_MAX_LEN
        ));
    }
    let credential = match make_credential(password, psk) {
        Ok(c) => c,
        Err(e) => {
            println!("credential error: {}", e);
            return Ok(());
        }
    };
    let sme = get_client_sme(wlan_svc, iface_id).await?;
    let (local, remote) = endpoints::create_proxy()?;
    let mut req = fidl_sme::ConnectRequest {
        ssid: ssid.as_bytes().to_vec(),
        credential,
        radio_cfg: fidl_sme::RadioConfig {
            override_phy: phy.is_some(),
            phy: phy.unwrap_or(PhyArg::Vht).into(),
            override_cbw: cbw.is_some(),
            cbw: cbw.unwrap_or(CbwArg::Cbw80).into(),
            override_primary_chan: false,
            primary_chan: 0,
        },
        deprecated_scan_type: scan_type.into(),
    };
    sme.connect(&mut req, Some(remote)).context("error sending connect request")?;
    handle_connect_transaction(local).await
}

async fn do_client_disconnect(
    cmd: opts::ClientDisconnectCmd,
    wlan_svc: WlanSvc,
) -> Result<(), Error> {
    let opts::ClientDisconnectCmd { iface_id } = cmd;
    let sme = get_client_sme(wlan_svc, iface_id).await?;
    sme.disconnect().await.map_err(|e| format_err!("error sending disconnect request: {}", e))
}

async fn do_client_scan(cmd: opts::ClientScanCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    let opts::ClientScanCmd { iface_id, scan_type } = cmd;
    let sme = get_client_sme(wlan_svc, iface_id).await?;
    let (local, remote) = endpoints::create_proxy()?;
    let mut req = match scan_type {
        ScanTypeArg::Passive => fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {}),
        ScanTypeArg::Active => fidl_sme::ScanRequest::Active(fidl_sme::ActiveScanRequest {
            ssids: vec![],
            channels: vec![],
        }),
    };
    sme.scan(&mut req, remote).context("error sending scan request")?;
    handle_scan_transaction(local).await
}

async fn print_iface_status(iface_id: u16, wlan_svc: WlanSvc) -> Result<(), Error> {
    let (status, resp) = wlan_svc.query_iface(iface_id).await.context("querying iface info")?;

    zx::Status::ok(status)?;
    if resp.is_none() {
        return Err(format_err!("No response"));
    }
    match resp.unwrap().role {
        MacRole::Client => {
            let sme = get_client_sme(wlan_svc, iface_id).await?;
            let st = sme.status().await?;
            match st.connected_to {
                Some(bss) => {
                    println!(
                        "Iface {}: Connected to '{}' (bssid {}) channel: {} rssi: {}dBm snr: {}dB",
                        iface_id,
                        String::from_utf8_lossy(&bss.ssid),
                        MacAddr(bss.bssid),
                        bss.channel,
                        bss.rx_dbm,
                        bss.snr_db,
                    );
                }
                None => println!("Iface {}: Not connected to a network", iface_id),
            }
            if !st.connecting_to_ssid.is_empty() {
                println!("Connecting to '{}'", String::from_utf8_lossy(&st.connecting_to_ssid));
            }
        }
        MacRole::Ap => {
            let sme = get_ap_sme(wlan_svc, iface_id).await?;
            let status = sme.status().await?;
            println!(
                "Iface {}: Running AP: {:?}",
                iface_id,
                status.running_ap.map(|ap| {
                    format!(
                        "ssid: {}, channel: {}, clients: {}",
                        String::from_utf8_lossy(&ap.ssid),
                        ap.channel,
                        ap.num_clients
                    )
                })
            );
        }
        MacRole::Mesh => println!("Iface {}: Mesh not supported", iface_id),
    }
    Ok(())
}

async fn do_status(cmd: opts::IfaceStatusCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    let ids = get_iface_ids(wlan_svc.clone(), cmd.iface_id).await?;

    if ids.len() == 0 {
        return Err(format_err!("No iface found"));
    }
    for iface_id in ids {
        if let Err(e) = print_iface_status(iface_id, wlan_svc.clone()).await {
            println!("Iface {}: Error querying status: {}", iface_id, e);
            continue;
        }
    }
    Ok(())
}

async fn do_ap(cmd: opts::ApCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    match cmd {
        opts::ApCmd::Start { iface_id, ssid, password, channel } => {
            let sme = get_ap_sme(wlan_svc, iface_id).await?;
            let mut config = fidl_sme::ApConfig {
                ssid: ssid.as_bytes().to_vec(),
                password: password.map_or(vec![], |p| p.as_bytes().to_vec()),
                radio_cfg: RadioConfig::new(Phy::Ht, Cbw::Cbw20, channel).to_fidl(),
            };
            let r = sme.start(&mut config).await?;
            match r {
                fidl_sme::StartApResultCode::InvalidArguments => {
                    println!("{:?}: Channel {:?} is invalid", r, config.radio_cfg.primary_chan);
                }
                fidl_sme::StartApResultCode::DfsUnsupported => {
                    println!(
                        "{:?}: The specified role does not support DFS channel {:?}",
                        r, config.radio_cfg.primary_chan
                    );
                }
                _ => {
                    println!("{:?}", r);
                }
            }
        }
        opts::ApCmd::Stop { iface_id } => {
            let sme = get_ap_sme(wlan_svc, iface_id).await?;
            let r = sme.stop().await;
            println!("{:?}", r);
        }
    }
    Ok(())
}

async fn do_mesh(cmd: opts::MeshCmd, wlan_svc: WlanSvc) -> Result<(), Error> {
    match cmd {
        opts::MeshCmd::Join { iface_id, mesh_id, channel } => {
            let sme = get_mesh_sme(wlan_svc, iface_id).await?;
            let mut config = fidl_sme::MeshConfig { mesh_id: mesh_id.as_bytes().to_vec(), channel };
            let r = sme.join(&mut config).await?;
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
            let sme = get_mesh_sme(wlan_svc, iface_id).await?;
            let r = sme.leave().await;
            println!("{:?}", r);
        }
        opts::MeshCmd::Paths { iface_id } => {
            let sme = get_mesh_sme(wlan_svc, iface_id).await?;
            let (code, table) = sme.get_mesh_path_table().await?;
            match code {
                fidl_sme::GetMeshPathTableResultCode::Success => {
                    println!("{:?}", table);
                }
                fidl_sme::GetMeshPathTableResultCode::InternalError => {
                    println!("Internal Error in getting the Mesh Path Table.");
                }
            }
        }
    }
    Ok(())
}

async fn do_rsn(cmd: opts::RsnCmd) -> Result<(), Error> {
    match cmd {
        opts::RsnCmd::GeneratePsk { passphrase, ssid } => {
            println!("{}", generate_psk(&passphrase, &ssid)?);
        }
    }
    Ok(())
}

fn generate_psk(passphrase: &str, ssid: &str) -> Result<String, Error> {
    let psk = psk::compute(passphrase.as_bytes(), ssid.as_bytes())?;
    let mut psk_hex = String::new();
    psk.write_hex(&mut psk_hex)?;
    return Ok(psk_hex);
}

fn make_credential(
    password: Option<String>,
    psk: Option<String>,
) -> Result<fidl_sme::Credential, anyhow::Error> {
    match (password, psk) {
        (Some(password), None) => Ok(fidl_sme::Credential::Password(password.as_bytes().to_vec())),
        (None, Some(psk)) => {
            let psk = Vec::from_hex(psk).map_err(|_| format_err!("PSK is invalid"))?;
            Ok(fidl_sme::Credential::Psk(psk))
        }
        (None, None) => Ok(fidl_sme::Credential::None(fidl_sme::Empty)),
        _ => return Err(format_err!("cannot use password and PSK at once")),
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
struct MacAddr([u8; 6]);

impl fmt::Display for MacAddr {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
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
                return Err(format_err!("Too many octets"));
            }
            bytes[index] = u8::from_str_radix(octet, 16)?;
            index += 1;
        }

        if index != 6 {
            return Err(format_err!("Too few octets"));
        }
        Ok(MacAddr(bytes))
    }
}

async fn handle_scan_transaction(scan_txn: fidl_sme::ScanTransactionProxy) -> Result<(), Error> {
    let mut printed_header = false;
    let mut events = scan_txn.take_event_stream();
    while let Some(evt) = events
        .try_next()
        .await
        .context("failed to fetch all results before the channel was closed")?
    {
        match evt {
            ScanTransactionEvent::OnResult { aps } => {
                if !printed_header {
                    print_scan_header();
                    printed_header = true;
                }
                for ap in aps.iter().sorted_by(|a, b| a.ssid.cmp(&b.ssid)) {
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

fn print_scan_line(
    bssid: impl fmt::Display,
    dbm: impl fmt::Display,
    chan: impl fmt::Display,
    protection: impl fmt::Display,
    compat: impl fmt::Display,
    ssid: impl fmt::Display,
) {
    println!("{:17} {:>4} {:>6} {:12} {:10} {}", bssid, dbm, chan, protection, compat, ssid)
}

fn print_scan_header() {
    print_scan_line("BSSID", "dBm", "Chan", "Protection", "Compatible", "SSID");
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

fn print_scan_result(bss: &fidl_sme::BssInfo) {
    let is_ascii = is_ascii(&bss.ssid);
    let is_ascii_print = is_printable_ascii(&bss.ssid);
    let is_utf8 = String::from_utf8(bss.ssid.clone()).is_ok();
    let is_hex = !is_utf8 || (is_ascii && !is_ascii_print);

    let ssid_str;
    if is_hex {
        ssid_str = format!("({:X?})", &*bss.ssid);
    } else {
        ssid_str = format!("\"{}\"", String::from_utf8_lossy(&bss.ssid));
    }

    print_scan_line(
        MacAddr(bss.bssid),
        bss.rx_dbm,
        bss.channel,
        match bss.protection {
            fidl_sme::Protection::Unknown => "Unknown",
            fidl_sme::Protection::Open => "Open",
            fidl_sme::Protection::Wep => "WEP",
            fidl_sme::Protection::Wpa1 => "WPA1",
            fidl_sme::Protection::Wpa1Wpa2Personal => "WPA1/2 PSK",
            fidl_sme::Protection::Wpa2Personal => "WPA2 PSK",
            fidl_sme::Protection::Wpa2Wpa3Personal => "WPA2/3 PSK",
            fidl_sme::Protection::Wpa3Personal => "WPA3 PSK",
            fidl_sme::Protection::Wpa2Enterprise => "WPA2 802.1X",
            fidl_sme::Protection::Wpa3Enterprise => "WPA3 802.1X",
        },
        if bss.compatible { "Y" } else { "N" },
        ssid_str,
    );
}

async fn handle_connect_transaction(
    connect_txn: fidl_sme::ConnectTransactionProxy,
) -> Result<(), Error> {
    let mut events = connect_txn.take_event_stream();
    while let Some(evt) = events
        .try_next()
        .await
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
                    ConnectResultCode::CredentialRejected
                    | ConnectResultCode::WrongCredentialType => {
                        eprintln!("Failed to connect to network: {:?}", code)
                    }
                }
                break;
            }
        }
    }
    Ok(())
}

/// Constructs a `Result<(), Error>` from a `zx::zx_status_t` returned
/// from one of the `get_client_sme`, `get_ap_sme`, or `get_mesh_sme`
/// functions. In particular, when `zx::Status::from_raw(raw_status)` does
/// not match `zx::Status::OK`, this function will attach the appropriate
/// error message to the returned `Result`. When `zx::Status::from_raw(raw_status)`
/// does match `zx::Status::OK`, this function returns `Ok()`.
///
/// If this function returns an `Err`, it includes both a cause and a context.
/// The cause is a readable conversion of `raw_status` based on `station_mode`
/// and `iface_id`. The context notes the failed operation and suggests the
/// interface be checked for support of the given `station_mode`.
fn result_from_sme_raw_status(
    raw_status: zx::zx_status_t,
    station_mode: StationMode,
    iface_id: u16,
) -> Result<(), Error> {
    match zx::Status::from_raw(raw_status) {
        zx::Status::OK => Ok(()),
        zx::Status::NOT_FOUND => Err(Error::msg("invalid interface id")),
        zx::Status::NOT_SUPPORTED => Err(Error::msg("operation not supported on SME interface")),
        zx::Status::INTERNAL => {
            Err(Error::msg("internal server error sending endpoint to the SME server future"))
        }
        _ => Err(Error::msg("unrecognized error associated with SME interface")),
    }
    .context(format!(
        "Failed to access {} for interface id {}. \
                      Please ensure the selected iface supports {} mode.",
        station_mode, iface_id, station_mode,
    ))
}

async fn get_client_sme(
    wlan_svc: WlanSvc,
    iface_id: u16,
) -> Result<fidl_sme::ClientSmeProxy, Error> {
    let (proxy, remote) = endpoints::create_proxy()?;
    let raw_status = wlan_svc
        .get_client_sme(iface_id, remote)
        .await
        .context("error sending GetClientSme request")?;
    result_from_sme_raw_status(raw_status, StationMode::Client, iface_id).map(|_| proxy)
}

async fn get_ap_sme(wlan_svc: WlanSvc, iface_id: u16) -> Result<fidl_sme::ApSmeProxy, Error> {
    let (proxy, remote) = endpoints::create_proxy()?;
    let raw_status =
        wlan_svc.get_ap_sme(iface_id, remote).await.context("error sending GetApSme request")?;
    result_from_sme_raw_status(raw_status, StationMode::Ap, iface_id).map(|_| proxy)
}

async fn get_mesh_sme(wlan_svc: WlanSvc, iface_id: u16) -> Result<fidl_sme::MeshSmeProxy, Error> {
    let (proxy, remote) = endpoints::create_proxy()?;
    let raw_status = wlan_svc
        .get_mesh_sme(iface_id, remote)
        .await
        .context("error sending GetMeshSme request")?;
    result_from_sme_raw_status(raw_status, StationMode::Mesh, iface_id).map(|_| proxy)
}

async fn get_iface_ids(wlan_svc: WlanSvc, iface_id: Option<u16>) -> Result<Vec<u16>, Error> {
    match iface_id {
        Some(id) => Ok(vec![id]),
        None => {
            let response = wlan_svc.list_ifaces().await.context("error listing ifaces")?;
            Ok(response.ifaces.into_iter().map(|iface| iface.iface_id).collect())
        }
    }
}

async fn list_minstrel_peers(wlan_svc: WlanSvc, iface_id: u16) -> Result<Vec<MacAddr>, Error> {
    let (status, resp) = wlan_svc
        .get_minstrel_list(iface_id)
        .await
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
    let peer_addrs = get_peer_addrs(wlan_svc.clone(), id, peer_addr).await?;
    let mut first_peer = true;
    for mut peer_addr in peer_addrs {
        let (status, resp) = wlan_svc
            .get_minstrel_stats(id, &mut peer_addr.0)
            .await
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
        None => list_minstrel_peers(wlan_svc, iface_id).await,
    }
}

fn format_iface_query_response(resp: QueryIfaceResponse) -> String {
    format!(
        "QueryIfaceResponse {{ role: {:?}, id: {}, phy_id: {}, phy_assigned_id: {}, mac_addr: {} }}",
        resp.role,
        resp.id,
        resp.phy_id,
        resp.phy_assigned_id,
        MacAddr(resp.mac_addr)
    )
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
    use {
        super::*, fidl::endpoints::create_proxy, futures::task::Poll, matches::assert_matches,
        pin_utils::pin_mut, wlan_common::assert_variant,
    };

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

    #[test]
    fn make_credentials() {
        let credential = make_credential(None, None).expect("credential is valid");
        assert_eq!(credential, fidl_sme::Credential::None(fidl_sme::Empty));

        let credential =
            make_credential(Some("hi".to_string()), None).expect("credential is valid");
        assert_eq!(credential, fidl_sme::Credential::Password("hi".as_bytes().to_vec()));

        let psk = "f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e";
        let credential = make_credential(None, Some(psk.to_string())).expect("credential is valid");
        assert_eq!(
            credential,
            fidl_sme::Credential::Psk(vec![
                0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef, 0x9e, 0xbb, 0x4b, 0x90, 0xb3, 0x8a,
                0x5f, 0x90, 0x2e, 0x83, 0xfe, 0x1b, 0x13, 0x5a, 0x70, 0xe2, 0x3a, 0xed, 0x76, 0x2e,
                0x97, 0x10, 0xa1, 0x2e,
            ])
        );

        make_credential(Some("hi".to_string()), Some(psk.to_string()))
            .expect_err("credential is invalid");
    }

    #[test]
    fn destroy_iface() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (wlansvc_local, wlansvc_remote) =
            create_proxy::<DeviceServiceMarker>().expect("failed to create DeviceService service");
        let mut wlansvc_stream = wlansvc_remote.into_stream().expect("failed to create stream");
        let del_fut = do_iface(IfaceCmd::Delete { iface_id: 5 }, wlansvc_local);
        pin_mut!(del_fut);

        assert_variant!(exec.run_until_stalled(&mut del_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut wlansvc_stream.next()),
            Poll::Ready(Some(Ok(wlan_service::DeviceServiceRequest::DestroyIface {
                req, responder
            }))) => {
                assert_eq!(req.iface_id, 5);
                responder.send(zx::Status::OK.into_raw()).expect("failed to send response");
            }
        );
    }

    #[test]
    fn test_country_input() {
        assert!(is_valid_country_str(&"RS".to_string()));
        assert!(is_valid_country_str(&"00".to_string()));
        assert!(is_valid_country_str(&"M1".to_string()));
        assert!(is_valid_country_str(&"-M".to_string()));

        assert!(!is_valid_country_str(&"ABC".to_string()));
        assert!(!is_valid_country_str(&"X".to_string()));
        assert!(!is_valid_country_str(&"❤".to_string()));
    }

    #[test]
    fn test_get_country() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (wlansvc_local, wlansvc_remote) =
            create_proxy::<DeviceServiceMarker>().expect("failed to create DeviceService service");
        let mut wlansvc_stream = wlansvc_remote.into_stream().expect("failed to create stream");
        let fut = do_phy(PhyCmd::GetCountry { phy_id: 45 }, wlansvc_local);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut wlansvc_stream.next()),
            Poll::Ready(Some(Ok(wlan_service::DeviceServiceRequest::GetCountry {
                phy_id, responder,
            }))) => {
                assert_eq!(phy_id, 45);
                responder.send(
                    &mut Ok(fidl_fuchsia_wlan_device_service::GetCountryResponse {
                        alpha2: [40u8, 40u8],
                    })).expect("failed to send response");
            }
        );

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    #[test]
    fn test_set_country() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (wlansvc_local, wlansvc_remote) =
            create_proxy::<DeviceServiceMarker>().expect("failed to create DeviceService service");
        let mut wlansvc_stream = wlansvc_remote.into_stream().expect("failed to create stream");
        let fut =
            do_phy(PhyCmd::SetCountry { phy_id: 45, country: "RS".to_string() }, wlansvc_local);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut wlansvc_stream.next()),
            Poll::Ready(Some(Ok(wlan_service::DeviceServiceRequest::SetCountry {
                req, responder,
            }))) => {
                assert_eq!(req.phy_id, 45);
                assert_eq!(req.alpha2, "RS".as_bytes());
                responder.send(zx::Status::OK.into_raw()).expect("failed to send response");
            }
        );
    }

    #[test]
    fn test_clear_country() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (wlansvc_local, wlansvc_remote) =
            create_proxy::<DeviceServiceMarker>().expect("failed to create DeviceService service");
        let mut wlansvc_stream = wlansvc_remote.into_stream().expect("failed to create stream");
        let fut = do_phy(PhyCmd::ClearCountry { phy_id: 45 }, wlansvc_local);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut wlansvc_stream.next()),
            Poll::Ready(Some(Ok(wlan_service::DeviceServiceRequest::ClearCountry {
                req, responder,
            }))) => {
                assert_eq!(req.phy_id, 45);
                responder.send(zx::Status::OK.into_raw()).expect("failed to send response");
            }
        );
    }

    #[test]
    fn test_generate_psk() {
        assert_eq!(
            generate_psk("12345678", "coolnet").unwrap(),
            "1ec9ee30fdff1961a9abd083f571464cc0fe27f62f9f59992bd39f8e625e9f52"
        );
        assert!(generate_psk("short", "coolnet").is_err());
    }

    fn has_expected_cause(result: Result<(), Error>, message: &str) -> bool {
        match result {
            Err(e) => e.chain().any(|cause| cause.to_string() == message),
            _ => false,
        }
    }

    #[test]
    fn test_result_from_sme_raw_status() {
        let ok = result_from_sme_raw_status(zx::Status::OK.into_raw(), StationMode::Client, 0);
        let not_found =
            result_from_sme_raw_status(zx::Status::NOT_FOUND.into_raw(), StationMode::Mesh, 1);
        let not_supported =
            result_from_sme_raw_status(zx::Status::NOT_SUPPORTED.into_raw(), StationMode::Ap, 2);
        let internal_error =
            result_from_sme_raw_status(zx::Status::INTERNAL.into_raw(), StationMode::Client, 3);
        let unrecognized_error = result_from_sme_raw_status(
            zx::Status::INTERRUPTED_RETRY.into_raw(),
            StationMode::Mesh,
            4,
        );

        assert_matches!(ok, Ok(()));
        assert!(has_expected_cause(not_found, "invalid interface id"));
        assert!(has_expected_cause(not_supported, "operation not supported on SME interface"));
        assert!(has_expected_cause(
            internal_error,
            "internal server error sending endpoint to the SME server future"
        ));
        assert!(has_expected_cause(
            unrecognized_error,
            "unrecognized error associated with SME interface"
        ));
    }

    #[test]
    fn reject_connect_ssid_too_long() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let (wlansvc_local, wlansvc_remote) =
            create_proxy::<DeviceServiceMarker>().expect("failed to create DeviceService service");
        let mut wlansvc_stream = wlansvc_remote.into_stream().expect("failed to create stream");
        // SSID is one byte too long.
        let cmd = opts::ClientConnectCmd {
            iface_id: 0,
            ssid: String::from_utf8(vec![65; 33]).unwrap(),
            password: None,
            psk: None,
            phy: None,
            cbw: None,
            scan_type: opts::ScanTypeArg::Passive,
        };

        let connect_fut = do_client_connect(cmd, wlansvc_local.clone());
        pin_mut!(connect_fut);

        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Ready(Err(e)) => {
          assert!(format!("{}", e).contains("SSID is too long (33 bytes). Max is 32") );
        });
        // No connect request is sent to SME because the command is invalid and rejected.
        assert_variant!(exec.run_until_stalled(&mut wlansvc_stream.next()), Poll::Pending);
    }
}
