// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use Never;
use async::temp::{Either, TempFutureExt, TempStreamExt};
use known_ess_store::{KnownEssStore, KnownEss};
use failure;
use fidl::endpoints2::create_endpoints;
use fidl_sme;
use future_util::retry_until;
use futures::channel::{oneshot, mpsc};
use futures::future;
use futures::prelude::*;
use futures::stream;
use state_machine::{self, IntoStateExt};
use std::boxed::PinBox;
use std::sync::Arc;
use zx::prelude::*;

const AUTO_CONNECT_RETRY_SECONDS: u64 = 10;
const AUTO_CONNECT_SCAN_TIMEOUT_SECONDS: u8 = 20;

#[derive(Clone)]
pub struct Client {
    req_sender: mpsc::UnboundedSender<ManualRequest>,
}

impl Client {
    pub fn connect(&self, request: ConnectRequest) -> Result<(), failure::Error> {
        self.req_sender.unbounded_send(ManualRequest::Connect(request))
            .map_err(|_| format_err!("Station does not exist anymore"))
    }
}

pub struct ConnectRequest {
    pub ssid: Vec<u8>,
    pub password: Vec<u8>,
    pub responder: oneshot::Sender<fidl_sme::ConnectResultCode>,
}

enum ManualRequest {
    Connect(ConnectRequest),
}

pub fn new_client(iface_id: u16,
                  sme: fidl_sme::ClientSmeProxy,
                  ess_store: Arc<KnownEssStore>)
    -> (Client, impl Future<Output = ()>)
{
    let (req_sender, req_receiver) = mpsc::unbounded();
    let sme_event_stream = sme.take_event_stream();
    let services = Services {
        sme,
        ess_store: Arc::clone(&ess_store)
    };
    let state_machine = future::lazy(move |_| {
        auto_connect_state(services, req_receiver.into_future())
            .into_future()
            .map_ok(Never::never_into::<()>)
            .unwrap_or_else(move |e| eprintln!("wlancfg: Client station state machine \
                    for iface {} terminated with an error: {}", iface_id, e))
    }).then(|fut| fut);
    let removal_watcher = sme_event_stream.map_ok(|_| ()).try_collect::<()>()
        .map_ok(move |()| println!("wlancfg: Client station removed (iface {})", iface_id))
        .unwrap_or_else(move |e|
            println!("wlancfg: Removing client station (iface {}) because of an error: {}", iface_id, e));
    let fut = state_machine.select(removal_watcher).map(|_| ());
    let client = Client { req_sender };
    (client, fut)
}

type State = state_machine::State<failure::Error>;
type NextReqFut = stream::StreamFuture<mpsc::UnboundedReceiver<ManualRequest>>;

#[derive(Clone)]
struct Services {
    sme: fidl_sme::ClientSmeProxy,
    ess_store: Arc<KnownEssStore>,
}

fn auto_connect_state(services: Services, next_req: NextReqFut) -> State {
    println!("wlancfg: Starting auto-connect loop");
    PinBox::new(auto_connect(services.clone())).select_unpin(next_req)
        .map(move |r| match r {
            Either::Left((services_res, next_req)) => {
                match services_res {
                    Ok(_) => Ok(connected_state(services, next_req)),
                    Err(e) => Err(e),
                }
            },
            Either::Right((_, (req, req_stream))) =>
                handle_manual_request(services, req, req_stream),
        })
        .into_state()
}

fn handle_manual_request(services: Services,
                         req: Option<ManualRequest>,
                         req_stream: mpsc::UnboundedReceiver<ManualRequest>)
    -> Result<State, failure::Error>
{
    match req {
        Some(ManualRequest::Connect(req)) => {
            Ok(manual_connect_state(services, req_stream.into_future(), req))
        },
        None => bail!("The stream of user requests ended unexpectedly")
    }
}

fn auto_connect(services: Services)
    -> impl Future<Output = Result<Vec<u8>, failure::Error>>
{
    retry_until(AUTO_CONNECT_RETRY_SECONDS.seconds(),
        move || attempt_auto_connect(services.clone()))
}

