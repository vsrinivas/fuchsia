// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints,
    fidl_fuchsia_wlan_common::PowerSaveType,
    fidl_fuchsia_wlan_common::{self as fidl_common, WlanMacRole},
    fidl_fuchsia_wlan_common_security as fidl_security,
    fidl_fuchsia_wlan_device_service::{
        self as wlan_service, DeviceMonitorProxy, QueryIfaceResponse,
    },
    fidl_fuchsia_wlan_ieee80211 as fidl_ieee80211, fidl_fuchsia_wlan_internal as fidl_internal,
    fidl_fuchsia_wlan_sme as fidl_sme,
    fidl_fuchsia_wlan_sme::ConnectTransactionEvent,
    fuchsia_zircon_status as zx_status, fuchsia_zircon_types as zx_sys,
    futures::prelude::*,
    ieee80211::Ssid,
    ieee80211::NULL_MAC_ADDR,
    itertools::Itertools,
    std::convert::TryFrom,
    std::fmt,
    std::str::FromStr,
    wlan_common::{
        bss::{BssDescription, Protection},
        scan::ScanResult,
        security::{
            wep::WepKey,
            wpa::credential::{Passphrase, Psk},
            SecurityError,
        },
    },
};

#[cfg(target_os = "fuchsia")]
use {hex::ToHex, wlan_rsn::psk};

pub mod opts;
use crate::opts::*;

type DeviceMonitor = DeviceMonitorProxy;

/// Context for negotiating an `Authentication` (security protocol and credentials).
///
/// This ephemeral type joins a BSS description with credential data to negotiate an
/// `Authentication`. See the `TryFrom` implementation below.
#[derive(Clone, Debug)]
struct SecurityContext {
    pub bss: BssDescription,
    pub unparsed_password_text: Option<String>,
    pub unparsed_psk_text: Option<String>,
}

/// Negotiates an `Authentication` from security information (given credentials and a BSS
/// description). The security protocol is based on the protection information described by the BSS
/// description. This is used to parse and validate the given credentials.
///
/// This is necessary, because `wlandev` communicates directly with SME, which requires more
/// detailed information than the Policy layer.
impl TryFrom<SecurityContext> for fidl_security::Authentication {
    type Error = SecurityError;

    fn try_from(context: SecurityContext) -> Result<Self, SecurityError> {
        /// Interprets the given password and PSK both as WPA credentials and attempts to parse the
        /// pair.
        ///
        /// Note that the given password can also represent a WEP key, so this function should only
        /// be used in WPA contexts.
        fn parse_wpa_credential_pair(
            password: Option<String>,
            psk: Option<String>,
        ) -> Result<fidl_security::Credentials, SecurityError> {
            match (password, psk) {
                (Some(password), None) => Passphrase::try_from(password)
                    .map(|passphrase| {
                        fidl_security::Credentials::Wpa(fidl_security::WpaCredentials::Passphrase(
                            passphrase.into(),
                        ))
                    })
                    .map_err(From::from),
                (None, Some(psk)) => Psk::parse(psk.as_bytes())
                    .map(|psk| {
                        fidl_security::Credentials::Wpa(fidl_security::WpaCredentials::Psk(
                            psk.into(),
                        ))
                    })
                    .map_err(From::from),
                _ => Err(SecurityError::Incompatible),
            }
        }

        let SecurityContext { bss, unparsed_password_text, unparsed_psk_text } = context;
        match bss.protection() {
            // Unsupported.
            // TODO(fxbug.dev/92693): Implement conversions for WPA Enterprise.
            Protection::Unknown | Protection::Wpa2Enterprise | Protection::Wpa3Enterprise => {
                Err(SecurityError::Unsupported)
            }
            Protection::Open => match (unparsed_password_text, unparsed_psk_text) {
                (None, None) => Ok(fidl_security::Authentication {
                    protocol: fidl_security::Protocol::Open,
                    credentials: None,
                }),
                _ => Err(SecurityError::Incompatible),
            },
            Protection::Wep => unparsed_password_text
                .ok_or(SecurityError::Incompatible)
                .and_then(|unparsed_password_text| {
                    WepKey::parse(unparsed_password_text.as_bytes()).map_err(From::from)
                })
                .map(|key| fidl_security::Authentication {
                    protocol: fidl_security::Protocol::Wep,
                    credentials: Some(Box::new(fidl_security::Credentials::Wep(
                        fidl_security::WepCredentials { key: key.into() },
                    ))),
                }),
            Protection::Wpa1 => {
                parse_wpa_credential_pair(unparsed_password_text, unparsed_psk_text).map(
                    |credentials| fidl_security::Authentication {
                        protocol: fidl_security::Protocol::Wpa1,
                        credentials: Some(Box::new(credentials)),
                    },
                )
            }
            Protection::Wpa1Wpa2PersonalTkipOnly
            | Protection::Wpa1Wpa2Personal
            | Protection::Wpa2PersonalTkipOnly
            | Protection::Wpa2Personal => {
                parse_wpa_credential_pair(unparsed_password_text, unparsed_psk_text).map(
                    |credentials| fidl_security::Authentication {
                        protocol: fidl_security::Protocol::Wpa2Personal,
                        credentials: Some(Box::new(credentials)),
                    },
                )
            }
            // Use WPA2 for transitional networks when a PSK is supplied.
            Protection::Wpa2Wpa3Personal => {
                parse_wpa_credential_pair(unparsed_password_text, unparsed_psk_text).map(
                    |credentials| match credentials {
                        fidl_security::Credentials::Wpa(
                            fidl_security::WpaCredentials::Passphrase(_),
                        ) => fidl_security::Authentication {
                            protocol: fidl_security::Protocol::Wpa3Personal,
                            credentials: Some(Box::new(credentials)),
                        },
                        fidl_security::Credentials::Wpa(fidl_security::WpaCredentials::Psk(_)) => {
                            fidl_security::Authentication {
                                protocol: fidl_security::Protocol::Wpa2Personal,
                                credentials: Some(Box::new(credentials)),
                            }
                        }
                        _ => unreachable!(),
                    },
                )
            }
            Protection::Wpa3Personal => match (unparsed_password_text, unparsed_psk_text) {
                (Some(unparsed_password_text), None) => {
                    Passphrase::try_from(unparsed_password_text)
                        .map(|passphrase| fidl_security::Authentication {
                            protocol: fidl_security::Protocol::Wpa3Personal,
                            credentials: Some(Box::new(fidl_security::Credentials::Wpa(
                                fidl_security::WpaCredentials::Passphrase(passphrase.into()),
                            ))),
                        })
                        .map_err(From::from)
                }
                _ => Err(SecurityError::Incompatible),
            },
        }
    }
}

