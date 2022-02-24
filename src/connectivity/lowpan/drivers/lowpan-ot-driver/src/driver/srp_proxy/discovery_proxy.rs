// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_net_mdns::*;
use fuchsia_async::Task;
use fuchsia_component::client::connect_to_protocol;
use futures::channel::mpsc;
use futures::never::Never;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::ffi::{CStr, CString};
use std::pin::Pin;
use std::sync::Arc;
use std::task::{Context, Poll};

// This value was chosen arbitrarily to hopefully be large enough to
// eliminate contention cases under most circumstances.
const DISCOVERY_PROXY_BUFFER_LEN: usize = 50;

// This value was chosen arbitrarily to hopefully be large enough to
// allow all addresses to be gathered when resolving a hostname over mDNS.
const DEFAULT_RESOLVE_DURATION: i64 = 3;

#[derive(Debug)]
enum DnssdUpdate {
    Host { host_name: CString, addresses: Vec<ot::Ip6Address>, ttl: u32 },
    Service(ServiceSubscriberRequest),
}

/// The discovery proxy handles taking unicast DNS requests and converting them to mDNS requests.
#[derive(Debug)]
pub struct DiscoveryProxy {
    update_receiver: Mutex<mpsc::Receiver<DnssdUpdate>>,
    subscriptions: Arc<Mutex<HashMap<CString, Task<Result>>>>,
}

impl DiscoveryProxy {
    pub fn new<T: ot::InstanceInterface>(instance: &T) -> Result<DiscoveryProxy, anyhow::Error> {
        let (update_sender, update_receiver) = mpsc::channel(DISCOVERY_PROXY_BUFFER_LEN);
        let subscriptions = Arc::new(Mutex::new(HashMap::new()));

        // All of the following variables will be moved into the `dnssd_query` callback closure.
        let subscriptions_clone = subscriptions.clone();
        let srp_domain = instance.srp_server_get_domain().to_str()?.to_string();
        let subscriber = connect_to_protocol::<SubscriberMarker>()?;
        let resolver = connect_to_protocol::<ResolverMarker>()?;

        instance.dnssd_query_set_callbacks(Some(
            move |subscribed: bool, name_srp_domain: &CStr| {
                let name_srp_domain: CString = name_srp_domain.into();

                let name_local_domain =
                    match replace_domain_cstr(&name_srp_domain, &srp_domain, LOCAL_DOMAIN) {
                        Ok(x) => x,
                        Err(err) => {
                            error!("dnssd_query_callback: {:?}", err);
                            return;
                        }
                    };

                if !subscribed {
                    Self::dnssd_unsubscribed_from(name_srp_domain, &subscriptions_clone);
                } else {
                    Self::dnssd_subscribed_to(
                        name_local_domain,
                        name_srp_domain,
                        &subscriptions_clone,
                        &update_sender,
                        &subscriber,
                        &resolver,
                    );
                }
            },
        ));

        Ok(DiscoveryProxy { update_receiver: Mutex::new(update_receiver), subscriptions })
    }

    fn dnssd_unsubscribed_from(
        name_srp_domain: CString,
        subscriptions: &Arc<Mutex<HashMap<CString, Task<Result>>>>,
    ) {
        subscriptions.lock().remove(&name_srp_domain);
    }

    fn dnssd_subscribed_to_host(
        name_local_domain: String,
        name_srp_domain: CString,
        subscriptions: &Arc<Mutex<HashMap<CString, Task<Result>>>>,
        update_sender: &mpsc::Sender<DnssdUpdate>,
        resolver: &ResolverProxy,
    ) {
        info!("DNS-SD subscription to {:?} as host name", name_srp_domain);

        let mut update_sender_clone = update_sender.clone();
        let name_srp_domain_copy = name_srp_domain.clone();

        // TODO(fxbug.dev/94368): This isn't really a subscription to a host, this is just
        //                        resolving the hostname once and hoping it doesn't change for
        //                        the duration of the subscription. This is OK for now, but must
        //                        be addressed at some point.

        let future = resolver
            .resolve_host_name(&name_local_domain, DEFAULT_RESOLVE_DURATION)
            .map_err(anyhow::Error::from)
            .and_then(move |x| async move {
                if let Some(ipv6_addr_box) = x.1 {
                    let ipv6_addr = ot::Ip6Address::from(ipv6_addr_box.addr);
                    update_sender_clone
                        .send(DnssdUpdate::Host {
                            host_name: name_srp_domain_copy,
                            addresses: vec![ipv6_addr],
                            ttl: DEFAULT_MDNS_TTL, // TODO(fxbug.dev/94352): Change when available.
                        })
                        .await?;
                } else {
                    update_sender_clone
                        .send(DnssdUpdate::Host {
                            host_name: name_srp_domain_copy,
                            addresses: vec![],
                            ttl: 0,
                        })
                        .await?;
                    warn!("Unable to resolve {:?} to an IPv6 address.", &name_local_domain);
                }
                Ok(())
            })
            .inspect_err(|err| {
                error!("resolve_host_name: {:?}", err);
            });

        subscriptions.lock().insert(name_srp_domain, fasync::Task::spawn(future));
    }