fn attempt_auto_connect(services: Services)
    -> impl Future<Output = Result<Option<Vec<u8>>, failure::Error>>
{
    // first check if we have saved networks
    if services.ess_store.known_network_count() < 1 {
        return Either::Left(future::ready(Ok(None)))
    }

    future::ready(start_scan_txn(&services.sme))
        .and_then(fetch_scan_results)
        .and_then(move |results| {
            let known_networks = {
                let services = services.clone();
                results.into_iter()
                    .filter_map(move |ess| {
                        services.ess_store.lookup(&ess.best_bss.ssid)
                            .map(|known_ess| (ess.best_bss.ssid, known_ess))
                    })
            };
            stream::iter(known_networks)
                .map(Ok)
                .try_skip_while(move |(ssid, known_ess)| {
                    connect_to_known_network(&services.sme, ssid, known_ess)
                        .map_ok(|connected| !connected)
                })
                .first_elem().map(Option::transpose)
        })
        .map_ok(|item| item.map(|(ssid, _)| ssid))
        .right_future()
}

fn connect_to_known_network(sme: &fidl_sme::ClientSmeProxy, ssid: &[u8], known_ess: &KnownEss)
    -> impl Future<Output = Result<bool, failure::Error>>
{
    let ssid_str = String::from_utf8_lossy(ssid).into_owned();
    println!("wlancfg: Auto-connecting to '{}'", ssid_str);
    future::ready(start_connect_txn(sme, &ssid, &known_ess.password))
        .and_then(wait_until_connected)
        .map_ok(move |r| match r {
            fidl_sme::ConnectResultCode::Success => {
                println!("wlancfg: Auto-connected to '{}'", ssid_str);
                true
            },
            other => {
                println!("wlancfg: Failed to auto-connect to '{}': {:?}", ssid_str, other);
                false
            },
        })
}

fn manual_connect_state(services: Services, next_req: NextReqFut, req: ConnectRequest) -> State {
    println!("wlancfg: Connecting to '{}' because of a manual request from the user",
        String::from_utf8_lossy(&req.ssid));
    let connect_fut = future::ready(start_connect_txn(&services.sme, &req.ssid, &req.password))
        .and_then(wait_until_connected);

    connect_fut.select_unpin(next_req)
        .map(move |r| match r {
            Either::Left((res, next_req)) => {
                let error_code = res?;
                req.responder.send(error_code).unwrap_or_else(|_| ());
                Ok(match error_code {
                    fidl_sme::ConnectResultCode::Success => {
                        println!("wlancfg: Successfully connected to '{}'", String::from_utf8_lossy(&req.ssid));
                        services.ess_store.store(req.ssid.clone(), KnownEss {
                            password: req.password.clone()
                        }).unwrap_or_else(|e| eprintln!("wlancfg: Failed to store network password: {}", e));
                        connected_state(services, next_req)
                    },
                    other => {
                        println!("wlancfg: Failed to connect to '{}': {:?}",
                                 String::from_utf8_lossy(&req.ssid), other);
                        auto_connect_state(services, next_req)
                    }
                })
            },
            Either::Right((_coonect_fut, (new_req, req_stream))) => {
                req.responder.send(fidl_sme::ConnectResultCode::Canceled).unwrap_or_else(|_| ());
                handle_manual_request(services, new_req, req_stream)
            }
        })
        .into_state()
}

fn connected_state(services: Services, next_req: NextReqFut) -> State {
    // TODO(gbonik): monitor connection status and jump back to auto-connect state when disconnected
    next_req
        .map(|(req, req_stream)| {
            handle_manual_request(services, req, req_stream)
        }).into_state()
}

fn start_scan_txn(sme: &fidl_sme::ClientSmeProxy)
    -> Result<fidl_sme::ScanTransactionProxy, failure::Error>
{
    let (scan_txn, remote) = create_endpoints()?;
    let mut req = fidl_sme::ScanRequest {
        timeout: AUTO_CONNECT_SCAN_TIMEOUT_SECONDS,
    };
    sme.scan(&mut req, remote)?;
    Ok(scan_txn)
}

