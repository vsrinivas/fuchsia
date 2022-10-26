// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::target_handle::TargetHandle,
    anyhow::{anyhow, Result},
    async_trait::async_trait,
    chrono::Utc,
    ffx_daemon_events::{FastbootInterface, TargetConnectionState, TargetInfo},
    ffx_daemon_target::manual_targets,
    ffx_daemon_target::target::{
        target_addr_info_to_socketaddr, Target, TargetAddrEntry, TargetAddrType,
    },
    ffx_daemon_target::target_collection::TargetCollection,
    ffx_stream_util::TryStreamUtilExt,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_ffx as ffx,
    fidl_fuchsia_developer_remotecontrol::RemoteControlMarker,
    futures::TryStreamExt,
    protocols::prelude::*,
    std::net::IpAddr,
    std::net::{SocketAddr, SocketAddrV4, SocketAddrV6},
    std::rc::Rc,
    std::time::{Duration, Instant, SystemTime, UNIX_EPOCH},
    tasks::TaskManager,
};

mod reboot;
mod target_handle;

#[ffx_protocol(ffx::MdnsMarker, ffx::FastbootTargetStreamMarker)]
pub struct TargetCollectionProtocol {
    tasks: TaskManager,

    // An online cache of configured target entries (the non-discoverable targets represented in the
    // ffx configuration).
    // The cache can be updated by calls to AddTarget and RemoveTarget.
    // With manual_targets, we have access to the targets.manual field of the configuration (a
    // vector of strings). Each target is defined by an IP address and a port.
    manual_targets: Rc<dyn manual_targets::ManualTargets>,
}

impl Default for TargetCollectionProtocol {
    fn default() -> Self {
        #[cfg(not(test))]
        let manual_targets = manual_targets::Config::default();
        #[cfg(test)]
        let manual_targets = manual_targets::Mock::default();

        Self { tasks: Default::default(), manual_targets: Rc::new(manual_targets) }
    }
}

async fn add_manual_target(
    manual_targets: Rc<dyn manual_targets::ManualTargets>,
    tc: &TargetCollection,
    addr: SocketAddr,
    lifetime: Option<Duration>,
) -> Rc<Target> {
    // Expiry is the SystemTime (represented as seconds after the UNIX_EPOCH) at which a manual
    // target is allowed to expire and enter the Disconnected state. If no lifetime is given,
    // the target is allowed to persist indefinitely. This is persisted in FFX config.
    // Timeout is the number of seconds until the expiry is met; it is used in-memory only.
    let (timeout, expiry, last_seen) = if lifetime.is_none() {
        (None, None, None)
    } else {
        let timeout = SystemTime::now() + lifetime.unwrap();
        let expiry = timeout.duration_since(UNIX_EPOCH).unwrap_or(Duration::ZERO).as_secs();
        (Some(timeout), Some(expiry), Some(Instant::now()))
    };

    let tae = TargetAddrEntry::new(addr.into(), Utc::now(), TargetAddrType::Manual(timeout));
    let _ = manual_targets.add(format!("{}", addr), expiry).await.map_err(|e| {
        tracing::error!("Unable to persist manual target: {:?}", e);
    });
    let target = Target::new_with_addr_entries(Option::<String>::None, Some(tae).into_iter());
    if addr.port() != 0 {
        target.set_ssh_port(Some(addr.port()));
    }

    target.update_connection_state(|_| TargetConnectionState::Manual(last_seen));
    let target = tc.merge_insert(target);
    target.run_host_pipe();
    target
}

async fn remove_manual_target(
    manual_targets: Rc<dyn manual_targets::ManualTargets>,
    tc: &TargetCollection,
    target_id: String,
) -> bool {
    if let Some(target) = tc.get(target_id.clone()) {
        let ssh_port = target.ssh_port();
        for addr in target.manual_addrs() {
            let mut sockaddr = SocketAddr::from(addr);
            ssh_port.map(|p| sockaddr.set_port(p));
            let _ = manual_targets.remove(format!("{}", sockaddr)).await.map_err(|e| {
                tracing::error!("Unable to persist target removal: {}", e);
            });
        }
    }
    tc.remove_target(target_id)
}

