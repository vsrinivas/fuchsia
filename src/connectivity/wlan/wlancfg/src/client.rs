// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        config_manager::{credential_from_bytes, derive_security_type, SavedNetworksManager},
        known_ess_store::{KnownEss, KnownEssStore},
        network_config::clone_credential,
        policy::client::sme_credential_from_policy,
        state_machine::{self, IntoStateExt},
    },
    failure::{bail, format_err},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_wlan_common as fidl_common, fidl_fuchsia_wlan_policy as fidl_policy,
    fidl_fuchsia_wlan_sme as fidl_sme,
    fuchsia_async::DurationExt,
    fuchsia_zircon::prelude::*,
    futures::{
        channel::{mpsc, oneshot},
        future::{Future, FutureExt},
        select,
        stream::{self, StreamExt, TryStreamExt},
    },
    pin_utils::pin_mut,
    std::{collections::HashMap, sync::Arc},
    void::ResultVoidErrExt,
};

const AUTO_CONNECT_RETRY_SECONDS: i64 = 10;
const AUTO_CONNECT_SCAN_TIMEOUT_SECONDS: u8 = 20;
const DISCONNECTION_MONITOR_SECONDS: i64 = 10;

#[derive(Clone)]
pub struct Client {
    req_sender: mpsc::UnboundedSender<ManualRequest>,
}

impl Client {
    pub fn connect(&self, request: ConnectRequest) -> Result<(), failure::Error> {
        handle_send_err(self.req_sender.unbounded_send(ManualRequest::Connect(request)))
    }

    pub fn disconnect(&self, responder: oneshot::Sender<()>) -> Result<(), failure::Error> {
        handle_send_err(self.req_sender.unbounded_send(ManualRequest::Disconnect(responder)))
    }
}

fn handle_send_err(r: Result<(), mpsc::TrySendError<ManualRequest>>) -> Result<(), failure::Error> {
    r.map_err(|_| format_err!("Station does not exist anymore"))
}

pub struct ConnectRequest {
    pub ssid: Vec<u8>,
    pub password: Vec<u8>,
    pub responder: oneshot::Sender<fidl_sme::ConnectResultCode>,
}

enum ManualRequest {
    Connect(ConnectRequest),
    // The sender will be notified once we are done disconnecting.
    // If the disconnect request is canceled or superseded (e.g., by a connect request),
    // then the sender will be dropped.
    Disconnect(oneshot::Sender<()>),
}

pub fn new_client(
    iface_id: u16,
    sme: fidl_sme::ClientSmeProxy,
    ess_store: Arc<KnownEssStore>,
    saved_networks: Arc<SavedNetworksManager>,
) -> (Client, impl Future<Output = ()>) {
    let (req_sender, req_receiver) = mpsc::unbounded();
    let sme_event_stream = sme.take_event_stream();
    let services = Services {
        sme,
        ess_store: Arc::clone(&ess_store),
        saved_networks: Arc::clone(&saved_networks),
    };
    let fut = serve(iface_id, services, sme_event_stream, req_receiver);
    let client = Client { req_sender };
    (client, fut)
}

type State = state_machine::State<failure::Error>;
type NextReqFut = stream::StreamFuture<mpsc::UnboundedReceiver<ManualRequest>>;

#[derive(Clone)]
struct Services {
    sme: fidl_sme::ClientSmeProxy,
    ess_store: Arc<KnownEssStore>,
    saved_networks: Arc<SavedNetworksManager>,
}

async fn serve(
    iface_id: u16,
    services: Services,
    sme_event_stream: fidl_sme::ClientSmeEventStream,
    req_stream: mpsc::UnboundedReceiver<ManualRequest>,
) {
    let state_machine = auto_connect_state(services, req_stream.into_future()).into_state_machine();
    let removal_watcher = sme_event_stream.map_ok(|_| ()).try_collect::<()>();
    select! {
        state_machine = state_machine.fuse() =>
            println!("wlancfg: Client station state machine for iface #{} terminated with an error: {}",
                iface_id, state_machine.void_unwrap_err()),
        removal_watcher = removal_watcher.fuse() => if let Err(e) = removal_watcher {
            println!("wlancfg: Error reading from Client SME channel of iface #{}: {}",
                iface_id, e);
        },
    }
    println!("wlancfg: Removed client station for iface #{}", iface_id);
}

async fn auto_connect_state(
    services: Services,
    mut next_req: NextReqFut,
) -> Result<State, failure::Error> {
    println!(
        "wlancfg: Starting auto-connect loop with {} saved networks",
        services.saved_networks.known_network_count()
    );
    let auto_connected = auto_connect(&services);
    pin_mut!(auto_connected);
    select! {
        ssid_res = auto_connected.fuse() => {
            let _ssid = ssid_res?;
            Ok(connected_state(services.clone(), next_req).into_state())
        },
        (req, req_stream) = next_req => {
            handle_manual_request(services.clone(), req, req_stream)
        },
    }
}

