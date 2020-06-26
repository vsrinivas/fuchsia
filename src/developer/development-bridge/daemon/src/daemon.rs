// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::discovery::{TargetFinder, TargetFinderConfig},
    crate::events::{self, DaemonEvent, EventHandler, WireTrafficType},
    crate::mdns::MdnsTargetFinder,
    crate::target::{RCSConnection, Target, TargetCollection, ToFidlTarget},
    anyhow::{anyhow, Context, Error},
    async_std::task,
    async_trait::async_trait,
    ffx_core::constants::{MAX_RETRY_COUNT, RETRY_DELAY, SOCKET},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_developer_bridge::{DaemonError, DaemonRequest, DaemonRequestStream},
    fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    fidl_fuchsia_overnet_protocol::NodeId,
    futures::prelude::*,
    hoist::spawn,
    std::sync::Arc,
    std::time::Duration,
};

// Daemon
#[derive(Clone)]
pub struct Daemon {
    pub event_queue: events::Queue<DaemonEvent>,

    target_collection: Arc<TargetCollection>,
}

pub struct DaemonEventHandler {
    target_collection: Arc<TargetCollection>,
}

#[async_trait]
impl EventHandler<DaemonEvent> for DaemonEventHandler {
    async fn on_event(&self, event: DaemonEvent) -> Result<bool, Error> {
        match event {
            DaemonEvent::WireTraffic(traffic) => match traffic {
                WireTrafficType::Mdns(t) => {
                    log::info!("Found new target via mdns: {}", t.nodename);
                    self.target_collection
                        .merge_insert(t.into())
                        .then(move |target| target.run_host_pipe())
                        .await;
                }
                WireTrafficType::Fastboot(t) => {
                    log::info!("Found new target via fastboot: {}", t.nodename);
                    self.target_collection.merge_insert(t.into()).await;
                }
            },
            DaemonEvent::OvernetPeer(node_id) => {
                let remote_control_proxy = RCSConnection::new(&mut NodeId { id: node_id })
                    .await
                    .context("unable to convert proxy to target")?;
                let target = Target::from_rcs_connection(remote_control_proxy)
                    .await
                    .map_err(|e| anyhow!("unable to convert proxy to target: {}", e))?;
                log::info!("Found new target via overnet: {}", target.nodename);
                self.target_collection.merge_insert(target).await;
            }
            _ => (),
        }

        // This handler is never done, so always return false.
        Ok(false)
    }
}

impl Daemon {
    pub async fn new() -> Result<Daemon, Error> {
        log::info!("Starting daemon overnet server");
        let target_collection = Arc::new(TargetCollection::new());
        let queue = events::Queue::new(&target_collection);
        queue
            .add_handler(DaemonEventHandler { target_collection: target_collection.clone() })
            .await;
        target_collection.set_event_queue(queue.clone()).await;
        Daemon::spawn_onet_discovery(queue.clone());
        Daemon::spawn_fastboot_discovery(queue.clone());

        let config =
            TargetFinderConfig { broadcast_interval: Duration::from_secs(120), mdns_ttl: 255 };
        let mdns = MdnsTargetFinder::new(&config)?;
        mdns.start(queue.clone())?;
        Ok(Daemon { target_collection: target_collection.clone(), event_queue: queue })
    }

    #[cfg(test)]
    pub fn new_for_test() -> Daemon {
        let target_collection = Arc::new(TargetCollection::new());
        let event_queue = events::Queue::new(&target_collection);
        Daemon { target_collection, event_queue }
    }

