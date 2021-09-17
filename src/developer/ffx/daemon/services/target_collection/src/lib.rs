// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::target_handle::TargetHandle,
    anyhow::{anyhow, Result},
    async_trait::async_trait,
    chrono::Utc,
    ffx_core::TryStreamUtilExt,
    ffx_daemon_events::TargetConnectionState,
    ffx_daemon_target::target::{
        target_addr_info_to_socketaddr, Target, TargetAddrEntry, TargetAddrType,
    },
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_bridge as bridge,
    fuchsia_async::futures::TryStreamExt,
    services::prelude::*,
    std::net::SocketAddr,
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
}
