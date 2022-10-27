// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;
use anyhow::bail;
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
const DEFAULT_RESOLVE_DURATION_NS: i64 = std::time::Duration::from_secs(3).as_nanos() as i64;

/// Converts an optional vector of strings to a single DNS-compatible string.
fn flatten_txt(txt: Option<Vec<Vec<u8>>>) -> Vec<u8> {
    let mut ret = vec![];

    for mut txt in txt.iter().flat_map(|x| x.iter()).map(Vec::as_slice) {
        if txt.len() > u8::MAX as usize {
            // Limit the size of the records to 255 characters.
            txt = &txt[0..(u8::MAX as usize) + 1];
        }
        ret.push(u8::try_from(txt.len()).unwrap());
        ret.extend_from_slice(txt);
    }

    ret
}

/// Converts an iterator over [`HostAddress`]es to a vector of [`ot::Ip6Address`]es.
fn process_addresses_from_host_addresses<T: IntoIterator<Item = HostAddress>>(
    addresses: T,
) -> Vec<ot::Ip6Address> {
    let mut addresses = addresses
        .into_iter()
        .flat_map(|x| {
            if let fidl_fuchsia_net::IpAddress::Ipv6(addr) = x.address {
                let addr = ot::Ip6Address::from(addr.addr);
                if should_proxy_address(&addr) {
                    return Some(addr);
                }
            }
            None
        })
        .collect::<Vec<_>>();
    addresses.sort();
    addresses
}

/// Converts an iterator over [`fidl_fuchsia_net::SocketAddress`]es to a vector of
/// [`ot::Ip6Address`]es and a port.
fn process_addresses_from_socket_addresses<
    T: IntoIterator<Item = fidl_fuchsia_net::SocketAddress>,
>(
    addresses: T,
) -> (Vec<ot::Ip6Address>, Option<u16>) {
    let mut ret_port: Option<u16> = None;
    let mut addresses =
        addresses
            .into_iter()
            .flat_map(|x| {
                if let fidl_fuchsia_net::SocketAddress::Ipv6(
                    fidl_fuchsia_net::Ipv6SocketAddress { address, port, .. },
                ) = x
                {
                    let addr = ot::Ip6Address::from(address.addr);
                    if ret_port.is_none() {
                        ret_port = Some(port);
                    } else if ret_port != Some(port) {
                        warn!(
                            "mDNS service has multiple ports for the same service, {:?} != {:?}",
                            ret_port.unwrap(),
                            port
                        );
                    }
                    if should_proxy_address(&addr) {
                        return Some(addr);
                    }
                }
                None
            })
            .collect::<Vec<_>>();
    addresses.sort();
    (addresses, ret_port)
}

#[derive(Debug)]
enum DnssdUpdate {
    Host { host_name: CString, addresses: Vec<ot::Ip6Address>, ttl: u32 },
    Service(ServiceSubscriptionListenerRequest),
}

/// The discovery proxy handles taking unicast DNS requests and converting them to mDNS requests.
#[derive(Debug)]
pub struct DiscoveryProxy {
    update_receiver: Mutex<mpsc::Receiver<DnssdUpdate>>,
    subscriptions: Arc<Mutex<HashMap<CString, Task<Result>>>>,
}

/// Returns `true` if the address is a unicast address with link-local scope.
///
/// The official equivalent of this method is [`std::net::Ipv6Addr::is_unicast_link_local()`],
/// however that method is [still experimental](https://github.com/rust-lang/rust/issues/27709).
fn ipv6addr_is_unicast_link_local(addr: &std::net::Ipv6Addr) -> bool {
    (addr.segments()[0] & 0xffc0) == 0xfe80
}

/// Returns `true` if the address should be proxied to the thread network.
fn should_proxy_address(addr: &std::net::Ipv6Addr) -> bool {
    !ipv6addr_is_unicast_link_local(addr) && !addr.is_loopback() && !addr.is_unspecified()
}

