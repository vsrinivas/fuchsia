// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::{get_socket, MDNS_BROADCAST_INTERVAL_SECS},
    crate::discovery::{TargetFinder, TargetFinderConfig},
    crate::events::{self, DaemonEvent, EventHandler, WireTrafficType},
    crate::fastboot::{client::Fastboot, spawn_fastboot_discovery},
    crate::mdns::MdnsTargetFinder,
    crate::target::{
        ConnectionState, RcsConnection, SshAddrFetcher, Target, TargetCollection, TargetEvent,
        ToFidlTarget,
    },
    anyhow::{anyhow, Context, Result},
    async_trait::async_trait,
    chrono::Utc,
    ffx_core::{build_info, TryStreamUtilExt},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_developer_bridge::{
        DaemonError, DaemonRequest, DaemonRequestStream, FastbootError, TargetAddrInfo,
    },
    fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
    fidl_fuchsia_overnet_protocol::NodeId,
    fuchsia_async::{Task, Timer},
    futures::prelude::*,
    hoist::{hoist, OvernetInstance},
    std::convert::TryInto,
    std::sync::{Arc, Weak},
    std::time::Duration,
};

// Daemon
#[derive(Clone)]
pub struct Daemon {
    pub event_queue: events::Queue<DaemonEvent>,

    target_collection: Arc<TargetCollection>,
}

// This is just for mocking config values for unit testing.
#[async_trait]
trait ConfigReader: Send + Sync {
    async fn get(&self, q: &str) -> Result<ffx_config::Value, ffx_config::api::ConfigError>;
}

#[derive(Default)]
struct DefaultConfigReader {}

#[async_trait]
impl ConfigReader for DefaultConfigReader {
    async fn get(&self, q: &str) -> Result<ffx_config::Value, ffx_config::api::ConfigError> {
        ffx_config::get(q).await
    }
}

pub struct DaemonEventHandler {
    // This must be a weak ref or else it will cause a circular reference.
    // Generally this should be the common practice for any handler pointing
    // to shared state, as it could be the handler's parent state that is
    // holding the event queue itself (as is the case with the target collection
    // here).
    target_collection: Weak<TargetCollection>,
    config_reader: Arc<dyn ConfigReader>,
}

impl DaemonEventHandler {
    fn new(target_collection: Weak<TargetCollection>) -> Self {
        Self { target_collection, config_reader: Arc::new(DefaultConfigReader::default()) }
    }

    async fn handle_mdns(t: events::TargetInfo, tc: &TargetCollection, cr: Weak<dyn ConfigReader>) {
        log::trace!("Found new target via mdns: {}", t.nodename);
        tc.merge_insert(t.into())
            .then(|target| {
                async move {
                    let nodename = target.nodename();

                    let t_clone = target.clone();
                    let autoconnect_fut = async move {
                        if let Some(cr) = cr.upgrade() {
                            let n = cr.get("target.default").await.ok();
                            if let Some(n) = n {
                                if n.as_str().map(|n| n == nodename).unwrap_or(false) {
                                    log::trace!(
                                        "Doing autoconnect for default target: {}",
                                        nodename
                                    );
                                    t_clone.run_host_pipe().await;
                                }
                            }
                        }
                    };

                    let _: ((), ()) = futures::join!(target.run_mdns_monitor(), autoconnect_fut);

                    // Updates state last so that if tasks are waiting on this state, everything is
                    // already running and there aren't any races.
                    target
                        .update_connection_state(|s| match s {
                            ConnectionState::Disconnected | ConnectionState::Mdns(_) => {
                                ConnectionState::Mdns(Utc::now())
                            }
                            _ => s,
                        })
                        .await;
                }
            })
            .await;
    }