    pub async fn handle_requests_from_stream(
        &self,
        mut stream: DaemonRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    pub fn spawn_fastboot_discovery(queue: events::Queue<DaemonEvent>) {
        spawn(async move {
            loop {
                let fastboot_devices = ffx_fastboot::find_devices();
                for dev in fastboot_devices {
                    // Add to target collection
                    let nodename = format!("{:?}", dev);
                    queue
                        .push(DaemonEvent::WireTraffic(WireTrafficType::Fastboot(
                            events::TargetInfo { nodename, addresses: Vec::new() },
                        )))
                        .await
                        .unwrap_or_else(|err| {
                            log::warn!("Fastboot discovery failed to enqueue event: {}", err)
                        });
                }
                // Sleep
                task::sleep(std::time::Duration::from_secs(3)).await;
            }
        });
    }

    pub fn spawn_onet_discovery(queue: events::Queue<DaemonEvent>) {
        spawn(async move {
            loop {
                let svc = match hoist::connect_as_service_consumer() {
                    Ok(svc) => svc,
                    Err(err) => {
                        log::info!("Overnet setup failed: {}, will retry in 1s", err);
                        task::sleep(Duration::from_secs(1)).await;
                        continue;
                    }
                };
                loop {
                    let peers = match svc.list_peers().await {
                        Ok(peers) => peers,
                        Err(err) => {
                            log::info!("Overnet peer discovery failed: {}, will retry", err);
                            task::sleep(Duration::from_secs(1)).await;
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
                    task::sleep(Duration::from_millis(100)).await;
                }
            }
        });
    }

    /// Attempts to get at most one target. If the target_selector is empty and there is only one
    /// device connected, that target will be returned.  If there are more devices and no target
    /// selector, an error is returned.  An error is also returned if there are no connected
    /// devices.
    /// TODO(fxb/47843): Implement target lookup for commands to deprecate this
    /// function, and as a result remove the inner_lock() function.
    async fn target_from_cache(&self, target_selector: String) -> Result<Arc<Target>, Error> {
        let targets = self.target_collection.inner_lock().await;

        if targets.len() == 0 {
            return Err(anyhow!("no targets connected - is your device plugged in?"));
        }

        if targets.len() > 1 && target_selector.is_empty() {
            return Err(anyhow!("more than one target - specify a target"));
        }

        let target = if target_selector.is_empty() && targets.len() == 1 {
            let target = targets.values().next();
            log::debug!("No target selector and only one target - using {:?}", target);
            target
        } else {
            // TODO: Maybe don't require an exact selector match, but calculate string distances
            // and try to make an educated guess.
            log::debug!("Using target selector {}", target_selector);
            targets.get(&target_selector)
        };

        match target {
            Some(t) => Ok(t.clone()),
            None => Err(anyhow!("no targets found")),
        }
    }

    pub async fn handle_request(&self, req: DaemonRequest) -> Result<(), Error> {
        log::debug!("daemon received request: {:?}", req);
        match req {
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
                                .map(|t| t.to_fidl_target())
                                .collect(),
                            _ => match self.target_collection.get(value.into()).await {
                                Some(t) => vec![t.to_fidl_target()],
                                None => vec![],
                            },
                        })
                        .await
                        .drain(..),
                    )
                    .context("error sending response")?;
            }
            DaemonRequest::GetRemoteControl { target, remote, responder } => {
                let target = match self.target_from_cache(target).await {
                    Ok(t) => t,
                    Err(e) => {
                        log::warn!("{}", e);
                        responder
                            .send(&mut Err(DaemonError::TargetCacheError))
                            .context("sending error response")?;
                        return Ok(());
                    }
                };
                let mut target_state =
                    match target.wait_for_state_with_rcs(MAX_RETRY_COUNT, RETRY_DELAY).await {
                        Ok(state) => state,
                        Err(e) => {
                            log::warn!("{}", e);
                            responder
                                .send(&mut Err(DaemonError::TargetStateError))
                                .context("sending error response")?;
                            return Ok(());
                        }
                    };
                let mut response = target_state
                    .rcs
                    .as_mut()
                    .unwrap()
                    .copy_to_channel(remote.into_channel())
                    .map_err(|_| DaemonError::RcsConnectionError);
                responder.send(&mut response).context("error sending response")?;
            }
            DaemonRequest::Quit { responder } => {
                log::info!("Received quit request.");
                responder.send(true).context("error sending response")?;

                task::sleep(std::time::Duration::from_millis(10)).await;

                match std::fs::remove_file(SOCKET) {
                    Ok(()) => {}
                    Err(e) => log::error!("failed to remove socket file: {}", e),
                }

                // This is not guaranteed to clean all processes all the time,
                // but is a best-effort for the time being. In the future this
                // could do something like prevent new targets from being added,
                // or from new child processes being spawned.
                let targets = self.target_collection.targets().await;
                futures::future::try_join_all(targets.iter().map(|t| t.disconnect())).await?;
                std::process::exit(0);
            }
        }
        Ok(())
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;
    use fidl_fuchsia_developer_bridge as bridge;
    use fidl_fuchsia_developer_bridge::DaemonMarker;
    use fidl_fuchsia_developer_remotecontrol::{RemoteControlMarker, RemoteControlProxy};
    use fidl_fuchsia_overnet_protocol::NodeId;
    use futures::channel::mpsc;

    struct TestHookFakeRCS {
        tc: Arc<TargetCollection>,
        ready_channel: mpsc::UnboundedSender<bool>,
    }

    #[async_trait]
    impl EventHandler<DaemonEvent> for TestHookFakeRCS {
        async fn on_event(&self, event: DaemonEvent) -> Result<bool, Error> {
            match event {
                DaemonEvent::WireTraffic(WireTrafficType::Mdns(t)) => {
                    let target = self.tc.merge_insert(t.into()).await;
                    let mut target_state = target.state.lock().await;
                    target_state.rcs = match &target_state.rcs {
                        Some(_) => panic!("fake RCS should be set at most once"),
                        None => Some(RCSConnection::new_with_proxy(
                            setup_fake_target_service(),
                            &NodeId { id: 0u64 },
                        )),
                    };
                    self.ready_channel.unbounded_send(true).unwrap();
                }
                _ => panic!("unexpected event"),
            }

            Ok(false)
        }
    }

    struct TargetControlChannels {
        event_queue: events::Queue<DaemonEvent>,
        target_ready_channel: mpsc::UnboundedReceiver<bool>,
    }

    impl TargetControlChannels {
        pub async fn send_mdns_discovery_event(&mut self, t: Target) {
            self.event_queue
                .push(DaemonEvent::WireTraffic(WireTrafficType::Mdns(events::TargetInfo {
                    nodename: t.nodename.clone(),
                    addresses: Vec::new(),
                })))
                .await
                .unwrap();
            assert!(self.next_target_ready().await);
        }