pub async fn handle_wlantool_command(monitor_proxy: DeviceMonitor, opt: Opt) -> Result<(), Error> {
    match opt {
        Opt::Phy(cmd) => do_phy(cmd, monitor_proxy).await,
        Opt::Iface(cmd) => do_iface(cmd, monitor_proxy).await,
        Opt::Client(opts::ClientCmd::Connect(cmd)) => do_client_connect(cmd, monitor_proxy).await,
        Opt::Connect(cmd) => do_client_connect(cmd, monitor_proxy).await,
        Opt::Client(opts::ClientCmd::Disconnect(cmd)) | Opt::Disconnect(cmd) => {
            do_client_disconnect(cmd, monitor_proxy).await
        }
        Opt::Client(opts::ClientCmd::Scan(cmd)) => do_client_scan(cmd, monitor_proxy).await,
        Opt::Scan(cmd) => do_client_scan(cmd, monitor_proxy).await,
        Opt::Client(opts::ClientCmd::WmmStatus(cmd)) | Opt::WmmStatus(cmd) => {
            do_client_wmm_status(cmd, monitor_proxy, &mut std::io::stdout()).await
        }
        Opt::Ap(cmd) => do_ap(cmd, monitor_proxy).await,
        #[cfg(target_os = "fuchsia")]
        Opt::Rsn(cmd) => do_rsn(cmd).await,
        Opt::Status(cmd) => do_status(cmd, monitor_proxy).await,
    }
}