    async fn handle_overnet_peer(node_id: u64, tc: &TargetCollection) {
        // It's possible that the target will be dropped in the middle of this operation.
        // Do not exit an error, just log and exit this round.
        let res = {
            match RcsConnection::new(&mut NodeId { id: node_id })
                .await
                .context("unable to convert proxy to target")
            {
                Ok(r) => Target::from_rcs_connection(r)
                    .await
                    .map_err(|e| anyhow!("unable to convert proxy to target: {}", e)),
                Err(e) => Err(e),
            }
        };

        let target = match res {
            Ok(t) => t,
            Err(e) => {
                log::error!("{:#?}", e);
                return;
            }
        };

        log::trace!("Found new target via overnet: {}", target.nodename());
        tc.merge_insert(target).then(|target| async move { target.run_logger().await }).await;
    }

    async fn handle_fastboot(t: events::TargetInfo, tc: &TargetCollection) {
        log::trace!("Found new target via fastboot: {}", t.nodename);
        tc.merge_insert(t.into())
            .then(|target| async move {
                target
                    .update_connection_state(|s| match s {
                        ConnectionState::Disconnected => ConnectionState::Fastboot,
                        _ => s,
                    })
                    .await;
            })
            .await;
    }
}

#[async_trait]
impl EventHandler<DaemonEvent> for DaemonEventHandler {
    async fn on_event(&self, event: DaemonEvent) -> Result<bool> {
        let tc = match self.target_collection.upgrade() {
            Some(t) => t,
            None => {
                log::debug!("dropped target collection");
                return Ok(true); // We're done, as the parent has been dropped.
            }
        };

        match event {
            DaemonEvent::WireTraffic(traffic) => match traffic {
                WireTrafficType::Mdns(t) => {
                    Self::handle_mdns(t, &tc, Arc::downgrade(&self.config_reader)).await;
                }
                WireTrafficType::Fastboot(t) => {
                    Self::handle_fastboot(t, &tc).await;
                }
            },
            DaemonEvent::OvernetPeer(node_id) => {
                Self::handle_overnet_peer(node_id, &tc).await;
            }
            _ => (),
        }

        // This handler is never done unless the target_collection is dropped,
        // so always return false.
        Ok(false)
    }
}

macro_rules! default_target_or_err {
    ($s:ident, $t:ident, $responder:ident, $e:expr $(,)?) => {
        match $s.get_default_target($t).await {
            Ok(t) => t,
            Err(e) => {
                log::warn!("{}", e);
                $responder.send(&mut Err($e)).context("sending error response")?;
                return Ok(());
            }
        }
    };
}

impl Daemon {
    pub async fn new() -> Result<Daemon> {
        log::info!("Starting daemon overnet server");
        let target_collection = Arc::new(TargetCollection::new());
        let queue = events::Queue::new(&target_collection);
        let daemon_event_handler = DaemonEventHandler::new(Arc::downgrade(&target_collection));
        queue.add_handler(daemon_event_handler).await;
        target_collection.set_event_queue(queue.clone()).await;
        Daemon::spawn_onet_discovery(queue.clone());
        spawn_fastboot_discovery(queue.clone());

        let config = TargetFinderConfig {
            interface_discovery_interval: Duration::from_secs(1),
            broadcast_interval: Duration::from_secs(MDNS_BROADCAST_INTERVAL_SECS),
            mdns_ttl: 255,
        };
        let mut mdns = MdnsTargetFinder::new(&config)?;
        mdns.start(queue.clone())?;
        Ok(Daemon { target_collection: target_collection.clone(), event_queue: queue })
    }

    pub async fn get_default_target(&self, n: Option<String>) -> Result<Target> {
        let n_clone = n.clone();
        // Infinite timeout here is fine, as the client dropping connection
        // will lead to this being cleaned up eventually. It is the client's
        // responsibility to determine their respective timeout(s).
        self.event_queue
            .wait_for(None, move |e| {
                if let DaemonEvent::NewTarget(n) = e {
                    // Gets either a target with the correct name if matching,
                    // or returns true if there is ANY target at all.
                    n_clone.as_ref().map(|s| n.contains(s)).unwrap_or(true)
                } else {
                    false
                }
            })
            .await?;

        // TODO(awdavies): It's possible something might happen between the new
        // target event and now, so it would make sense to give the
        // user some information on what happened: likely something
        // to do with the target suddenly being forced out of the cache
        // (this isn't a problem yet, but will be once more advanced
        // lifetime tracking is implemented). If a name isn't specified it's
        // possible a secondary/tertiary target showed up, and those cases are
        // handled here.
        self.target_collection.get_default(n).await
    }