    fn dnssd_subscribed_to_service(
        name_local_domain: String,
        name_srp_domain: CString,
        subscriptions: &Arc<Mutex<HashMap<CString, Task<Result>>>>,
        update_sender: &mpsc::Sender<DnssdUpdate>,
        subscriber: &SubscriberProxy,
    ) {
        info!("DNS-SD subscription to {:?} as service", name_srp_domain);

        let (client, server) = match create_endpoints::<ServiceSubscriberMarker>() {
            Ok(x) => x,
            Err(err) => {
                error!("create_endpoints::<ServiceSubscriberMarker>: {:?}", err);
                return;
            }
        };

        if let Err(err) = subscriber.subscribe_to_service(&name_local_domain, client) {
            error!("Unable to subscribe to {:?}: {:?}", name_local_domain, err);
            return;
        }

        let stream = server.into_stream().unwrap();

        let update_sender_clone = update_sender.clone();
        let future = stream
            .map_err(anyhow::Error::from)
            .try_for_each(move |event: ServiceSubscriberRequest| {
                let mut update_sender_clone = update_sender_clone.clone();
                async move {
                    update_sender_clone
                        .send(DnssdUpdate::Service(event))
                        .map_err(anyhow::Error::from)
                        .await
                }
            })
            .inspect_err(|err| {
                error!("service_subscription: {:?}", err);
            });

        subscriptions.lock().insert(name_srp_domain, fasync::Task::spawn(future));
    }

    fn dnssd_subscribed_to(
        name_local_domain: String,
        name_srp_domain: CString,
        subscriptions: &Arc<Mutex<HashMap<CString, Task<Result>>>>,
        update_sender: &mpsc::Sender<DnssdUpdate>,
        subscriber: &SubscriberProxy,
        resolver: &ResolverProxy,
    ) {
        let is_this_a_service =
            name_local_domain.contains("._udp.") || name_local_domain.contains("._tcp.");

        if is_this_a_service {
            Self::dnssd_subscribed_to_service(
                name_local_domain,
                name_srp_domain,
                subscriptions,
                update_sender,
                subscriber,
            );
        } else {
            Self::dnssd_subscribed_to_host(
                name_local_domain,
                name_srp_domain,
                subscriptions,
                update_sender,
                resolver,
            );
        }
    }

    fn handle_service_instance_update(
        instance: &ot::Instance,
        service_instance: ServiceInstance,
    ) -> Result {
        if let ServiceInstance {
            service: Some(service_name),
            instance: Some(instance_name),
            ipv6_endpoint: Some(ipv6_endpoint),
            text,
            srv_priority: Some(srv_priority),
            srv_weight: Some(srv_weight),
            target: Some(host_name),
            ..
        } = service_instance
        {
            let sockaddr: ot::SockAddr = ipv6_endpoint.into();

            if net_types::ip::Ipv6Addr::from(sockaddr.addr().octets()).is_unicast_link_local() {
                // Skip unicast link local since traffic to these
                // addresses cannot be forwarded.
                return Ok(());
            }
            let srp_domain = instance.srp_server_get_domain().to_str()?;

            let instance_name_srp =
                CString::new(replace_domain(&instance_name, LOCAL_DOMAIN, srp_domain)?)?;
            let service_name_srp =
                CString::new(replace_domain(&service_name, LOCAL_DOMAIN, srp_domain)?)?;
            let host_name_srp =
                CString::new(replace_domain(&host_name, LOCAL_DOMAIN, srp_domain)?)?;

            let ttl = DEFAULT_MDNS_TTL; // TODO(fxbug.dev/94352): Change when available.
            let addresses = [sockaddr.addr()];

            instance.dnssd_query_handle_discovered_service_instance(
                service_name_srp.as_c_str(),
                &addresses,
                instance_name_srp.as_c_str(),
                host_name_srp.as_c_str(),
                sockaddr.port(),
                srv_priority,
                ttl,
                flatten_txt(text).as_bytes(),
                srv_weight,
            );

            // Also go ahead and treat this as a discovered host.
            instance.dnssd_query_handle_discovered_host(host_name_srp.as_c_str(), &addresses, ttl);
        }
        Ok(())
    }