impl TargetCollectionProtocol {
    async fn load_manual_targets(&self, tc: &TargetCollection) {
        // The FFX config value for a manual target contains a target ID (typically the IP:PORT
        // combo) and a timeout (which is None, if the target is indefinitely persistent).
        for (unparsed_addr, val) in self.manual_targets.get_or_default().await {
            let (addr, scope, port) = match netext::parse_address_parts(unparsed_addr.as_str()) {
                Ok(res) => res,
                Err(e) => {
                    tracing::error!("Skipping load of manual target address due to parsing error '{unparsed_addr}': {e}");
                    continue;
                }
            };
            let scope_id = if let Some(scope) = scope {
                match netext::get_verified_scope_id(scope) {
                    Ok(res) => res,
                    Err(e) => {
                        tracing::error!("Scope load of manual address '{unparsed_addr}', which had a scope ID of '{scope}', which was not verifiable: {e}");
                        continue;
                    }
                }
            } else {
                0
            };
            let port = port.unwrap_or(0);
            let sa = match addr {
                IpAddr::V4(i) => std::net::SocketAddr::V4(SocketAddrV4::new(i, port)),
                IpAddr::V6(i) => std::net::SocketAddr::V6(SocketAddrV6::new(i, port, 0, scope_id)),
            };
            let secs = val.as_u64();
            if secs.is_some() {
                // If the manual target has a lifetime specified, we need to include it in the
                // reloaded entry.
                let lifetime_from_epoch = Duration::from_secs(secs.unwrap());
                let now = SystemTime::now();
                if let Ok(elapsed) = now.duration_since(UNIX_EPOCH) {
                    let remaining = if lifetime_from_epoch < elapsed {
                        Duration::ZERO
                    } else {
                        lifetime_from_epoch - elapsed
                    };
                    add_manual_target(self.manual_targets.clone(), tc, sa, Some(remaining)).await;
                }
            } else {
                // Manual targets without a lifetime are always reloaded.
                add_manual_target(self.manual_targets.clone(), tc, sa, None).await;
            }
        }
    }
}

