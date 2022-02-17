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
    fidl_fuchsia_developer_bridge as bridge,
    fuchsia_async::futures::TryStreamExt,
    protocols::prelude::*,
    std::net::SocketAddr,
    std::rc::Rc,
    std::time::Instant,
    tasks::TaskManager,
};

mod reboot;
mod target_handle;

#[ffx_protocol(bridge::MdnsMarker, bridge::FastbootTargetStreamMarker)]
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

impl TargetCollectionProtocol {
    async fn add_manual_target(&self, tc: &TargetCollection, addr: SocketAddr) {
        let tae = TargetAddrEntry::new(addr.into(), Utc::now(), TargetAddrType::Manual);
        let _ = self.manual_targets.add(format!("{}", addr)).await.map_err(|e| {
            log::error!("Unable to persist manual target: {:?}", e);
        });
        let target = Target::new_with_addr_entries(Option::<String>::None, Some(tae).into_iter());
        if addr.port() != 0 {
            target.set_ssh_port(Some(addr.port()));
        }
        target.update_connection_state(|_| TargetConnectionState::Manual);
        let target = tc.merge_insert(target);
        target.run_host_pipe();
    }

    async fn load_manual_targets(&self, tc: &TargetCollection) {
        for str in self.manual_targets.get_or_default().await {
            let sa = match str.parse::<std::net::SocketAddr>() {
                Ok(sa) => sa,
                Err(e) => {
                    log::error!("Parse of manual target config failed: {}", e);
                    continue;
                }
            };
            self.add_manual_target(tc, sa).await;
        }
    }
}