    fn handle_service_subscriber_request(
        ot_instance: &ot::Instance,
        service_subscriber_request: ServiceSubscriberRequest,
    ) -> Result {
        match service_subscriber_request {
            // A DNS-SD IPv6 service instance has been discovered.
            ServiceSubscriberRequest::OnInstanceDiscovered {
                instance: service_instance,
                responder,
            } => {
                Self::handle_service_instance_update(ot_instance, service_instance)?;
                responder.send()?;
            }

            // A DNS-SD IPv6 service instance has changed.
            ServiceSubscriberRequest::OnInstanceChanged {
                instance: service_instance,
                responder,
            } => {
                Self::handle_service_instance_update(ot_instance, service_instance)?;
                responder.send()?;
            }

            // A DNS-SD IPv6 service instance has been lost.
            ServiceSubscriberRequest::OnInstanceLost {
                instance: service_instance,
                responder,
                ..
            } => {
                // TODO(fxbug.dev/94362): It is not entirely clear how to handle this case,
                //                        so for the time being we are ignoring it.
                info!("Ignoring loss of service instance {:?}", service_instance);

                responder.send()?;
            }

            ServiceSubscriberRequest::OnQuery { responder, .. } => {
                // We don't care about queries.
                responder.send()?;
            }
        }
        Ok(())
    }

    /// Async entrypoint. Called from [`DiscoveryProxyPollerExt::discovery_proxy_poll`].
    fn poll(&self, instance: &ot::Instance, cx: &mut Context<'_>) {
        match self.update_receiver.lock().poll_next_unpin(cx) {
            Poll::Ready(Some(DnssdUpdate::Service(request))) => {
                if let Err(err) = Self::handle_service_subscriber_request(instance, request) {
                    error!("handle_service_subscriber_request: {:?}", err);
                }
            }
            Poll::Ready(Some(DnssdUpdate::Host { host_name, addresses, ttl })) => {
                instance.dnssd_query_handle_discovered_host(&host_name, &addresses, ttl);
            }
            Poll::Ready(None) => {
                warn!("update_receiver stream has finished unexpectedly.");
            }
            Poll::Pending => {}
        }

        self.subscriptions.lock().retain(|k, v| {
            if let Poll::Ready(ret) = v.poll_unpin(cx) {
                warn!("Subscription to {:?} has stopped: {:?}", k, ret);
                false
            } else {
                true
            }
        });
    }
}

#[derive(Debug)]
pub struct DiscoveryProxyPoller<'a, T: ?Sized>(&'a T);
impl<'a, T: DiscoveryProxyPollerExt + ?Sized> Future for DiscoveryProxyPoller<'a, T> {
    type Output = Never;
    fn poll(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        self.0.discovery_proxy_poll(cx)
    }
}

pub trait DiscoveryProxyPollerExt {
    fn discovery_proxy_poll(&self, cx: &mut Context<'_>) -> Poll<Never>;

    fn discovery_proxy_future(&self) -> DiscoveryProxyPoller<'_, Self> {
        DiscoveryProxyPoller(self)
    }
}

impl<T: AsRef<ot::Instance> + AsRef<Option<DiscoveryProxy>>> DiscoveryProxyPollerExt
    for parking_lot::Mutex<T>
{
    fn discovery_proxy_poll(&self, cx: &mut std::task::Context<'_>) -> std::task::Poll<Never> {
        let guard = self.lock();

        let ot: &ot::Instance = guard.as_ref();
        let discovery_proxy: &Option<DiscoveryProxy> = guard.as_ref();

        if let Some(discovery_proxy) = discovery_proxy.as_ref() {
            discovery_proxy.poll(ot, cx);
        }
        Poll::Pending
    }
}