#[async_trait(?Send)]
impl FidlProtocol for TargetCollectionProtocol {
    type Protocol = ffx::TargetCollectionMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    #[tracing::instrument(level = "info", skip(self, cx))]
    async fn handle(&self, cx: &Context, req: ffx::TargetCollectionRequest) -> Result<()> {
        let target_collection = cx.get_target_collection().await?;
        match req {
            ffx::TargetCollectionRequest::ListTargets { reader, query, .. } => {
                let reader = reader.into_proxy()?;
                let targets = match query.string_matcher.as_deref() {
                    None | Some("") => target_collection
                        .targets()
                        .into_iter()
                        .filter_map(
                            |t| if t.is_connected() { Some(t.as_ref().into()) } else { None },
                        )
                        .collect::<Vec<ffx::TargetInfo>>(),
                    q => match target_collection.get_connected(q) {
                        Some(t) => vec![t.as_ref().into()],
                        None => vec![],
                    },
                };
                // This was chosen arbitrarily. It's possible to determine a
                // better chunk size using some FIDL constant math.
                const TARGET_CHUNK_SIZE: usize = 20;
                let mut iter = targets.into_iter();
                loop {
                    let next_chunk = iter.by_ref().take(TARGET_CHUNK_SIZE);
                    let next_chunk_len = next_chunk.len();
                    reader.next(&mut next_chunk.collect::<Vec<_>>().into_iter()).await?;
                    if next_chunk_len == 0 {
                        break;
                    }
                }
                Ok(())
            }
            ffx::TargetCollectionRequest::OpenTarget { query, responder, target_handle } => {
                tracing::trace!("handling request to open target");
                let target = match target_collection.wait_for_match(query.string_matcher).await {
                    Ok(t) => {
                        tracing::trace!("Found target: {t:?}");
                        t
                    }
                    Err(e) => {
                        return responder
                            .send(&mut match e {
                                ffx::DaemonError::TargetAmbiguous => {
                                    tracing::warn!("Ambiguous Query");
                                    Err(ffx::OpenTargetError::QueryAmbiguous)
                                }
                                ffx::DaemonError::TargetNotFound => {
                                    tracing::warn!("Target Not Found.");
                                    Err(ffx::OpenTargetError::TargetNotFound)
                                }
                                e => {
                                    // This, so far, will only happen if encountering
                                    // TargetCacheError, which is highly unlikely.
                                    tracing::warn!("encountered unhandled error: {:?}", e);
                                    Err(ffx::OpenTargetError::TargetNotFound)
                                }
                            })
                            .map_err(Into::into);
                    }
                };
                self.tasks.spawn(TargetHandle::new(target, cx.clone(), target_handle)?);
                responder.send(&mut Ok(())).map_err(Into::into)
            }
            ffx::TargetCollectionRequest::AddTarget {
                ip, config, add_target_responder, ..
            } => {
                let add_target_responder = add_target_responder.into_proxy()?;
                let addr = target_addr_info_to_socketaddr(ip);
                match config.verify_connection {
                    Some(true) => {}
                    _ => {
                        let _ = add_manual_target(
                            self.manual_targets.clone(),
                            &target_collection,
                            addr,
                            None,
                        )
                        .await;
                        return add_target_responder.success().map_err(Into::into);
                    }
                };
                // The drop guard is here for the impatient user: if the user closes their channel
                // prematurely (before this operation either succeeds or fails), then they will
                // risk adding a manual target that can never be connected to, and then have to
                // manually remove the target themselves.
                struct DropGuard(
                    Option<(
                        Rc<dyn manual_targets::ManualTargets>,
                        Rc<TargetCollection>,
                        SocketAddr,
                    )>,
                );
                impl Drop for DropGuard {
                    fn drop(&mut self) {
                        match self.0.take() {
                            Some((mt, tc, addr)) => fuchsia_async::Task::local(async move {
                                remove_manual_target(mt, &tc, addr.to_string()).await
                            })
                            .detach(),
                            None => {}
                        }
                    }
                }
                let mut drop_guard = DropGuard(Some((
                    self.manual_targets.clone(),
                    target_collection.clone(),
                    addr.clone(),
                )));
                let target =
                    add_manual_target(self.manual_targets.clone(), &target_collection, addr, None)
                        .await;
                let rcs = target_handle::wait_for_rcs(&target).await?;
                match rcs {
                    Ok(mut rcs) => {
                        let (rcs_proxy, server) =
                            fidl::endpoints::create_proxy::<RemoteControlMarker>()?;
                        rcs.copy_to_channel(server.into_channel())?;
                        match rcs::knock_rcs(&rcs_proxy).await {
                            Ok(_) => {
                                let _ = drop_guard.0.take();
                            }
                            Err(e) => {
                                return add_target_responder
                                    .error(ffx::AddTargetError {
                                        connection_error: Some(e),
                                        connection_error_logs: Some(
                                            target.host_pipe_log_buffer().lines(),
                                        ),
                                        ..ffx::AddTargetError::EMPTY
                                    })
                                    .map_err(Into::into)
                            }
                        }
                    }
                    Err(e) => {
                        let logs = target.host_pipe_log_buffer().lines();
                        let _ = remove_manual_target(
                            self.manual_targets.clone(),
                            &target_collection,
                            addr.to_string(),
                        )
                        .await;
                        let _ = drop_guard.0.take();
                        return add_target_responder
                            .error(ffx::AddTargetError {
                                connection_error: Some(e),
                                connection_error_logs: Some(logs),
                                ..ffx::AddTargetError::EMPTY
                            })
                            .map_err(Into::into);
                    }
                }
                add_target_responder.success().map_err(Into::into)
            }
            ffx::TargetCollectionRequest::AddEphemeralTarget {
                ip,
                connect_timeout_seconds,
                responder,
            } => {
                let addr = target_addr_info_to_socketaddr(ip);
                add_manual_target(
                    self.manual_targets.clone(),
                    &target_collection,
                    addr,
                    Some(Duration::from_secs(connect_timeout_seconds)),
                )
                .await;
                responder.send().map_err(Into::into)
            }
            ffx::TargetCollectionRequest::RemoveTarget { target_id, responder } => {
                let result = remove_manual_target(
                    self.manual_targets.clone(),
                    &target_collection,
                    target_id,
                )
                .await;
                responder.send(result).map_err(Into::into)
            }
            ffx::TargetCollectionRequest::AddInlineFastbootTarget { serial_number, responder } => {
                let t = TargetInfo {
                    serial: Some(serial_number),
                    fastboot_interface: Some(FastbootInterface::Usb),
                    ..Default::default()
                };
                let target =
                    target_collection.merge_insert(match Target::from_fastboot_target_info(t) {
                        Ok(ret) => ret,
                        Err(e) => {
                            tracing::warn!("encountered unhandled error: {:?}", e);
                            return responder.send().map_err(Into::into);
                        }
                    });
                target.update_connection_state(|s| match s {
                    TargetConnectionState::Disconnected | TargetConnectionState::Fastboot(_) => {
                        TargetConnectionState::Fastboot(Instant::now())
                    }
                    _ => s,
                });
                tracing::info!("added inline target: {:?}", target);
                responder.send().map_err(Into::into)
            }
        }
    }