impl DiscoveryProxy {
    pub fn new<T: ot::InstanceInterface>(instance: &T) -> Result<DiscoveryProxy, anyhow::Error> {
        let (update_sender, update_receiver) = mpsc::channel(DISCOVERY_PROXY_BUFFER_LEN);
        let subscriptions = Arc::new(Mutex::new(HashMap::new()));

        // All of the following variables will be moved into the `dnssd_query` callback closure.
        let subscriptions_clone = subscriptions.clone();
        let srp_domain = instance.srp_server_get_domain().to_str()?.to_string();

        instance.dnssd_query_set_callbacks(Some(
            move |subscribed: bool, name_srp_domain: &CStr| {
                if subscribed {
                    debug!("Subscribing to DNS-SD query: {:?}", name_srp_domain);
                } else {
                    debug!("Unsubscribing from DNS-SD query: {:?}", name_srp_domain);
                }
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
                } else if let Err(err) = Self::dnssd_subscribed_to(
                    name_local_domain,
                    name_srp_domain,
                    &subscriptions_clone,
                    &update_sender,
                ) {
                    error!("dnssd_subscribed_to: {:?}", err);
                }
            },
        ));

        info!("DiscoveryProxy Started");

        Ok(DiscoveryProxy { update_receiver: Mutex::new(update_receiver), subscriptions })
    }

    fn dnssd_unsubscribed_from(
        name_srp_domain: CString,
        subscriptions: &Arc<Mutex<HashMap<CString, Task<Result>>>>,
    ) {
        subscriptions.lock().remove(&name_srp_domain);
    }

    const fn host_name_subscription_options() -> HostNameSubscriptionOptions {
        HostNameSubscriptionOptions {
            exclude_local_proxies: Some(true),
            ..HostNameSubscriptionOptions::EMPTY
        }
    }

    const fn host_name_resolution_options() -> HostNameResolutionOptions {
        HostNameResolutionOptions {
            exclude_local_proxies: Some(true),
            ..HostNameResolutionOptions::EMPTY
        }
    }

    const fn service_subscription_options() -> ServiceSubscriptionOptions {
        ServiceSubscriptionOptions {
            exclude_local_proxies: Some(true),
            ..ServiceSubscriptionOptions::EMPTY
        }
    }

    fn dnssd_subscribed_to_host(
        name_local_domain: String,
        name_srp_domain: CString,
        subscriptions: &Arc<Mutex<HashMap<CString, Task<Result>>>>,
        update_sender: &mpsc::Sender<DnssdUpdate>,
    ) -> Result<(), anyhow::Error> {
        let subscriber = match connect_to_protocol::<HostNameSubscriberMarker>() {
            Ok(subscriber) => subscriber,
            Err(err) => {
                bail!("Cannot connect to HostNameSubscriber: {:?}", err);
            }
        };

        // Trim the `local.` part, trim the trailing dot.
        let name = name_local_domain.trim_end_matches(LOCAL_DOMAIN).trim_end_matches('.');

        let (client, server) = match create_endpoints::<HostNameSubscriptionListenerMarker>() {
            Ok(x) => x,
            Err(err) => {
                bail!("create_endpoints::<HostNameSubscriberMarker>: {:?}", err);
            }
        };

        debug!(
            "DNS-SD subscription to {:?} as host name, will proxy to {:?}",
            name_srp_domain, name
        );

        if name.len() > MAX_DNSSD_HOST_LEN {
            bail!("Host {:?} is too long (max {} chars)", name, MAX_DNSSD_HOST_LEN);
        }

        if let Err(err) =
            subscriber.subscribe_to_host_name(name, Self::host_name_subscription_options(), client)
        {
            bail!("Unable to subscribe to {:?}({:?}): {:?}", name, name_local_domain, err);
        }

        let stream = server.into_stream().unwrap();

        let update_sender_clone = update_sender.clone();
        let name_srp_domain_copy = name_srp_domain.clone();

        let future = stream
            .map_err(anyhow::Error::from)
            .try_for_each(
                move |HostNameSubscriptionListenerRequest::OnAddressesChanged {
                          addresses,
                          responder,
                      }: HostNameSubscriptionListenerRequest| {
                    let mut update_sender_clone = update_sender_clone.clone();
                    let addresses = process_addresses_from_host_addresses(addresses);
                    let name_srp_domain_copy = name_srp_domain_copy.clone();
                    let dnssd_update = DnssdUpdate::Host {
                        host_name: name_srp_domain_copy,
                        addresses,
                        ttl: DEFAULT_MDNS_TTL, // TODO(fxbug.dev/94352): Change when available.
                    };
                    debug!("DNS-SD host subscription update: {:?}", dnssd_update);
                    async move {
                        update_sender_clone.send(dnssd_update).map_err(anyhow::Error::from).await?;
                        responder.send().map_err(anyhow::Error::from)
                    }
                },
            )
            .inspect_err(move |err| {
                // Due to fxbug.dev/99755, the subscription will close
                // if the servicesubscriber that created it is closed.
                // TODO(fxbug.dev/99755): Remove this line once fxbug.dev/99755 is fixed.
                let _ = subscriber.clone();

                error!("host_name_subscription: {:?}", err);
            });

        // TODO(fxbug.dev/94368): It is unclear why this step still appears to be necessary,
        //                        but we don't seem to get a response otherwise.
        match connect_to_protocol::<HostNameResolverMarker>() {
            Ok(resolver) => {
                let name_srp_domain_copy = name_srp_domain.clone();
                let mut update_sender_clone = update_sender.clone();
                let initial_resolution_future = resolver
                    .resolve_host_name(
                        name,
                        DEFAULT_RESOLVE_DURATION_NS,
                        Self::host_name_resolution_options(),
                    )
                    .map_err(anyhow::Error::from)
                    .and_then(move |host_addresses| async move {
                        let addresses =
                            process_addresses_from_host_addresses(host_addresses.clone());

                        if addresses.is_empty() {
                            warn!("Unable to resolve {:?} to an IPv6 address.", &name_local_domain);
                            debug!(
                                "Full list for {:?} was {:?}",
                                &name_local_domain, host_addresses
                            );
                        } else {
                            debug!("Resolved {:?} to {:?}", &name_local_domain, addresses);
                        }

                        update_sender_clone
                            .send(DnssdUpdate::Host {
                                host_name: name_srp_domain_copy,
                                addresses,
                                ttl: DEFAULT_MDNS_TTL, // TODO(fxbug.dev/94352): Change when available.
                            })
                            .await?;
                        Ok(())
                    })
                    .inspect_err(|err| {
                        error!("resolve_host_name: {:?}", err);
                    })
                    .map(|_| ());
                fasync::Task::spawn(initial_resolution_future).detach();
            }
            Err(err) => {
                error!("connect_to_protocol::<ResolverMarker>(): {:?}", err);
            }
        }

        subscriptions.lock().insert(name_srp_domain, fasync::Task::spawn(future));

        Ok(())
    }

    fn dnssd_subscribed_to_service(
        name_local_domain: String,
        name_srp_domain: CString,
        subscriptions: &Arc<Mutex<HashMap<CString, Task<Result>>>>,
        update_sender: &mpsc::Sender<DnssdUpdate>,
    ) -> Result<(), anyhow::Error> {
        let subscriber = match connect_to_protocol::<ServiceSubscriber2Marker>() {
            Ok(subscriber) => subscriber,
            Err(err) => {
                bail!("Cannot connect to SubscriberMarker: {:?}", err);
            }
        };

        // Trim the `local.` part, keep the trailing dot.
        let service_name = name_local_domain.trim_end_matches(LOCAL_DOMAIN);

        debug!(
            "DNS-SD subscription to {:?} as service, will proxy to {:?}",
            name_srp_domain, service_name
        );

        let (client, server) = match create_endpoints::<ServiceSubscriptionListenerMarker>() {
            Ok(x) => x,
            Err(err) => {
                bail!("create_endpoints::<ServiceSubscriberMarker>: {:?}", err);
            }
        };

        if service_name.len() > MAX_DNSSD_SERVICE_LEN {
            bail!(
                "Unable to subscribe to {:?}({:?}): Service too long (max 22 chars)",
                service_name,
                name_local_domain
            );
        }

        if let Err(err) = subscriber.subscribe_to_service(
            service_name,
            Self::service_subscription_options(),
            client,
        ) {
            bail!("Unable to subscribe to {:?}({:?}): {:?}", service_name, name_local_domain, err);
        }

        let stream = server.into_stream().unwrap();

        let update_sender_clone = update_sender.clone();
        let future = stream
            .map_err(anyhow::Error::from)
            .try_for_each(move |event: ServiceSubscriptionListenerRequest| {
                let mut update_sender_clone = update_sender_clone.clone();
                async move {
                    update_sender_clone
                        .send(DnssdUpdate::Service(event))
                        .map_err(anyhow::Error::from)
                        .await
                }
            })
            .inspect_err(move |err| {
                // Due to fxbug.dev/99755, the subscription will close
                // if the servicesubscriber that created it is closed.
                // The bug tracking the specific issue this fixes is <b/241818894>.
                // TODO(fxbug.dev/99755): Remove this line once fxbug.dev/99755 is fixed.
                let _ = subscriber.clone();

                error!("service_subscription: {:?}", err);
            });

        subscriptions.lock().insert(name_srp_domain, fasync::Task::spawn(future));

        Ok(())
    }

    fn dnssd_subscribed_to_instance(
        name_local_domain: String,
        name_srp_domain: CString,
        subscriptions: &Arc<Mutex<HashMap<CString, Task<Result>>>>,
        update_sender: &mpsc::Sender<DnssdUpdate>,
    ) -> Result<(), anyhow::Error> {
        // Trim the `local.` part.
        let mut instance_name = name_local_domain.trim_end_matches(LOCAL_DOMAIN);

        // Remove the trailing `._udp.` or `._tcp.`
        if let Some(i) = instance_name.trim_end_matches('.').rfind("._") {
            instance_name = &instance_name[..i];
        }

        // Remove the trailing service name.
        if let Some(i) = instance_name.rfind("._") {
            instance_name = &instance_name[..i];
        }

        let service_name = &name_local_domain[instance_name.len() + 1..];

        // TODO: At the moment we are just subscribing to the whole service, since we don't yet
        //       have the API to just subscribe to a single service instance. The effect is largely
        //       the same.

        Self::dnssd_subscribed_to_service(
            service_name.to_string(),
            name_srp_domain,
            subscriptions,
            update_sender,
        )
    }

    fn dnssd_subscribed_to(
        name_local_domain: String,
        name_srp_domain: CString,
        subscriptions: &Arc<Mutex<HashMap<CString, Task<Result>>>>,
        update_sender: &mpsc::Sender<DnssdUpdate>,
    ) -> Result<(), anyhow::Error> {
        let is_this_a_service =
            name_local_domain.contains("._udp.") || name_local_domain.contains("._tcp.");
        let is_this_an_instance = is_this_a_service && !name_local_domain.starts_with('_');

        if is_this_an_instance {
            Self::dnssd_subscribed_to_instance(
                name_local_domain,
                name_srp_domain,
                subscriptions,
                update_sender,
            )
        } else if is_this_a_service {
            Self::dnssd_subscribed_to_service(
                name_local_domain,
                name_srp_domain,
                subscriptions,
                update_sender,
            )
        } else {
            Self::dnssd_subscribed_to_host(
                name_local_domain,
                name_srp_domain,
                subscriptions,
                update_sender,
            )
        }
    }

    fn handle_service_instance_update(
        instance: &ot::Instance,
        service_instance: ServiceInstance,
    ) -> Result {
        if let ServiceInstance {
            service: Some(service_name),
            instance: Some(instance_name),
            addresses: Some(addresses),
            text_strings,
            srv_priority: Some(srv_priority),
            srv_weight: Some(srv_weight),
            target: Some(host_name),
            ..
        } = service_instance
        {
            let srp_domain = instance.srp_server_get_domain().to_str()?;

            let instance_name_srp =
                CString::new(format!("{}.{}{}", instance_name, service_name, srp_domain))?;

            let service_name_srp = CString::new(format!("{}{}", service_name, srp_domain))?;

            let host_name_srp = CString::new(format!("{}.{}", host_name, srp_domain))?;

            let (addresses, port) = process_addresses_from_socket_addresses(addresses);

            if addresses.is_empty() {
                warn!(
                    "Unable to resolve {:?} to an IPv6 address for service {:?}.",
                    host_name, instance_name_srp
                );
            } else {
                debug!(
                    "Resolved {:?} to {:?} for service {:?}.",
                    host_name, addresses, instance_name_srp
                );

                instance.dnssd_query_handle_discovered_service_instance(
                    service_name_srp.as_c_str(),
                    &addresses,
                    instance_name_srp.as_c_str(),
                    host_name_srp.as_c_str(),
                    port.unwrap_or(0),
                    srv_priority,
                    DEFAULT_MDNS_TTL, // TODO(fxbug.dev/94352): Change when available.
                    &flatten_txt(text_strings),
                    srv_weight,
                );
            }
        } else {
            warn!("Unable to handle discovered service instance: {:?}", service_instance);
        }
        Ok(())
    }

    fn handle_service_subscription_listener_request(
        ot_instance: &ot::Instance,
        service_subscription_listener_request: ServiceSubscriptionListenerRequest,
    ) -> Result {
        match service_subscription_listener_request {
            // A DNS-SD IPv6 service instance has been discovered.
            ServiceSubscriptionListenerRequest::OnInstanceDiscovered {
                instance: service_instance,
                responder,
            } => {
                Self::handle_service_instance_update(ot_instance, service_instance)?;
                responder.send()?;
            }

            // A DNS-SD IPv6 service instance has changed.
            ServiceSubscriptionListenerRequest::OnInstanceChanged {
                instance: service_instance,
                responder,
            } => {
                Self::handle_service_instance_update(ot_instance, service_instance)?;
                responder.send()?;
            }

            // A DNS-SD IPv6 service instance has been lost.
            ServiceSubscriptionListenerRequest::OnInstanceLost {
                instance: service_instance,
                responder,
                ..
            } => {
                // TODO(fxbug.dev/94362): It is not entirely clear how to handle this case,
                //                        so for the time being we are ignoring it.
                info!("Ignoring loss of service instance {:?}", service_instance);

                responder.send()?;
            }

            ServiceSubscriptionListenerRequest::OnQuery { responder, .. } => {
                // We don't care about queries.
                responder.send()?;
            }
        }
        Ok(())
    }

    /// Async entrypoint. Called from [`DiscoveryProxyPollerExt::discovery_proxy_poll`].
    fn poll(&self, instance: &ot::Instance, cx: &mut Context<'_>) {
        while let Poll::Ready(ready) = self.update_receiver.lock().poll_next_unpin(cx) {
            match ready {
                Some(DnssdUpdate::Service(request)) => {
                    debug!("DnssdUpdate::Service: {:?}", request);
                    if let Err(err) =
                        Self::handle_service_subscription_listener_request(instance, request)
                    {
                        error!("handle_service_subscription_listener_request: {:?}", err);
                    }
                }
                Some(DnssdUpdate::Host { host_name, addresses, ttl }) => {
                    debug!("DnssdUpdate::Host: {:?}, {:?}, {}", host_name, addresses, ttl);
                    instance.dnssd_query_handle_discovered_host(&host_name, &addresses, ttl);
                }
                None => {
                    warn!("update_receiver stream has finished unexpectedly.");
                }
            }
        }

        self.subscriptions.lock().retain(|k, v| {
            if let Poll::Ready(ret) = v.poll_unpin(cx) {
                warn!("Subscription to {:?} has stopped unexpectedly: {:?}", k, ret);
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

#[cfg(test)]
mod test {
    use super::*;
    use std::net::Ipv6Addr;

    #[test]
    fn test_should_proxy_address() {
        assert!(should_proxy_address(&Ipv6Addr::new(0x2001, 0xdb8, 0, 0, 0, 0, 0, 1)));
        assert!(!should_proxy_address(&Ipv6Addr::new(0xfe80, 0xdb8, 0, 0, 0, 0, 0, 1)));
        assert!(!should_proxy_address(&Ipv6Addr::new(0xfe81, 0xdb8, 0, 0, 0, 0, 0, 1)));
        assert!(!should_proxy_address(&Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 1)));
        assert!(!should_proxy_address(&Ipv6Addr::new(0, 0, 0, 0, 0, 0, 0, 0)));
    }
}
