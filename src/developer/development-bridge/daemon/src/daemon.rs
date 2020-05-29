// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::discovery::{TargetFinder, TargetFinderConfig},
    crate::mdns::MdnsTargetFinder,
    crate::ok_or_continue,
    crate::target::{RCSConnection, Target, TargetCollection, ToFidlTarget},
    anyhow::{anyhow, Context, Error},
    async_std::task,
    async_trait::async_trait,
    ffx_core::constants::{MAX_RETRY_COUNT, RETRY_DELAY, SOCKET},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_developer_bridge::{DaemonError, DaemonRequest, DaemonRequestStream},
    fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
    fidl_fuchsia_overnet::ServiceConsumerProxyInterface,
    futures::channel::mpsc,
    futures::lock::Mutex,
    futures::prelude::*,
    hoist::spawn,
    std::rc::Rc,
    std::sync::Arc,
    std::time::Duration,
};

#[async_trait]
pub trait DiscoveryHook {
    async fn on_new_target(&self, target: &Arc<Target>, tc: &Arc<TargetCollection>);
}

#[derive(Default)]
struct RCSActivatorHook {}

#[async_trait]
impl DiscoveryHook for RCSActivatorHook {
    async fn on_new_target(&self, target: &Arc<Target>, _tc: &Arc<TargetCollection>) {
        Target::run_host_pipe(target.clone()).await;
    }
}

// Daemon
#[derive(Clone)]
pub struct Daemon {
    target_collection: Arc<TargetCollection>,

    discovered_target_hooks: Arc<Mutex<Vec<Rc<dyn DiscoveryHook>>>>,
}

impl Daemon {
    pub async fn new() -> Result<Daemon, Error> {
        log::info!("Starting daemon overnet server");
        let (tx, rx) = mpsc::unbounded::<Target>();
        let target_collection = Arc::new(TargetCollection::new());
        let discovered_target_hooks = Arc::new(Mutex::new(Vec::<Rc<dyn DiscoveryHook>>::new()));
        Daemon::spawn_receiver_loop(rx, target_collection.clone(), discovered_target_hooks.clone());
        Daemon::spawn_onet_discovery(target_collection.clone());
        Daemon::spawn_fastboot_discovery(target_collection.clone());
        let mut d = Daemon {
            target_collection: target_collection.clone(),
            discovered_target_hooks: discovered_target_hooks.clone(),
        };
        d.register_hook(RCSActivatorHook::default()).await;

        // MDNS must be started as late as possible to avoid races with registered
        // hooks.
        let config =
            TargetFinderConfig { broadcast_interval: Duration::from_secs(120), mdns_ttl: 255 };
        let mdns = MdnsTargetFinder::new(&config)?;
        mdns.start(&tx)?;

        Ok(d)
    }

    pub async fn register_hook(&mut self, cb: impl DiscoveryHook + 'static) {
        let mut hooks = self.discovered_target_hooks.lock().await;
        hooks.push(Rc::new(cb));
    }

    pub fn spawn_receiver_loop(
        rx: mpsc::UnboundedReceiver<Target>,
        tc: Arc<TargetCollection>,
        hooks: Arc<Mutex<Vec<Rc<dyn DiscoveryHook>>>>,
    ) {
        spawn(async move {
            rx.for_each_concurrent(None, |target| async {
                let target_clone = tc.merge_insert(target).await;
                let tc_clone = tc.clone();
                let hooks_clone = (*hooks.lock().await).clone();
                futures::future::join_all(
                    hooks_clone.iter().map(|hook| hook.on_new_target(&target_clone, &tc_clone)),
                )
                .await;
            })
            .await
        });
    }