fn handle_manual_request(
    services: Services,
    req: Option<ManualRequest>,
    req_stream: mpsc::UnboundedReceiver<ManualRequest>,
) -> Result<State, failure::Error> {
    match req {
        Some(ManualRequest::Connect(req)) => {
            Ok(manual_connect_state(services, req_stream.into_future(), req).into_state())
        }
        Some(ManualRequest::Disconnect(responder)) => {
            Ok(disconnected_state(responder, services, req_stream.into_future()).into_state())
        }
        None => bail!("The stream of user requests ended unexpectedly"),
    }
}

async fn auto_connect(services: &Services) -> Result<Vec<u8>, failure::Error> {
    loop {
        if let Some(ssid) = attempt_auto_connect(services).await? {
            return Ok(ssid);
        }
        fuchsia_async::Timer::new(AUTO_CONNECT_RETRY_SECONDS.seconds().after_now()).await;
    }
}

async fn attempt_auto_connect(services: &Services) -> Result<Option<Vec<u8>>, failure::Error> {
    // first check if we have saved networks
    if services.saved_networks.known_network_count() < 1 {
        return Ok(None);
    }

    let txn = start_scan_txn(&services.sme)?;
    let results = fetch_scan_results(txn).await?;
    let network_by_ssid = results
        .into_iter()
        .map(|b| {
            services
                .saved_networks
                .lookup((b.ssid.to_vec(), security_from_protection(b.protection)))
                .into_iter()
                .map(|cfg| (cfg.ssid, cfg.credential))
        })
        .flatten()
        .collect::<HashMap<_, _>>();

    for (ssid, credential) in network_by_ssid {
        if connect_to_known_network(&services.sme, ssid.clone(), clone_credential(&credential))
            .await?
        {
            services.saved_networks.record_connect_success(
                (ssid.clone(), derive_security_type(&credential)),
                &credential,
            );
            return Ok(Some(ssid));
        }
    }
    Ok(None)
}

fn security_from_protection(protection: fidl_sme::Protection) -> fidl_policy::SecurityType {
    use fidl_sme::Protection;
    match protection {
        Protection::Wpa2Enterprise | Protection::Wpa2Personal => fidl_policy::SecurityType::Wpa2,
        _ => fidl_policy::SecurityType::None,
    }
}

async fn connect_to_known_network(
    sme: &fidl_sme::ClientSmeProxy,
    ssid: Vec<u8>,
    credential: fidl_policy::Credential,
) -> Result<bool, failure::Error> {
    let ssid_str = String::from_utf8_lossy(&ssid).into_owned();
    println!("wlancfg: Auto-connecting to '{}'", ssid_str);
    let txn = start_connect_txn(sme, &ssid, &credential)?;
    match wait_until_connected(txn).await? {
        fidl_sme::ConnectResultCode::Success => {
            println!("wlancfg: Auto-connected to '{}'", ssid_str);
            Ok(true)
        }
        other => {
            println!("wlancfg: Failed to auto-connect to '{}': {:?}", ssid_str, other);
            Ok(false)
        }
    }
}

async fn manual_connect_state(
    services: Services,
    mut next_req: NextReqFut,
    req: ConnectRequest,
) -> Result<State, failure::Error> {
    println!(
        "wlancfg: Connecting to '{}' because of a manual request from the user",
        String::from_utf8_lossy(&req.ssid)
    );
    let credential = credential_from_bytes(req.password.clone());
    let txn = start_connect_txn(&services.sme, &req.ssid, &credential)?;
    let connected_fut = wait_until_connected(txn);
    pin_mut!(connected_fut);

    select! {
        connected = connected_fut.fuse() => {
            let code = connected?;
            req.responder.send(code).unwrap_or_else(|_| ());
            Ok(match code {
                fidl_sme::ConnectResultCode::Success => {
                    println!("wlancfg: Successfully connected to '{}'",
                             String::from_utf8_lossy(&req.ssid));
                    let ess = KnownEss { password: req.password.clone() };
                    services.ess_store.store(req.ssid.clone(), ess).unwrap_or_else(
                            |e| eprintln!("wlancfg: Failed to store network password: {}", e));
                    services.saved_networks.store(req.ssid.clone(), clone_credential(&credential))
                         .unwrap_or_else(
                            |e| eprintln!("wlancfg: Failed to store network config: {}", e));
                    services.saved_networks
                        .record_connect_success(
                            (req.ssid.clone(), derive_security_type(&credential)),
                            &credential_from_bytes(req.password)
                        );
                    connected_state(services, next_req).into_state()
                },
                other => {
                    println!("wlancfg: Failed to connect to '{}': {:?}",
                             String::from_utf8_lossy(&req.ssid), other);
                    auto_connect_state(services, next_req).into_state()
                }
            })
        },
        (new_req, req_stream) = next_req => {
            req.responder.send(fidl_sme::ConnectResultCode::Canceled).unwrap_or_else(|_| ());
            handle_manual_request(services, new_req, req_stream)
        },
    }
}

