// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::{anyhow, Result},
    async_trait::async_trait,
    fidl_fuchsia_developer_bridge as bridge,
    fuchsia_async::Task,
    services::prelude::*,
    std::cell::RefCell,
    std::collections::HashSet,
    std::hash::{Hash, Hasher},
    std::rc::Rc,
    std::time::Duration,
};

mod mdns;

pub(crate) const MDNS_BROADCAST_INTERVAL_SECS: u64 = 20;
pub(crate) const MDNS_INTERFACE_DISCOVERY_INTERVAL_SECS: u64 = 1;
pub(crate) const MDNS_TTL: u32 = 255;

#[derive(Debug)]
struct CachedTarget(bridge::Target);

impl Hash for CachedTarget {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.0.nodename.as_ref().unwrap_or(&"<unknown>".to_string()).hash(state);
    }
}

impl PartialEq for CachedTarget {
    fn eq(&self, other: &CachedTarget) -> bool {
        self.0.nodename.eq(&other.0.nodename)
    }
}

impl Eq for CachedTarget {}

pub(crate) struct MdnsServiceInner {
    events_in: async_channel::Receiver<bridge::MdnsEventType>,
    events_out: async_channel::Sender<bridge::MdnsEventType>,
    target_cache: RefCell<HashSet<CachedTarget>>,
}

impl MdnsServiceInner {
    async fn handle_target(&self, t: bridge::Target) {
        if self.target_cache.borrow_mut().insert(CachedTarget(t.clone())) {
            self.publish_event(bridge::MdnsEventType::TargetFound(t)).await
        } else {
            self.publish_event(bridge::MdnsEventType::TargetRediscovered(t)).await
        }
    }

    async fn publish_event(&self, event: bridge::MdnsEventType) {
        let _ = self.events_out.send(event).await;
    }

    fn target_cache(&self) -> Vec<bridge::Target> {
        self.target_cache.borrow().iter().map(|c| c.0.clone()).collect()
    }
}

// TODO(fxb/74871): Implement cache eviction when targets expire (need
// to look into the mDNS implementation to see where this number is stored).
#[ffx_service]
#[derive(Default)]
pub struct Mdns {
    inner: Option<Rc<MdnsServiceInner>>,
    mdns_task: Option<Task<()>>,
}

#[async_trait(?Send)]
impl FidlService for Mdns {
    type Service = bridge::MdnsMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, _cx: &Context, req: bridge::MdnsRequest) -> Result<()> {
        match req {
            bridge::MdnsRequest::GetTargets { responder } => responder
                .send(
                    &mut self
                        .inner
                        .as_ref()
                        .expect("inner state should be initalized")
                        .target_cache()
                        .into_iter(),
                )
                .map_err(Into::into),
            bridge::MdnsRequest::GetNextEvent { responder } => responder
                .send(
                    self.inner
                        .as_ref()
                        .expect("inner state should be initialized")
                        .events_in
                        .recv()
                        .await
                        .ok()
                        .as_mut(),
                )
                .map_err(Into::into),
        }
    }

    async fn start(&mut self, _cx: &Context) -> Result<()> {
        let (sender, receiver) = async_channel::bounded::<bridge::MdnsEventType>(1);
        let inner = Rc::new(MdnsServiceInner {
            events_in: receiver,
            events_out: sender,
            target_cache: Default::default(),
        });
        self.inner.replace(inner.clone());
        let inner = Rc::downgrade(&inner);
        self.mdns_task.replace(Task::local(mdns::discovery_loop(mdns::DiscoveryConfig {
            socket_tasks: Default::default(),
            mdns_service: inner,
            discovery_interval: Duration::from_secs(MDNS_INTERFACE_DISCOVERY_INTERVAL_SECS),
            query_interval: Duration::from_secs(MDNS_BROADCAST_INTERVAL_SECS),
            ttl: MDNS_TTL,
        })));
        Ok(())
    }

    async fn stop(&mut self, _cx: &Context) -> Result<()> {
        self.mdns_task.take().ok_or(anyhow!("mdns_task never started"))?.cancel().await;
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use services::testing::FakeDaemonBuilder;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mdns_get_targets_empty() {
        let daemon = FakeDaemonBuilder::new().register_fidl_service::<Mdns>().build();
        let proxy = daemon.open_proxy::<bridge::MdnsMarker>().await;
        let targets = proxy.get_targets().await.unwrap();
        assert_eq!(targets.len(), 0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mdns_stop() {
        let daemon = FakeDaemonBuilder::new().build();
        let service = Rc::new(RefCell::new(Mdns::default()));
        let (proxy, task) = services::testing::create_proxy(service.clone(), &daemon).await;
        drop(proxy);
        task.await;
        assert!(service.borrow().mdns_task.is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_new_and_rediscovered_target() {
        let daemon = FakeDaemonBuilder::new().build();
        let service = Rc::new(RefCell::new(Mdns::default()));
        let (proxy, _task) = services::testing::create_proxy(service.clone(), &daemon).await;
        let svc_inner = service.borrow().inner.as_ref().unwrap().clone();
        let nodename = "plop".to_owned();
        svc_inner
            .handle_target(bridge::Target {
                nodename: Some(nodename.clone()),
                ..bridge::Target::EMPTY
            })
            .await;
        while let Some(e) = proxy.get_next_event().await.unwrap() {
            assert!(matches!(*e, bridge::MdnsEventType::TargetFound(_),));
            break;
        }
        assert_eq!(
            proxy.get_targets().await.unwrap().into_iter().next().unwrap().nodename.unwrap(),
            nodename
        );
        svc_inner
            .handle_target(bridge::Target {
                nodename: Some(nodename.clone()),
                ..bridge::Target::EMPTY
            })
            .await;
        while let Some(e) = proxy.get_next_event().await.unwrap() {
            assert!(matches!(*e, bridge::MdnsEventType::TargetRediscovered(_),));
            break;
        }
        assert_eq!(
            proxy.get_targets().await.unwrap().into_iter().next().unwrap().nodename.unwrap(),
            nodename
        );
    }
}