    #[cfg(test)]
    pub async fn new_for_test() -> Daemon {
        let target_collection = Arc::new(TargetCollection::new());
        let event_queue = events::Queue::new(&target_collection);
        target_collection.set_event_queue(event_queue.clone()).await;
        Daemon { target_collection, event_queue }
    }

    pub async fn handle_requests_from_stream(&self, stream: DaemonRequestStream) -> Result<()> {
        stream
            .map_err(|e| anyhow!("reading FIDL stream: {:#}", e))
            .try_for_each_concurrent_while_connected(None, |r| self.handle_request(r))
            .await
    }

    pub fn spawn_onet_discovery(queue: events::Queue<DaemonEvent>) {
        fuchsia_async::Task::spawn(async move {
            loop {
                let svc = match hoist().connect_as_service_consumer() {
                    Ok(svc) => svc,
                    Err(err) => {
                        log::info!("Overnet setup failed: {}, will retry in 1s", err);
                        Timer::new(Duration::from_secs(1)).await;
                        continue;
                    }
                };
                loop {
                    let peers = match svc.list_peers().await {
                        Ok(peers) => peers,
                        Err(err) => {
                            log::info!("Overnet peer discovery failed: {}, will retry", err);
                            Timer::new(Duration::from_secs(1)).await;
                            // break out of the peer discovery loop on error in
                            // order to reconnect, in case the error causes the
                            // overnet interface to go bad.
                            break;
                        }
                    };
                    for peer in peers {
                        let peer_has_rcs = peer.description.services.map_or(false, |s| {
                            s.iter().find(|name| *name == RemoteControlMarker::NAME).is_some()
                        });
                        if peer_has_rcs {
                            queue.push(DaemonEvent::OvernetPeer(peer.id.id)).await.unwrap_or_else(
                                |err| {
                                    log::warn!(
                                        "Overnet discovery failed to enqueue event: {}",
                                        err
                                    );
                                },
                            );
                        }
                    }
                    Timer::new(Duration::from_millis(100)).await;
                }
            }
        })
        .detach();
    }