// This function was introduced to resolve the following error:
// ```
// error[E0391]: cycle detected when evaluating trait selection obligation
// `impl core::future::future::Future: std::marker::Send`
// ```
// which occurs when two functions that return an `impl Trait` call each other
// in a cycle. (in this case `auto_connect_state` calling `connected_state`,
// which calls `auto_connect_state`)
fn go_to_auto_connect_state(services: Services, next_req: NextReqFut) -> State {
    auto_connect_state(services, next_req).into_state()
}

async fn connected_state(
    services: Services,
    mut next_req: NextReqFut,
) -> Result<State, failure::Error> {
    let disconnected = wait_for_disconnection(services.clone());
    pin_mut!(disconnected);
    select! {
        disconnected = disconnected.fuse() => {
            disconnected?;
            Ok(go_to_auto_connect_state(services, next_req))
        },
        (req, req_stream) = next_req => {
            handle_manual_request(services, req, req_stream)
        },
    }
}

async fn wait_for_disconnection(services: Services) -> Result<(), failure::Error> {
    loop {
        let status = services.sme.status().await?;
        if status.connected_to.is_none() && status.connecting_to_ssid.is_empty() {
            return Ok(());
        }
        fuchsia_async::Timer::new(DISCONNECTION_MONITOR_SECONDS.seconds().after_now()).await;
    }
}

async fn disconnected_state(
    responder: oneshot::Sender<()>,
    services: Services,
    mut next_req: NextReqFut,
) -> Result<State, failure::Error> {
    // First, ask the SME to disconnect and wait for its response.
    // In the meantime, also listen to user requests.
    let mut responders = vec![responder];
    let mut pending_disconnect = services.sme.disconnect().fuse();
    'waiting_to_disconnect: loop {
        next_req = select! {
            res = pending_disconnect => {
                // If 'disconnect' call to SME failed, return an error since we can't
                // recover from it
                res.map_err(
                    |e| format_err!("Failed to send a disconnect command to wlanstack: {}", e))?;
                break 'waiting_to_disconnect;
            },
            (req, req_stream) = next_req => {
                match req {
                    // If another disconnect request comes in, save its responder
                    Some(ManualRequest::Disconnect(responder)) => {
                        responders.push(responder);
                        req_stream.into_future()
                    },
                    // Drop all responders to indicate that disconnecting was superseded
                    // by another command and transition to an appropriate state
                    other => return handle_manual_request(services.clone(), other, req_stream),
                }
            },
        }
    }

    // Notify the user(s) that disconnect was confirmed by the SME
    for responder in responders {
        responder.send(()).unwrap_or_else(|_| ())
    }

    // Now that we are officially disconnected, wait for user requests
    loop {
        let (req, req_stream) = next_req.await;
        next_req = match req {
            // If asked to disconnect, just reply immediately since we are already disconnected
            Some(ManualRequest::Disconnect(responder)) => {
                responder.send(()).unwrap_or_else(|_e| ());
                req_stream.into_future()
            }
            // Otherwise, handle the request normally
            other => return handle_manual_request(services.clone(), other, req_stream),
        }
    }
}

fn start_scan_txn(
    sme: &fidl_sme::ClientSmeProxy,
) -> Result<fidl_sme::ScanTransactionProxy, failure::Error> {
    let (scan_txn, remote) = create_proxy()?;
    let mut req = fidl_sme::ScanRequest {
        timeout: AUTO_CONNECT_SCAN_TIMEOUT_SECONDS,
        // TODO(WLAN-943): This means that we won't be able to auto-connect to a hidden network.
        //                 Consider in what cases we should do an active scan.
        scan_type: fidl_common::ScanType::Passive,
    };
    sme.scan(&mut req, remote)?;
    Ok(scan_txn)
}

fn start_connect_txn(
    sme: &fidl_sme::ClientSmeProxy,
    ssid: &[u8],
    credential: &fidl_policy::Credential,
) -> Result<fidl_sme::ConnectTransactionProxy, failure::Error> {
    let (connect_txn, remote) = create_proxy()?;
    let credential = sme_credential_from_policy(credential);
    let mut req = fidl_sme::ConnectRequest {
        ssid: ssid.to_vec(),
        credential,
        radio_cfg: fidl_sme::RadioConfig {
            override_phy: false,
            phy: fidl_common::Phy::Ht,
            override_cbw: false,
            cbw: fidl_common::Cbw::Cbw20,
            override_primary_chan: false,
            primary_chan: 0,
        },
        scan_type: fidl_common::ScanType::Passive,
    };
    sme.connect(&mut req, Some(remote))?;
    Ok(connect_txn)
}

async fn wait_until_connected(
    txn: fidl_sme::ConnectTransactionProxy,
) -> Result<fidl_sme::ConnectResultCode, failure::Error> {
    let mut stream = txn.take_event_stream();
    while let Some(event) = stream.try_next().await? {
        match event {
            fidl_sme::ConnectTransactionEvent::OnFinished { code } => return Ok(code),
        }
    }
    Err(format_err!("Server closed the ConnectTransaction channel before sending a response"))
}