#[async_trait(?Send)]
impl FidlProtocol for TargetCollectionProtocol {
    type Protocol = bridge::TargetCollectionMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, cx: &Context, req: bridge::TargetCollectionRequest) -> Result<()> {
        let target_collection = cx.get_target_collection().await?;
        match req {
            bridge::TargetCollectionRequest::ListTargets { reader, query, .. } => {
                let reader = reader.into_proxy()?;
                let targets = match query.string_matcher.as_deref() {
                    None | Some("") => target_collection
                        .targets()
                        .into_iter()
                        .filter_map(
                            |t| if t.is_connected() { Some(t.as_ref().into()) } else { None },
                        )
                        .collect::<Vec<bridge::TargetInfo>>(),
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
            bridge::TargetCollectionRequest::OpenTarget { query, responder, target_handle } => {
                let target = match target_collection.wait_for_match(query.string_matcher).await {
                    Ok(t) => t,
                    Err(e) => {
                        return responder
                            .send(&mut match e {
                                bridge::DaemonError::TargetAmbiguous => {
                                    Err(bridge::OpenTargetError::QueryAmbiguous)
                                }
                                bridge::DaemonError::TargetNotFound => {
                                    Err(bridge::OpenTargetError::TargetNotFound)
                                }
                                e => {
                                    // This, so far, will only happen if encountering
                                    // TargetCacheError, which is highly unlikely.
                                    log::warn!("encountered unhandled error: {:?}", e);
                                    Err(bridge::OpenTargetError::TargetNotFound)
                                }
                            })
                            .map_err(Into::into);
                    }
                };
                self.tasks.spawn(TargetHandle::new(target, cx.clone(), target_handle)?);
                responder.send(&mut Ok(())).map_err(Into::into)
            }
            bridge::TargetCollectionRequest::AddTarget { ip, responder } => {
                // TODO(awdavies): The related tests for this are still implemented in
                // daemon/src/daaemon.rs and should be migrated here once:
                // a.) The previous AddTargets function is deprecated, and
                // b.) All references to manual_targets are moved here instead
                //     of in the daemon.
                let addr = target_addr_info_to_socketaddr(ip);
                self.add_manual_target(&target_collection, addr).await;
                responder.send().map_err(Into::into)
            }
            bridge::TargetCollectionRequest::RemoveTarget { target_id, responder } => {
                if let Some(target) = target_collection.get(target_id.clone()) {
                    let ssh_port = target.ssh_port();
                    for addr in target.manual_addrs() {
                        let mut sockaddr = SocketAddr::from(addr);
                        ssh_port.map(|p| sockaddr.set_port(p));
                        let _ = self.manual_targets.remove(format!("{}", sockaddr)).await.map_err(
                            |e| {
                                log::error!("Unable to persist target removal: {}", e);
                            },
                        );
                    }
                }
                let result = target_collection.remove_target(target_id.clone());
                responder.send(result).map_err(Into::into)
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
                    bridge::MdnsEventType::TargetFound(t)
                    | bridge::MdnsEventType::TargetRediscovered(t) => {
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

fn handle_fastboot_target(tc: &Rc<TargetCollection>, target: bridge::FastbootTarget) {
    if let Some(ref serial) = target.serial {
        log::trace!("Found new target via fastboot: {}", serial);
    } else {
        log::trace!("Fastboot target has no serial number. Not able to merge.");
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

fn handle_mdns_event(tc: &Rc<TargetCollection>, t: bridge::TargetInfo) {
    let t = TargetInfo {
        nodename: t.nodename,
        addresses: t
            .addresses
            .map(|a| a.into_iter().map(Into::into).collect())
            .unwrap_or(Vec::new()),
        fastboot_interface: if t.target_state == Some(bridge::TargetState::Fastboot) {
            t.fastboot_interface.map(|v| match v {
                bridge::FastbootInterface::Usb => FastbootInterface::Usb,
                bridge::FastbootInterface::Udp => FastbootInterface::Udp,
                bridge::FastbootInterface::Tcp => FastbootInterface::Tcp,
            })
        } else {
            None
        },
        ..Default::default()
    };
    if t.fastboot_interface.is_some() {
        log::trace!(
            "Found new fastboot target via mdns: {}",
            t.nodename.clone().unwrap_or("<unknown>".to_string())
        );
        let target = tc.merge_insert(match Target::from_fastboot_target_info(t) {
            Ok(ret) => ret,
            Err(e) => {
                log::trace!("Error while making target: {:?}", e);
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
        log::trace!(
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
    use std::cell::RefCell;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_handle_mdns_non_fastboot() {
        let t = Target::new_named("this-is-a-thing");
        let tc = Rc::new(TargetCollection::new());
        tc.merge_insert(t.clone());
        let before_update = Instant::now();

        handle_mdns_event(
            &tc,
            bridge::TargetInfo {
                nodename: Some(t.nodename().unwrap()),
                ..bridge::TargetInfo::EMPTY
            },
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
            bridge::TargetInfo {
                nodename: Some(t.nodename().unwrap()),
                target_state: Some(bridge::TargetState::Fastboot),
                fastboot_interface: Some(bridge::FastbootInterface::Tcp),
                ..bridge::TargetInfo::EMPTY
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
        next_event: Receiver<bridge::MdnsEventType>,
    }

    impl Default for TestMdns {
        fn default() -> Self {
            unimplemented!()
        }
    }

    #[async_trait(?Send)]
    impl FidlProtocol for TestMdns {
        type Protocol = bridge::MdnsMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, req: bridge::MdnsRequest) -> Result<()> {
            match req {
                bridge::MdnsRequest::GetNextEvent { responder } => {
                    self.call_started.send(()).await.unwrap();
                    responder.send(self.next_event.recv().await.ok().as_mut()).map_err(Into::into)
                }
                _ => panic!("unsupported"),
            }
        }
    }

    async fn list_targets(
        query: Option<&str>,
        tc: &bridge::TargetCollectionProxy,
    ) -> Vec<bridge::TargetInfo> {
        let (reader, server) =
            fidl::endpoints::create_endpoints::<bridge::TargetCollectionReaderMarker>().unwrap();
        tc.list_targets(
            bridge::TargetQuery {
                string_matcher: query.map(|s| s.to_owned()),
                ..bridge::TargetQuery::EMPTY
            },
            reader,
        )
        .unwrap();
        let mut res = Vec::new();
        let mut stream = server.into_stream().unwrap();
        while let Ok(Some(bridge::TargetCollectionReaderRequest::Next { entry, responder })) =
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
        type Protocol = bridge::FastbootTargetStreamMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(
            &self,
            _cx: &Context,
            _req: bridge::FastbootTargetStreamRequest,
        ) -> Result<()> {
            fuchsia_async::futures::future::pending::<()>().await;
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
        let (target_sender, r) = async_channel::unbounded::<bridge::MdnsEventType>();
        let mdns_protocol =
            Rc::new(RefCell::new(TestMdns { call_started: call_started_sender, next_event: r }));
        let fake_daemon = FakeDaemonBuilder::new()
            .inject_fidl_protocol(mdns_protocol)
            .register_fidl_protocol::<FakeFastboot>()
            .register_fidl_protocol::<TargetCollectionProtocol>()
            .build();
        let tc = fake_daemon.open_proxy::<bridge::TargetCollectionMarker>().await;
        let res = list_targets(None, &tc).await;
        assert_eq!(res.len(), 0);
        call_started_receiver.recv().await.unwrap();
        target_sender
            .send(bridge::MdnsEventType::TargetFound(bridge::TargetInfo {
                nodename: Some(NAME.to_owned()),
                ..bridge::TargetInfo::EMPTY
            }))
            .await
            .unwrap();
        target_sender
            .send(bridge::MdnsEventType::TargetFound(bridge::TargetInfo {
                nodename: Some(NAME2.to_owned()),
                ..bridge::TargetInfo::EMPTY
            }))
            .await
            .unwrap();
        target_sender
            .send(bridge::MdnsEventType::TargetFound(bridge::TargetInfo {
                nodename: Some(NAME3.to_owned()),
                ..bridge::TargetInfo::EMPTY
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
        handle_fastboot_target(&tc, bridge::FastbootTarget::EMPTY);
        assert_eq!(tc.targets().len(), 0, "target collection should remain empty");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_handle_fastboot_target() {
        let tc = Rc::new(TargetCollection::new());
        handle_fastboot_target(
            &tc,
            bridge::FastbootTarget {
                serial: Some("12345".to_string()),
                ..bridge::FastbootTarget::EMPTY
            },
        );
        assert_eq!(tc.targets()[0].serial().as_deref(), Some("12345"));
    }

    #[derive(Default)]
    struct FakeMdns {}

    #[async_trait(?Send)]
    impl FidlProtocol for FakeMdns {
        type Protocol = bridge::MdnsMarker;
        type StreamHandler = FidlStreamHandler<Self>;

        async fn handle(&self, _cx: &Context, _req: bridge::MdnsRequest) -> Result<()> {
            fuchsia_async::futures::future::pending::<()>().await;
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
        tc_impl.borrow().manual_targets.add("127.0.0.1:8022".to_string()).await.unwrap();
        let target_collection =
            Context::new(fake_daemon.clone()).get_target_collection().await.unwrap();
        tc_impl.borrow().load_manual_targets(&target_collection).await;
        let proxy = fake_daemon.open_proxy::<bridge::TargetCollectionMarker>().await;
        let res = list_targets(None, &proxy).await;
        assert_eq!(1, res.len());
        assert!(proxy.remove_target("127.0.0.1:8022").await.unwrap());
        assert_eq!(0, list_targets(None, &proxy).await.len());
        assert_eq!(tc_impl.borrow().manual_targets.get_or_default().await, Vec::<String>::new());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_add_target() {
        let fake_daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeMdns>()
            .register_fidl_protocol::<FakeFastboot>()
            .register_fidl_protocol::<TargetCollectionProtocol>()
            .build();
        let target_addr = TargetAddr::new("[::1]:0").unwrap();
        let proxy = fake_daemon.open_proxy::<bridge::TargetCollectionMarker>().await;
        proxy.add_target(&mut target_addr.into()).await.unwrap();
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
        let proxy = fake_daemon.open_proxy::<bridge::TargetCollectionMarker>().await;
        proxy.add_target(&mut target_addr.into()).await.unwrap();
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
        let proxy = fake_daemon.open_proxy::<bridge::TargetCollectionMarker>().await;
        proxy
            .add_target(&mut bridge::TargetAddrInfo::IpPort(bridge::TargetIpPort {
                ip: IpAddress::Ipv6(Ipv6Address {
                    addr: [0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1],
                }),
                port: 8022,
                scope_id: 1,
            }))
            .await
            .unwrap();
        let target_collection = Context::new(fake_daemon).get_target_collection().await.unwrap();
        assert_eq!(1, target_collection.targets().len());
        assert_eq!(tc_impl.borrow().manual_targets.get().await.unwrap(), vec!["[fe80::1%1]:8022"]);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_persisted_manual_target_load() {
        let tc_impl = Rc::new(RefCell::new(TargetCollectionProtocol::default()));
        let fake_daemon = FakeDaemonBuilder::new()
            .register_fidl_protocol::<FakeMdns>()
            .register_fidl_protocol::<FakeFastboot>()
            .inject_fidl_protocol(tc_impl.clone())
            .build();
        tc_impl.borrow().manual_targets.add("127.0.0.1:8022".to_string()).await.unwrap();

        let target_collection = Context::new(fake_daemon).get_target_collection().await.unwrap();
        // This happens in FidlProtocol::start(), but we want to avoid binding the
        // network sockets in unit tests, thus not calling start.
        tc_impl.borrow().load_manual_targets(&target_collection).await;

        let target = target_collection.get("127.0.0.1:8022".to_string()).unwrap();
        assert_eq!(target.ssh_address(), Some("127.0.0.1:8022".parse::<SocketAddr>().unwrap()));
    }
}