    pub async fn handle_request(&self, req: DaemonRequest) -> Result<()> {
        log::debug!("daemon received request: {:?}", req);
        match req {
            DaemonRequest::Crash { .. } => panic!("instructed to crash by client!"),
            DaemonRequest::EchoString { value, responder } => {
                log::info!("Received echo request for string {:?}", value);
                responder.send(value.as_ref()).context("error sending response")?;
                log::info!("echo response sent successfully");
            }
            DaemonRequest::ListTargets { value, responder } => {
                log::info!("Received list target request for '{:?}'", value);
                responder
                    .send(
                        &mut future::join_all(match value.as_ref() {
                            "" => self
                                .target_collection
                                .targets()
                                .await
                                .drain(..)
                                .map(|t| {
                                    async move {
                                        if t.is_connected().await {
                                            Some(t.to_fidl_target().await)
                                        } else {
                                            None
                                        }
                                    }
                                    .boxed()
                                })
                                .collect(),
                            _ => match self.target_collection.get(value.into()).await {
                                Some(t) => {
                                    vec![async move { Some(t.to_fidl_target().await) }.boxed()]
                                }
                                None => vec![],
                            },
                        })
                        .await
                        .drain(..)
                        .filter_map(|m| m)
                        .collect::<Vec<_>>()
                        .drain(..),
                    )
                    .context("error sending response")?;
            }
            DaemonRequest::GetRemoteControl { target, remote, responder } => {
                let target =
                    default_target_or_err!(self, target, responder, DaemonError::TargetCacheError);
                // Ensure auto-connect has at least started.
                target.run_host_pipe().await;
                match target.events.wait_for(None, |e| e == TargetEvent::RcsActivated).await {
                    Ok(()) => (),
                    Err(e) => {
                        log::warn!("{}", e);
                        responder
                            .send(&mut Err(DaemonError::RcsConnectionError))
                            .context("sending error response")?;
                        return Ok(());
                    }
                }
                let mut rcs = match target.rcs().await {
                    Some(r) => r,
                    None => {
                        log::warn!("rcs dropped after event fired");
                        responder
                            .send(&mut Err(DaemonError::TargetStateError))
                            .context("sending error response")?;
                        return Ok(());
                    }
                };
                let mut response = rcs
                    .copy_to_channel(remote.into_channel())
                    .map_err(|_| DaemonError::RcsConnectionError);
                responder.send(&mut response).context("error sending response")?;
            }
            DaemonRequest::GetFastboot { target, fastboot, responder } => {
                let target =
                    default_target_or_err!(self, target, responder, FastbootError::TargetError);
                let mut fastboot_manager = Fastboot::new(target);
                let stream = fastboot.into_stream()?;
                fuchsia_async::Task::spawn(async move {
                    match fastboot_manager.0.handle_fastboot_requests_from_stream(stream).await {
                        Ok(_) => log::debug!("Fastboot proxy finished - client disconnected"),
                        Err(e) => {
                            log::error!("There was an error handling fastboot requests: {:?}", e)
                        }
                    }
                })
                .detach();
                responder.send(&mut Ok(())).context("error sending response")?;
            }
            DaemonRequest::Quit { responder } => {
                log::info!("Received quit request.");

                match std::fs::remove_file(get_socket().await) {
                    Ok(()) => {}
                    Err(e) => log::error!("failed to remove socket file: {}", e),
                }

                if cfg!(test) {
                    panic!("quit() should not be invoked in test code");
                }

                // It is desirable for the client to receive an ACK for the quit
                // request. As Overnet has a potentially complicated routing
                // path, it is tricky to implement some notion of a bounded
                // "flush" for this response, however in practice it is only
                // necessary here to wait long enough for the message to likely
                // leave the local process before exiting. Enqueue a detached
                // timer to shut down the daemon before sending the response.
                // This is detached because once the client receives the
                // response, the client will disconnect it's socket. If the
                // local reactor observes this disconnection before the timer
                // expires, an in-line timer wait would never fire, and the
                // daemon would never exit.
                Task::spawn(
                    Timer::new(std::time::Duration::from_millis(20)).map(|_| std::process::exit(0)),
                )
                .detach();

                responder.send(true).context("error sending response")?;
            }
            DaemonRequest::GetSshAddress { responder, target, timeout } => {
                let fut = async move {
                    let target = match self.get_default_target(target).await {
                        Ok(t) => t,
                        Err(e) => {
                            log::warn!("{}", e);
                            return Err(DaemonError::TargetCacheError);
                        }
                    };

                    let poll_duration = std::time::Duration::from_millis(15);
                    loop {
                        let addrs = target.addrs().await;
                        if let Some(addr) = (&addrs).to_ssh_addr() {
                            let res: TargetAddrInfo = addr.into();
                            return Ok(res);
                        }
                        Timer::new(poll_duration).await;
                    }
                };

                return responder
                    .send(&mut match async_std::future::timeout(
                        Duration::from_nanos(timeout.try_into()?),
                        fut,
                    )
                    .await
                    {
                        Ok(mut r) => r,
                        Err(_) => Err(DaemonError::Timeout),
                    })
                    .context("sending client response");
            }
            DaemonRequest::GetVersionInfo { responder } => {
                return responder.send(build_info()).context("sending GetVersionInfo response");
            }
        }
        Ok(())
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use {
        super::*,
        async_std::future::timeout,
        fidl_fuchsia_developer_bridge as bridge,
        fidl_fuchsia_developer_bridge::DaemonMarker,
        fidl_fuchsia_developer_remotecontrol as rcs,
        fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy},
        fidl_fuchsia_net as fidl_net,
        fidl_fuchsia_net::{IpAddress, Ipv4Address, Subnet},
        fidl_fuchsia_overnet_protocol::NodeId,
        fuchsia_async::Task,
        std::net::{SocketAddr, SocketAddrV6},
    };