async fn fetch_scan_results(
    txn: fidl_sme::ScanTransactionProxy,
) -> Result<Vec<fidl_sme::BssInfo>, failure::Error> {
    let mut stream = txn.take_event_stream();
    let mut all_aps = vec![];
    while let Some(event) = stream.next().await {
        match event? {
            fidl_sme::ScanTransactionEvent::OnResult { aps } => all_aps.extend(aps),
            fidl_sme::ScanTransactionEvent::OnFinished {} => return Ok(all_aps),
            fidl_sme::ScanTransactionEvent::OnError { error } => {
                eprintln!("wlancfg: Scanning failed with error: {:?}", error);
                return Ok(all_aps);
            }
        }
    }
    eprintln!("Server closed the ScanTransaction channel before sending a Finished or Error event");
    Ok(all_aps)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::network_config::NetworkConfig,
        fidl::endpoints::RequestStream,
        fidl_fuchsia_wlan_sme::{ClientSmeRequest, ClientSmeRequestStream},
        fuchsia_async as fasync,
        futures::{stream::StreamFuture, task::Poll},
        std::path::Path,
        tempfile,
        wlan_common::assert_variant,
    };

    #[test]
    fn scans_only_requested_with_saved_networks() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let not_saved_ssid = &b"foo"[..];
        let saved_ssid = &b"bar"[..];
        let saved_password = credential_from_bytes(b"qwertyuio".to_vec());
        let (_client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // the ess store should be empty
        assert_eq!(0, saved_networks.known_network_count());

        // now verify that a scan was not requested
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        // we do not cancel the timer
        assert!(exec.wake_next_timer().is_some());

        // now add a network, and verify the count reflects it
        saved_networks
            .store(saved_ssid.to_vec(), saved_password)
            .expect("failed to store a network password");
        assert_eq!(1, saved_networks.known_network_count());

        // Expect the state machine to initiate the scan, then send results back
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(
            &mut exec,
            &mut next_sme_req,
            &mut vec![bss_info(saved_ssid), bss_info(not_saved_ssid)],
        );
    }

    #[test]
    fn auto_connect_to_known_ess() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let not_saved_ssid = b"foo";
        let saved_ssid = b"bar";
        let saved_password_str = b"qwertyuio";
        let saved_password = credential_from_bytes(saved_password_str.to_vec());
        // save the network to trigger a scan
        saved_networks
            .store(saved_ssid.to_vec(), saved_password)
            .expect("failed to store a network password");

        let (_client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Expect the state machine to initiate the scan, then send results back without
        // the saved network
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &mut vec![bss_info(not_saved_ssid)]);

        // None of the returned ssids are known though, so expect the state machine to
        // simply sleep
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());

        assert!(exec.wake_next_timer().is_some());
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect another scan request to the SME and send results
        send_scan_results(
            &mut exec,
            &mut next_sme_req,
            &mut vec![bss_info(&not_saved_ssid[..]), bss_info(&saved_ssid[..])],
        );

        // Let the state machine process the results
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a "connect" request to the SME and reply to it
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            saved_ssid,
            saved_password_str,
            fidl_sme::ConnectResultCode::Success,
        );

        // Let the state machine absorb the connect ack
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // We should be in the 'connected' state now, with a pending status request and
        // no pending timers
        expect_status_req_to_sme(&mut exec, &mut next_sme_req);
        assert_eq!(None, exec.wake_next_timer());

        let config = saved_networks
            .lookup((b"bar".to_vec(), fidl_policy::SecurityType::Wpa2))
            .pop()
            .expect("Failed to get network config");
        assert!(config.has_ever_connected);
    }

    #[test]
    fn failed_auto_connect_to_known_ess() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        // save the network to trigger a scan
        saved_networks
            .store(b"bar".to_vec(), credential_from_bytes(b"qwertyuio".to_vec()))
            .expect("failed to store a network password");

        let (_client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Expect the state machine to initiate the scan
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &mut vec![bss_info(&b"bar"[..])]);

        // Let the state machine process the results
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a "connect" request to the SME and reply to it with failure
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            b"bar",
            b"qwertyuio",
            fidl_sme::ConnectResultCode::Failed,
        );
        // Auto connect failed so we should remain in the 'auto connect' state and sleep
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert!(exec.wake_next_timer().is_some());

        // After failed auto connect, the network should not have been marked connected
        let config = saved_networks
            .lookup((b"bar".to_vec(), fidl_policy::SecurityType::Wpa2))
            .pop()
            .expect("Failed to get network config");
        assert_eq!(false, config.has_ever_connected);
    }

    #[test]
    fn auto_connect_to_multiple_bss() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let saved_ssid = b"foo";
        let saved_password = b"qwertyuio";
        let saved_credential = credential_from_bytes(saved_password.to_vec());
        // save the network to trigger a scan
        saved_networks
            .store(saved_ssid.to_vec(), saved_credential)
            .expect("failed to store a network password");

        let (_client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);
        // Expect the state machine to initiate the scan, then send results back without
        // the saved network
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect another scan request to the SME and send results
        let mut bss_list = vec![
            fidl_sme::BssInfo { bssid: [0, 1, 2, 3, 4, 5], ..bss_info(&saved_ssid[..]) },
            fidl_sme::BssInfo { bssid: [5, 4, 3, 2, 1, 0], ..bss_info(&saved_ssid[..]) },
        ];
        send_scan_results(&mut exec, &mut next_sme_req, &mut bss_list);

        // Let the state machine process the results
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a "connect" request to the SME and reply to it
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            saved_ssid,
            saved_password,
            fidl_sme::ConnectResultCode::Failed,
        );

        // Both scan results had same SSID, so there shouldn't be another connect request
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());

        assert!(exec.wake_next_timer().is_some());
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        assert_eq!(None, exec.wake_next_timer());
    }

    #[test]
    fn auto_connect_when_deauth() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let saved_ssid = b"foo";
        let saved_password = b"12345678";
        let saved_credential = credential_from_bytes(saved_password.to_vec());
        // save the network that will be autoconnected
        saved_networks
            .store(saved_ssid.to_vec(), saved_credential)
            .expect("failed to store a network password");

        let (_client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Get the state machine into the connected state by auto-connecting to a known
        // network
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &mut vec![bss_info(&saved_ssid[..])]);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            saved_ssid,
            saved_password,
            fidl_sme::ConnectResultCode::Success,
        );
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // We should be in the 'connected' state now, with a pending status request and
        // no pending timers
        assert_eq!(None, exec.wake_next_timer());
        send_default_sme_status(&mut exec, &mut next_sme_req);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Since we responded that the client is still connected, a timer should be
        // scheduled for a next status request
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert!(exec.wake_next_timer().is_some());
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Timer has triggered. Respond no BSS connected to get client back to
        // auto-connect loop
        send_sme_status(&mut exec, &mut next_sme_req, None, vec![]);

        // Repeat the same auto-connect sequence as before
        // Get the state machine into the connected state by auto-connecting to a known
        // network
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &mut vec![bss_info(&saved_ssid[..])]);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            saved_ssid,
            saved_password,
            fidl_sme::ConnectResultCode::Success,
        );
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // We should be in the 'connected' state now, with a pending status request and
        // no pending timers
        expect_status_req_to_sme(&mut exec, &mut next_sme_req);
        assert_eq!(None, exec.wake_next_timer());

        // Since we connected to foo, the saved network configuration should reflect this
        let config = saved_networks
            .lookup((b"foo".to_vec(), fidl_policy::SecurityType::Wpa2))
            .pop()
            .expect("failed to get network config");
        assert!(config.has_ever_connected);
    }

    #[test]
    fn manual_connect_cancels_auto_connect() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let manual_connect_ssid = b"foo";
        let manual_connect_password = b"qwertyuio";
        let manual_connect_security = fidl_policy::SecurityType::Wpa2;
        let (client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Send a manual connect request and expect the state machine
        // to start connecting to the network immediately
        let mut receiver = send_manual_connect_request(&client, b"foo");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            manual_connect_ssid,
            manual_connect_password,
            fidl_sme::ConnectResultCode::Success,
        );

        // Let the state machine absorb the response
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a response to the user's request
        assert_eq!(
            Poll::Ready(Ok(fidl_sme::ConnectResultCode::Success)),
            exec.run_until_stalled(&mut receiver)
        );

        // Expect no pending timers
        assert_eq!(None, exec.wake_next_timer());

        // Network should be saved as known since we connected successfully
        assert_eq!(
            1,
            saved_networks.lookup((manual_connect_ssid.to_vec(), manual_connect_security)).len()
        );
        let cfg = NetworkConfig::new(
            (manual_connect_ssid.to_vec(), manual_connect_security),
            fidl_policy::Credential::Password(manual_connect_password.to_vec()),
            true,
            false,
        )
        .expect("Failed to create expected network config");
        assert_eq!(
            vec![cfg],
            saved_networks.lookup((manual_connect_ssid.to_vec(), manual_connect_security))
        );
    }

    #[test]
    fn manual_connect_cancels_manual_connect() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let manual_connect_ssid = b"foo";
        let second_network_ssid = b"bar";
        let second_network_password = b"qwertyuio";
        let (client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Send the first manual connect request
        let mut receiver_one = send_manual_connect_request(&client, manual_connect_ssid);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect the state machine to start connecting to the network immediately.
        let (req, _connect_txn) = assert_variant!(poll_sme_req(&mut exec, &mut next_sme_req),
            Poll::Ready(ClientSmeRequest::Connect { req, txn, .. }) => (req, txn)
        );
        assert_eq!(manual_connect_ssid, &req.ssid[..]);

        // Send another connect request without waiting for the first one to complete
        let mut receiver_two = send_manual_connect_request(&client, second_network_ssid);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect the first request to be canceled
        assert_eq!(
            Poll::Ready(Ok(fidl_sme::ConnectResultCode::Canceled)),
            exec.run_until_stalled(&mut receiver_one)
        );

        // Expect the state machine to start connecting to the network immediately.
        // Send a successful result and let the state machine absorb it.
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            second_network_ssid,
            second_network_password,
            fidl_sme::ConnectResultCode::Success,
        );
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a response to the second request from user
        assert_eq!(
            Poll::Ready(Ok(fidl_sme::ConnectResultCode::Success)),
            exec.run_until_stalled(&mut receiver_two)
        );

        // Expect no pending timers
        assert_eq!(None, exec.wake_next_timer());

        // Since we successfully connected to bar but not to foo, bar should have been saved and
        // foo should not have been saved
        let bar_config = saved_networks
            .lookup((b"bar".to_vec(), fidl_policy::SecurityType::Wpa2))
            .pop()
            .expect("failed to get network config");
        assert!(bar_config.has_ever_connected);
        assert!(saved_networks
            .lookup((b"foo".to_vec(), fidl_policy::SecurityType::Wpa2))
            .is_empty());
    }

    #[test]
    fn manual_connect_when_already_connected() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let saved_ssid = b"foo";
        let saved_password = b"12345678";
        let manual_connect_ssid = b"bar";
        let manual_connect_password = b"qwertyuio";
        // save the network that will be autoconnected
        saved_networks
            .store(saved_ssid.to_vec(), credential_from_bytes(saved_password.to_vec()))
            .expect("failed to store a network password");

        let (client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Get the state machine into the connected state by auto-connecting to a known
        // network
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &mut vec![bss_info(&b"foo"[..])]);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            saved_ssid,
            saved_password,
            fidl_sme::ConnectResultCode::Success,
        );
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // We should be in the connected state now. Clear out status request.
        send_default_sme_status(&mut exec, &mut next_sme_req);

        // Now, send a manual connect request and expect the machine to start connecting
        // immediately
        let mut receiver = send_manual_connect_request(&client, manual_connect_ssid);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            manual_connect_ssid,
            manual_connect_password,
            fidl_sme::ConnectResultCode::Success,
        );

        // Let the state machine absorb the response
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a response to the user's request
        assert_eq!(
            Poll::Ready(Ok(fidl_sme::ConnectResultCode::Success)),
            exec.run_until_stalled(&mut receiver)
        );

        // Expect no pending timers
        assert_eq!(None, exec.wake_next_timer());

        // Check that saved network configs for both networks have been created
        let foo_config = saved_networks
            .lookup((b"foo".to_vec(), fidl_policy::SecurityType::Wpa2))
            .pop()
            .expect("failed to get config for foo");
        let bar_config = saved_networks
            .lookup((b"bar".to_vec(), fidl_policy::SecurityType::Wpa2))
            .pop()
            .expect("failed to get config for bar");
        assert!(foo_config.has_ever_connected);
        assert!(bar_config.has_ever_connected);
    }

    #[test]
    fn manual_connect_failure_triggers_auto_connect() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let saved_ssid = b"bar";
        let saved_password = b"qwertyuio";
        let manual_connect_ssid = b"foo";
        let manual_connect_password = b"qwertyuio";
        let (client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Send a manual connect request and expect the state machine
        // to start connecting to the network immediately.
        // Reply with a failure.
        let mut receiver = send_manual_connect_request(&client, manual_connect_ssid);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            manual_connect_ssid,
            manual_connect_password,
            fidl_sme::ConnectResultCode::Failed,
        );
        // auto connect will only scan with a saved network, make sure we have one
        saved_networks
            .store(saved_ssid.to_vec(), credential_from_bytes(saved_password.to_vec()))
            .expect("failed to store a network password");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // State machine should be in the auto-connect state now and is expected to
        // start scanning immediately
        assert_variant!(
            poll_sme_req(&mut exec, &mut next_sme_req),
            Poll::Ready(ClientSmeRequest::Scan { .. })
        );

        // Expect a response to the user's request
        assert_eq!(
            Poll::Ready(Ok(fidl_sme::ConnectResultCode::Failed)),
            exec.run_until_stalled(&mut receiver)
        );

        // Expect no other messages to SME or pending timers for now
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());

        // Network should not be saved as known since we failed to connect
        assert!(saved_networks
            .lookup((manual_connect_ssid.to_vec(), fidl_policy::SecurityType::Wpa2))
            .is_empty());
        assert_eq!(1, saved_networks.known_network_count());
    }

    #[test]
    fn manual_connect_after_sme_disconnected() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let manual_connect_ssid = b"foo";
        let manual_connect_password = b"qwertyuio";
        let (client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Transition to disconnected state
        let (sender, mut receiver) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a disconnect request to SME, reply to it and absorb the response
        exchange_disconnect_with_sme(&mut exec, &mut next_sme_req);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Our disconnect request must have been processed at this point
        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut receiver));

        // Issue a manual connect request and expect a corresponding message to SME
        let _receiver = send_manual_connect_request(&client, manual_connect_ssid);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            manual_connect_ssid,
            manual_connect_password,
            fidl_sme::ConnectResultCode::Success,
        );
    }

    #[test]
    fn manual_connect_while_sme_is_disconnecting() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let manual_connect_ssid = b"foo";
        let manual_connect_password = b"qwertyuio";
        let (client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Transition to disconnected state
        let (sender, mut receiver_one) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed (1)");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a disconnect request to SME, but don't reply to it yet
        let _disconnect_responder = expect_disconnect_req_to_sme(&mut exec, &mut next_sme_req);

        // Send another disconnect request from the user
        let (sender, mut receiver_two) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed (2)");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Don't expect another message to SME since we already sent a disconnect
        // request
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());

        // Issue a manual connect request and expect a corresponding message to SME
        let _receiver = send_manual_connect_request(&client, manual_connect_ssid);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a connect request to SME, but don't reply yet
        let _connect_txn = expect_connect_req_to_sme(
            &mut exec,
            &mut next_sme_req,
            manual_connect_ssid,
            manual_connect_password,
        );

        // Both user's disconnect requests must have been marked as canceled
        assert_eq!(Poll::Ready(Err(oneshot::Canceled)), exec.run_until_stalled(&mut receiver_one));
        assert_eq!(Poll::Ready(Err(oneshot::Canceled)), exec.run_until_stalled(&mut receiver_two));
    }

    #[test]
    fn disconnect_request_when_already_disconnected() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let (client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Transition to disconnected state
        let (sender, mut receiver) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed (1)");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a disconnect request to SME, reply to it and absorb the response
        exchange_disconnect_with_sme(&mut exec, &mut next_sme_req);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Our disconnect request must have been processed at this point
        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut receiver));

        // Issue another disconnect request and expect a reply immediately since
        // we are already disconnected
        let (sender, mut receiver) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed (2)");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut receiver));
    }

    #[test]
    fn disconnect_request_when_manually_connecting() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let manual_connect_ssid = b"foo";
        let manual_connect_password = b"qwertyuio";
        let (client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Send the first manual connect request and expect a corresponding message to
        // SME
        let mut connect_receiver = send_manual_connect_request(&client, manual_connect_ssid);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        let mut connect_txn = expect_connect_req_to_sme(
            &mut exec,
            &mut next_sme_req,
            manual_connect_ssid,
            manual_connect_password,
        );

        // Before the SME replies, issue a Disconnect request
        let (sender, _disconnect_receiver) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect the connect transaction to be dropped by us
        let fut_result = exec.run_until_stalled(&mut connect_txn.next());
        assert_variant!(fut_result, Poll::Ready(None));

        // Expect a disconnect request to the SME
        let _disconnect_responder = expect_disconnect_req_to_sme(&mut exec, &mut next_sme_req);

        // User should be notified that their connect request was canceled
        assert_eq!(
            Poll::Ready(Ok(fidl_sme::ConnectResultCode::Canceled)),
            exec.run_until_stalled(&mut connect_receiver)
        );
    }

    #[test]
    fn disconnect_when_connected() {
        let mut exec = fasync::Executor::new().expect("failed to create an executor");
        let temp_dir = tempfile::TempDir::new().expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let saved_networks = create_saved_networks(temp_dir.path());
        let saved_ssid = b"foo";
        let saved_password = b"12345678";
        let (client, fut, sme_server) =
            create_client(Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let mut next_sme_req = sme_server.into_future();
        pin_mut!(fut);

        // Save the network that we will auto-connect to
        saved_networks
            .store(saved_ssid.to_vec(), credential_from_bytes(saved_password.to_vec()))
            .expect("failed to store a network password");

        // Get the state machine into the connected state by auto-connecting to a known
        // network
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &mut vec![bss_info(&b"foo"[..])]);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        exchange_connect_with_sme(
            &mut exec,
            &mut next_sme_req,
            saved_ssid,
            saved_password,
            fidl_sme::ConnectResultCode::Success,
        );
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Clear out status request that's sent when in connected state
        send_default_sme_status(&mut exec, &mut next_sme_req);

        // Request to disconnect
        let (sender, mut receiver) = oneshot::channel();
        client.disconnect(sender).expect("sending a disconnect request failed");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a disconnect request to SME, reply to it and absorb the response
        exchange_disconnect_with_sme(&mut exec, &mut next_sme_req);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Our disconnect request must have been processed at this point
        assert_eq!(Poll::Ready(Ok(())), exec.run_until_stalled(&mut receiver));
    }

    fn send_manual_connect_request(
        client: &Client,
        ssid: &[u8],
    ) -> oneshot::Receiver<fidl_sme::ConnectResultCode> {
        let (responder, receiver) = oneshot::channel();
        client
            .connect(ConnectRequest {
                ssid: ssid.to_vec(),
                password: b"qwertyuio".to_vec(),
                responder,
            })
            .expect("sending a connect request failed");
        receiver
    }

    fn poll_sme_req(
        exec: &mut fasync::Executor,
        next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
    ) -> Poll<ClientSmeRequest> {
        exec.run_until_stalled(next_sme_req).map(|(req, stream)| {
            *next_sme_req = stream.into_future();
            req.expect("did not expect the SME request stream to end")
                .expect("error polling SME request stream")
        })
    }

    fn send_scan_results(
        exec: &mut fasync::Executor,
        next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
        results: &mut Vec<fidl_sme::BssInfo>,
    ) {
        let txn = assert_variant!(poll_sme_req(exec, next_sme_req),
            Poll::Ready(ClientSmeRequest::Scan { txn, .. }) => txn
        );
        let txn = txn.into_stream().expect("failed to create a scan txn stream").control_handle();
        txn.send_on_result(&mut results.iter_mut()).expect("failed to send scan results");
        txn.send_on_finished().expect("failed to send OnFinished to ScanTxn");
    }

    fn send_sme_status(
        exec: &mut fasync::Executor,
        next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
        connected_to: Option<Box<fidl_sme::BssInfo>>,
        connecting_to_ssid: Vec<u8>,
    ) {
        let responder = assert_variant!(poll_sme_req(exec, next_sme_req),
            Poll::Ready(ClientSmeRequest::Status { responder }) => responder
        );
        let mut response = fidl_sme::ClientStatusResponse { connected_to, connecting_to_ssid };
        responder.send(&mut response).expect("failed to send status response");
    }

    fn send_default_sme_status(
        exec: &mut fasync::Executor,
        next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
    ) {
        let ssid = b"foo";
        let bss_info = bss_info(&ssid[..]);
        send_sme_status(exec, next_sme_req, Some(Box::new(bss_info)), ssid.to_vec());
    }

    fn expect_status_req_to_sme(
        exec: &mut fasync::Executor,
        next_req: &mut StreamFuture<ClientSmeRequestStream>,
    ) {
        assert_variant!(poll_sme_req(exec, next_req), Poll::Ready(ClientSmeRequest::Status { .. }));
    }

    fn exchange_connect_with_sme(
        exec: &mut fasync::Executor,
        next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
        expected_ssid: &[u8],
        expected_password: &[u8],
        code: fidl_sme::ConnectResultCode,
    ) {
        let txn = expect_connect_req_to_sme(exec, next_sme_req, expected_ssid, expected_password);
        txn.control_handle()
            .send_on_finished(code)
            .expect("failed to send OnFinished to ConnectTxn");
    }

    fn expect_connect_req_to_sme(
        exec: &mut fasync::Executor,
        next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
        expected_ssid: &[u8],
        expected_password: &[u8],
    ) -> fidl_sme::ConnectTransactionRequestStream {
        let (req, txn) = assert_variant!(poll_sme_req(exec, next_sme_req),
            Poll::Ready(ClientSmeRequest::Connect { req, txn, .. }) => (req, txn)
        );
        assert_eq!(expected_ssid, &req.ssid[..]);
        assert_variant!(&req.credential, fidl_sme::Credential::Password(password) => {
            assert_eq!(&expected_password[..], &password[..]);
        });
        txn.expect("expected a Connect transaction channel")
            .into_stream()
            .expect("failed to create a connect txn stream")
    }

    fn exchange_disconnect_with_sme(
        exec: &mut fasync::Executor,
        next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
    ) {
        let responder = expect_disconnect_req_to_sme(exec, next_sme_req);
        responder.send().expect("failed to respond to Disconnect request");
    }

    fn expect_disconnect_req_to_sme(
        exec: &mut fasync::Executor,
        next_req: &mut StreamFuture<ClientSmeRequestStream>,
    ) -> fidl_sme::ClientSmeDisconnectResponder {
        let responder = assert_variant!(
            poll_sme_req(exec, next_req),
            Poll::Ready(ClientSmeRequest::Disconnect { responder }) => responder
        );
        responder
    }

    fn create_ess_store(path: &Path) -> Arc<KnownEssStore> {
        Arc::new(
            KnownEssStore::new_with_paths(path.join("store.json"), path.join("store.json.tmp"))
                .expect("failed to create an KnownEssStore"),
        )
    }

    fn create_saved_networks(path: &Path) -> Arc<SavedNetworksManager> {
        Arc::new(
            SavedNetworksManager::new_with_paths(path.join("store.json"))
                .expect("failed to create an KnownEssStore"),
        )
    }

    fn create_client(
        ess_store: Arc<KnownEssStore>,
        saved_networks: Arc<SavedNetworksManager>,
    ) -> (Client, impl Future<Output = ()>, ClientSmeRequestStream) {
        let (proxy, server) =
            create_proxy::<fidl_sme::ClientSmeMarker>().expect("failed to create an sme channel");
        let (client, fut) =
            new_client(0, proxy, Arc::clone(&ess_store), Arc::clone(&saved_networks));
        let server = server.into_stream().expect("failed to create a request stream");
        (client, fut, server)
    }

    fn bss_info(ssid: &[u8]) -> fidl_sme::BssInfo {
        fidl_sme::BssInfo {
            bssid: [0, 1, 2, 3, 4, 5],
            ssid: ssid.to_vec(),
            rx_dbm: -30,
            channel: 1,
            protection: fidl_sme::Protection::Wpa2Personal,
            compatible: true,
        }
    }
}