    #[cfg(test)]
    pub fn new_with_rx(rx: mpsc::UnboundedReceiver<Target>) -> Daemon {
        let target_collection = Arc::new(TargetCollection::new());
        let discovered_target_hooks = Arc::new(Mutex::new(Vec::<Rc<dyn DiscoveryHook>>::new()));
        Daemon::spawn_receiver_loop(rx, target_collection.clone(), discovered_target_hooks.clone());
        Daemon { target_collection, discovered_target_hooks }
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

    pub fn spawn_fastboot_discovery(tc: Arc<TargetCollection>) {
        spawn(async move {
            loop {
                let fastboot_devices = ffx_fastboot::find_devices();
                for dev in fastboot_devices {
                    // Add to target collection
                    let nodename = format!("{:?}", dev);
                    let target = Target::new(&nodename);
                    log::info!("Found new target via fastboot: {}", target.nodename);
                    tc.merge_insert(target).await;
                }
                // Sleep
                task::sleep(std::time::Duration::from_secs(3)).await;
            }
        });
    }

    pub fn spawn_onet_discovery(tc: Arc<TargetCollection>) {
        spawn(async move {
            let svc = hoist::connect_as_service_consumer().unwrap();
            loop {
                let peers = svc.list_peers().await.unwrap();
                for mut peer in peers {
                    if peer.description.services.is_none() {
                        continue;
                    }
                    if peer
                        .description
                        .services
                        .unwrap()
                        .iter()
                        .find(|name| *name == RemoteControlMarker::NAME)
                        .is_none()
                    {
                        continue;
                    }
                    let remote_control_proxy = ok_or_continue!(RCSConnection::new(&mut peer.id)
                        .await
                        .context("unable to convert proxy to target"));
                    let target = ok_or_continue!(
                        Target::from_rcs_connection(remote_control_proxy).await,
                        "unable to convert proxy to target",
                    );
                    log::info!("Found new target via overnet: {}", target.nodename);
                    tc.merge_insert(target).await;
                }
            }
        });
    }

    /// Attempts to get at most one target. If there is more than one target,
    /// returns an error.
    /// TODO(fxb/47843): Implement target lookup for commands to deprecate this
    /// function, and as a result remove the inner_lock() function.
    async fn target_from_cache(&self) -> Result<Arc<Target>, Error> {
        let targets = self.target_collection.inner_lock().await;
        if targets.len() > 1 {
            return Err(anyhow!("more than one target"));
        }

        match targets.values().next() {
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
            DaemonRequest::GetRemoteControl { remote, responder } => {
                let target = match self.target_from_cache().await {
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
    use crate::target::TargetState;
    use fidl::endpoints::create_proxy;
    use fidl_fuchsia_developer_bridge as bridge;
    use fidl_fuchsia_developer_bridge::DaemonMarker;
    use fidl_fuchsia_developer_remotecontrol::{
        RemoteControlMarker, RemoteControlProxy, RemoteControlRequest,
    };
    use fidl_fuchsia_overnet_protocol::NodeId;
    use std::collections::HashSet;

    struct TestHookFakeRCS {
        ready_channel: mpsc::UnboundedSender<bool>,
    }

    impl TestHookFakeRCS {
        pub fn new(ready_channel: mpsc::UnboundedSender<bool>) -> Self {
            Self { ready_channel }
        }
    }

    #[async_trait]
    impl DiscoveryHook for TestHookFakeRCS {
        async fn on_new_target(&self, target: &Arc<Target>, _tc: &Arc<TargetCollection>) {
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
    }

    struct TargetControlChannels {
        target_ready_channel: mpsc::UnboundedReceiver<bool>,
        target_detected_channel: mpsc::UnboundedSender<Target>,
    }

    impl TargetControlChannels {
        pub async fn send_target(&mut self, t: Target) {
            self.target_detected_channel.unbounded_send(t).unwrap();
            assert!(self.next_target_ready().await);
        }

        pub async fn next_target_ready(&mut self) -> bool {
            self.target_ready_channel.next().await.unwrap()
        }
    }

    async fn spawn_daemon_server_with_target_ctrl(
        stream: DaemonRequestStream,
    ) -> TargetControlChannels {
        let (target_in, target_out) = mpsc::unbounded::<Target>();
        let (target_ready_channel_in, target_ready_channel_out) = mpsc::unbounded::<bool>();
        spawn(async move {
            let mut d = Daemon::new_with_rx(target_out);
            d.register_hook(TestHookFakeRCS::new(target_ready_channel_in)).await;
            d.handle_requests_from_stream(stream)
                .await
                .unwrap_or_else(|err| panic!("Fatal error handling request: {:?}", err));
        });

        TargetControlChannels {
            target_ready_channel: target_ready_channel_out,
            target_detected_channel: target_in,
        }
    }

    async fn spawn_daemon_server_with_fake_target(
        stream: DaemonRequestStream,
    ) -> TargetControlChannels {
        let mut res = spawn_daemon_server_with_target_ctrl(stream).await;
        res.send_target(Target::new("foobar")).await;
        res
    }

    fn setup_fake_target_service() -> RemoteControlProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<RemoteControlMarker>().unwrap();

        spawn(async move {
            while let Ok(req) = stream.try_next().await {
                match req {
                    Some(RemoteControlRequest::StartComponent { responder, .. }) => {
                        let _ = responder.send(&mut Ok(())).context("sending ok response");
                    }
                    _ => assert!(false),
                }
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
    fn test_getting_rcs_multiple_targets() -> Result<(), Error> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        let (_, remote_server_end) = create_proxy::<RemoteControlMarker>()?;
        hoist::run(async move {
            let mut ctrl = spawn_daemon_server_with_fake_target(stream).await;
            ctrl.send_target(Target::new("bazmumble")).await;
            match daemon_proxy.get_remote_control(remote_server_end).await.unwrap() {
                Ok(_) => panic!("failure expected for multiple targets"),
                _ => (),
            }
        });

        Ok(())
    }

    #[test]
    fn test_list_targets() -> Result<(), Error> {
        let (daemon_proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DaemonMarker>().unwrap();
        hoist::run(async move {
            let mut ctrl = spawn_daemon_server_with_fake_target(stream).await;
            ctrl.send_target(Target::new("baz")).await;
            ctrl.send_target(Target::new("quux")).await;
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
            let mut _ctrl = spawn_daemon_server_with_fake_target(stream).await;
            let r = daemon_proxy.quit().await.unwrap();

            assert!(r);

            assert!(!std::path::Path::new(SOCKET).is_file());
        });

        Ok(())
    }

    struct TestHookFirst {
        callbacks_done: mpsc::UnboundedSender<bool>,
    }

    #[async_trait]
    impl DiscoveryHook for TestHookFirst {
        async fn on_new_target(&self, target: &Arc<Target>, tc: &Arc<TargetCollection>) {
            // This will crash if the target isn't already inserted.
            let t = tc.get(target.nodename.clone().into()).await.unwrap().clone();
            assert_eq!(t.nodename, "nothin");
            assert_eq!(*t.state.lock().await, TargetState::new());
            assert_eq!(t.addrs().await, HashSet::new());
            self.callbacks_done.unbounded_send(true).unwrap();
        }
    }

    struct TestHookSecond {
        callbacks_done: mpsc::UnboundedSender<bool>,
    }

    #[async_trait]
    impl DiscoveryHook for TestHookSecond {
        async fn on_new_target(&self, _target: &Arc<Target>, _tc: &Arc<TargetCollection>) {
            self.callbacks_done.unbounded_send(true).unwrap();
        }
    }

    #[test]
    fn test_receive_target() {
        hoist::run(async move {
            let (tx_from_callback, mut rx_from_callback) = mpsc::unbounded::<bool>();
            let (tx, rx) = mpsc::unbounded::<Target>();
            let mut daemon = Daemon::new_with_rx(rx);
            daemon.register_hook(TestHookFirst { callbacks_done: tx_from_callback.clone() }).await;
            daemon.register_hook(TestHookSecond { callbacks_done: tx_from_callback }).await;
            tx.unbounded_send(Target::new("nothin")).unwrap();
            assert!(rx_from_callback.next().await.unwrap());
            assert!(rx_from_callback.next().await.unwrap());
        });
    }
}