    struct TestHookFakeRcs {
        tc: Weak<TargetCollection>,
    }

    struct TargetControl {
        event_queue: events::Queue<DaemonEvent>,
        tc: Arc<TargetCollection>,
        _task: Task<()>,
    }

    impl TargetControl {
        pub async fn send_mdns_discovery_event(&mut self, t: Target) {
            self.event_queue
                .push(DaemonEvent::WireTraffic(WireTrafficType::Mdns(events::TargetInfo {
                    nodename: t.nodename(),
                    addresses: t.addrs().await.iter().cloned().collect(),
                    ..Default::default()
                })))
                .await
                .unwrap();

            let nodename = t.nodename();
            self.event_queue
                .wait_for(None, move |e| e == DaemonEvent::NewTarget(nodename.clone()))
                .await
                .unwrap();
            self.tc
                .get(t.nodename().into())
                .await
                .unwrap()
                .events
                .wait_for(None, |e| e == TargetEvent::RcsActivated)
                .await
                .unwrap();
        }
    }

    #[async_trait]
    impl EventHandler<DaemonEvent> for TestHookFakeRcs {
        async fn on_event(&self, event: DaemonEvent) -> Result<bool> {
            let tc = match self.tc.upgrade() {
                Some(t) => t,
                None => return Ok(true),
            };
            match event {
                DaemonEvent::WireTraffic(WireTrafficType::Mdns(t)) => {
                    tc.merge_insert(t.into()).await;
                }
                DaemonEvent::NewTarget(n) => {
                    let rcs = RcsConnection::new_with_proxy(
                        setup_fake_target_service(n),
                        &NodeId { id: 0u64 },
                    );
                    tc.merge_insert(Target::from_rcs_connection(rcs).await.unwrap()).await;
                }
                _ => panic!("unexpected event"),
            }

            Ok(false)
        }
    }

    async fn spawn_daemon_server_with_target_ctrl(stream: DaemonRequestStream) -> TargetControl {
        let d = Daemon::new_for_test().await;
        d.event_queue
            .add_handler(TestHookFakeRcs { tc: Arc::downgrade(&d.target_collection) })
            .await;
        let event_clone = d.event_queue.clone();
        let res = TargetControl {
            event_queue: event_clone,
            tc: d.target_collection.clone(),
            _task: Task::spawn(async move {
                d.handle_requests_from_stream(stream)
                    .await
                    .unwrap_or_else(|err| log::warn!("Fatal error handling request: {:?}", err));
            }),
        };
        res
    }

    async fn spawn_daemon_server_with_fake_target(
        nodename: &str,
        stream: DaemonRequestStream,
    ) -> TargetControl {
        let mut res = spawn_daemon_server_with_target_ctrl(stream).await;
        let fake_target = Target::new(nodename);
        fake_target
            .addrs_insert(
                SocketAddr::V6(SocketAddrV6::new("fe80::1".parse().unwrap(), 0, 0, 0)).into(),
            )
            .await;
        res.send_mdns_discovery_event(fake_target).await;
        res
    }

    fn setup_fake_target_service(nodename: String) -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();