fn start_connect_txn(sme: &fidl_sme::ClientSmeProxy, ssid: &[u8], password: &[u8])
    -> Result<fidl_sme::ConnectTransactionProxy, failure::Error>
{
    let (connect_txn, remote) = create_endpoints()?;
    let mut req = fidl_sme::ConnectRequest { ssid: ssid.to_vec(), password: password.to_vec() };
    sme.connect(&mut req, Some(remote))?;
    Ok(connect_txn)
}

fn wait_until_connected(txn: fidl_sme::ConnectTransactionProxy)
    -> impl Future<Output = Result<fidl_sme::ConnectResultCode, failure::Error>>
{
    txn.take_event_stream()
        .map_ok(|fidl_sme::ConnectTransactionEvent::OnFinished { code }| code)
        .first_elem()
        .map(Option::transpose)
        .err_into()
        .and_then(|code| future::ready(code.ok_or_else(||
            format_err!("Server closed the ConnectTransaction channel before sending a response"))))
}

fn fetch_scan_results(txn: fidl_sme::ScanTransactionProxy)
    -> impl Future<Output = Result<Vec<fidl_sme::EssInfo>, failure::Error>>
{
    txn.take_event_stream().try_fold(Vec::new(), |mut old_aps, event| {
        future::ready(match event {
            fidl_sme::ScanTransactionEvent::OnResult { aps } => {
                old_aps.extend(aps);
                Ok(old_aps)
            },
            fidl_sme::ScanTransactionEvent::OnFinished { } => Ok(old_aps),
            fidl_sme::ScanTransactionEvent::OnError { error } => {
                eprintln!("wlancfg: Scanning failed with error: {:?}", error);
                Ok(old_aps)
            }
        })
    }).err_into()
}

#[cfg(test)]
mod tests {
    use super::*;
    use async;
    use fidl::endpoints2::RequestStream;
    use fidl_sme::{ClientSmeRequest, ClientSmeRequestStream};
    use futures::stream::StreamFuture;
    use std::path::Path;
    use tempdir;

    #[test]
    fn scans_only_requested_with_saved_networks() {
        let mut exec = async::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (_client, mut fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();

        // the ess store should be empty
        assert_eq!(0, ess_store.known_network_count());

        // now verify that a scan was not requested
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        // we do not cancel the timer
        assert!(exec.wake_next_timer().is_some());

        // now add a network, and verify the count reflects it
        ess_store.store(b"bar".to_vec(), KnownEss { password: b"qwerty".to_vec() })
            .expect("failed to store a network password");
        assert_eq!(1, ess_store.known_network_count());

        // Expect the state machine to initiate the scan, then send results back
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &[&b"foo"[..], &b"bar"[..]]);
    }

    #[test]
    fn auto_connect_to_known_ess() {
        let mut exec = async::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        // save the network to trigger a scan
        ess_store.store(b"bar".to_vec(), KnownEss { password: b"qwerty".to_vec() })
            .expect("failed to store a network password");

        let (_client, mut fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();

        // Expect the state machine to initiate the scan, then send results back without the saved
        // network
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &[&b"foo"[..]]);

        // None of the returned ssids are known though, so expect the state machine to simply sleep
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());

        assert!(exec.wake_next_timer().is_some());
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect another scan request to the SME and send results
        send_scan_results(&mut exec, &mut next_sme_req, &[&b"foo"[..], &b"bar"[..]]);

        // Let the state machine process the results
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a "connect" request to the SME and reply to it
        send_connect_result(&mut exec, &mut next_sme_req, b"bar", b"qwerty",
                            fidl_sme::ConnectResultCode::Success);

