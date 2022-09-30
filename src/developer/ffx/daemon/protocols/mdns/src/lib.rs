// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    async_trait::async_trait,
    ffx_stream_util::TryStreamUtilExt,
    fidl::endpoints::ProtocolMarker,
    fidl_fuchsia_developer_ffx as ffx,
    fuchsia_async::Task,
    futures::TryStreamExt,
    protocols::prelude::*,
    std::cell::RefCell,
    std::collections::HashSet,
    std::hash::{Hash, Hasher},
    std::rc::Rc,
    std::time::Duration,
};

mod mdns;

pub(crate) const MDNS_BROADCAST_INTERVAL_SECS: u64 = 10;
pub(crate) const MDNS_INTERFACE_DISCOVERY_INTERVAL_SECS: u64 = 1;
pub(crate) const MDNS_TTL: u32 = 255;

#[derive(Debug)]
struct CachedTarget {
    target: ffx::TargetInfo,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    eviction_task: Option<Task<()>>,
}

impl CachedTarget {
    fn new(target: ffx::TargetInfo) -> Self {
        Self { target, eviction_task: None }
    }

    fn new_with_task(target: ffx::TargetInfo, eviction_task: Task<()>) -> Self {
        Self { target, eviction_task: Some(eviction_task) }
    }
}

impl Hash for CachedTarget {
    fn hash<H: Hasher>(&self, state: &mut H) {
        self.target.nodename.as_ref().unwrap_or(&"<unknown>".to_string()).hash(state);
    }
}

impl PartialEq for CachedTarget {
    fn eq(&self, other: &CachedTarget) -> bool {
        self.target.nodename.eq(&other.target.nodename)
    }
}

impl Eq for CachedTarget {}

pub(crate) struct MdnsProtocolInner {
    events_in: async_channel::Receiver<ffx::MdnsEventType>,
    events_out: async_channel::Sender<ffx::MdnsEventType>,
    target_cache: RefCell<HashSet<CachedTarget>>,
}

impl MdnsProtocolInner {
    async fn handle_target(self: &Rc<Self>, t: ffx::TargetInfo, ttl: u32) {
        let weak = Rc::downgrade(self);
        let t_clone = t.clone();
        let eviction_task = Task::local(async move {
            fuchsia_async::Timer::new(Duration::from_secs(ttl.into())).await;
            if let Some(this) = weak.upgrade() {
                this.evict_target(t_clone).await;
            }
        });

        if self
            .target_cache
            .borrow_mut()
            .replace(CachedTarget::new_with_task(t.clone(), eviction_task))
            .is_none()
        {
            self.publish_event(ffx::MdnsEventType::TargetFound(t)).await;
        } else {
            self.publish_event(ffx::MdnsEventType::TargetRediscovered(t)).await
        }
    }

    async fn evict_target(&self, t: ffx::TargetInfo) {
        if self.target_cache.borrow_mut().remove(&CachedTarget::new(t.clone())) {
            self.publish_event(ffx::MdnsEventType::TargetExpired(t)).await
        }
    }

    async fn publish_event(&self, event: ffx::MdnsEventType) {
        let _ = self.events_out.send(event).await;
    }

    fn target_cache(&self) -> Vec<ffx::TargetInfo> {
        self.target_cache.borrow().iter().map(|c| c.target.clone()).collect()
    }
}

#[ffx_protocol]
#[derive(Default)]
pub struct Mdns {
    inner: Option<Rc<MdnsProtocolInner>>,
    mdns_task: Option<Task<()>>,
}

#[async_trait(?Send)]
impl FidlProtocol for Mdns {
    type Protocol = ffx::MdnsMarker;
    type StreamHandler = FidlStreamHandler<Self>;