async fn do_phy(cmd: opts::PhyCmd, monitor_proxy: DeviceMonitor) -> Result<(), Error> {
    match cmd {
        opts::PhyCmd::List => {
            // TODO(tkilbourn): add timeouts to prevent hanging commands
            let response = monitor_proxy.list_phys().await.context("error getting response")?;
            println!("response: {:?}", response);
        }
        opts::PhyCmd::Query { phy_id } => {
            let mac_roles = monitor_proxy
                .get_supported_mac_roles(phy_id)
                .await
                .context("error querying MAC roles")?;
            let device_path =
                monitor_proxy.get_dev_path(phy_id).await.context("error querying device path")?;
            println!("PHY ID: {}", phy_id);
            println!("Device Path: {:?}", device_path);
            println!("Supported MAC roles: {:?}", mac_roles);
        }
        opts::PhyCmd::GetCountry { phy_id } => {
            let result =
                monitor_proxy.get_country(phy_id).await.context("error getting country")?;
            match result {
                Ok(country) => {
                    println!("response: \"{}\"", std::str::from_utf8(&country.alpha2[..])?);
                }
                Err(status) => {
                    println!(
                        "response: Failed with status {:?}",
                        zx_status::Status::from_raw(status)
                    );
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
            let response =
                monitor_proxy.set_country(&mut req).await.context("error setting country")?;
            println!("response: {:?}", zx_status::Status::from_raw(response));
        }
        opts::PhyCmd::ClearCountry { phy_id } => {
            let mut req = wlan_service::ClearCountryRequest { phy_id };
            let response =
                monitor_proxy.clear_country(&mut req).await.context("error clearing country")?;
            println!("response: {:?}", zx_status::Status::from_raw(response));
        }
        opts::PhyCmd::SetPsMode { phy_id, mode } => {
            println!("SetPSMode: phy_id {:?} ps_mode {:?}", phy_id, mode);
            let mut req = wlan_service::SetPsModeRequest { phy_id, ps_mode: mode.into() };
            let response =
                monitor_proxy.set_ps_mode(&mut req).await.context("error setting ps mode")?;
            println!("response: {:?}", zx_status::Status::from_raw(response));
        }
        opts::PhyCmd::GetPsMode { phy_id } => {
            let result =
                monitor_proxy.get_ps_mode(phy_id).await.context("error getting ps mode")?;
            match result {
                Ok(resp) => match resp.ps_mode {
                    PowerSaveType::PsModePerformance => {
                        println!("PS Mode Off");
                    }
                    PowerSaveType::PsModeBalanced => {
                        println!("Medium PS Mode");
                    }
                    PowerSaveType::PsModeLowPower => {
                        println!("Low Ps Mode");
                    }
                    PowerSaveType::PsModeUltraLowPower => {
                        println!("Ultra low Ps Mode");
                    }
                },
                Err(status) => {
                    println!(
                        "response: Failed with status {:?}",
                        zx_status::Status::from_raw(status)
                    );
                }
            }
        }
    }
    Ok(())
}

fn is_valid_country_str(country: &String) -> bool {
    country.len() == 2 && country.chars().all(|x| x.is_ascii())
}

async fn do_iface(cmd: opts::IfaceCmd, monitor_proxy: DeviceMonitor) -> Result<(), Error> {
    match cmd {
        opts::IfaceCmd::New { phy_id, role, sta_addr } => {
            let sta_addr = match sta_addr {
                Some(s) => s.parse::<MacAddr>()?.0,
                None => NULL_MAC_ADDR,
            };

            let mut req = wlan_service::CreateIfaceRequest { phy_id, role: role.into(), sta_addr };

            let response =
                monitor_proxy.create_iface(&mut req).await.context("error getting response")?;
            println!("response: {:?}", response);
        }
        opts::IfaceCmd::Delete { iface_id } => {
            let mut req = wlan_service::DestroyIfaceRequest { iface_id };

            let response =
                monitor_proxy.destroy_iface(&mut req).await.context("error destroying iface")?;
            match zx_status::Status::ok(response) {
                Ok(()) => println!("destroyed iface {:?}", iface_id),
                Err(s) => println!("error destroying iface: {:?}", s),
            }
        }
        opts::IfaceCmd::List => {
            let response = monitor_proxy.list_ifaces().await.context("error getting response")?;
            println!("response: {:?}", response);
        }
        opts::IfaceCmd::Query { iface_id } => {
            let result =
                monitor_proxy.query_iface(iface_id).await.context("error querying iface")?;
            match result {
                Ok(response) => println!("response: {}", format_iface_query_response(response)),
                Err(err) => println!("error querying Iface {}: {}", iface_id, err),
            }
        }
        opts::IfaceCmd::Minstrel(cmd) => match cmd {
            opts::MinstrelCmd::List { iface_id: _ } => {
                println!("List minstrel peers is not supported.");
            }
            opts::MinstrelCmd::Show { iface_id: _, peer_addr: _ } => {
                println!("Show minstrel peer is not supported.");
            }
        },
        opts::IfaceCmd::Status(cmd) => do_status(cmd, monitor_proxy).await?,
    }
    Ok(())
}

async fn do_client_connect(
    cmd: opts::ClientConnectCmd,
    monitor_proxy: DeviceMonitorProxy,
) -> Result<(), Error> {
    async fn try_get_bss_desc(
        scan_result: fidl_sme::ClientSmeScanResult,
        ssid: &Ssid,
    ) -> Result<fidl_internal::BssDescription, Error> {
        let mut bss_description = None;
        match scan_result {
            Ok(mut scan_result_list) => {
                if bss_description.is_none() {
                    // Write the first matching `BssDescription`. Any additional information is
                    // ignored.
                    if let Some(bss_info) = scan_result_list.drain(0..).find(|scan_result| {
                        // TODO(fxbug.dev/83708): Until the error produced by
                        // `ScanResult::try_from` includes some details about the scan result
                        // which failed conversion, `scan_result` must be cloned for debug
                        // logging if conversion fails.
                        match ScanResult::try_from(scan_result.clone()) {
                            Ok(scan_result) => scan_result.bss_description.ssid == *ssid,
                            Err(e) => {
                                println!("Failed to convert ScanResult: {:?}", e);
                                println!("  {:?}", scan_result);
                                false
                            }
                        }
                    }) {
                        bss_description = Some(bss_info.bss_description);
                    }
                }
            }
            Err(scan_error_code) => {
                return Err(format_err!("failed to fetch scan result: {:?}", scan_error_code));
            }
        }
        bss_description.ok_or_else(|| format_err!("failed to find BSS information for SSID"))
    }

    println!(
        "The `connect` command performs an implicit scan. This behavior is DEPRECATED and in the \
        future detailed BSS information will be required to connect! Use the `donut` tool to \
        connect to networks using an SSID."
    );
    let opts::ClientConnectCmd { iface_id, ssid, password, psk, scan_type } = cmd;
    let ssid = Ssid::try_from(ssid)?;
    let sme = get_client_sme(monitor_proxy, iface_id).await?;
    let mut req = match scan_type {
        ScanTypeArg::Active => fidl_sme::ScanRequest::Active(fidl_sme::ActiveScanRequest {
            ssids: vec![ssid.to_vec()],
            channels: vec![],
        }),
        ScanTypeArg::Passive => fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {}),
    };
    let scan_result = sme.scan(&mut req).await.context("error sending scan request")?;
    let bss_description = try_get_bss_desc(scan_result, &ssid).await?;
    let authentication = match fidl_security::Authentication::try_from(SecurityContext {
        unparsed_password_text: password,
        unparsed_psk_text: psk,
        bss: BssDescription::try_from(bss_description.clone())?,
    }) {
        Ok(authentication) => authentication,
        Err(error) => {
            println!("authentication error: {}", error);
            return Ok(());
        }
    };
    let (local, remote) = endpoints::create_proxy()?;
    let mut req = fidl_sme::ConnectRequest {
        ssid: ssid.to_vec(),
        bss_description,
        authentication,
        deprecated_scan_type: scan_type.into(),
        multiple_bss_candidates: false, // only used for metrics, select arbitrary value
    };
    sme.connect(&mut req, Some(remote)).context("error sending connect request")?;
    handle_connect_transaction(local).await
}

async fn do_client_disconnect(
    cmd: opts::ClientDisconnectCmd,
    monitor_proxy: DeviceMonitor,
) -> Result<(), Error> {
    let opts::ClientDisconnectCmd { iface_id } = cmd;
    let sme = get_client_sme(monitor_proxy, iface_id).await?;
    sme.disconnect(fidl_sme::UserDisconnectReason::WlanDevTool)
        .await
        .map_err(|e| format_err!("error sending disconnect request: {}", e))
}

async fn do_client_scan(
    cmd: opts::ClientScanCmd,
    monitor_proxy: DeviceMonitor,
) -> Result<(), Error> {
    let opts::ClientScanCmd { iface_id, scan_type } = cmd;
    let sme = get_client_sme(monitor_proxy, iface_id).await?;
    let mut req = match scan_type {
        ScanTypeArg::Passive => fidl_sme::ScanRequest::Passive(fidl_sme::PassiveScanRequest {}),
        ScanTypeArg::Active => fidl_sme::ScanRequest::Active(fidl_sme::ActiveScanRequest {
            ssids: vec![],
            channels: vec![],
        }),
    };
    let scan_result = sme.scan(&mut req).await.context("error sending scan request")?;
    print_scan_result(scan_result);
    Ok(())
}

async fn print_iface_status(iface_id: u16, monitor_proxy: DeviceMonitor) -> Result<(), Error> {
    let result = monitor_proxy
        .query_iface(iface_id)
        .await
        .context("querying iface info")?
        .map_err(|e| zx_status::Status::from_raw(e))?;

    match result.role {
        WlanMacRole::Client => {
            let client_sme = get_client_sme(monitor_proxy, iface_id).await?;
            let client_status_response = client_sme.status().await?;
            match client_status_response {
                fidl_sme::ClientStatusResponse::Connected(serving_ap_info) => {
                    println!(
                        "Iface {}: Connected to '{}' (bssid {}) channel: {:?} rssi: {}dBm snr: {}dB",
                        iface_id,
                        String::from_utf8_lossy(&serving_ap_info.ssid),
                        MacAddr(serving_ap_info.bssid),
                        serving_ap_info.channel,
                        serving_ap_info.rssi_dbm,
                        serving_ap_info.snr_db,
                    );
                }
                fidl_sme::ClientStatusResponse::Connecting(ssid) => {
                    println!("Connecting to '{}'", String::from_utf8_lossy(&ssid));
                }
                fidl_sme::ClientStatusResponse::Idle(_) => {
                    println!("Iface {}: Not connected to a network", iface_id)
                }
            }
        }
        WlanMacRole::Ap => {
            let sme = get_ap_sme(monitor_proxy, iface_id).await?;
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
        WlanMacRole::Mesh => println!("Iface {}: Mesh not supported", iface_id),
    }
    Ok(())
}

async fn do_status(cmd: opts::IfaceStatusCmd, monitor_proxy: DeviceMonitor) -> Result<(), Error> {
    let ids = get_iface_ids(monitor_proxy.clone(), cmd.iface_id).await?;

    if ids.len() == 0 {
        return Err(format_err!("No iface found"));
    }
    for iface_id in ids {
        if let Err(e) = print_iface_status(iface_id, monitor_proxy.clone()).await {
            println!("Iface {}: Error querying status: {}", iface_id, e);
            continue;
        }
    }
    Ok(())
}

async fn do_client_wmm_status(
    cmd: opts::ClientWmmStatusCmd,
    monitor_proxy: DeviceMonitor,
    stdout: &mut dyn std::io::Write,
) -> Result<(), Error> {
    let sme = get_client_sme(monitor_proxy, cmd.iface_id).await?;
    let wmm_status = sme
        .wmm_status()
        .await
        .map_err(|e| format_err!("error sending WmmStatus request: {}", e))?;
    match wmm_status {
        Ok(wmm_status) => print_wmm_status(&wmm_status, stdout)?,
        Err(code) => writeln!(stdout, "ClientSme::WmmStatus fails with status code: {}", code)?,
    }
    Ok(())
}

fn print_wmm_status(
    wmm_status: &fidl_internal::WmmStatusResponse,
    stdout: &mut dyn std::io::Write,
) -> Result<(), Error> {
    writeln!(stdout, "apsd={}", wmm_status.apsd)?;
    print_wmm_ac_params("ac_be", &wmm_status.ac_be_params, stdout)?;
    print_wmm_ac_params("ac_bk", &wmm_status.ac_bk_params, stdout)?;
    print_wmm_ac_params("ac_vi", &wmm_status.ac_vi_params, stdout)?;
    print_wmm_ac_params("ac_vo", &wmm_status.ac_vo_params, stdout)?;
    Ok(())
}

fn print_wmm_ac_params(
    ac_name: &str,
    ac_params: &fidl_internal::WmmAcParams,
    stdout: &mut dyn std::io::Write,
) -> Result<(), Error> {
    writeln!(stdout, "{ac_name}: aifsn={aifsn} acm={acm} ecw_min={ecw_min} ecw_max={ecw_max} txop_limit={txop_limit}",
             ac_name=ac_name,
             aifsn=ac_params.aifsn,
             acm=ac_params.acm,
             ecw_min=ac_params.ecw_min,
             ecw_max=ac_params.ecw_max,
             txop_limit=ac_params.txop_limit,
    )?;
    Ok(())
}

async fn do_ap(cmd: opts::ApCmd, monitor_proxy: DeviceMonitor) -> Result<(), Error> {
    match cmd {
        opts::ApCmd::Start { iface_id, ssid, password, channel } => {
            let sme = get_ap_sme(monitor_proxy, iface_id).await?;
            let mut config = fidl_sme::ApConfig {
                ssid: ssid.as_bytes().to_vec(),
                password: password.map_or(vec![], |p| p.as_bytes().to_vec()),
                radio_cfg: fidl_sme::RadioConfig {
                    phy: PhyArg::Ht.into(),
                    channel: fidl_common::WlanChannel {
                        primary: channel,
                        cbw: CbwArg::Cbw20.into(),
                        secondary80: 0,
                    },
                },
            };
            println!("{:?}", sme.start(&mut config).await?);
        }
        opts::ApCmd::Stop { iface_id } => {
            let sme = get_ap_sme(monitor_proxy, iface_id).await?;
            let r = sme.stop().await;
            println!("{:?}", r);
        }
    }
    Ok(())
}

#[cfg(target_os = "fuchsia")]
async fn do_rsn(cmd: opts::RsnCmd) -> Result<(), Error> {
    match cmd {
        opts::RsnCmd::GeneratePsk { passphrase, ssid } => {
            println!("{}", generate_psk(&passphrase, &ssid)?);
        }
    }
    Ok(())
}

#[cfg(target_os = "fuchsia")]
fn generate_psk(passphrase: &str, ssid: &str) -> Result<String, Error> {
    let psk = psk::compute(passphrase.as_bytes(), &Ssid::try_from(ssid)?)?;
    let mut psk_hex = String::new();
    psk.write_hex(&mut psk_hex)?;
    return Ok(psk_hex);
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

fn print_scan_result(scan_result: fidl_sme::ClientSmeScanResult) {
    match scan_result {
        Ok(scan_result_list) => {
            print_scan_header();
            scan_result_list
                .into_iter()
                .filter_map(
                    // TODO(fxbug.dev/83708): Until the error produced by
                    // ScanResult::TryFrom includes some details about the
                    // scan result which failed conversion, scan_result must
                    // be cloned for debug logging if conversion fails.
                    |scan_result| match ScanResult::try_from(scan_result.clone()) {
                        Ok(scan_result) => Some(scan_result),
                        Err(e) => {
                            eprintln!("Failed to convert ScanResult: {:?}", e);
                            eprintln!("  {:?}", scan_result);
                            None
                        }
                    },
                )
                .sorted_by(|a, b| a.bss_description.ssid.cmp(&b.bss_description.ssid))
                .by_ref()
                .for_each(|scan_result| print_one_scan_result(&scan_result));
        }
        Err(scan_error_code) => {
            eprintln!("Error: {:?}", scan_error_code);
        }
    }
}

fn print_scan_line(
    bssid: impl fmt::Display,
    dbm: impl fmt::Display,
    channel: impl fmt::Display,
    protection: impl fmt::Display,
    compat: impl fmt::Display,
    ssid: impl fmt::Display,
) {
    println!("{:17} {:>4} {:>6} {:12} {:10} {}", bssid, dbm, channel, protection, compat, ssid)
}

fn print_scan_header() {
    print_scan_line("BSSID", "dBm", "Chan", "Protection", "Compatible", "SSID");
}

fn print_one_scan_result(scan_result: &wlan_common::scan::ScanResult) {
    print_scan_line(
        MacAddr(scan_result.bss_description.bssid.0),
        scan_result.bss_description.rssi_dbm,
        wlan_common::channel::Channel::from(scan_result.bss_description.channel),
        scan_result.bss_description.protection(),
        if scan_result.is_compatible() { "Y" } else { "N" },
        scan_result.bss_description.ssid.to_string_not_redactable(),
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
            ConnectTransactionEvent::OnConnectResult { result } => {
                match (result.code, result.is_credential_rejected) {
                    (fidl_ieee80211::StatusCode::Success, _) => println!("Connected successfully"),
                    (fidl_ieee80211::StatusCode::Canceled, _) => {
                        eprintln!("Connecting was canceled or superseded by another command")
                    }
                    (code, true) => eprintln!("Credential rejected, status code: {:?}", code),
                    (code, false) => eprintln!("Failed to connect to network: {:?}", code),
                }
                break;
            }
            evt => {
                eprintln!("Expected ConnectTransactionEvent::OnConnectResult event, got {:?}", evt);
            }
        }
    }
    Ok(())
}

/// Constructs a `Result<(), Error>` from a `zx::zx_status_t` returned
/// from one of the `get_client_sme` or `get_ap_sme`
/// functions. In particular, when `zx_status::Status::from_raw(raw_status)` does
/// not match `zx_status::Status::OK`, this function will attach the appropriate
/// error message to the returned `Result`. When `zx_status::Status::from_raw(raw_status)`
/// does match `zx_status::Status::OK`, this function returns `Ok()`.
///
/// If this function returns an `Err`, it includes both a cause and a context.
/// The cause is a readable conversion of `raw_status` based on `station_mode`
/// and `iface_id`. The context notes the failed operation and suggests the
/// interface be checked for support of the given `station_mode`.
fn error_from_sme_raw_status(
    raw_status: zx_sys::zx_status_t,
    station_mode: WlanMacRole,
    iface_id: u16,
) -> Error {
    match zx_status::Status::from_raw(raw_status) {
        zx_status::Status::OK => Error::msg("Unexpected OK error"),
        zx_status::Status::NOT_FOUND => Error::msg("invalid interface id"),
        zx_status::Status::NOT_SUPPORTED => Error::msg("operation not supported on SME interface"),
        zx_status::Status::INTERNAL => {
            Error::msg("internal server error sending endpoint to the SME server future")
        }
        _ => Error::msg("unrecognized error associated with SME interface"),
    }
    .context(format!(
        "Failed to access {:?} for interface id {}. \
                      Please ensure the selected iface supports {:?} mode.",
        station_mode, iface_id, station_mode,
    ))
}

async fn get_client_sme(
    monitor_proxy: DeviceMonitor,
    iface_id: u16,
) -> Result<fidl_sme::ClientSmeProxy, Error> {
    let (proxy, remote) = endpoints::create_proxy()?;
    monitor_proxy
        .get_client_sme(iface_id, remote)
        .await
        .context("error sending GetClientSme request")?
        .map_err(|e| error_from_sme_raw_status(e, WlanMacRole::Client, iface_id))?;
    Ok(proxy)
}

async fn get_ap_sme(
    monitor_proxy: DeviceMonitor,
    iface_id: u16,
) -> Result<fidl_sme::ApSmeProxy, Error> {
    let (proxy, remote) = endpoints::create_proxy()?;
    monitor_proxy
        .get_ap_sme(iface_id, remote)
        .await
        .context("error sending GetApSme request")?
        .map_err(|e| error_from_sme_raw_status(e, WlanMacRole::Ap, iface_id))?;
    Ok(proxy)
}

async fn get_iface_ids(
    monitor_proxy: DeviceMonitor,
    iface_id: Option<u16>,
) -> Result<Vec<u16>, Error> {
    match iface_id {
        Some(id) => Ok(vec![id]),
        None => monitor_proxy.list_ifaces().await.context("error listing ifaces"),
    }
}

fn format_iface_query_response(resp: QueryIfaceResponse) -> String {
    format!(
        "QueryIfaceResponse {{ role: {:?}, id: {}, phy_id: {}, phy_assigned_id: {}, sta_addr: {} }}",
        resp.role,
        resp.id,
        resp.phy_id,
        resp.phy_assigned_id,
        MacAddr(resp.sta_addr)
    )
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy,
        fidl_fuchsia_wlan_device_service::DeviceMonitorMarker,
        fuchsia_async as fasync,
        futures::task::Poll,
        ieee80211::SsidError,
        pin_utils::pin_mut,
        wlan_common::{assert_variant, fake_bss_description},
    };

    #[test]
    fn format_mac_addr() {
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
    fn negotiate_authentication() {
        let bss = fake_bss_description!(Open);
        assert_eq!(
            fidl_security::Authentication::try_from(SecurityContext {
                unparsed_password_text: None,
                unparsed_psk_text: None,
                bss
            }),
            Ok(fidl_security::Authentication {
                protocol: fidl_security::Protocol::Open,
                credentials: None
            }),
        );

        let bss = fake_bss_description!(Wpa1);
        assert_eq!(
            fidl_security::Authentication::try_from(SecurityContext {
                unparsed_password_text: Some(String::from("password")),
                unparsed_psk_text: None,
                bss,
            }),
            Ok(fidl_security::Authentication {
                protocol: fidl_security::Protocol::Wpa1,
                credentials: Some(Box::new(fidl_security::Credentials::Wpa(
                    fidl_security::WpaCredentials::Passphrase(b"password".to_vec())
                ))),
            }),
        );

        let bss = fake_bss_description!(Wpa2);
        let psk = String::from("f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e");
        assert_eq!(
            fidl_security::Authentication::try_from(SecurityContext {
                unparsed_password_text: None,
                unparsed_psk_text: Some(psk),
                bss
            }),
            Ok(fidl_security::Authentication {
                protocol: fidl_security::Protocol::Wpa2Personal,
                credentials: Some(Box::new(fidl_security::Credentials::Wpa(
                    fidl_security::WpaCredentials::Psk([
                        0xf4, 0x2c, 0x6f, 0xc5, 0x2d, 0xf0, 0xeb, 0xef, 0x9e, 0xbb, 0x4b, 0x90,
                        0xb3, 0x8a, 0x5f, 0x90, 0x2e, 0x83, 0xfe, 0x1b, 0x13, 0x5a, 0x70, 0xe2,
                        0x3a, 0xed, 0x76, 0x2e, 0x97, 0x10, 0xa1, 0x2e,
                    ])
                ))),
            }),
        );

        let bss = fake_bss_description!(Wpa2);
        let psk = String::from("f42c6fc52df0ebef9ebb4b90b38a5f902e83fe1b135a70e23aed762e9710a12e");
        assert!(matches!(
            fidl_security::Authentication::try_from(SecurityContext {
                unparsed_password_text: Some(String::from("password")),
                unparsed_psk_text: Some(psk),
                bss,
            }),
            Err(_),
        ));
    }

    #[test]
    fn destroy_iface() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (monitor_svc_local, monitor_svc_remote) =
            create_proxy::<DeviceMonitorMarker>().expect("failed to create DeviceMonitor service");
        let mut monitor_svc_stream =
            monitor_svc_remote.into_stream().expect("failed to create stream");
        let del_fut = do_iface(IfaceCmd::Delete { iface_id: 5 }, monitor_svc_local);
        pin_mut!(del_fut);

        assert_variant!(exec.run_until_stalled(&mut del_fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut monitor_svc_stream.next()),
            Poll::Ready(Some(Ok(wlan_service::DeviceMonitorRequest::DestroyIface {
                req, responder
            }))) => {
                assert_eq!(req.iface_id, 5);
                responder.send(zx_status::Status::OK.into_raw()).expect("failed to send response");
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
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (monitor_svc_local, monitor_svc_remote) =
            create_proxy::<DeviceMonitorMarker>().expect("failed to create DeviceMonitor service");
        let mut monitor_svc_stream =
            monitor_svc_remote.into_stream().expect("failed to create stream");
        let fut = do_phy(PhyCmd::GetCountry { phy_id: 45 }, monitor_svc_local);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut monitor_svc_stream.next()),
            Poll::Ready(Some(Ok(wlan_service::DeviceMonitorRequest::GetCountry {
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
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (monitor_svc_local, monitor_svc_remote) =
            create_proxy::<DeviceMonitorMarker>().expect("failed to create DeviceMonitor service");
        let mut monitor_svc_stream =
            monitor_svc_remote.into_stream().expect("failed to create stream");
        let fut =
            do_phy(PhyCmd::SetCountry { phy_id: 45, country: "RS".to_string() }, monitor_svc_local);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut monitor_svc_stream.next()),
            Poll::Ready(Some(Ok(wlan_service::DeviceMonitorRequest::SetCountry {
                req, responder,
            }))) => {
                assert_eq!(req.phy_id, 45);
                assert_eq!(req.alpha2, "RS".as_bytes());
                responder.send(zx_status::Status::OK.into_raw()).expect("failed to send response");
            }
        );
    }

    #[test]
    fn test_clear_country() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (monitor_svc_local, monitor_svc_remote) =
            create_proxy::<DeviceMonitorMarker>().expect("failed to create DeviceMonitor service");
        let mut monitor_svc_stream =
            monitor_svc_remote.into_stream().expect("failed to create stream");
        let fut = do_phy(PhyCmd::ClearCountry { phy_id: 45 }, monitor_svc_local);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut monitor_svc_stream.next()),
            Poll::Ready(Some(Ok(wlan_service::DeviceMonitorRequest::ClearCountry {
                req, responder,
            }))) => {
                assert_eq!(req.phy_id, 45);
                responder.send(zx_status::Status::OK.into_raw()).expect("failed to send response");
            }
        );
    }

    #[test]
    fn test_get_ps_mode() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (monitor_svc_local, monitor_svc_remote) =
            create_proxy::<DeviceMonitorMarker>().expect("failed to create DeviceMonitor service");
        let mut monitor_svc_stream =
            monitor_svc_remote.into_stream().expect("failed to create stream");
        let fut = do_phy(PhyCmd::GetPsMode { phy_id: 45 }, monitor_svc_local);
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut monitor_svc_stream.next()),
            Poll::Ready(Some(Ok(wlan_service::DeviceMonitorRequest::GetPsMode {
                phy_id, responder,
            }))) => {
                assert_eq!(phy_id, 45);
                responder.send(
                    &mut Ok(fidl_fuchsia_wlan_device_service::GetPsModeResponse {
                        ps_mode: PowerSaveType::PsModePerformance,
                    })).expect("failed to send response");
            }
        );

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
    }

    #[test]
    fn test_set_ps_mode() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (monitor_svc_local, monitor_svc_remote) =
            create_proxy::<DeviceMonitorMarker>().expect("failed to create DeviceMonitor service");
        let mut monitor_svc_stream =
            monitor_svc_remote.into_stream().expect("failed to create stream");
        let fut = do_phy(
            PhyCmd::SetPsMode { phy_id: 45, mode: PsModeArg::PsModeBalanced },
            monitor_svc_local,
        );
        pin_mut!(fut);

        assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
        assert_variant!(
            exec.run_until_stalled(&mut monitor_svc_stream.next()),
            Poll::Ready(Some(Ok(wlan_service::DeviceMonitorRequest::SetPsMode {
                req, responder,
            }))) => {
                assert_eq!(req.phy_id, 45);
                assert_eq!(req.ps_mode, PowerSaveType::PsModeBalanced);
                responder.send(zx_status::Status::OK.into_raw()).expect("failed to send response");
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

    fn has_expected_cause(error: Error, message: &str) -> bool {
        error.chain().any(|cause| cause.to_string() == message)
    }

    #[test]
    fn test_error_from_sme_raw_status() {
        let not_found = error_from_sme_raw_status(
            zx_status::Status::NOT_FOUND.into_raw(),
            WlanMacRole::Mesh,
            1,
        );
        let not_supported = error_from_sme_raw_status(
            zx_status::Status::NOT_SUPPORTED.into_raw(),
            WlanMacRole::Ap,
            2,
        );
        let internal_error = error_from_sme_raw_status(
            zx_status::Status::INTERNAL.into_raw(),
            WlanMacRole::Client,
            3,
        );
        let unrecognized_error = error_from_sme_raw_status(
            zx_status::Status::INTERRUPTED_RETRY.into_raw(),
            WlanMacRole::Mesh,
            4,
        );

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
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (monitor_local, monitor_remote) =
            create_proxy::<DeviceMonitorMarker>().expect("failed to create DeviceMonitor service");
        let mut monitor_stream = monitor_remote.into_stream().expect("failed to create stream");
        // SSID is one byte too long.
        let cmd = opts::ClientConnectCmd {
            iface_id: 0,
            ssid: String::from_utf8(vec![65; 33]).unwrap(),
            password: None,
            psk: None,
            scan_type: opts::ScanTypeArg::Passive,
        };

        let connect_fut = do_client_connect(cmd, monitor_local.clone());
        pin_mut!(connect_fut);

        assert_variant!(exec.run_until_stalled(&mut connect_fut), Poll::Ready(Err(e)) => {
          assert_eq!(format!("{}", e), format!("{}", SsidError::Size(33)));
        });
        // No connect request is sent to SME because the command is invalid and rejected.
        assert_variant!(exec.run_until_stalled(&mut monitor_stream.next()), Poll::Pending);
    }

    #[test]
    fn test_wmm_status() {
        let mut exec = fasync::TestExecutor::new().expect("failed to create an executor");
        let (monitor_local, monitor_remote) =
            create_proxy::<DeviceMonitorMarker>().expect("failed to create DeviceMonitor service");
        let mut monitor_stream = monitor_remote.into_stream().expect("failed to create stream");
        let mut stdout = Vec::new();
        {
            let fut = do_client_wmm_status(
                ClientWmmStatusCmd { iface_id: 11 },
                monitor_local,
                &mut stdout,
            );
            pin_mut!(fut);

            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            let mut fake_sme_server_stream = assert_variant!(
                exec.run_until_stalled(&mut monitor_stream.next()),
                Poll::Ready(Some(Ok(wlan_service::DeviceMonitorRequest::GetClientSme {
                    iface_id, sme_server, responder,
                }))) => {
                    assert_eq!(iface_id, 11);
                    responder.send(&mut Ok(())).expect("failed to send GetClientSme response");
                    sme_server.into_stream().expect("sme server stream failed")
                }
            );

            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Pending);
            assert_variant!(
                exec.run_until_stalled(&mut fake_sme_server_stream.next()),
                Poll::Ready(Some(Ok(fidl_sme::ClientSmeRequest::WmmStatus { responder }))) => {
                    let mut wmm_status_resp = Ok(fidl_internal::WmmStatusResponse {
                        apsd: true,
                        ac_be_params: fidl_internal::WmmAcParams {
                            aifsn: 1,
                            acm: false,
                            ecw_min: 2,
                            ecw_max: 3,
                            txop_limit: 4,
                        },
                        ac_bk_params: fidl_internal::WmmAcParams {
                            aifsn: 5,
                            acm: false,
                            ecw_min: 6,
                            ecw_max: 7,
                            txop_limit: 8,
                        },
                        ac_vi_params: fidl_internal::WmmAcParams {
                            aifsn: 9,
                            acm: true,
                            ecw_min: 10,
                            ecw_max: 11,
                            txop_limit: 12,
                        },
                        ac_vo_params: fidl_internal::WmmAcParams {
                            aifsn: 13,
                            acm: true,
                            ecw_min: 14,
                            ecw_max: 15,
                            txop_limit: 16,
                        },
                    });
                    responder.send(&mut wmm_status_resp).expect("failed to send WMM status response");
                }
            );

            assert_variant!(exec.run_until_stalled(&mut fut), Poll::Ready(Ok(())));
        }
        assert_eq!(
            String::from_utf8(stdout).expect("expect valid UTF8"),
            "apsd=true\n\
             ac_be: aifsn=1 acm=false ecw_min=2 ecw_max=3 txop_limit=4\n\
             ac_bk: aifsn=5 acm=false ecw_min=6 ecw_max=7 txop_limit=8\n\
             ac_vi: aifsn=9 acm=true ecw_min=10 ecw_max=11 txop_limit=12\n\
             ac_vo: aifsn=13 acm=true ecw_min=14 ecw_max=15 txop_limit=16\n"
        );
    }
}