        // Let the state machine absorb the connect ack
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // We should be in the 'connected' state now, with no further requests to the SME
        // or pending timers
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());
    }

    #[test]
    fn manual_connect_cancels_auto_connect() {
        let mut exec = async::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (client, mut fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();

        // Send a manual connect request and expect the state machine
        // to start connecting to the network immediately
        let mut receiver = send_manual_connect_request(&client, b"foo");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_connect_result(&mut exec, &mut next_sme_req, b"foo", b"qwerty",
                            fidl_sme::ConnectResultCode::Success);

        // Let the state machine absorb the response
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a response to the user's request
        assert_eq!(Poll::Ready(Ok(fidl_sme::ConnectResultCode::Success)),
                   exec.run_until_stalled(&mut receiver));

        // Expect no other messages to SME or pending timers
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());

        // Network should be saved as known since we connected successfully
        let known_ess = ess_store.lookup(b"foo")
            .expect("expected 'foo' to be saved as a known network");
        assert_eq!(b"qwerty", &known_ess.password[..]);
    }

    #[test]
    fn manual_connect_cancels_manual_connect() {
        let mut exec = async::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (client, mut fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();

        // Send the first manual connect request
        let mut receiver_one = send_manual_connect_request(&client, b"foo");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect the state machine to start connecting to the network immediately.
        let _connect_txn = match poll_sme_req(&mut exec, &mut next_sme_req) {
            Poll::Ready(ClientSmeRequest::Connect { req, txn, .. }) => {
                assert_eq!(b"foo", &req.ssid[..]);
                txn
            },
            _ => panic!("expected a Connect request"),
        };

        // Send another connect request without waiting for the first one to complete
        let mut receiver_two = send_manual_connect_request(&client, b"bar");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect the first request to be canceled
        assert_eq!(Poll::Ready(Ok(fidl_sme::ConnectResultCode::Canceled)),
                   exec.run_until_stalled(&mut receiver_one));

        // Expect the state machine to start connecting to the network immediately.
        // Send a successful result and let the state machine absorb it.
        send_connect_result(&mut exec, &mut next_sme_req, b"bar", b"qwerty",
                            fidl_sme::ConnectResultCode::Success);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a response to the second request from user
        assert_eq!(Poll::Ready(Ok(fidl_sme::ConnectResultCode::Success)),
                   exec.run_until_stalled(&mut receiver_two));

        // Expect no other messages to SME or pending timers
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());
    }

    #[test]
    fn manual_connect_when_already_connected() {
        let mut exec = async::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        // save the network that will be autoconnected
        ess_store.store(b"foo".to_vec(), KnownEss { password: b"12345".to_vec() })
            .expect("failed to store a network password");

        let (client, mut fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();

        // Get the state machine into the connected state by auto-connecting to a known network
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_scan_results(&mut exec, &mut next_sme_req, &[&b"foo"[..]]);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_connect_result(&mut exec, &mut next_sme_req, b"foo", b"12345",
                            fidl_sme::ConnectResultCode::Success);
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // We should be in the connected state now, with no pending timers or messages to SME
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());

        // Now, send a manual connect request and expect the machine to start connecting immediately
        let mut receiver = send_manual_connect_request(&client, b"bar");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_connect_result(&mut exec, &mut next_sme_req, b"bar", b"qwerty",
                            fidl_sme::ConnectResultCode::Success);

        // Let the state machine absorb the response
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // Expect a response to the user's request
        assert_eq!(Poll::Ready(Ok(fidl_sme::ConnectResultCode::Success)),
                   exec.run_until_stalled(&mut receiver));

        // Expect no other messages to SME or pending timers
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());
    }

    #[test]
    fn manual_connect_failure_triggers_auto_connect() {
        let mut exec = async::Executor::new().expect("failed to create an executor");
        let temp_dir = tempdir::TempDir::new("client_test").expect("failed to create temp dir");
        let ess_store = create_ess_store(temp_dir.path());
        let (client, mut fut, sme_server) = create_client(Arc::clone(&ess_store));
        let mut next_sme_req = sme_server.into_future();

        // Send a manual connect request and expect the state machine
        // to start connecting to the network immediately.
        // Reply with a failure.
        let mut receiver = send_manual_connect_request(&client, b"foo");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));
        send_connect_result(&mut exec, &mut next_sme_req, b"foo", b"qwerty",
                            fidl_sme::ConnectResultCode::Failed);
        // auto connect will only scan with a saved network, make sure we have one
        ess_store.store(b"bar".to_vec(), KnownEss { password: b"qwerty".to_vec() })
            .expect("failed to store a network password");
        assert_eq!(Poll::Pending, exec.run_until_stalled(&mut fut));

        // State machine should be in the auto-connect state now and is expected to
        // start scanning immediately
        match poll_sme_req(&mut exec, &mut next_sme_req) {
            Poll::Ready(ClientSmeRequest::Scan { .. }) => {},
            _ => panic!("expected a Scan request"),
        };

        // Expect a response to the user's request
        assert_eq!(Poll::Ready(Ok(fidl_sme::ConnectResultCode::Failed)),
                   exec.run_until_stalled(&mut receiver));

        // Expect no other messages to SME or pending timers for now
        assert!(poll_sme_req(&mut exec, &mut next_sme_req).is_pending());
        assert_eq!(None, exec.wake_next_timer());

        // Network should not be saved as known since we failed to connect
        assert_eq!(None, ess_store.lookup(b"foo"));
        assert_eq!(1, ess_store.known_network_count());
    }

    fn send_manual_connect_request(client: &Client, ssid: &[u8])
        -> oneshot::Receiver<fidl_sme::ConnectResultCode>
    {
        let (responder, receiver) = oneshot::channel();
        client.connect(ConnectRequest {
            ssid: ssid.to_vec(),
            password: b"qwerty".to_vec(),
            responder
        }).expect("sending a connect request failed");
        receiver
    }

    fn poll_sme_req(exec: &mut async::Executor,
                    next_sme_req: &mut StreamFuture<ClientSmeRequestStream>)
        -> Poll<ClientSmeRequest>
    {
        exec.run_until_stalled(next_sme_req).map(|(req, stream)| {
            *next_sme_req = stream.into_future();
            req.expect("did not expect the SME request stream to end")
                .expect("error polling SME request stream")
        })
    }

    fn send_scan_results(exec: &mut async::Executor,
                         next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
                         ssids: &[&[u8]]) {
        let txn = match poll_sme_req(exec, next_sme_req) {
            Poll::Ready(ClientSmeRequest::Scan { txn, .. }) => txn,
            Poll::Pending => panic!("expected a request to be available"),
            _ => panic!("expected a Scan request"),
        };
        let txn = txn.into_stream().expect("failed to create a scan txn stream").control_handle();
        let mut results = Vec::new();
        for ssid in ssids {
            results.push(fidl_sme::EssInfo {
                best_bss: fidl_sme::BssInfo {
                    bssid: [0, 1, 2, 3, 4, 5],
                    ssid: ssid.to_vec(),
                    rx_dbm: -30,
                    channel: 1,
                    protected: true,
                    compatible: true,
                }
            });
        }
        txn.send_on_result(&mut results.iter_mut()).expect("failed to send scan results");
        txn.send_on_finished().expect("failed to send OnFinished to ScanTxn");
    }

    fn send_connect_result(exec: &mut async::Executor,
                           next_sme_req: &mut StreamFuture<ClientSmeRequestStream>,
                           expected_ssid: &[u8],
                           expected_password: &[u8],
                           code: fidl_sme::ConnectResultCode) {
        let txn = match poll_sme_req(exec, next_sme_req) {
            Poll::Ready(ClientSmeRequest::Connect { req, txn, .. }) => {
                assert_eq!(expected_ssid, &req.ssid[..]);
                assert_eq!(expected_password, &req.password[..]);
                txn.expect("expected a Connect transaction channel")
            },
            _ => panic!("expected a Connect request"),
        };
        let txn = txn.into_stream().expect("failed to create a connect txn stream").control_handle();
        txn.send_on_finished(code).expect("failed to send OnFinished to ConnectTxn");
    }

    fn create_ess_store(path: &Path) -> Arc<KnownEssStore> {
        Arc::new(KnownEssStore::new_with_paths(path.join("store.json"), path.join("store.json.tmp"))
            .expect("failed to create an KnownEssStore"))
    }

    fn create_client(ess_store: Arc<KnownEssStore>)
        -> (Client, impl Future<Output = ()>, ClientSmeRequestStream)
    {
        let (proxy, server) = create_endpoints::<fidl_sme::ClientSmeMarker>()
            .expect("failed to create an sme channel");
        let (client, fut) = new_client(0, proxy, Arc::clone(&ess_store));
        let server = server.into_stream().expect("failed to create a request stream");
        (client, fut, server)
    }

}