        fuchsia_async::Task::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    rcs::RemoteControlRequest::IdentifyHost { responder } => {
                        let result: Vec<Subnet> = vec![Subnet {
                            addr: IpAddress::Ipv4(Ipv4Address { addr: [192, 168, 0, 1] }),
                            prefix_len: 24,
                        }];
                        let nodename =
                            if nodename.len() == 0 { None } else { Some(nodename.clone()) };
                        responder
                            .send(&mut Ok(rcs::IdentifyHostResponse {
                                nodename,
                                addresses: Some(result),
                                ..rcs::IdentifyHostResponse::EMPTY
                            }))
                            .context("sending testing response")
                            .unwrap();
                    }
                    _ => assert!(false),
                }
            }
        })
        .detach();

        proxy
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_getting_rcs_multiple_targets_mdns_with_empty_selector_should_err() -> Result<()> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        let (_, remote_server_end) = fidl::endpoints::create_proxy::<RemoteControlMarker>()?;
        let mut ctrl = spawn_daemon_server_with_fake_target("foobar", stream).await;
        ctrl.send_mdns_discovery_event(Target::new("bazmumble")).await;
        if let Ok(_) = daemon_proxy.get_remote_control(None, remote_server_end).await.unwrap() {
            panic!("failure expected for multiple targets");
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_getting_rcs_multiple_targets_mdns_with_unclear_selector_should_not_err(
    ) -> Result<()> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        let (_, remote_server_end) = fidl::endpoints::create_proxy::<RemoteControlMarker>()?;
        let mut ctrl = spawn_daemon_server_with_fake_target("foobar", stream).await;
        ctrl.send_mdns_discovery_event(Target::new("bazmumble")).await;
        if let Err(_) = daemon_proxy.get_remote_control(Some(""), remote_server_end).await.unwrap()
        {
            panic!("failure expected for multiple targets");
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_getting_rcs_multiple_targets_mdns_with_correct_selector_should_not_err(
    ) -> Result<()> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        let (_, remote_server_end) = fidl::endpoints::create_proxy::<RemoteControlMarker>()?;
        let mut ctrl = spawn_daemon_server_with_fake_target("foobar", stream).await;
        ctrl.send_mdns_discovery_event(Target::new("bazmumble")).await;
        if let Err(_) =
            daemon_proxy.get_remote_control(Some("foobar"), remote_server_end).await.unwrap()
        {
            panic!("failure unexpected for multiple targets with a matching selector");
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_getting_rcs_multiple_targets_mdns_with_incorrect_selector_should_err(
    ) -> Result<()> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        let (_, remote_server_end) = fidl::endpoints::create_proxy::<RemoteControlMarker>()?;
        let mut ctrl = spawn_daemon_server_with_fake_target("foobar", stream).await;
        ctrl.send_mdns_discovery_event(Target::new("bazmumble")).await;
        if let Ok(_) = timeout(Duration::from_millis(10), async move {
            daemon_proxy.get_remote_control(Some("rando"), remote_server_end).await.unwrap()
        })
        .await
        {
            panic!("failure expected for multiple targets with a mismatched selector");
        }
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_list_targets_mdns_discovery() -> Result<()> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        let mut ctrl = spawn_daemon_server_with_fake_target("foobar", stream).await;
        ctrl.send_mdns_discovery_event(Target::new("baz")).await;
        ctrl.send_mdns_discovery_event(Target::new("quux")).await;
        let res = daemon_proxy.list_targets("").await.unwrap();

        // Daemon server contains one fake target plus these two.
        assert_eq!(res.len(), 3);

        let has_nodename =
            |v: &Vec<bridge::Target>, s: &str| v.iter().any(|x| x.nodename.as_ref().unwrap() == s);
        assert!(has_nodename(&res, "foobar"));
        assert!(has_nodename(&res, "baz"));
        assert!(has_nodename(&res, "quux"));

        let res = daemon_proxy.list_targets("mlorp").await.unwrap();
        assert!(!has_nodename(&res, "foobar"));
        assert!(!has_nodename(&res, "baz"));
        assert!(!has_nodename(&res, "quux"));

        let res = daemon_proxy.list_targets("foobar").await.unwrap();
        assert!(has_nodename(&res, "foobar"));
        assert!(!has_nodename(&res, "baz"));
        assert!(!has_nodename(&res, "quux"));

        let res = daemon_proxy.list_targets("baz").await.unwrap();
        assert!(!has_nodename(&res, "foobar"));
        assert!(has_nodename(&res, "baz"));
        assert!(!has_nodename(&res, "quux"));

        let res = daemon_proxy.list_targets("quux").await.unwrap();
        assert!(!has_nodename(&res, "foobar"));
        assert!(!has_nodename(&res, "baz"));
        assert!(has_nodename(&res, "quux"));
        Ok(())
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_get_ssh_address() -> Result<()> {
        let (daemon_proxy, stream) = fidl::endpoints::create_proxy_and_stream::<DaemonMarker>()?;
        let mut ctrl = spawn_daemon_server_with_fake_target("foobar", stream).await;
        let timeout = std::i64::MAX;
        let r = daemon_proxy.get_ssh_address(Some("foobar"), timeout).await?;

        // This is from the `spawn_daemon_server_with_fake_target` impl.
        let want = Ok(bridge::TargetAddrInfo::Ip(bridge::TargetIp {
            ip: fidl_net::IpAddress::Ipv6(fidl_net::Ipv6Address {
                addr: [254, 128, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
            }),
            scope_id: 0,
        }));
        assert_eq!(r, want);

        let r = daemon_proxy.get_ssh_address(Some("toothpaste"), 10000).await?;
        assert_eq!(r, Err(DaemonError::Timeout));

        // Target with empty addresses should timeout.
        ctrl.send_mdns_discovery_event(Target::new("baz")).await;
        let r = daemon_proxy.get_ssh_address(Some("baz"), 10000).await?;
        assert_eq!(r, Err(DaemonError::Timeout));

        let r = daemon_proxy.get_ssh_address(Some("foobar"), timeout).await?;
        assert_eq!(r, want);

        Ok(())
    }

    struct FakeConfigReader {
        query_expected: String,
        value: String,
    }

    #[async_trait]
    impl ConfigReader for FakeConfigReader {
        async fn get(&self, q: &str) -> Result<ffx_config::Value, ffx_config::api::ConfigError> {
            assert_eq!(q, self.query_expected);
            Ok(ffx_config::Value::String(self.value.clone()))
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_daemon_mdns_event_handler() {
        let t = Target::new("this-town-aint-big-enough-for-the-three-of-us");

        let tc = Arc::new(TargetCollection::new());
        tc.merge_insert(t.clone()).await;

        let handler = DaemonEventHandler {
            target_collection: Arc::downgrade(&tc),
            config_reader: Arc::new(FakeConfigReader {
                query_expected: "target.default".to_owned(),
                value: "florp".to_owned(),
            }),
        };
        assert!(!handler
            .on_event(DaemonEvent::WireTraffic(WireTrafficType::Mdns(events::TargetInfo {
                nodename: t.nodename(),
                ..Default::default()
            })))
            .await
            .unwrap());

        // This is a bit of a hack to check the target state without digging
        // into internals. This also avoids running into a race waiting for an
        // event to be propagated.
        t.update_connection_state(|s| {
            if let ConnectionState::Mdns(ref _t) = s {
                s
            } else {
                panic!("state not updated by event, should be set to MDNS state.");
            }
        })
        .await;

        assert_eq!(
            t.task_manager.task_snapshot(crate::target::TargetTaskType::HostPipe).await,
            crate::task::TaskSnapshot::NotRunning
        );

        // This handler will now return the value of the default target as
        // intended.
        let handler = DaemonEventHandler {
            target_collection: Arc::downgrade(&tc),
            config_reader: Arc::new(FakeConfigReader {
                query_expected: "target.default".to_owned(),
                value: "this-town-aint-big-enough-for-the-three-of-us".to_owned(),
            }),
        };
        assert!(!handler
            .on_event(DaemonEvent::WireTraffic(WireTrafficType::Mdns(events::TargetInfo {
                nodename: t.nodename(),
                ..Default::default()
            })))
            .await
            .unwrap());

        assert_eq!(
            t.task_manager.task_snapshot(crate::target::TargetTaskType::HostPipe).await,
            crate::task::TaskSnapshot::Running
        );

        // TODO(awdavies): RCS, Fastboot, etc. events.
    }
}