    async fn handle(&self, _cx: &Context, req: ffx::MdnsRequest) -> Result<()> {
        match req {
            ffx::MdnsRequest::GetTargets { responder } => responder
                .send(
                    &mut self
                        .inner
                        .as_ref()
                        .expect("inner state should be initalized")
                        .target_cache()
                        .into_iter(),
                )
                .map_err(Into::into),
            ffx::MdnsRequest::GetNextEvent { responder } => responder
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
        let (sender, receiver) = async_channel::bounded::<ffx::MdnsEventType>(1);
        let inner = Rc::new(MdnsProtocolInner {
            events_in: receiver,
            events_out: sender,
            target_cache: Default::default(),
        });
        self.inner.replace(inner.clone());
        let inner = Rc::downgrade(&inner);
        self.mdns_task.replace(Task::local(mdns::discovery_loop(mdns::DiscoveryConfig {
            socket_tasks: Default::default(),
            mdns_protocol: inner,
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

    async fn serve<'a>(
        &'a self,
        cx: &'a Context,
        stream: <Self::Protocol as ProtocolMarker>::RequestStream,
    ) -> Result<()> {
        // This is necessary as we'll be hanging forever waiting on incoming
        // traffic. This will exit early if the stream is closed at any point.
        stream
            .map_err(|err| anyhow!("{}", err))
            .try_for_each_concurrent_while_connected(None, |req| self.handle(cx, req))
            .await
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ::mdns::protocol::{
        Class, DomainBuilder, EmbeddedPacketBuilder, MessageBuilder, RecordBuilder, Type,
    };
    use lazy_static::lazy_static;
    use packet::{InnerPacketBuilder, Serializer};
    use protocols::testing::FakeDaemonBuilder;
    use std::net::IpAddr;
    use std::net::SocketAddr;

    lazy_static! {
        // This is copied from the //fuchsia/lib/src/mdns/rust/src/protocol.rs
        // tests library.
        static ref MDNS_PACKET: Vec<u8> = vec![
            0x00, 0x00, 0x84, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x08, 0x5f,
            0x66, 0x75, 0x63, 0x68, 0x73, 0x69, 0x61, 0x04, 0x5f, 0x75, 0x64, 0x70, 0x05, 0x6c,
            0x6f, 0x63, 0x61, 0x6c, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00,
            0x18, 0x15, 0x74, 0x68, 0x75, 0x6d, 0x62, 0x2d, 0x73, 0x65, 0x74, 0x2d, 0x68, 0x75,
            0x6d, 0x61, 0x6e, 0x2d, 0x73, 0x68, 0x72, 0x65, 0x64, 0xc0, 0x0c, 0xc0, 0x2b, 0x00,
            0x21, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x14,
            0xe9, 0x15, 0x74, 0x68, 0x75, 0x6d, 0x62, 0x2d, 0x73, 0x65, 0x74, 0x2d, 0x68, 0x75,
            0x6d, 0x61, 0x6e, 0x2d, 0x73, 0x68, 0x72, 0x65, 0x64, 0xc0, 0x1a, 0xc0, 0x2b, 0x00,
            0x10, 0x80, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00, 0x01, 0x00, 0xc0, 0x55, 0x00, 0x01,
            0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 0xac, 0x10, 0xf3, 0x26, 0xc0, 0x55,
            0x00, 0x1c, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x10, 0xfe, 0x80, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x8e, 0xae, 0x4c, 0xff, 0xfe, 0xe9, 0xc9, 0xd3,
        ];
    }

    async fn wait_for_port_binds(proxy: &ffx::MdnsProxy) -> u16 {
        while let Some(e) = proxy.get_next_event().await.unwrap() {
            match *e {
                ffx::MdnsEventType::SocketBound(ffx::MdnsBindEvent { port, .. }) => {
                    let p = port.unwrap();
                    assert_ne!(p, 0);
                    return p;
                }
                e => panic!(
                    "events should start with two port binds. encountered unrecognized event: {:?}",
                    e
                ),
            }
        }
        panic!("no port bound");
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mdns_get_targets_empty() {
        let daemon = FakeDaemonBuilder::new().register_fidl_protocol::<Mdns>().build();
        let proxy = daemon.open_proxy::<ffx::MdnsMarker>().await;
        let targets = proxy.get_targets().await.unwrap();
        assert_eq!(targets.len(), 0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mdns_stop() {
        let daemon = FakeDaemonBuilder::new().build();
        let protocol = Rc::new(RefCell::new(Mdns::default()));
        let (proxy, task) = protocols::testing::create_proxy(protocol.clone(), &daemon).await;
        drop(proxy);
        task.await;
        assert!(protocol.borrow().mdns_task.is_none());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mdns_bind_event_on_first_listen() {
        let daemon = FakeDaemonBuilder::new().register_fidl_protocol::<Mdns>().build();
        let proxy = daemon.open_proxy::<ffx::MdnsMarker>().await;
        while let Some(e) = proxy.get_next_event().await.unwrap() {
            if matches!(*e, ffx::MdnsEventType::SocketBound(_)) {
                break;
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mdns_network_traffic_valid() {
        let daemon = FakeDaemonBuilder::new().register_fidl_protocol::<Mdns>().build();
        let proxy = daemon.open_proxy::<ffx::MdnsMarker>().await;
        let bound_port = wait_for_port_binds(&proxy).await;

        // Note: this and other tests are only using IPv4 due to some issues
        // on Mac, wherein sending on the unspecified address leads to a "no
        // route to host" error. For some reason using IPv6 with either the
        // unspecified address or the localhost address leads in errors or
        // hangs on Mac tests.
        let socket = socket2::Socket::new(
            socket2::Domain::IPV4,
            socket2::Type::DGRAM,
            Some(socket2::Protocol::UDP),
        )
        .unwrap();
        socket.set_ttl(1).unwrap();
        socket.set_multicast_ttl_v4(1).unwrap();
        let addr: SocketAddr = (std::net::Ipv4Addr::LOCALHOST, bound_port).into();

        let my_ip = match addr.ip() {
            IpAddr::V4(addr) => addr.octets(),
            _ => panic!("expected ipv4 addr"),
        };

        let mut message = MessageBuilder::new(0, true);
        let domain =
            DomainBuilder::from_str("thumb-set-human-shred._fuchsia._udp.local").unwrap().bytes();

        let ptr = RecordBuilder::new(
            DomainBuilder::from_str("_fuchsia._udp.local").unwrap(),
            Type::Ptr,
            Class::In,
            true,
            1,
            &domain,
        );
        message.add_answer(ptr);

        let nodename = DomainBuilder::from_str("thumb-set-human-shred.local").unwrap();
        let rec = RecordBuilder::new(nodename, Type::A, Class::In, true, 4500, &my_ip);
        message.add_additional(rec);

        let msg_bytes = message
            .into_serializer()
            .serialize_vec_outer()
            .unwrap_or_else(|_| panic!("failed to serialize"))
            .unwrap_b();
        socket.send_to(msg_bytes.as_ref(), &addr.into()).unwrap();

        while let Some(e) = proxy.get_next_event().await.unwrap() {
            if matches!(*e, ffx::MdnsEventType::TargetFound(_),) {
                break;
            }
        }
        assert_eq!(
            proxy.get_targets().await.unwrap().into_iter().next().unwrap().nodename.unwrap(),
            "thumb-set-human-shred",
        );
        socket.send_to(msg_bytes.as_ref(), &addr.into()).unwrap();
        while let Some(e) = proxy.get_next_event().await.unwrap() {
            if matches!(*e, ffx::MdnsEventType::TargetRediscovered(_)) {
                break;
            }
        }
        assert_eq!(
            proxy.get_targets().await.unwrap().into_iter().next().unwrap().nodename.unwrap(),
            "thumb-set-human-shred",
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mdns_network_traffic_invalid() {
        let daemon = FakeDaemonBuilder::new().register_fidl_protocol::<Mdns>().build();
        let proxy = daemon.open_proxy::<ffx::MdnsMarker>().await;
        let bound_port = wait_for_port_binds(&proxy).await;

        let socket = socket2::Socket::new(
            socket2::Domain::IPV4,
            socket2::Type::DGRAM,
            Some(socket2::Protocol::UDP),
        )
        .unwrap();
        socket.set_ttl(1).unwrap();
        socket.set_multicast_ttl_v4(1).unwrap();
        let addr: SocketAddr = (std::net::Ipv4Addr::UNSPECIFIED, bound_port).into();
        // This is just a copy of the valid mdns packet but with a few bytes altered.
        let packet: Vec<u8> = vec![
            0x00, 0x00, 0x84, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x08, 0x5f,
            0x66, 0x75, 0x63, 0x68, 0x73, 0x69, 0x61, 0x04, 0x5f, 0x75, 0x64, 0x70, 0x05, 0x6c,
            0x6f, 0x63, 0x61, 0x6c, 0x00, 0x00, 0x0c, 0x00, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00,
            0x18, 0x95, 0x74, 0x68, 0x75, 0x6d, 0x62, 0x2d, 0x73, 0x65, 0x74, 0x2d, 0x68, 0x75,
            0x6d, 0x61, 0x6e, 0x2d, 0x73, 0x68, 0x72, 0x65, 0x64, 0xc0, 0x0c, 0xc0, 0x2b, 0x00,
            0x21, 0x80, 0x01, 0x00, 0x10, 0x02, 0x78, 0x00, 0x1e, 0x00, 0x00, 0x00, 0x00, 0x14,
            0xe9, 0x15, 0x74, 0x68, 0x75, 0x6d, 0x62, 0x2d, 0x73, 0x65, 0x74, 0x2d, 0x68, 0x75,
            0x6d, 0x61, 0x6e, 0x2d, 0x73, 0x68, 0x72, 0x65, 0x64, 0xc0, 0x1a, 0xc0, 0x2b, 0x00,
            0x10, 0x80, 0x01, 0x00, 0x00, 0x11, 0x94, 0x00, 0x01, 0x00, 0xc0, 0x55, 0x00, 0x01,
            0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 0xac, 0x10, 0xf3, 0x26, 0xc0, 0x55,
            0x00, 0x1c, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x10, 0xfe, 0x80, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00, 0x8e, 0xae, 0x4c, 0xff, 0xfe, 0xe9, 0xc9, 0xd3,
        ];
        socket.send_to(&packet, &addr.into()).unwrap();
        // This is here to un-stick the executor a bit, as otherwise the socket
        // will not get read, and the code being tested will not get exercised.
        //
        // If this ever flakes it will be because somehow valid traffic made it
        // onto the network.
        fuchsia_async::Timer::new(std::time::Duration::from_millis(200)).await;
        assert_eq!(proxy.get_targets().await.unwrap().len(), 0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_mdns_network_traffic_wrong_protocol() {
        let daemon = FakeDaemonBuilder::new().register_fidl_protocol::<Mdns>().build();
        let proxy = daemon.open_proxy::<ffx::MdnsMarker>().await;
        let bound_port = wait_for_port_binds(&proxy).await;
        let socket = socket2::Socket::new(
            socket2::Domain::IPV4,
            socket2::Type::DGRAM,
            Some(socket2::Protocol::UDP),
        )
        .unwrap();
        socket.set_ttl(1).unwrap();
        socket.set_multicast_ttl_v4(1).unwrap();
        let addr: SocketAddr = (std::net::Ipv4Addr::UNSPECIFIED, bound_port).into();

        let domain = DomainBuilder::from_str("_nonsense._udp.local").unwrap();
        let record = RecordBuilder::new(
            domain,
            Type::Ptr,
            Class::In,
            true,
            4500,
            &[0x03, 'f' as u8, 'o' as u8, 'o' as u8, 0],
        );
        let nodename = DomainBuilder::from_str("fuchsia_thing._fuchsia._udp.local").unwrap();
        let other_record =
            RecordBuilder::new(nodename, Type::A, Class::In, true, 4500, &[8, 8, 8, 8]);
        let mut message = MessageBuilder::new(0, true);
        message.add_additional(record);
        message.add_additional(other_record);
        let msg_bytes = message
            .into_serializer()
            .serialize_vec_outer()
            .unwrap_or_else(|_| panic!("failed to serialize"))
            .unwrap_b();
        socket.send_to(msg_bytes.as_ref(), &addr.into()).unwrap();
        // This is here to un-stick the executor a bit, as otherwise the socket
        // will not get read, and the code being tested will not get exercised.
        //
        // If this ever flakes it will be because somehow valid traffic made it
        // onto the network.
        fuchsia_async::Timer::new(std::time::Duration::from_millis(200)).await;
        assert_eq!(proxy.get_targets().await.unwrap().len(), 0);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_new_and_rediscovered_target() {
        let daemon = FakeDaemonBuilder::new().build();
        let protocol = Rc::new(RefCell::new(Mdns::default()));
        let (proxy, _task) = protocols::testing::create_proxy(protocol.clone(), &daemon).await;
        let svc_inner = protocol.borrow().inner.as_ref().unwrap().clone();
        let nodename = "plop".to_owned();
        // Skip port binding.
        let _ = wait_for_port_binds(&proxy).await;
        svc_inner
            .handle_target(
                ffx::TargetInfo { nodename: Some(nodename.clone()), ..ffx::TargetInfo::EMPTY },
                5000,
            )
            .await;
        if let Some(e) = proxy.get_next_event().await.unwrap() {
            assert!(matches!(*e, ffx::MdnsEventType::TargetFound(_),));
        }
        assert_eq!(
            proxy.get_targets().await.unwrap().into_iter().next().unwrap().nodename.unwrap(),
            nodename
        );
        svc_inner
            .handle_target(
                ffx::TargetInfo { nodename: Some(nodename.clone()), ..ffx::TargetInfo::EMPTY },
                5000,
            )
            .await;
        if let Some(e) = proxy.get_next_event().await.unwrap() {
            assert!(matches!(*e, ffx::MdnsEventType::TargetRediscovered(_),));
        }
        assert_eq!(
            proxy.get_targets().await.unwrap().into_iter().next().unwrap().nodename.unwrap(),
            nodename
        );
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_eviction() {
        let daemon = FakeDaemonBuilder::new().build();
        let protocol = Rc::new(RefCell::new(Mdns::default()));
        let (proxy, _task) = protocols::testing::create_proxy(protocol.clone(), &daemon).await;
        let svc_inner = protocol.borrow().inner.as_ref().unwrap().clone();
        let nodename = "plop".to_owned();
        // Skip port binding.
        let _ = wait_for_port_binds(&proxy).await;
        svc_inner
            .handle_target(
                ffx::TargetInfo { nodename: Some(nodename.clone()), ..ffx::TargetInfo::EMPTY },
                1,
            )
            .await;
        // Hangs until the target expires.
        let mut new_target_found = false;
        while let Some(e) = proxy.get_next_event().await.unwrap() {
            match *e {
                ffx::MdnsEventType::TargetExpired(_) => {
                    break;
                }
                ffx::MdnsEventType::TargetFound(t) => {
                    assert_eq!(t.nodename.unwrap(), nodename);
                    new_target_found = true;
                }
                _ => {}
            }
        }
        assert_eq!(proxy.get_targets().await.unwrap().len(), 0);
        assert!(new_target_found);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_target_eviction_timer_override() {
        let daemon = FakeDaemonBuilder::new().build();
        let protocol = Rc::new(RefCell::new(Mdns::default()));
        let (proxy, _task) = protocols::testing::create_proxy(protocol.clone(), &daemon).await;
        let svc_inner = protocol.borrow().inner.as_ref().unwrap().clone();
        let nodename = "plop".to_owned();
        // Skip port binding.
        let _ = wait_for_port_binds(&proxy).await;
        svc_inner
            .handle_target(
                ffx::TargetInfo { nodename: Some(nodename.clone()), ..ffx::TargetInfo::EMPTY },
                50000,
            )
            .await;
        while let Some(e) = proxy.get_next_event().await.unwrap() {
            match *e {
                ffx::MdnsEventType::TargetFound(t) => {
                    assert_eq!(t.nodename.unwrap(), nodename);
                    break;
                }
                _ => {}
            }
        }
        svc_inner
            .handle_target(
                ffx::TargetInfo { nodename: Some(nodename.clone()), ..ffx::TargetInfo::EMPTY },
                1,
            )
            .await;
        while let Some(e) = proxy.get_next_event().await.unwrap() {
            if matches!(*e, ffx::MdnsEventType::TargetExpired(_)) {
                break;
            }
        }
        assert_eq!(proxy.get_targets().await.unwrap().len(), 0);
    }
}
