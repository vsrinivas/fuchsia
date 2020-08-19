// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    fidl_fuchsia_net::{IpAddress, Ipv4Address, Ipv6Address},
    fidl_fuchsia_netstack as fnetstack,
    futures::{stream::FusedStream, TryStreamExt},
};

// TODO(https://github.com/bitflags/bitflags/issues/180): replace this function with normal BitOr.
const fn const_bitor(left: fnetstack::Flags, right: fnetstack::Flags) -> fnetstack::Flags {
    fnetstack::Flags::from_bits_truncate(left.bits() | right.bits())
}

// Returns true iff the supplied `NetInterface` appears to provide network connectivity, i.e. is up,
// has DHCP, and has a non-zero IP address.
fn network_available(net_interface: fnetstack::NetInterface) -> bool {
    const REQUIRED_FLAGS: fnetstack::Flags =
        const_bitor(fnetstack::Flags::Up, fnetstack::Flags::Dhcp);
    let fnetstack::NetInterface { flags, addr, .. } = net_interface;
    if !flags.contains(REQUIRED_FLAGS) {
        return false;
    }
    match addr {
        IpAddress::Ipv4(Ipv4Address { addr }) => addr.iter().copied().any(|octet| octet != 0),
        IpAddress::Ipv6(Ipv6Address { addr }) => addr.iter().copied().any(|octet| octet != 0),
    }
}

/// Returns Ok once the netstack indicates Internet connectivity should be available.
pub async fn wait_for_network_available(
    mut netstack_events: fnetstack::NetstackEventStream,
) -> Result<(), Error> {
    while !netstack_events.is_terminated() {
        if let Some(fnetstack::NetstackEvent::OnInterfacesChanged { interfaces }) =
            netstack_events.try_next().await?
        {
            if interfaces.into_iter().any(network_available) {
                return Ok(());
            }
        }
    }
    Err(anyhow!("Stream terminated"))
}

// Functions to assist in the unit testing of other modules. Define outside ::test so our
// internal unit test doesn't need to be public.

#[cfg(test)]
fn create_interface(flags: fnetstack::Flags, addr: IpAddress) -> fnetstack::NetInterface {
    fnetstack::NetInterface {
        id: 1,
        flags,
        features: fidl_fuchsia_hardware_ethernet::Features::empty(),
        configuration: 0,
        name: "my little pony".to_string(),
        addr,
        netmask: IpAddress::Ipv4(Ipv4Address { addr: [0, 0, 0, 0] }),
        broadaddr: IpAddress::Ipv4(Ipv4Address { addr: [0, 0, 0, 0] }),
        ipv6addrs: vec![],
        hwaddr: vec![],
    }
}

#[cfg(test)]
fn create_event_service(
    interface_sets: Vec<Vec<fnetstack::NetInterface>>,
) -> fnetstack::NetstackProxy {
    let (netstack_service, netstack_server) =
        fidl::endpoints::create_proxy::<fnetstack::NetstackMarker>().unwrap();
    let (_, netstack_control) = netstack_server.into_stream_and_control_handle().unwrap();
    for mut interface_set in interface_sets {
        let () =
            netstack_control.send_on_interfaces_changed(&mut interface_set.iter_mut()).unwrap();
    }
    netstack_service
}

#[cfg(test)]
pub fn create_event_service_with_valid_interface() -> fnetstack::NetstackProxy {
    create_event_service(vec![vec![create_interface(
        const_bitor(fnetstack::Flags::Up, fnetstack::Flags::Dhcp),
        IpAddress::Ipv4(Ipv4Address { addr: [192, 168, 1, 2] }),
    )]])
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync};

    const EMPTY_IP_V4: IpAddress = IpAddress::Ipv4(Ipv4Address { addr: [0, 0, 0, 0] });
    const VALID_IP_V4: IpAddress = IpAddress::Ipv4(Ipv4Address { addr: [192, 168, 1, 1] });
    const EMPTY_IP_V6: IpAddress = IpAddress::Ipv6(Ipv6Address { addr: [0; 16] });
    const VALID_IP_V6: IpAddress = IpAddress::Ipv6(Ipv6Address { addr: [1; 16] });
    const VALID_FLAGS: fnetstack::Flags = const_bitor(fnetstack::Flags::Up, fnetstack::Flags::Dhcp);

    #[test]
    fn network_availability() {
        // All these combinations should not indicate an available network.
        assert!(!network_available(create_interface(fnetstack::Flags::empty(), VALID_IP_V4)));
        assert!(!network_available(create_interface(fnetstack::Flags::Up, VALID_IP_V4)));
        assert!(!network_available(create_interface(fnetstack::Flags::Dhcp, VALID_IP_V4)));
        assert!(!network_available(create_interface(VALID_FLAGS, EMPTY_IP_V4)));
        assert!(!network_available(create_interface(VALID_FLAGS, EMPTY_IP_V6)));
        // But these should indicate an available network.
        assert!(network_available(create_interface(VALID_FLAGS, VALID_IP_V4)));
        assert!(network_available(create_interface(VALID_FLAGS, VALID_IP_V6)));
    }

    #[fasync::run_until_stalled(test)]
    #[should_panic]
    async fn wait_for_network_available_timeout() {
        // Create a server and send a first event
        let (service, server) =
            fidl::endpoints::create_proxy::<fnetstack::NetstackMarker>().unwrap();
        let mut interfaces = vec![create_interface(fnetstack::Flags::empty(), EMPTY_IP_V4)];
        let (_, netstack_control) = server.into_stream_and_control_handle().unwrap();
        netstack_control.send_on_interfaces_changed(&mut interfaces.iter_mut()).unwrap();
        // Swallow the wait_for_network_available result, if it returns either a success or an error
        // this method should complete successfully causing the test to fail.
        let _ = wait_for_network_available(service.take_event_stream()).await;
        // Send a second event before returning our future, this ensures wait_for_network_available
        // cannot complete immediately so should result in the test stalling.
        netstack_control.send_on_interfaces_changed(&mut interfaces.iter_mut()).unwrap();
    }

    #[fasync::run_until_stalled(test)]
    async fn wait_for_network_available_failure() {
        // Create a server and then immediately close the channel
        let (service, server) =
            fidl::endpoints::create_proxy::<fnetstack::NetstackMarker>().unwrap();
        drop(server);
        wait_for_network_available(service.take_event_stream())
            .await
            .expect_err("Wait for network available should have returned Err");
    }

    #[fasync::run_until_stalled(test)]
    async fn wait_for_network_available_success() {
        // Send four events, which get successively better until the last is sufficient.
        let netstack_service = create_event_service(vec![
            vec![],
            vec![create_interface(VALID_FLAGS, EMPTY_IP_V6)],
            vec![
                create_interface(VALID_FLAGS, EMPTY_IP_V6),
                create_interface(fnetstack::Flags::empty(), VALID_IP_V4),
            ],
            vec![
                create_interface(VALID_FLAGS, EMPTY_IP_V6),
                create_interface(VALID_FLAGS, VALID_IP_V4),
            ],
        ]);
        wait_for_network_available(netstack_service.take_event_stream())
            .await
            .expect("Wait for network available should have returned Ok");
    }
}