    async fn serve<'a>(
        &'a self,
        cx: &'a Context,
        stream: <Self::Protocol as ProtocolMarker>::RequestStream,
    ) -> Result<()> {
        // Necessary to avoid hanging forever when a client drops a connection
        // during a call to OpenTarget.
        stream
            .map_err(|err| anyhow!("{}", err))
            .try_for_each_concurrent_while_connected(None, |req| self.handle(cx, req))
            .await
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        drop(self.tasks.drain());
        Ok(())
    }

    async fn start(&mut self, cx: &Context) -> Result<()> {
        let target_collection = cx.get_target_collection().await?;
        self.load_manual_targets(&target_collection).await;
        let mdns = self.open_mdns_proxy(cx).await?;
        let fastboot = self.open_fastboot_target_stream_proxy(cx).await?;
        let tc = cx.get_target_collection().await?;
        let tc_clone = tc.clone();
        self.tasks.spawn(async move {
            while let Ok(Some(e)) = mdns.get_next_event().await {
                match *e {
                    ffx::MdnsEventType::TargetFound(t)
                    | ffx::MdnsEventType::TargetRediscovered(t) => {
                        handle_mdns_event(&tc_clone, t);
                    }
                    _ => {}
                }
            }
        });
        self.tasks.spawn(async move {
            while let Ok(target) = fastboot.get_next().await {
                handle_fastboot_target(&tc, target);
            }
        });
        Ok(())
    }
}

fn handle_fastboot_target(tc: &Rc<TargetCollection>, target: ffx::FastbootTarget) {
    if let Some(ref serial) = target.serial {
        tracing::trace!("Found new target via fastboot: {}", serial);
    } else {
        tracing::trace!("Fastboot target has no serial number. Not able to merge.");
        return;
    }
    let t = TargetInfo { serial: target.serial, ..Default::default() };
    let target = tc.merge_insert(Target::from_target_info(t.into()));
    target.update_connection_state(|s| match s {
        TargetConnectionState::Disconnected | TargetConnectionState::Fastboot(_) => {
            TargetConnectionState::Fastboot(Instant::now())
        }
        _ => s,
    });
}

