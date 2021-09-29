// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::target_handle::TargetHandle,
    anyhow::{anyhow, Result},
    async_trait::async_trait,
    chrono::Utc,
    ffx_core::TryStreamUtilExt,
    ffx_daemon_events::{FastbootInterface, TargetConnectionState, TargetInfo},
    ffx_daemon_target::target::{
        target_addr_info_to_socketaddr, Target, TargetAddrEntry, TargetAddrType,
    },
    ffx_daemon_target::target_collection::TargetCollection,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_bridge as bridge,
    fuchsia_async::futures::TryStreamExt,
    services::prelude::*,
    std::net::SocketAddr,
    std::rc::Rc,
    std::time::Instant,
    tasks::TaskManager,
};

mod target_handle;

#[ffx_service]
#[derive(Default)]
pub struct TargetCollectionService {
    tasks: TaskManager,
}

#[async_trait(?Send)]
impl FidlService for TargetCollectionService {
    type Service = bridge::TargetCollectionMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, cx: &Context, req: bridge::TargetCollectionRequest) -> Result<()> {
        let target_collection = cx.get_target_collection().await?;
        match req {
            bridge::TargetCollectionRequest::ListTargets { iterator, responder, query } => {
                let mut stream = iterator.into_stream()?;
                let targets = match query.as_ref().map(|s| s.as_str()) {
                    None | Some("") => target_collection
                        .targets()
                        .into_iter()
                        .filter_map(
                            |t| if t.is_connected() { Some(t.as_ref().into()) } else { None },
                        )
                        .collect::<Vec<bridge::Target>>(),
                    q => match target_collection.get_connected(q) {
                        Some(t) => vec![t.as_ref().into()],
                        None => vec![],
                    },
                };
                fuchsia_async::Task::local(async move {
                    // This was chosen arbitrarily. It's possible to determine a
                    // better chunk size using some FIDL constant math.
                    const TARGET_CHUNK_SIZE: usize = 20;
                    let mut iter = targets.into_iter();
                    while let Ok(Some(bridge::TargetCollectionIteratorRequest::GetNext {
                        responder,
                    })) = stream.try_next().await
                    {
                        let _ = responder
                            .send(
                                &mut iter
                                    .by_ref()
                                    .take(TARGET_CHUNK_SIZE)
                                    .collect::<Vec<_>>()
                                    .into_iter(),
                            )
                            .map_err(|e| {
                                log::warn!("responding to target collection iterator: {:?}", e)
                            });
                    }
                })
                .detach();
                responder.send().map_err(Into::into)
            }
            bridge::TargetCollectionRequest::OpenTarget { query, responder, target_handle } => {
                let target = match target_collection.wait_for_match(query).await {
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
                let tae = TargetAddrEntry::new(addr.into(), Utc::now(), TargetAddrType::Manual);
                let manual_targets = cx.get_manual_targets().await?;
                let _ = manual_targets.add(format!("{}", addr)).await.map_err(|e| {
                    log::error!("Unable to persist manual target: {:?}", e);
                });
                let target =
                    Target::new_with_addr_entries(Option::<String>::None, Some(tae).into_iter());
                if addr.port() != 0 {
                    target.set_ssh_port(Some(addr.port()));
                }
                target.update_connection_state(|_| TargetConnectionState::Manual);
                let target = target_collection.merge_insert(target);
                target.run_host_pipe();
                responder.send().map_err(Into::into)
            }
            bridge::TargetCollectionRequest::RemoveTarget { target_id, responder } => {
                let manual_targets = cx.get_manual_targets().await?;
                if let Some(target) = target_collection.get(target_id.clone()) {
                    let ssh_port = target.ssh_port();
                    for addr in target.manual_addrs() {
                        let mut sockaddr = SocketAddr::from(addr);
                        ssh_port.map(|p| sockaddr.set_port(p));
                        let _ = manual_targets.remove(format!("{}", sockaddr)).await.map_err(|e| {
                            log::error!("Unable to persist target removal: {}", e);
                        });
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
        stream: <Self::Service as ProtocolMarker>::RequestStream,
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
        let mdns = cx.open_service_proxy::<bridge::MdnsMarker>().await?;
        let tc = cx.get_target_collection().await?;
        self.tasks.spawn(async move {
            while let Ok(Some(e)) = mdns.get_next_event().await {
                match *e {
                    bridge::MdnsEventType::TargetFound(t)
                    | bridge::MdnsEventType::TargetRediscovered(t) => {
                        handle_mdns_event(&tc, t).await;
                    }
                    _ => {}
                }
            }
        });
        Ok(())
    }
}

async fn handle_mdns_event(tc: &Rc<TargetCollection>, t: bridge::Target) {
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
            "Found new target via fastboot: {}",
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
    use async_channel::{Receiver, Sender};
    use matches::assert_matches;
    use services::testing::FakeDaemonBuilder;
    use std::cell::RefCell;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_handle_mdns_non_fastboot() {
        let t = Target::new_named("this-is-a-thing");
        let tc = Rc::new(TargetCollection::new());
        tc.merge_insert(t.clone());
        let before_update = Instant::now();

        handle_mdns_event(
            &tc,
            bridge::Target { nodename: Some(t.nodename().unwrap()), ..bridge::Target::EMPTY },
        )
        .await;
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
            bridge::Target {
                nodename: Some(t.nodename().unwrap()),
                target_state: Some(bridge::TargetState::Fastboot),
                fastboot_interface: Some(bridge::FastbootInterface::Tcp),
                ..bridge::Target::EMPTY
            },
        )
        .await;
        assert!(!t.is_host_pipe_running());
        assert_matches!(t.get_connection_state(), TargetConnectionState::Fastboot(t) if t > before_update);
    }

    struct FakeMdns {
        /// Lets the test know that a call to `GetNextEvent` has started. This
        /// is just a hack to avoid using timers for races. This is dependent
        /// on the executor running in a single thread.
        call_started: Sender<()>,
        next_event: Receiver<bridge::MdnsEventType>,
    }

    impl Default for FakeMdns {
        fn default() -> Self {
            unimplemented!()
        }
    }

    #[async_trait(?Send)]
    impl FidlService for FakeMdns {
        type Service = bridge::MdnsMarker;
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

    async fn list_targets(tc: &bridge::TargetCollectionProxy) -> Vec<bridge::Target> {
        let (iterator_proxy, server) =
            fidl::endpoints::create_proxy::<bridge::TargetCollectionIteratorMarker>().unwrap();
        tc.list_targets(None, server).await.unwrap();
        let mut res = Vec::new();
        loop {
            let r = iterator_proxy.get_next().await.unwrap();
            if r.len() > 0 {
                res.extend(r);
            } else {
                break;
            }
        }
        res
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_service_integration() {
        const NAME: &'static str = "foooooo";
        let (call_started_sender, call_started_receiver) = async_channel::unbounded::<()>();
        let (target_sender, r) = async_channel::unbounded::<bridge::MdnsEventType>();
        let mdns_service =
            Rc::new(RefCell::new(FakeMdns { call_started: call_started_sender, next_event: r }));
        let fake_daemon = FakeDaemonBuilder::new()
            .inject_fidl_service(mdns_service)
            .register_fidl_service::<TargetCollectionService>()
            .build();
        let tc = fake_daemon.open_proxy::<bridge::TargetCollectionMarker>().await;
        let res = list_targets(&tc).await;
        assert_eq!(res.len(), 0);
        call_started_receiver.recv().await.unwrap();
        target_sender
            .send(bridge::MdnsEventType::TargetFound(bridge::Target {
                nodename: Some(NAME.to_owned()),
                ..bridge::Target::EMPTY
            }))
            .await
            .unwrap();
        call_started_receiver.recv().await.unwrap();
        let res = list_targets(&tc).await;
        assert_eq!(res.len(), 1, "received: {:?}", res);
        assert_eq!(res[0].nodename.as_ref().unwrap(), NAME);
    }
}
