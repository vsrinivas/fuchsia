// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fidl_fuchsia_net_interfaces as finterfaces, std::collections::HashMap};

// Returns true iff the supplied [`finterfaces::Properties`] (expected to be fully populated)
// appears to provide network connectivity, i.e. is not loopback, is online, has an IP address, and
// has default route (v4 or v6).
fn network_available(
    finterfaces::Properties {
        device_class,
        online,
        addresses,
        has_default_ipv4_route,
        has_default_ipv6_route,
        ..
    }: &finterfaces::Properties,
) -> bool {
    return device_class.is_some()
        && *device_class != Some(finterfaces::DeviceClass::Loopback(finterfaces::Empty {}))
        && *online == Some(true)
        && addresses.as_ref().map_or(false, |addresses| !addresses.is_empty())
        && (*has_default_ipv4_route == Some(true) || *has_default_ipv6_route == Some(true));
}

// Initialize an interface watcher and return its events as a stream.
pub(crate) fn event_stream(
    interfaces_state: &finterfaces::StateProxy,
) -> Result<impl futures::Stream<Item = Result<finterfaces::Event, Error>>, Error> {
    fidl_fuchsia_net_interfaces_ext::event_stream_from_state(interfaces_state)
}

// Returns Ok once the netstack indicates Internet connectivity should be available.
pub(crate) async fn wait_for_network_available<S>(stream: S) -> Result<(), Error>
where
    S: futures::Stream<Item = Result<finterfaces::Event, Error>>,
{
    fidl_fuchsia_net_interfaces_ext::wait_interface(stream, &mut HashMap::new(), |if_map| {
        if if_map.values().any(network_available) {
            Some(())
        } else {
            None
        }
    })
    .await
}

// Functions to assist in the unit testing of other modules. Define outside ::test so our
// internal unit test doesn't need to be public.

#[cfg(test)]
pub(crate) fn create_stream_with_valid_interface(
) -> impl futures::Stream<Item = Result<finterfaces::Event, Error>> {
    futures::stream::once(futures::future::ok(finterfaces::Event::Existing(
        tests::valid_interface(),
    )))
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_hardware_network as fnetwork, fidl_fuchsia_net as fnet,
        net_declare::fidl_ip,
    };

    pub(crate) fn valid_interface() -> finterfaces::Properties {
        finterfaces::Properties {
            id: Some(1),
            name: Some("test1".to_string()),
            device_class: Some(finterfaces::DeviceClass::Device(fnetwork::DeviceClass::Ethernet)),
            online: Some(true),
            addresses: Some(vec![finterfaces::Address {
                addr: Some(fnet::Subnet { addr: fidl_ip!(192.168.0.1), prefix_len: 16 }),
            }]),
            has_default_ipv4_route: Some(true),
            has_default_ipv6_route: Some(true),
        }
    }

    #[test]
    fn network_availability() {
        // All these combinations should not indicate an available network.
        assert!(!network_available(&finterfaces::Properties {
            device_class: None,
            ..valid_interface()
        }));
        assert!(!network_available(&finterfaces::Properties {
            device_class: Some(finterfaces::DeviceClass::Loopback(finterfaces::Empty {})),
            ..valid_interface()
        }));
        assert!(!network_available(&finterfaces::Properties {
            online: Some(false),
            ..valid_interface()
        }));
        assert!(!network_available(&finterfaces::Properties {
            addresses: Some(vec![]),
            ..valid_interface()
        }));
        assert!(!network_available(&finterfaces::Properties {
            has_default_ipv4_route: Some(false),
            has_default_ipv6_route: Some(false),
            ..valid_interface()
        }));
        // But this should indicate an available network.
        assert!(network_available(&valid_interface()));
    }
}