fn handle_mdns_event(tc: &Rc<TargetCollection>, t: ffx::TargetInfo) {
    let t = TargetInfo {
        nodename: t.nodename,
        addresses: t
            .addresses
            .map(|a| a.into_iter().map(Into::into).collect())
            .unwrap_or(Vec::new()),
        fastboot_interface: if t.target_state == Some(ffx::TargetState::Fastboot) {
            t.fastboot_interface.map(|v| match v {
                ffx::FastbootInterface::Usb => FastbootInterface::Usb,
                ffx::FastbootInterface::Udp => FastbootInterface::Udp,
                ffx::FastbootInterface::Tcp => FastbootInterface::Tcp,
            })
        } else {
            None
        },
        ..Default::default()
    };
    if t.fastboot_interface.is_some() {
        tracing::trace!(
            "Found new fastboot target via mdns: {}. Address: {:?}",
            t.nodename.clone().unwrap_or("<unknown>".to_string()),
            t.addresses
        );
        let target = tc.merge_insert(match Target::from_fastboot_target_info(t) {
            Ok(ret) => ret,
            Err(e) => {
                tracing::trace!("Error while making target: {:?}", e);
                return;
            }
        });
        target.update_connection_state(|s| match s {
            TargetConnectionState::Disconnected | TargetConnectionState::Fastboot(_) => {
                TargetConnectionState::Fastboot(Instant::now())
            }
            _ => s,
        });
    } else {
        tracing::trace!(
            "Found new target via mdns: {}",
            t.nodename.clone().unwrap_or("<unknown>".to_string())
        );
        let new_target = Target::from_target_info(t);
        new_target.update_connection_state(|_| TargetConnectionState::Mdns(Instant::now()));
        let target = tc.merge_insert(new_target);
        target.run_host_pipe();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use addr::TargetAddr;
    use assert_matches::assert_matches;
    use async_channel::{Receiver, Sender};
    use fidl_fuchsia_net::{IpAddress, Ipv6Address};
    use protocols::testing::FakeDaemonBuilder;
    use serde_json::{json, Map, Value};
    use std::cell::RefCell;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_handle_mdns_non_fastboot() {
        let t = Target::new_named("this-is-a-thing");
        let tc = Rc::new(TargetCollection::new());
        tc.merge_insert(t.clone());
        let before_update = Instant::now();

        handle_mdns_event(
            &tc,
            ffx::TargetInfo { nodename: Some(t.nodename().unwrap()), ..ffx::TargetInfo::EMPTY },
        );
        assert!(t.is_host_pipe_running());
        assert_matches!(t.get_connection_state(), TargetConnectionState::Mdns(t) if t > before_update);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_handle_mdns_fastboot() {
        let t = Target::new_named("this-is-a-thing");
        let tc = Rc::new(TargetCollection::new());
        tc.merge_insert(t.clone());
        let before_update = Instant::now();

        handle_mdns_event(
            &tc,
            ffx::TargetInfo {
                nodename: Some(t.nodename().unwrap()),
                target_state: Some(ffx::TargetState::Fastboot),
                fastboot_interface: Some(ffx::FastbootInterface::Tcp),
                ..ffx::TargetInfo::EMPTY
            },
        );
        assert!(!t.is_host_pipe_running());
        assert_matches!(t.get_connection_state(), TargetConnectionState::Fastboot(t) if t > before_update);
    }

    struct TestMdns {
        /// Lets the test know that a call to `GetNextEvent` has started. This
        /// is just a hack to avoid using timers for races. This is dependent
        /// on the executor running in a single thread.
        call_started: Sender<()>,
        next_event: Receiver<ffx::MdnsEventType>,
    }

    impl Default for TestMdns {
        fn default() -> Self {
            unimplemented!()
        }
    }

    #[async_trait(?Send)]
    impl FidlProtocol for TestMdns {
        type Protocol = ffx::MdnsMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, req: ffx::MdnsRequest) -> Result<()> {
            match req {
                ffx::MdnsRequest::GetNextEvent { responder } => {
                    self.call_started.send(()).await.unwrap();
                    responder.send(self.next_event.recv().await.ok().as_mut()).map_err(Into::into)
                }
                _ => panic!("unsupported"),
            }
        }
    }

    async fn list_targets(
        query: Option<&str>,
        tc: &ffx::TargetCollectionProxy,
    ) -> Vec<ffx::TargetInfo> {
        let (reader, server) =
            fidl::endpoints::create_endpoints::<ffx::TargetCollectionReaderMarker>().unwrap();
        tc.list_targets(
            ffx::TargetQuery {
                string_matcher: query.map(|s| s.to_owned()),
                ..ffx::TargetQuery::EMPTY
            },
            reader,
        )
        .unwrap();
        let mut res = Vec::new();
        let mut stream = server.into_stream().unwrap();
        while let Ok(Some(ffx::TargetCollectionReaderRequest::Next { entry, responder })) =
            stream.try_next().await
        {
            responder.send().unwrap();
            if entry.len() > 0 {
                res.extend(entry);
            } else {
                break;
            }
        }
        res
    }

    #[derive(Default)]
    struct FakeFastboot {}

    #[async_trait(?Send)]
    impl FidlProtocol for FakeFastboot {
        type Protocol = ffx::FastbootTargetStreamMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(
            &self,
            _cx: &Context,
            _req: ffx::FastbootTargetStreamRequest,
        ) -> Result<()> {
            futures::future::pending::<()>().await;
            Ok(())
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_protocol_integration() {
        const NAME: &'static str = "foo";
        const NAME2: &'static str = "bar";
        const NAME3: &'static str = "baz";
        const NON_MATCHING_NAME: &'static str = "mlorp";
        const PARTIAL_NAME_MATCH: &'static str = "ba";
        let (call_started_sender, call_started_receiver) = async_channel::unbounded::<()>();
        let (target_sender, r) = async_channel::unbounded::<ffx::MdnsEventType>();
        let mdns_protocol =
            Rc::new(RefCell::new(TestMdns { call_started: call_started_sender, next_event: r }));
        let fake_daemon = FakeDaemonBuilder::new()
            .inject_fidl_protocol(mdns_protocol)
            .register_fidl_protocol::<FakeFastboot>()
            .register_fidl_protocol::<TargetCollectionProtocol>()
            .build();
        let tc = fake_daemon.open_proxy::<ffx::TargetCollectionMarker>().await;
        let res = list_targets(None, &tc).await;
        assert_eq!(res.len(), 0);
        call_started_receiver.recv().await.unwrap();
        target_sender
            .send(ffx::MdnsEventType::TargetFound(ffx::TargetInfo {
                nodename: Some(NAME.to_owned()),
                ..ffx::TargetInfo::EMPTY
            }))
            .await
            .unwrap();
        target_sender
            .send(ffx::MdnsEventType::TargetFound(ffx::TargetInfo {
                nodename: Some(NAME2.to_owned()),
                ..ffx::TargetInfo::EMPTY
            }))
            .await
            .unwrap();
        target_sender
            .send(ffx::MdnsEventType::TargetFound(ffx::TargetInfo {
                nodename: Some(NAME3.to_owned()),
                ..ffx::TargetInfo::EMPTY
            }))
            .await
            .unwrap();
        call_started_receiver.recv().await.unwrap();
        let res = list_targets(None, &tc).await;
        assert_eq!(res.len(), 3, "received: {:?}", res);
        assert!(res.iter().any(|t| t.nodename.as_ref().unwrap() == NAME));
        assert!(res.iter().any(|t| t.nodename.as_ref().unwrap() == NAME2));
        assert!(res.iter().any(|t| t.nodename.as_ref().unwrap() == NAME3));

        let res = list_targets(Some(NON_MATCHING_NAME), &tc).await;
        assert_eq!(res.len(), 0, "received: {:?}", res);

        let res = list_targets(Some(NAME), &tc).await;
        assert_eq!(res.len(), 1, "received: {:?}", res);
        assert_eq!(res[0].nodename.as_ref().unwrap(), NAME);

        let res = list_targets(Some(NAME2), &tc).await;
        assert_eq!(res.len(), 1, "received: {:?}", res);
        assert_eq!(res[0].nodename.as_ref().unwrap(), NAME2);

        let res = list_targets(Some(NAME3), &tc).await;
        assert_eq!(res.len(), 1, "received: {:?}", res);
        assert_eq!(res[0].nodename.as_ref().unwrap(), NAME3);

        let res = list_targets(Some(PARTIAL_NAME_MATCH), &tc).await;
        assert_eq!(res.len(), 1, "received: {:?}", res);
        assert!(res.iter().any(|t| {
            let name = t.nodename.as_ref().unwrap();
            // Check either partial match just in case the backing impl
            // changes ordering. Possible todo here would be to return multiple
            // targets when there is a partial match.
            name == NAME3 || name == NAME2
        }));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_handle_fastboot_target_no_serial() {
        let tc = Rc::new(TargetCollection::new());
        handle_fastboot_target(&tc, ffx::FastbootTarget::EMPTY);
        assert_eq!(tc.targets().len(), 0, "target collection should remain empty");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_handle_fastboot_target() {
        let tc = Rc::new(TargetCollection::new());
        handle_fastboot_target(
            &tc,
            ffx::FastbootTarget { serial: Some("12345".to_string()), ..ffx::FastbootTarget::EMPTY },
        );
        assert_eq!(tc.targets()[0].serial().as_deref(), Some("12345"));
    }

    fn make_target_add_fut(
        server: fidl::endpoints::ServerEnd<ffx::AddTargetResponder_Marker>,
    ) -> impl std::future::Future<Output = Result<(), ffx::AddTargetError>> {
        async {
            let mut stream = server.into_stream().unwrap();
            if let Ok(Some(req)) = stream.try_next().await {
                match req {
                    ffx::AddTargetResponder_Request::Success { .. } => {
                        return Ok(());
                    }
                    ffx::AddTargetResponder_Request::Error { err, .. } => {
                        return Err(err);
                    }
                }
            } else {
                panic!("connection lost to stream. This should not be reachable");
            }
        }
    }

    #[derive(Default)]
    struct FakeMdns {}

    #[async_trait(?Send)]
    impl FidlProtocol for FakeMdns {
        type Protocol = ffx::MdnsMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, _req: ffx::MdnsRequest) -> Result<()> {
            futures::future::pending::<()>().await;
            Ok(())
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_persisted_manual_target_remove() {
        let tc_impl = Rc::new(RefCell::new(TargetCollectionProtocol::default()));
        let fake_daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeMdns>()
            .register_fidl_protocol::<FakeFastboot>()
            .inject_fidl_protocol(tc_impl.clone())
            .build();
        // Set one timeout 1 hour in the future; the other will have no timeout.
        let expiry = (SystemTime::now() + Duration::from_secs(3600))
            .duration_since(UNIX_EPOCH)
            .expect("Problem getting a duration relative to epoch.")
            .as_secs();
        tc_impl.borrow().manual_targets.add("127.0.0.1:8022".to_string(), None).await.unwrap();
        tc_impl
            .borrow()
            .manual_targets
            .add("127.0.0.1:8023".to_string(), Some(expiry))
            .await
            .unwrap();
        let target_collection =
            Context::new(fake_daemon.clone()).get_target_collection().await.unwrap();
        tc_impl.borrow().load_manual_targets(&target_collection).await;
        let proxy = fake_daemon.open_proxy::<ffx::TargetCollectionMarker>().await;
        let res = list_targets(None, &proxy).await;
        assert_eq!(2, res.len());
        assert!(proxy.remove_target("127.0.0.1:8022").await.unwrap());
        assert!(proxy.remove_target("127.0.0.1:8023").await.unwrap());
        assert_eq!(0, list_targets(None, &proxy).await.len());
        assert_eq!(
            tc_impl.borrow().manual_targets.get_or_default().await,
            Map::<String, Value>::new()
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_target() {
        let fake_daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeMdns>()
            .register_fidl_protocol::<FakeFastboot>()
            .register_fidl_protocol::<TargetCollectionProtocol>()
            .build();
        let target_addr = TargetAddr::new("[::1]:0").unwrap();
        let proxy = fake_daemon.open_proxy::<ffx::TargetCollectionMarker>().await;
        let (client, server) =
            fidl::endpoints::create_endpoints::<ffx::AddTargetResponder_Marker>().unwrap();
        let target_add_fut = make_target_add_fut(server);
        proxy.add_target(&mut target_addr.into(), ffx::AddTargetConfig::EMPTY, client).unwrap();
        target_add_fut.await.unwrap();
        let target_collection = Context::new(fake_daemon).get_target_collection().await.unwrap();
        let target = target_collection.get(target_addr.to_string()).unwrap();
        assert_eq!(target.addrs().len(), 1);
        assert_eq!(target.addrs().into_iter().next(), Some(target_addr));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_ephemeral_target() {
        let fake_daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeMdns>()
            .register_fidl_protocol::<FakeFastboot>()
            .register_fidl_protocol::<TargetCollectionProtocol>()
            .build();
        let target_addr = TargetAddr::new("[::1]:0").unwrap();
        let proxy = fake_daemon.open_proxy::<ffx::TargetCollectionMarker>().await;
        proxy.add_ephemeral_target(&mut target_addr.into(), 3600).await.unwrap();
        let target_collection = Context::new(fake_daemon).get_target_collection().await.unwrap();
        let target = target_collection.get(target_addr.to_string()).unwrap();
        assert_eq!(target.addrs().len(), 1);
        assert_eq!(target.addrs().into_iter().next(), Some(target_addr));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_target_with_port() {
        let fake_daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeMdns>()
            .register_fidl_protocol::<FakeFastboot>()
            .register_fidl_protocol::<TargetCollectionProtocol>()
            .build();
        let target_addr = TargetAddr::new("[::1]:8022").unwrap();
        let proxy = fake_daemon.open_proxy::<ffx::TargetCollectionMarker>().await;
        let (client, server) =
            fidl::endpoints::create_endpoints::<ffx::AddTargetResponder_Marker>().unwrap();
        let target_add_fut = make_target_add_fut(server);
        proxy.add_target(&mut target_addr.into(), ffx::AddTargetConfig::EMPTY, client).unwrap();
        target_add_fut.await.unwrap();
        let target_collection = Context::new(fake_daemon).get_target_collection().await.unwrap();
        let target = target_collection.get(target_addr.to_string()).unwrap();
        assert_eq!(target.addrs().len(), 1);
        assert_eq!(target.addrs().into_iter().next(), Some(target_addr));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_ephemeral_target_with_port() {
        let fake_daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeMdns>()
            .register_fidl_protocol::<FakeFastboot>()
            .register_fidl_protocol::<TargetCollectionProtocol>()
            .build();
        let target_addr = TargetAddr::new("[::1]:8022").unwrap();
        let proxy = fake_daemon.open_proxy::<ffx::TargetCollectionMarker>().await;
        proxy.add_ephemeral_target(&mut target_addr.into(), 3600).await.unwrap();
        let target_collection = Context::new(fake_daemon).get_target_collection().await.unwrap();
        let target = target_collection.get(target_addr.to_string()).unwrap();
        assert_eq!(target.addrs().len(), 1);
        assert_eq!(target.addrs().into_iter().next(), Some(target_addr));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_persisted_manual_target_add() {
        let tc_impl = Rc::new(RefCell::new(TargetCollectionProtocol::default()));
        let fake_daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeMdns>()
            .register_fidl_protocol::<FakeFastboot>()
            .inject_fidl_protocol(tc_impl.clone())
            .build();
        let (client, server) =
            fidl::endpoints::create_endpoints::<ffx::AddTargetResponder_Marker>().unwrap();
        let target_add_fut = make_target_add_fut(server);
        let proxy = fake_daemon.open_proxy::<ffx::TargetCollectionMarker>().await;
        proxy
            .add_target(
                &mut ffx::TargetAddrInfo::IpPort(ffx::TargetIpPort {
                    ip: IpAddress::Ipv6(Ipv6Address {
                        addr: [0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
                    }),
                    port: 8022,
                    scope_id: 1,
                }),
                ffx::AddTargetConfig::EMPTY,
                client,
            )
            .unwrap();
        target_add_fut.await.unwrap();
        let target_collection = Context::new(fake_daemon).get_target_collection().await.unwrap();
        assert_eq!(1, target_collection.targets().len());
        let mut map = Map::<String, Value>::new();
        map.insert("[fe80::1%1]:8022".to_string(), Value::Null);
        assert_eq!(tc_impl.borrow().manual_targets.get().await.unwrap(), json!(map));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_persisted_ephemeral_target_add() {
        let tc_impl = Rc::new(RefCell::new(TargetCollectionProtocol::default()));
        let fake_daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeMdns>()
            .register_fidl_protocol::<FakeFastboot>()
            .inject_fidl_protocol(tc_impl.clone())
            .build();
        let proxy = fake_daemon.open_proxy::<ffx::TargetCollectionMarker>().await;
        proxy
            .add_ephemeral_target(
                &mut ffx::TargetAddrInfo::IpPort(ffx::TargetIpPort {
                    ip: IpAddress::Ipv6(Ipv6Address {
                        addr: [0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
                    }),
                    port: 8022,
                    scope_id: 1,
                }),
                3600,
            )
            .await
            .unwrap();
        let target_collection = Context::new(fake_daemon).get_target_collection().await.unwrap();
        assert_eq!(1, target_collection.targets().len());
        assert!(tc_impl.borrow().manual_targets.get().await.unwrap().is_object());
        let value = tc_impl.borrow().manual_targets.get().await.unwrap();
        assert!(value.is_object());
        let map = value.as_object().unwrap();
        assert!(map.contains_key("[fe80::1%1]:8022"));
        let target = map.get(&"[fe80::1%1]:8022".to_string());
        assert!(target.is_some());
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("Couldn't get duration from epoch.")
            .as_secs();
        assert!(target.unwrap().as_u64().unwrap() > now);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_persisted_manual_target_load() {
        let tc_impl = Rc::new(RefCell::new(TargetCollectionProtocol::default()));
        let fake_daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeMdns>()
            .register_fidl_protocol::<FakeFastboot>()
            .inject_fidl_protocol(tc_impl.clone())
            .build();
        // We attempt to load three targets:
        // - One with no timeout, should load,
        // - One with an expired timeout, should load, and
        // - One with a future timeout, should load.
        let now = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("Couldn't load duration since epoch.")
            .as_secs();
        let expired = now - 3600;
        let future = now + 3600;
        tc_impl.borrow().manual_targets.add("127.0.0.1:8022".to_string(), None).await.unwrap();
        tc_impl
            .borrow()
            .manual_targets
            .add("127.0.0.1:8023".to_string(), Some(expired))
            .await
            .unwrap();
        tc_impl
            .borrow()
            .manual_targets
            .add("127.0.0.1:8024".to_string(), Some(future))
            .await
            .unwrap();

        let target_collection = Context::new(fake_daemon).get_target_collection().await.unwrap();
        // This happens in FidlProtocol::start(), but we want to avoid binding the
        // network sockets in unit tests, thus not calling start.
        tc_impl.borrow().load_manual_targets(&target_collection).await;

        let target = target_collection.get("127.0.0.1:8022".to_string()).unwrap();
        assert_eq!(target.ssh_address(), Some("127.0.0.1:8022".parse::<SocketAddr>().unwrap()));
        let target = target_collection.get("127.0.0.1:8023".to_string()).unwrap();
        assert_eq!(target.ssh_address(), Some("127.0.0.1:8023".parse::<SocketAddr>().unwrap()));
        let target = target_collection.get("127.0.0.1:8024".to_string()).unwrap();
        assert_eq!(target.ssh_address(), Some("127.0.0.1:8024".parse::<SocketAddr>().unwrap()));
    }
}