        pub async fn next_target_ready(&mut self) -> bool {
            self.target_ready_channel.next().await.unwrap()
        }
    }

    async fn spawn_daemon_server_with_target_ctrl(
        stream: DaemonRequestStream,
    ) -> TargetControlChannels {
        let (target_ready_channel_in, target_ready_channel_out) = mpsc::unbounded::<bool>();
        let d = Daemon::new_for_test();
        d.event_queue
            .add_handler(TestHookFakeRCS {
                ready_channel: target_ready_channel_in,
                tc: d.target_collection.clone(),
            })
            .await;
        let event_clone = d.event_queue.clone();
        spawn(async move {
            d.handle_requests_from_stream(stream)
                .await
                .unwrap_or_else(|err| panic!("Fatal error handling request: {:?}", err));
        });

        TargetControlChannels {
            target_ready_channel: target_ready_channel_out,
            event_queue: event_clone,
        }
    }

    async fn spawn_daemon_server_with_fake_target(
        nodename: &str,
        stream: DaemonRequestStream,
    ) -> TargetControlChannels {
        let mut res = spawn_daemon_server_with_target_ctrl(stream).await;
        res.send_mdns_discovery_event(Target::new(nodename)).await;
        res
    }

    fn setup_fake_target_service() -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();

        spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                // No requests should be made to the target RCS.
                assert!(false, format!("got unexpected {:?}", req))
            }
        });

        proxy
    }

    #[test]
    fn test_echo() {
        let echo = "test-echo";
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        hoist::run(async move {
            let _ctrl = spawn_daemon_server_with_target_ctrl(stream).await;
            let echoed = daemon_proxy.echo_string(echo).await.unwrap();
            assert_eq!(echoed, echo);
        });
    }

    #[test]
    fn test_getting_rcs_multiple_targets_mdns_with_no_selector_should_err() -> Result<(), Error> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        let (_, remote_server_end) = fidl::endpoints::create_proxy::<RemoteControlMarker>()?;
        hoist::run(async move {
            let mut ctrl = spawn_daemon_server_with_fake_target("foobar", stream).await;
            ctrl.send_mdns_discovery_event(Target::new("bazmumble")).await;
            if let Ok(_) = daemon_proxy.get_remote_control("", remote_server_end).await.unwrap() {
                panic!("failure expected for multiple targets");
            }
        });
        Ok(())
    }

    #[test]
    fn test_getting_rcs_multiple_targets_mdns_with_correct_selector_should_not_err(
    ) -> Result<(), Error> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        let (_, remote_server_end) = fidl::endpoints::create_proxy::<RemoteControlMarker>()?;
        hoist::run(async move {
            let mut ctrl = spawn_daemon_server_with_fake_target("foobar", stream).await;
            ctrl.send_mdns_discovery_event(Target::new("bazmumble")).await;
            if let Err(_) =
                daemon_proxy.get_remote_control("foobar", remote_server_end).await.unwrap()
            {
                panic!("failure unexpected for multiple targets with a matching selector");
            }
        });
        Ok(())
    }

    #[test]
    fn test_getting_rcs_multiple_targets_mdns_with_incorrect_selector_should_err(
    ) -> Result<(), Error> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        let (_, remote_server_end) = fidl::endpoints::create_proxy::<RemoteControlMarker>()?;
        hoist::run(async move {
            let mut ctrl = spawn_daemon_server_with_fake_target("foobar", stream).await;
            ctrl.send_mdns_discovery_event(Target::new("bazmumble")).await;
            if let Ok(_) =
                daemon_proxy.get_remote_control("rando", remote_server_end).await.unwrap()
            {
                panic!("failure expected for multiple targets with a mismatched selector");
            }
        });
        Ok(())
    }

    #[test]
    fn test_list_targets_mdns_discovery() -> Result<(), Error> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        hoist::run(async move {
            let mut ctrl = spawn_daemon_server_with_fake_target("foobar", stream).await;
            ctrl.send_mdns_discovery_event(Target::new("baz")).await;
            ctrl.send_mdns_discovery_event(Target::new("quux")).await;
            let res = daemon_proxy.list_targets("").await.unwrap();

            // Daemon server contains one fake target plus these two.
            assert_eq!(res.len(), 3);

            let has_nodename = |v: &Vec<bridge::Target>, s: &str| {
                v.iter().any(|x| x.nodename.as_ref().unwrap() == s)
            };
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
        });
        Ok(())
    }

    #[test]
    fn test_quit() -> Result<(), Error> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();

        if std::path::Path::new(SOCKET).is_file() {
            std::fs::remove_file(SOCKET).unwrap();
        }

        hoist::run(async move {
            let mut _ctrl = spawn_daemon_server_with_fake_target("florp", stream).await;
            let r = daemon_proxy.quit().await.unwrap();

            assert!(r);

            assert!(!std::path::Path::new(SOCKET).is_file());
        });

        Ok(())
    }
}
