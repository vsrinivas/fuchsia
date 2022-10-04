// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! The Neighbor Discovery Protocol (NDP).
//!
//! Neighbor Discovery for IPv6 as defined in [RFC 4861] defines mechanisms for
//! solving the following problems:
//! - Router Discovery
//! - Prefix Discovery
//! - Parameter Discovery
//! - Address Autoconfiguration
//! - Address resolution
//! - Next-hop determination
//! - Neighbor Unreachability Detection
//! - Duplicate Address Detection
//! - Redirect
//!
//! [RFC 4861]: https://tools.ietf.org/html/rfc4861

#[cfg(test)]
mod tests {
    use alloc::{collections::HashSet, vec, vec::Vec};
    use core::{
        convert::{TryFrom, TryInto as _},
        fmt::Debug,
        num::NonZeroU8,
        time::Duration,
    };

    use assert_matches::assert_matches;
    use log::trace;
    use net_declare::net::{mac, subnet_v6};
    use net_types::{
        ethernet::Mac,
        ip::{AddrSubnet, Ip as _, Ipv6, Ipv6Addr, Subnet},
        UnicastAddr, Witness as _,
    };
    use nonzero_ext::nonzero;
    use packet::{Buf, EmptyBuf, InnerPacketBuilder as _, Serializer as _};
    use packet_formats::{
        icmp::{
            ndp::{
                options::{NdpOption, NdpOptionBuilder, PrefixInformation},
                NdpPacket, NeighborAdvertisement, OptionSequenceBuilder, Options,
                RouterAdvertisement, RouterSolicitation,
            },
            IcmpEchoRequest, IcmpPacketBuilder, IcmpUnusedCode, Icmpv6Packet,
        },
        ip::{IpProto, Ipv6Proto},
        ipv6::Ipv6PacketBuilder,
        testutil::{parse_ethernet_frame, parse_icmp_packet_in_ip_packet_in_ethernet_frame},
        utils::NonZeroDuration,
    };
    use rand::RngCore;
    use zerocopy::ByteSlice;

    use crate::{
        algorithm::{
            generate_opaque_interface_identifier, OpaqueIidNonce, STABLE_IID_SECRET_KEY_BYTES,
        },
        context::{
            testutil::{
                handle_timer_helper_with_sc_ref, DummyInstant, DummyTimerCtxExt as _, StepResult,
            },
            InstantContext as _, RngContext as _, TimerContext,
        },
        device::{
            add_ip_addr_subnet, del_ip_addr, link::LinkAddress, testutil::receive_frame_or_panic,
            DeviceId, DeviceIdInner, EthernetDeviceId, FrameDestination,
        },
        ip::{
            device::{
                get_ipv6_hop_limit, is_ip_routing_enabled,
                router_solicitation::{MAX_RTR_SOLICITATION_DELAY, RTR_SOLICITATION_INTERVAL},
                set_ipv6_routing_enabled,
                slaac::{SlaacConfiguration, SlaacTimerId, TemporarySlaacAddressConfiguration},
                state::{
                    AddrConfig, AddressState, Ipv6AddressEntry, Ipv6DeviceConfiguration, Lifetime,
                    SlaacConfig, TemporarySlaacConfig,
                },
                testutil::{get_global_ipv6_addrs, with_assigned_ipv6_addr_subnets},
                Ipv6DeviceHandler, Ipv6DeviceTimerId,
            },
            receive_ipv6_packet,
            testutil::is_in_ip_multicast,
            SendIpPacketMeta,
        },
        testutil::{
            assert_empty, get_counter_val, handle_timer, set_logger_for_test,
            DummyEventDispatcherBuilder, TestIpExt, DUMMY_CONFIG_V6,
        },
        Ctx, Instant, StackStateBuilder, TimerId, TimerIdInner,
    };

    const REQUIRED_NDP_IP_PACKET_HOP_LIMIT: u8 = 255;

    // TODO(https://github.com/rust-lang/rust/issues/67441): Make these constants once const
    // Option::unwrap is stablized
    fn local_mac() -> UnicastAddr<Mac> {
        UnicastAddr::new(Mac::new([0, 1, 2, 3, 4, 5])).unwrap()
    }

    fn remote_mac() -> UnicastAddr<Mac> {
        UnicastAddr::new(Mac::new([6, 7, 8, 9, 10, 11])).unwrap()
    }

    fn local_ip() -> UnicastAddr<Ipv6Addr> {
        UnicastAddr::from_witness(DUMMY_CONFIG_V6.local_ip).unwrap()
    }

    fn remote_ip() -> UnicastAddr<Ipv6Addr> {
        UnicastAddr::from_witness(DUMMY_CONFIG_V6.remote_ip).unwrap()
    }

    fn neighbor_advertisement_message(
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
        router_flag: bool,
        solicited_flag: bool,
        override_flag: bool,
        mac: Option<Mac>,
    ) -> Buf<Vec<u8>> {
        let mac = mac.map(|x| x.bytes());

        let mut options = Vec::new();

        if let Some(ref mac) = mac {
            options.push(NdpOptionBuilder::TargetLinkLayerAddress(mac));
        }

        OptionSequenceBuilder::new(options.iter())
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                NeighborAdvertisement::new(router_flag, solicited_flag, override_flag, src_ip),
            ))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_b()
    }

    impl TryFrom<DeviceId> for EthernetDeviceId {
        type Error = DeviceId;
        fn try_from(id: DeviceId) -> Result<EthernetDeviceId, DeviceId> {
            match id.inner() {
                DeviceIdInner::Ethernet(id) => Ok(*id),
                DeviceIdInner::Loopback(_) => Err(id),
            }
        }
    }

    #[test]
    fn test_address_resolution() {
        set_logger_for_test();
        let mut local = DummyEventDispatcherBuilder::default();
        assert_eq!(local.add_device(local_mac()), 0);
        let mut remote = DummyEventDispatcherBuilder::default();
        assert_eq!(remote.add_device(remote_mac()), 0);
        let device_id = DeviceId::new_ethernet(0);

        let mut net = crate::context::testutil::new_legacy_simple_dummy_network(
            "local",
            local.build(),
            "remote",
            remote.build(),
        );

        // Let's try to ping the remote device from the local device:
        let req = IcmpEchoRequest::new(0, 0);
        let req_body = &[1, 2, 3, 4];
        let body = Buf::new(req_body.to_vec(), ..).encapsulate(
            IcmpPacketBuilder::<Ipv6, &[u8], _>::new(local_ip(), remote_ip(), IcmpUnusedCode, req),
        );
        // Manually assigning the addresses.
        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            add_ip_addr_subnet(
                sync_ctx,
                non_sync_ctx,
                &device_id,
                AddrSubnet::new(remote_ip().get(), 128).unwrap(),
            )
            .unwrap();

            assert_empty(non_sync_ctx.frames_sent());
        });
        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            add_ip_addr_subnet(
                sync_ctx,
                non_sync_ctx,
                &device_id,
                AddrSubnet::new(local_ip().get(), 128).unwrap(),
            )
            .unwrap();

            assert_empty(non_sync_ctx.frames_sent());

            crate::ip::send_ipv6_packet_from_device(
                &mut &*sync_ctx,
                non_sync_ctx,
                SendIpPacketMeta {
                    device: device_id,
                    src_ip: Some(local_ip().into_specified()),
                    dst_ip: remote_ip().into_specified(),
                    next_hop: remote_ip().into_specified(),
                    proto: Ipv6Proto::Icmpv6,
                    ttl: None,
                    mtu: None,
                },
                body,
            )
            .unwrap();
            // This should've triggered a neighbor solicitation to come out of
            // local.
            assert_eq!(non_sync_ctx.frames_sent().len(), 1);
            // A timer should've been started.
            assert_eq!(non_sync_ctx.timer_ctx().timers().len(), 1);
        });

        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);
        assert_eq!(
            get_counter_val(net.non_sync_ctx("remote"), "ndp::rx_neighbor_solicitation"),
            1,
            "remote received solicitation"
        );
        assert_eq!(net.non_sync_ctx("remote").frames_sent().len(), 1);

        // Forward advertisement response back to local.
        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);

        assert_eq!(
            get_counter_val(net.non_sync_ctx("local"), "ndp::rx_neighbor_advertisement"),
            1,
            "local received advertisement"
        );

        // The local timer should've been unscheduled.
        net.with_context("local", |Ctx { sync_ctx: _, non_sync_ctx }| {
            assert_empty(non_sync_ctx.timer_ctx().timers());

            // Upon link layer resolution, the original ping request should've been
            // sent out.
            assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        });
        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);
        assert_eq!(
            get_counter_val(net.non_sync_ctx("remote"), "<IcmpIpTransportContext as BufferIpTransportContext<Ipv6>>::receive_ip_packet::echo_request"),
            1
        );

        // TODO(brunodalbo): We should be able to verify that remote also sends
        //  back an echo reply, but we're having some trouble with IPv6 link
        //  local addresses.
    }

    #[test]
    fn test_dad_duplicate_address_detected_solicitation() {
        // Tests whether a duplicate address will get detected by solicitation
        // In this test, two nodes having the same MAC address will come up at
        // the same time. And both of them will use the EUI address. Each of
        // them should be able to detect each other is using the same address,
        // so they will both give up using that address.
        set_logger_for_test();
        let mac = UnicastAddr::new(Mac::new([6, 5, 4, 3, 2, 1])).unwrap();
        let ll_addr: Ipv6Addr = mac.to_ipv6_link_local().addr().get();
        let multicast_addr = ll_addr.to_solicited_node_address();
        let local = DummyEventDispatcherBuilder::default();
        let remote = DummyEventDispatcherBuilder::default();
        let device_id = DeviceId::new_ethernet(0);

        let stack_builder = StackStateBuilder::default();
        let mut net = crate::context::testutil::new_legacy_simple_dummy_network(
            "local",
            local.build_with(stack_builder.clone()),
            "remote",
            remote.build_with(stack_builder),
        );

        // Create the devices (will start DAD at the same time).
        let update = |ipv6_config: &mut Ipv6DeviceConfiguration| {
            ipv6_config.ip_config.ip_enabled = true;

            // Doesn't matter as long as we perform DAD.
            ipv6_config.dad_transmits = NonZeroU8::new(1);
        };
        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            assert_eq!(
                crate::device::add_ethernet_device(
                    sync_ctx,
                    non_sync_ctx,
                    mac,
                    Ipv6::MINIMUM_LINK_MTU.into(),
                ),
                device_id
            );
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, &device_id, update);
            assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        });
        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            assert_eq!(
                crate::device::add_ethernet_device(
                    sync_ctx,
                    non_sync_ctx,
                    mac,
                    Ipv6::MINIMUM_LINK_MTU.into(),
                ),
                device_id
            );
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, &device_id, update);
            assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        });

        // Both devices should be in the solicited-node multicast group.
        assert!(is_in_ip_multicast(net.sync_ctx("local"), &device_id, multicast_addr));
        assert!(is_in_ip_multicast(net.sync_ctx("remote"), &device_id, multicast_addr));

        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);

        // They should now realize the address they intend to use has a
        // duplicate in the local network.
        with_assigned_ipv6_addr_subnets(&&*net.sync_ctx("local"), &device_id, |addrs| {
            assert_empty(addrs)
        });
        with_assigned_ipv6_addr_subnets(&&*net.sync_ctx("remote"), &device_id, |addrs| {
            assert_empty(addrs)
        });

        // Both devices should not be in the multicast group
        assert!(!is_in_ip_multicast(&&*net.sync_ctx("local"), &device_id, multicast_addr));
        assert!(!is_in_ip_multicast(&&*net.sync_ctx("remote"), &device_id, multicast_addr));
    }

    fn dad_timer_id(id: EthernetDeviceId, addr: UnicastAddr<Ipv6Addr>) -> TimerId {
        TimerId(TimerIdInner::Ipv6Device(Ipv6DeviceTimerId::Dad(
            crate::ip::device::dad::DadTimerId { device_id: id.into(), addr },
        )))
    }

    fn rs_timer_id(id: EthernetDeviceId) -> TimerId {
        TimerId(TimerIdInner::Ipv6Device(Ipv6DeviceTimerId::Rs(
            crate::ip::device::router_solicitation::RsTimerId { device_id: id.into() },
        )))
    }

    #[test]
    fn test_dad_duplicate_address_detected_advertisement() {
        // Tests whether a duplicate address will get detected by advertisement
        // In this test, one of the node first assigned itself the local_ip(),
        // then the second node comes up and it should be able to find out that
        // it cannot use the address because someone else has already taken that
        // address.
        set_logger_for_test();
        let mut local = DummyEventDispatcherBuilder::default();
        assert_eq!(local.add_device(local_mac()), 0);
        let mut remote = DummyEventDispatcherBuilder::default();
        assert_eq!(remote.add_device(remote_mac()), 0);
        let device_id = DeviceId::new_ethernet(0);
        let eth_device_id = device_id.clone().try_into().expect("expected ethernet ID");

        let mut net = crate::context::testutil::new_legacy_simple_dummy_network(
            "local",
            local.build(),
            "remote",
            remote.build(),
        );

        // Enable DAD.
        let update = |ipv6_config: &mut Ipv6DeviceConfiguration| {
            ipv6_config.ip_config.ip_enabled = true;

            // Doesn't matter as long as we perform DAD.
            ipv6_config.dad_transmits = NonZeroU8::new(1);
        };
        let addr = AddrSubnet::new(local_ip().get(), 128).unwrap();
        let multicast_addr = local_ip().to_solicited_node_address();
        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, &device_id, update);
            add_ip_addr_subnet(sync_ctx, non_sync_ctx, &device_id, addr).unwrap();
        });
        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, &device_id, update);
        });

        // Only local should be in the solicited node multicast group.
        assert!(is_in_ip_multicast(net.sync_ctx("local"), &device_id, multicast_addr));
        assert!(!is_in_ip_multicast(net.sync_ctx("remote"), &device_id, multicast_addr));

        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&*sync_ctx, crate::handle_timer).unwrap(),
                dad_timer_id(eth_device_id, local_ip())
            );
        });

        assert_eq!(
            get_address_state(&&*net.sync_ctx("local"), &device_id, local_ip()),
            Some(AddressState::Assigned)
        );

        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            add_ip_addr_subnet(sync_ctx, non_sync_ctx, &device_id, addr).unwrap();
        });
        // Local & remote should be in the multicast group.
        assert!(is_in_ip_multicast(net.sync_ctx("local"), &device_id, multicast_addr));
        assert!(is_in_ip_multicast(net.sync_ctx("remote"), &device_id, multicast_addr));

        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);

        assert_eq!(
            with_assigned_ipv6_addr_subnets(&&*net.sync_ctx("remote"), &device_id, |addrs| addrs
                .count()),
            1
        );

        // Let's make sure that our local node still can use that address.
        assert_eq!(
            get_address_state(&&*net.sync_ctx("local"), &device_id, local_ip()),
            Some(AddressState::Assigned)
        );

        // Only local should be in the solicited node multicast group.
        assert!(is_in_ip_multicast(net.sync_ctx("local"), &device_id, multicast_addr));
        assert!(!is_in_ip_multicast(net.sync_ctx("remote"), &device_id, multicast_addr));
    }

    #[test]
    fn test_dad_set_ipv6_address_when_ongoing() {
        // Test that we can make our tentative address change when DAD is
        // ongoing.

        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let dev_id = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac(),
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &dev_id,
            |config| {
                config.ip_config.ip_enabled = true;
                config.dad_transmits = NonZeroU8::new(1);
            },
        );
        let addr = local_ip();
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &dev_id,
            AddrSubnet::new(addr.get(), 128).unwrap(),
        )
        .unwrap();
        assert_eq!(
            get_address_state(&sync_ctx, &dev_id, addr,),
            Some(AddressState::Tentative { dad_transmits_remaining: None })
        );

        let addr = remote_ip();
        assert_eq!(get_address_state(&sync_ctx, &dev_id, addr,), None,);
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &dev_id,
            AddrSubnet::new(addr.get(), 128).unwrap(),
        )
        .unwrap();
        assert_eq!(
            get_address_state(&sync_ctx, &dev_id, addr,),
            Some(AddressState::Tentative { dad_transmits_remaining: None })
        );
    }

    #[test]
    fn test_dad_three_transmits_no_conflicts() {
        let Ctx { sync_ctx, mut non_sync_ctx } = crate::testutil::DummyCtx::default();
        let mut sync_ctx = &sync_ctx;
        let dev_id = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac(),
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        let eth_dev_id = dev_id.clone().try_into().expect("expected ethernet ID");
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, &dev_id);

        // Enable DAD.
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &dev_id,
            |config| {
                config.ip_config.ip_enabled = true;
                config.dad_transmits = NonZeroU8::new(3);
            },
        );
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &dev_id,
            AddrSubnet::new(local_ip().get(), 128).unwrap(),
        )
        .unwrap();
        for _ in 0..3 {
            assert_eq!(
                non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
                dad_timer_id(eth_dev_id, local_ip())
            );
        }
        assert_eq!(
            get_address_state(&sync_ctx, &dev_id, local_ip(),),
            Some(AddressState::Assigned)
        );
    }

    #[test]
    fn test_dad_three_transmits_with_conflicts() {
        // Test if the implementation is correct when we have more than 1 NS
        // packets to send.
        set_logger_for_test();
        let mac = UnicastAddr::new(Mac::new([6, 5, 4, 3, 2, 1])).unwrap();
        let mut local = DummyEventDispatcherBuilder::default();
        assert_eq!(local.add_device(mac), 0);
        let mut remote = DummyEventDispatcherBuilder::default();
        assert_eq!(remote.add_device(mac), 0);
        let device_id = DeviceId::new_ethernet(0);
        let eth_device_id = device_id.clone().try_into().expect("expected ethernet ID");
        let mut net = crate::context::testutil::new_legacy_simple_dummy_network(
            "local",
            local.build(),
            "remote",
            remote.build(),
        );

        let update = |ipv6_config: &mut Ipv6DeviceConfiguration| {
            ipv6_config.ip_config.ip_enabled = true;
            ipv6_config.dad_transmits = NonZeroU8::new(3);
        };
        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, &device_id, update);

            add_ip_addr_subnet(
                sync_ctx,
                non_sync_ctx,
                &device_id,
                AddrSubnet::new(local_ip().get(), 128).unwrap(),
            )
            .unwrap();
        });
        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            crate::device::update_ipv6_configuration(sync_ctx, non_sync_ctx, &device_id, update);
        });

        let expected_timer_id = dad_timer_id(eth_device_id, local_ip());
        // During the first and second period, the remote host is still down.
        net.with_context("local", |Ctx { sync_ctx, non_sync_ctx }| {
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&*sync_ctx, crate::handle_timer).unwrap(),
                expected_timer_id
            );
            assert_eq!(
                non_sync_ctx.trigger_next_timer(&*sync_ctx, crate::handle_timer).unwrap(),
                expected_timer_id
            );
        });
        net.with_context("remote", |Ctx { sync_ctx, non_sync_ctx }| {
            add_ip_addr_subnet(
                sync_ctx,
                non_sync_ctx,
                &device_id,
                AddrSubnet::new(local_ip().get(), 128).unwrap(),
            )
            .unwrap();
        });
        // The local host should have sent out 3 packets while the remote one
        // should only have sent out 1.
        assert_eq!(net.non_sync_ctx("local").frames_sent().len(), 3);
        assert_eq!(net.non_sync_ctx("remote").frames_sent().len(), 1);

        let _: StepResult = net.step(receive_frame_or_panic, handle_timer);

        // Let's make sure that all timers are cancelled properly.
        net.with_context("local", |Ctx { sync_ctx: _, non_sync_ctx }| {
            assert_empty(non_sync_ctx.timer_ctx().timers());
        });
        net.with_context("remote", |Ctx { sync_ctx: _, non_sync_ctx }| {
            assert_empty(non_sync_ctx.timer_ctx().timers());
        });

        // They should now realize the address they intend to use has a
        // duplicate in the local network.
        assert_eq!(
            with_assigned_ipv6_addr_subnets(&&*net.sync_ctx("local"), &device_id, |a| a.count()),
            1
        );
        assert_eq!(
            with_assigned_ipv6_addr_subnets(&&*net.sync_ctx("remote"), &device_id, |a| a.count()),
            1
        );
    }

    fn get_address_state(
        sync_ctx: &&crate::testutil::DummySyncCtx,
        device: &DeviceId,
        addr: UnicastAddr<Ipv6Addr>,
    ) -> Option<AddressState> {
        crate::ip::device::IpDeviceContext::<Ipv6, _>::with_ip_device_state(
            sync_ctx,
            device,
            |state| state.ip_state.find_addr(&addr).map(|a| a.state),
        )
    }

    #[test]
    fn test_dad_multiple_ips_simultaneously() {
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let dev_id = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac(),
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        let eth_dev_id = dev_id.clone().try_into().expect("expected ethernet ID");
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, &dev_id);

        assert_empty(non_sync_ctx.frames_sent());

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &dev_id,
            |ipv6_config| {
                ipv6_config.ip_config.ip_enabled = true;
                ipv6_config.dad_transmits = NonZeroU8::new(3);
                ipv6_config.max_router_solicitations = None;
            },
        );

        // Add an IP.
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &dev_id,
            AddrSubnet::new(local_ip().get(), 128).unwrap(),
        )
        .unwrap();
        assert_matches!(
            get_address_state(&sync_ctx, &dev_id, local_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);

        // Send another NS.
        let local_timer_id = dad_timer_id(eth_dev_id, local_ip());
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                Duration::from_secs(1),
                handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer)
            ),
            [local_timer_id.clone()]
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);

        // Add another IP
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &dev_id,
            AddrSubnet::new(remote_ip().get(), 128).unwrap(),
        )
        .unwrap();
        assert_matches!(
            get_address_state(&sync_ctx, &dev_id, local_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_matches!(
            get_address_state(&sync_ctx, &dev_id, remote_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 3);

        // Run to the end for DAD for local ip
        let remote_timer_id = dad_timer_id(eth_dev_id, remote_ip());
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                Duration::from_secs(2),
                handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer)
            ),
            [
                local_timer_id.clone(),
                remote_timer_id.clone(),
                local_timer_id.clone(),
                remote_timer_id.clone()
            ]
        );
        assert_eq!(get_address_state(&sync_ctx, &dev_id, local_ip()), Some(AddressState::Assigned));
        assert_matches!(
            get_address_state(&sync_ctx, &dev_id, remote_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 6);

        // Run to the end for DAD for local ip
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                Duration::from_secs(1),
                handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer)
            ),
            [remote_timer_id]
        );
        assert_eq!(get_address_state(&sync_ctx, &dev_id, local_ip()), Some(AddressState::Assigned));
        assert_eq!(
            get_address_state(&sync_ctx, &dev_id, remote_ip()),
            Some(AddressState::Assigned)
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 6);

        // No more timers.
        assert_eq!(non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer), None);
    }

    #[test]
    fn test_dad_cancel_when_ip_removed() {
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let dev_id = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac(),
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        let eth_dev_id = dev_id.clone().try_into().expect("expected ethernet ID");
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, &dev_id);

        // Enable DAD.
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &dev_id,
            |ipv6_config| {
                ipv6_config.ip_config.ip_enabled = true;
                ipv6_config.dad_transmits = NonZeroU8::new(3);
                ipv6_config.max_router_solicitations = None;
            },
        );

        assert_empty(non_sync_ctx.frames_sent());

        // Add an IP.
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &dev_id,
            AddrSubnet::new(local_ip().get(), 128).unwrap(),
        )
        .unwrap();
        assert_matches!(
            get_address_state(&sync_ctx, &dev_id, local_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);

        // Send another NS.
        let local_timer_id = dad_timer_id(eth_dev_id, local_ip());
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                Duration::from_secs(1),
                handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer)
            ),
            [local_timer_id.clone()]
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);

        // Add another IP
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &dev_id,
            AddrSubnet::new(remote_ip().get(), 128).unwrap(),
        )
        .unwrap();
        assert_matches!(
            get_address_state(&sync_ctx, &dev_id, local_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_matches!(
            get_address_state(&sync_ctx, &dev_id, remote_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 3);

        // Run 1s
        let remote_timer_id = dad_timer_id(eth_dev_id, remote_ip());
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                Duration::from_secs(1),
                handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer)
            ),
            [local_timer_id, remote_timer_id.clone()]
        );
        assert_matches!(
            get_address_state(&sync_ctx, &dev_id, local_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_matches!(
            get_address_state(&sync_ctx, &dev_id, remote_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 5);

        // Remove local ip
        del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, &dev_id, &local_ip().into_specified())
            .unwrap();
        assert_eq!(get_address_state(&sync_ctx, &dev_id, local_ip()), None);
        assert_matches!(
            get_address_state(&sync_ctx, &dev_id, remote_ip()),
            Some(AddressState::Tentative { dad_transmits_remaining: _ })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 5);

        // Run to the end for DAD for local ip
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                Duration::from_secs(2),
                handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer)
            ),
            [remote_timer_id.clone(), remote_timer_id]
        );
        assert_eq!(get_address_state(&sync_ctx, &dev_id, local_ip()), None);
        assert_eq!(
            get_address_state(&sync_ctx, &dev_id, remote_ip()),
            Some(AddressState::Assigned)
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 6);

        // No more timers.
        assert_eq!(non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer), None);
    }

    trait UnwrapNdp<B: ByteSlice> {
        fn unwrap_ndp(self) -> NdpPacket<B>;
    }

    impl<B: ByteSlice> UnwrapNdp<B> for Icmpv6Packet<B> {
        fn unwrap_ndp(self) -> NdpPacket<B> {
            match self {
                Icmpv6Packet::Ndp(ndp) => ndp,
                _ => unreachable!(),
            }
        }
    }

    #[test]
    fn test_receiving_router_solicitation_validity_check() {
        let config = Ipv6::DUMMY_CONFIG;
        let src_ip = Ipv6Addr::from([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 10]);
        let src_mac = [10, 11, 12, 13, 14, 15];
        let options = vec![NdpOptionBuilder::SourceLinkLayerAddress(&src_mac[..])];

        // Test receiving NDP RS when not a router (should not receive)

        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(config.clone()).build();
        let mut sync_ctx = &sync_ctx;
        let device_id = DeviceId::new_ethernet(0);

        let icmpv6_packet_buf = OptionSequenceBuilder::new(options.iter())
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
                IcmpUnusedCode,
                RouterSolicitation::default(),
            ))
            .encapsulate(Ipv6PacketBuilder::new(
                src_ip,
                Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
                REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
                Ipv6Proto::Icmpv6,
            ))
            .serialize_vec_outer()
            .unwrap();
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            FrameDestination::Multicast,
            icmpv6_packet_buf,
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_solicitation"), 0);
    }

    #[test]
    fn test_receiving_router_advertisement_validity_check() {
        fn router_advertisement_message(src_ip: Ipv6Addr, dst_ip: Ipv6Addr) -> Buf<Vec<u8>> {
            EmptyBuf
                .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                    src_ip,
                    dst_ip,
                    IcmpUnusedCode,
                    RouterAdvertisement::new(
                        0,     /* current_hop_limit */
                        false, /* managed_flag */
                        false, /* other_config_flag */
                        0,     /* router_lifetime */
                        0,     /* reachable_time */
                        0,     /* retransmit_timer */
                    ),
                ))
                .encapsulate(Ipv6PacketBuilder::new(
                    src_ip,
                    dst_ip,
                    REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
                    Ipv6Proto::Icmpv6,
                ))
                .serialize_vec_outer()
                .unwrap()
                .unwrap_b()
        }

        let config = Ipv6::DUMMY_CONFIG;
        let src_mac = [10, 11, 12, 13, 14, 15];
        let src_ip = Ipv6Addr::from([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 192, 168, 0, 10]);
        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(config.clone()).build();
        let mut sync_ctx = &sync_ctx;
        let device_id = DeviceId::new_ethernet(0);

        // Test receiving NDP RA where source IP is not a link local address
        // (should not receive).

        let icmpv6_packet_buf = router_advertisement_message(src_ip.into(), config.local_ip.get());
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            FrameDestination::Unicast,
            icmpv6_packet_buf,
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 0);

        // Test receiving NDP RA where source IP is a link local address (should
        // receive).

        let src_ip = Mac::new(src_mac).to_ipv6_link_local().addr().get();
        let icmpv6_packet_buf = router_advertisement_message(src_ip, config.local_ip.get());
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            FrameDestination::Unicast,
            icmpv6_packet_buf,
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 1);
    }

    #[test]
    fn test_sending_ipv6_packet_after_hop_limit_change() {
        // Sets the hop limit with a router advertisement and sends a packet to
        // make sure the packet uses the new hop limit.
        fn inner_test(
            sync_ctx: &mut &crate::testutil::DummySyncCtx,
            ctx: &mut crate::testutil::DummyNonSyncCtx,
            hop_limit: u8,
            frame_offset: usize,
        ) {
            let config = Ipv6::DUMMY_CONFIG;
            let device_id = DeviceId::new_ethernet(0);
            let src_ip = config.remote_mac.to_ipv6_link_local().addr();
            let src_ip: Ipv6Addr = src_ip.get();

            let icmpv6_packet_buf = EmptyBuf
                .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                    src_ip,
                    Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
                    IcmpUnusedCode,
                    RouterAdvertisement::new(hop_limit, false, false, 0, 0, 0),
                ))
                .encapsulate(Ipv6PacketBuilder::new(
                    src_ip,
                    Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
                    REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
                    Ipv6Proto::Icmpv6,
                ))
                .serialize_vec_outer()
                .unwrap()
                .unwrap_b();
            receive_ipv6_packet(
                sync_ctx,
                ctx,
                &device_id,
                FrameDestination::Multicast,
                icmpv6_packet_buf,
            );
            assert_eq!(get_ipv6_hop_limit(sync_ctx, &device_id).get(), hop_limit);
            crate::ip::send_ipv6_packet_from_device(
                sync_ctx,
                ctx,
                SendIpPacketMeta {
                    device: device_id,
                    src_ip: Some(config.local_ip),
                    dst_ip: config.remote_ip,
                    next_hop: config.remote_ip,
                    proto: IpProto::Tcp.into(),
                    ttl: None,
                    mtu: None,
                },
                Buf::new(vec![0; 10], ..),
            )
            .unwrap();
            let (buf, _, _, _) =
                parse_ethernet_frame(&ctx.frames_sent()[frame_offset].1[..]).unwrap();
            // Packet's hop limit should be 100.
            assert_eq!(buf[7], hop_limit);
        }

        let Ctx { sync_ctx, mut non_sync_ctx } =
            DummyEventDispatcherBuilder::from_config(Ipv6::DUMMY_CONFIG).build();
        let mut sync_ctx = &sync_ctx;

        // Set hop limit to 100.
        inner_test(&mut sync_ctx, &mut non_sync_ctx, 100, 0);

        // Set hop limit to 30.
        inner_test(&mut sync_ctx, &mut non_sync_ctx, 30, 1);
    }

    #[test]
    fn test_receiving_router_advertisement_mtu_option() {
        fn packet_buf(src_ip: Ipv6Addr, dst_ip: Ipv6Addr, mtu: u32) -> Buf<Vec<u8>> {
            let options = &[NdpOptionBuilder::Mtu(mtu)];
            OptionSequenceBuilder::new(options.iter())
                .into_serializer()
                .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                    src_ip,
                    dst_ip,
                    IcmpUnusedCode,
                    RouterAdvertisement::new(0, false, false, 0, 0, 0),
                ))
                .encapsulate(Ipv6PacketBuilder::new(
                    src_ip,
                    dst_ip,
                    REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
                    Ipv6Proto::Icmpv6,
                ))
                .serialize_vec_outer()
                .unwrap()
                .unwrap_b()
        }

        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let hw_mtu = 5000;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            local_mac(),
            hw_mtu,
        );
        let src_mac = Mac::new([10, 11, 12, 13, 14, 15]);
        let src_ip = src_mac.to_ipv6_link_local().addr();

        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, &device);

        // Receive a new RA with a valid MTU option (but the new MTU should only
        // be 5000 as that is the max MTU of the device).

        let icmpv6_packet_buf =
            packet_buf(src_ip.get(), Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(), 5781);
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            FrameDestination::Multicast,
            icmpv6_packet_buf,
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 1);
        assert_eq!(crate::ip::IpDeviceContext::<Ipv6, _>::get_mtu(&sync_ctx, &device), hw_mtu);

        // Receive a new RA with an invalid MTU option (value is lower than IPv6
        // min MTU).

        let icmpv6_packet_buf = packet_buf(
            src_ip.get(),
            Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            u32::from(Ipv6::MINIMUM_LINK_MTU) - 1,
        );
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            FrameDestination::Multicast,
            icmpv6_packet_buf,
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 2);
        assert_eq!(crate::ip::IpDeviceContext::<Ipv6, _>::get_mtu(&sync_ctx, &device), hw_mtu);

        // Receive a new RA with a valid MTU option (value is exactly IPv6 min
        // MTU).

        let icmpv6_packet_buf = packet_buf(
            src_ip.get(),
            Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            FrameDestination::Multicast,
            icmpv6_packet_buf,
        );
        assert_eq!(get_counter_val(&non_sync_ctx, "ndp::rx_router_advertisement"), 3);
        assert_eq!(
            crate::ip::IpDeviceContext::<Ipv6, _>::get_mtu(&sync_ctx, &device),
            Ipv6::MINIMUM_LINK_MTU.into()
        );
    }

    fn get_source_link_layer_option<L: LinkAddress, B>(options: &Options<B>) -> Option<L>
    where
        B: ByteSlice,
    {
        options.iter().find_map(|o| match o {
            NdpOption::SourceLinkLayerAddress(a) => {
                if a.len() >= L::BYTES_LENGTH {
                    Some(L::from_bytes(&a[..L::BYTES_LENGTH]))
                } else {
                    None
                }
            }
            _ => None,
        })
    }

    #[test]
    fn test_host_send_router_solicitations() {
        fn validate_params(
            src_mac: Mac,
            src_ip: Ipv6Addr,
            message: RouterSolicitation,
            code: IcmpUnusedCode,
        ) {
            let dummy_config = Ipv6::DUMMY_CONFIG;
            assert_eq!(src_mac, dummy_config.local_mac.get());
            assert_eq!(src_ip, dummy_config.local_mac.to_ipv6_link_local().addr().get());
            assert_eq!(message, RouterSolicitation::default());
            assert_eq!(code, IcmpUnusedCode);
        }

        let dummy_config = Ipv6::DUMMY_CONFIG;

        let Ctx { sync_ctx, mut non_sync_ctx } = crate::testutil::DummyCtx::default();
        let mut sync_ctx = &sync_ctx;

        assert_empty(non_sync_ctx.frames_sent());
        let device_id = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        let eth_device_id = device_id.clone().try_into().expect("expected ethernet ID");
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            |config| {
                config.ip_config.ip_enabled = true;

                // Test expects to send 3 RSs.
                config.max_router_solicitations = NonZeroU8::new(3);
            },
        );
        assert_empty(non_sync_ctx.frames_sent());

        let time = non_sync_ctx.now();
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            rs_timer_id(eth_device_id).into()
        );
        // Initial router solicitation should be a random delay between 0 and
        // `MAX_RTR_SOLICITATION_DELAY`.
        assert!(non_sync_ctx.now().duration_since(time) < MAX_RTR_SOLICITATION_DELAY);
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        let (src_mac, _, src_ip, _, _, message, code) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                &non_sync_ctx.frames_sent()[0].1,
                |_| {},
            )
            .unwrap();
        validate_params(src_mac, src_ip, message, code);

        // Should get 2 more router solicitation messages
        let time = non_sync_ctx.now();
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            rs_timer_id(eth_device_id).into()
        );
        assert_eq!(non_sync_ctx.now().duration_since(time), RTR_SOLICITATION_INTERVAL);
        let (src_mac, _, src_ip, _, _, message, code) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                &non_sync_ctx.frames_sent()[1].1,
                |_| {},
            )
            .unwrap();
        validate_params(src_mac, src_ip, message, code);

        // Before the next one, lets assign an IP address (DAD won't be
        // performed so it will be assigned immediately). The router solicitation
        // message should continue to use the link-local address.
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            AddrSubnet::new(dummy_config.local_ip.get(), 128).unwrap(),
        )
        .unwrap();
        let time = non_sync_ctx.now();
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            rs_timer_id(eth_device_id).into()
        );
        assert_eq!(non_sync_ctx.now().duration_since(time), RTR_SOLICITATION_INTERVAL);
        let (src_mac, _, src_ip, _, _, message, code) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                &non_sync_ctx.frames_sent()[2].1,
                |p| {
                    // We should have a source link layer option now because we
                    // have a source IP address set.
                    assert_eq!(p.body().iter().count(), 1);
                    if let Some(ll) = get_source_link_layer_option::<Mac, _>(p.body()) {
                        assert_eq!(ll, dummy_config.local_mac.get());
                    } else {
                        panic!("Should have a source link layer option");
                    }
                },
            )
            .unwrap();
        validate_params(src_mac, src_ip, message, code);

        // No more timers.
        assert_eq!(non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer), None);
        // Should have only sent 3 packets (Router solicitations).
        assert_eq!(non_sync_ctx.frames_sent().len(), 3);

        let Ctx { sync_ctx, mut non_sync_ctx } = crate::testutil::DummyCtx::default();
        let mut sync_ctx = &sync_ctx;
        assert_empty(non_sync_ctx.frames_sent());
        let device_id = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device_id,
            |config| {
                config.ip_config.ip_enabled = true;
                config.max_router_solicitations = NonZeroU8::new(2);
            },
        );
        assert_empty(non_sync_ctx.frames_sent());

        let time = non_sync_ctx.now();
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            rs_timer_id(eth_device_id).into()
        );
        // Initial router solicitation should be a random delay between 0 and
        // `MAX_RTR_SOLICITATION_DELAY`.
        assert!(non_sync_ctx.now().duration_since(time) < MAX_RTR_SOLICITATION_DELAY);
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);

        // Should trigger 1 more router solicitations
        let time = non_sync_ctx.now();
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            rs_timer_id(eth_device_id).into()
        );
        assert_eq!(non_sync_ctx.now().duration_since(time), RTR_SOLICITATION_INTERVAL);
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);

        // Each packet would be the same.
        for f in non_sync_ctx.frames_sent() {
            let (src_mac, _, src_ip, _, _, message, code) =
                parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                    &f.1,
                    |_| {},
                )
                .unwrap();
            validate_params(src_mac, src_ip, message, code);
        }

        // No more timers.
        assert_eq!(non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer), None);
    }

    #[test]
    fn test_router_solicitation_on_routing_enabled_changes() {
        // Make sure that when an interface goes from host -> router, it stops
        // sending Router Solicitations, and starts sending them when it goes
        // form router -> host as routers should not send Router Solicitation
        // messages, but hosts should.

        let dummy_config = Ipv6::DUMMY_CONFIG;

        // If netstack is not set to forward packets, make sure router
        // solicitations do not get cancelled when we enable forwarding on the
        // device.

        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;

        assert_empty(non_sync_ctx.frames_sent());
        assert_empty(non_sync_ctx.timer_ctx().timers());

        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |config| {
                config.ip_config.ip_enabled = true;

                // Doesn't matter as long as we are configured to send at least 2
                // solicitations.
                config.max_router_solicitations = NonZeroU8::new(2);
            },
        );
        let timer_id: TimerId =
            rs_timer_id(device.clone().try_into().expect("expected ethernet ID")).into();

        // Send the first router solicitation.
        assert_empty(non_sync_ctx.frames_sent());
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer_id.clone(), ..)]);

        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            timer_id
        );

        // Should have sent a router solicitation and still have the timer
        // setup.
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        let (_, _dst_mac, _, _, _, _, _) =
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                &non_sync_ctx.frames_sent()[0].1,
                |_| {},
            )
            .unwrap();
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer_id.clone(), ..)]);

        // Enable routing on device.
        set_ipv6_routing_enabled(&mut sync_ctx, &mut non_sync_ctx, &device, true)
            .expect("error setting routing enabled");
        assert!(is_ip_routing_enabled::<Ipv6, _, _>(&sync_ctx, &device));

        // Should have not sent any new packets, but unset the router
        // solicitation timer.
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        assert_empty(non_sync_ctx.timer_ctx().timers().iter().filter(|x| &x.1 == &timer_id));

        // Unsetting routing should succeed.
        set_ipv6_routing_enabled(&mut sync_ctx, &mut non_sync_ctx, &device, false)
            .expect("error setting routing enabled");
        assert!(!is_ip_routing_enabled::<Ipv6, _, _>(&sync_ctx, &device));
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer_id.clone(), ..)]);

        // Send the first router solicitation after being turned into a host.
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            timer_id
        );

        // Should have sent a router solicitation.
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);
        assert_matches::assert_matches!(
            parse_icmp_packet_in_ip_packet_in_ethernet_frame::<Ipv6, _, RouterSolicitation, _>(
                &non_sync_ctx.frames_sent()[1].1,
                |_| {},
            ),
            Ok((_, _, _, _, _, _, _))
        );
        non_sync_ctx.timer_ctx().assert_timers_installed([(timer_id, ..)]);
    }

    #[test]
    fn test_set_ndp_config_dup_addr_detect_transmits() {
        // Test that updating the DupAddrDetectTransmits parameter on an
        // interface updates the number of DAD messages (NDP Neighbor
        // Solicitations) sent before concluding that an address is not a
        // duplicate.

        let dummy_config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            dummy_config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, &device);
        assert_empty(non_sync_ctx.frames_sent());
        assert_empty(non_sync_ctx.timer_ctx().timers());

        // Updating the IP should resolve immediately since DAD is turned off by
        // `DummyEventDispatcherBuilder::build`.
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            AddrSubnet::new(dummy_config.local_ip.get(), 128).unwrap(),
        )
        .unwrap();
        let device_id = device.clone().try_into().unwrap();
        assert_eq!(
            get_address_state(&sync_ctx, &device, dummy_config.local_ip.try_into().unwrap()),
            Some(AddressState::Assigned)
        );
        assert_empty(non_sync_ctx.frames_sent());
        assert_empty(non_sync_ctx.timer_ctx().timers());

        // Enable DAD for the device.
        const DUP_ADDR_DETECT_TRANSMITS: u8 = 3;
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |ipv6_config| {
                ipv6_config.ip_config.ip_enabled = true;
                ipv6_config.dad_transmits = NonZeroU8::new(DUP_ADDR_DETECT_TRANSMITS);
            },
        );

        // Updating the IP should start the DAD process.
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            AddrSubnet::new(dummy_config.remote_ip.get(), 128).unwrap(),
        )
        .unwrap();
        assert_eq!(
            get_address_state(&sync_ctx, &device, dummy_config.local_ip.try_into().unwrap()),
            Some(AddressState::Assigned)
        );
        assert_eq!(
            get_address_state(&sync_ctx, &device, dummy_config.remote_ip.try_into().unwrap()),
            Some(AddressState::Tentative {
                dad_transmits_remaining: NonZeroU8::new(DUP_ADDR_DETECT_TRANSMITS - 1)
            })
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 1);
        assert_eq!(non_sync_ctx.timer_ctx().timers().len(), 1);

        // Disable DAD during DAD.
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |ipv6_config| {
                ipv6_config.dad_transmits = None;
            },
        );
        let expected_timer_id = dad_timer_id(device_id, dummy_config.remote_ip.try_into().unwrap());
        // Allow already started DAD to complete (2 more more NS, 3 more timers).
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            expected_timer_id
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 2);
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            expected_timer_id
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 3);
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            expected_timer_id
        );
        assert_eq!(non_sync_ctx.frames_sent().len(), 3);
        assert_eq!(
            get_address_state(&sync_ctx, &device, dummy_config.remote_ip.try_into().unwrap()),
            Some(AddressState::Assigned)
        );

        // Updating the IP should resolve immediately since DAD has just been
        // turned off.
        let new_ip = Ipv6::get_other_ip_address(3);
        add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            AddrSubnet::new(new_ip.get(), 128).unwrap(),
        )
        .unwrap();
        assert_eq!(
            get_address_state(&sync_ctx, &device, dummy_config.local_ip.try_into().unwrap()),
            Some(AddressState::Assigned)
        );
        assert_eq!(
            get_address_state(&sync_ctx, &device, dummy_config.remote_ip.try_into().unwrap()),
            Some(AddressState::Assigned)
        );
        assert_eq!(
            get_address_state(&sync_ctx, &device, new_ip.try_into().unwrap()),
            Some(AddressState::Assigned)
        );
    }

    fn slaac_packet_buf(
        src_ip: Ipv6Addr,
        dst_ip: Ipv6Addr,
        prefix: Ipv6Addr,
        prefix_length: u8,
        on_link_flag: bool,
        autonomous_address_configuration_flag: bool,
        valid_lifetime_secs: u32,
        preferred_lifetime_secs: u32,
    ) -> Buf<Vec<u8>> {
        let p = PrefixInformation::new(
            prefix_length,
            on_link_flag,
            autonomous_address_configuration_flag,
            valid_lifetime_secs,
            preferred_lifetime_secs,
            prefix,
        );
        let options = &[NdpOptionBuilder::PrefixInformation(p)];
        OptionSequenceBuilder::new(options.iter())
            .into_serializer()
            .encapsulate(IcmpPacketBuilder::<Ipv6, &[u8], _>::new(
                src_ip,
                dst_ip,
                IcmpUnusedCode,
                RouterAdvertisement::new(0, false, false, 0, 0, 0),
            ))
            .encapsulate(Ipv6PacketBuilder::new(
                src_ip,
                dst_ip,
                REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
                Ipv6Proto::Icmpv6,
            ))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_b()
    }

    #[test]
    fn test_router_stateless_address_autoconfiguration() {
        // Routers should not perform SLAAC for global addresses.

        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, &device);
        set_ipv6_routing_enabled(&mut sync_ctx, &mut non_sync_ctx, &device, true)
            .expect("error setting routing enabled");

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&src_mac.to_eui64()[..]);

        // Receive a new RA with new prefix (autonomous).
        //
        // Should not get a new IP.

        let icmpv6_packet_buf = slaac_packet_buf(
            src_ip,
            Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            prefix,
            prefix_length,
            false,
            false,
            100,
            0,
        );
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            FrameDestination::Multicast,
            icmpv6_packet_buf,
        );

        assert_empty(get_global_ipv6_addrs(&sync_ctx, &device));

        // No timers.
        assert_eq!(non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer), None);
    }

    impl From<SlaacTimerId<DeviceId>> for TimerId {
        fn from(id: SlaacTimerId<DeviceId>) -> TimerId {
            TimerId(TimerIdInner::Ipv6Device(Ipv6DeviceTimerId::Slaac(id)))
        }
    }

    #[derive(Copy, Clone, Debug)]
    struct TestSlaacPrefix {
        prefix: Subnet<Ipv6Addr>,
        valid_for: u32,
        preferred_for: u32,
    }
    impl TestSlaacPrefix {
        fn send_prefix_update(
            &self,
            sync_ctx: &mut &crate::testutil::DummySyncCtx,
            ctx: &mut crate::testutil::DummyNonSyncCtx,
            device: &DeviceId,
            src_ip: Ipv6Addr,
        ) {
            let Self { prefix, valid_for, preferred_for } = *self;

            receive_prefix_update(sync_ctx, ctx, device, src_ip, prefix, preferred_for, valid_for);
        }

        fn valid_until<I: Instant>(&self, now: I) -> I {
            now.checked_add(Duration::from_secs(self.valid_for.into())).unwrap()
        }
    }

    fn slaac_address<I: Instant>(
        entry: Ipv6AddressEntry<I>,
    ) -> Option<(UnicastAddr<Ipv6Addr>, SlaacConfig<I>)> {
        match entry.config {
            AddrConfig::Manual => None,
            AddrConfig::Slaac(s) => Some((entry.addr_sub().addr(), s)),
        }
    }

    /// Extracts the single static and temporary address config from the provided iterator and
    /// returns them as (static, temporary).
    ///
    /// Panics
    ///
    /// Panics if the iterator doesn't contain exactly one static and one temporary SLAAC entry.
    fn single_static_and_temporary<
        I: Copy + Debug,
        A: Copy + Debug,
        It: Iterator<Item = (A, SlaacConfig<I>)>,
    >(
        slaac_configs: It,
    ) -> ((A, SlaacConfig<I>), (A, SlaacConfig<I>)) {
        {
            let (static_addresses, temporary_addresses): (Vec<_>, Vec<_>) = slaac_configs
                .partition(|(_, s)| if let SlaacConfig::Static { .. } = s { true } else { false });

            let static_addresses: [_; 1] =
                static_addresses.try_into().expect("expected a single static address");
            let temporary_addresses: [_; 1] =
                temporary_addresses.try_into().expect("expected a single temporary address");
            (static_addresses[0], temporary_addresses[0])
        }
    }

    /// Enables temporary addressing with the provided parameters.
    ///
    /// `rng` is used to initialize the key that is used to generate new addresses.
    fn enable_temporary_addresses<R: RngCore>(
        config: &mut SlaacConfiguration,
        rng: &mut R,
        max_valid_lifetime: NonZeroDuration,
        max_preferred_lifetime: NonZeroDuration,
        max_generation_retries: u8,
    ) {
        let mut secret_key = [0; STABLE_IID_SECRET_KEY_BYTES];
        rng.fill_bytes(&mut secret_key);
        config.temporary_address_configuration = Some(TemporarySlaacAddressConfiguration {
            temp_valid_lifetime: max_valid_lifetime,
            temp_preferred_lifetime: max_preferred_lifetime,
            temp_idgen_retries: max_generation_retries,
            secret_key,
        })
    }

    fn initialize_with_temporary_addresses_enabled(
    ) -> (crate::testutil::DummyCtx, DeviceId, SlaacConfiguration) {
        set_logger_for_test();
        let config = Ipv6::DUMMY_CONFIG;
        let mut ctx = DummyEventDispatcherBuilder::default().build();
        let Ctx { sync_ctx, non_sync_ctx } = &mut ctx;
        let mut sync_ctx = &*sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, non_sync_ctx, &device);

        let max_valid_lifetime = Duration::from_secs(60 * 60);
        let max_preferred_lifetime = Duration::from_secs(30 * 60);
        let idgen_retries = 3;
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(max_valid_lifetime).unwrap(),
            NonZeroDuration::new(max_preferred_lifetime).unwrap(),
            idgen_retries,
        );

        crate::ip::device::update_ipv6_configuration(
            &mut sync_ctx,
            non_sync_ctx,
            &device,
            |ipv6_config| {
                ipv6_config.slaac_config = slaac_config;
            },
        );
        (ctx, device, slaac_config)
    }

    #[test]
    fn test_host_stateless_address_autoconfiguration_multiple_prefixes() {
        let (Ctx { sync_ctx, mut non_sync_ctx }, device, _): (_, _, SlaacConfiguration) =
            initialize_with_temporary_addresses_enabled();
        let mut sync_ctx = &sync_ctx;
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |config| {
                config.slaac_config.enable_stable_addresses = true;
            },
        );

        let prefix1 = TestSlaacPrefix {
            prefix: subnet_v6!("1:2:3:4::/64"),
            valid_for: 1500,
            preferred_for: 900,
        };
        let prefix2 = TestSlaacPrefix {
            prefix: subnet_v6!("5:6:7:8::/64"),
            valid_for: 1200,
            preferred_for: 600,
        };

        let config = Ipv6::DUMMY_CONFIG;
        let src_mac = config.remote_mac;
        let src_ip: Ipv6Addr = src_mac.to_ipv6_link_local().addr().get();

        // After the RA for the first prefix, we should have two addresses, one
        // static and one temporary.
        prefix1.send_prefix_update(&mut sync_ctx, &mut non_sync_ctx, &device, src_ip);

        let (prefix_1_static, prefix_1_temporary) = {
            let slaac_configs = get_global_ipv6_addrs(&sync_ctx, &device)
                .into_iter()
                .filter_map(slaac_address)
                .filter(|(a, _)| prefix1.prefix.contains(a));

            let (static_address, temporary_address) = single_static_and_temporary(slaac_configs);

            let now = non_sync_ctx.now();
            let prefix1_valid_until = prefix1.valid_until(now);
            assert_matches!(static_address, (_addr,
            SlaacConfig::Static { valid_until }) => {
                assert_eq!(valid_until, Lifetime::Finite(prefix1_valid_until))
            });
            assert_matches!(temporary_address, (_addr,
                SlaacConfig::Temporary(TemporarySlaacConfig {
                    valid_until,
                    creation_time,
                    desync_factor: _,
                    dad_counter: _ })) => {
                    assert_eq!(creation_time, now);
                    assert_eq!(valid_until, prefix1_valid_until);
            });
            (static_address.0, temporary_address.0)
        };

        // When the RA for the second prefix comes in, we should leave the entries for the addresses
        // in the first prefix alone.
        prefix2.send_prefix_update(&mut sync_ctx, &mut non_sync_ctx, &device, src_ip);

        {
            // Check prefix 1 addresses again.
            let slaac_configs = get_global_ipv6_addrs(&sync_ctx, &device)
                .into_iter()
                .filter_map(slaac_address)
                .filter(|(a, _)| prefix1.prefix.contains(a));
            let (static_address, temporary_address) = single_static_and_temporary(slaac_configs);

            let now = non_sync_ctx.now();
            let prefix1_valid_until = prefix1.valid_until(now);
            assert_matches!(static_address, (addr, SlaacConfig::Static { valid_until }) => {
                assert_eq!(addr, prefix_1_static);
                assert_eq!(valid_until, Lifetime::Finite(prefix1_valid_until));
            });
            assert_matches!(temporary_address,
            (addr, SlaacConfig::Temporary(TemporarySlaacConfig { valid_until, creation_time, desync_factor: _, dad_counter: 0 })) => {
                assert_eq!(addr, prefix_1_temporary);
                assert_eq!(creation_time, now);
                assert_eq!(valid_until, prefix1_valid_until);
            });
        }
        {
            // Check prefix 2 addresses.
            let slaac_configs = get_global_ipv6_addrs(&sync_ctx, &device)
                .into_iter()
                .filter_map(slaac_address)
                .filter(|(a, _)| prefix2.prefix.contains(a));
            let (static_address, temporary_address) = single_static_and_temporary(slaac_configs);

            let now = non_sync_ctx.now();
            let prefix2_valid_until = prefix2.valid_until(now);
            assert_matches!(static_address, (_, SlaacConfig::Static { valid_until }) => {
                assert_eq!(valid_until, Lifetime::Finite(prefix2_valid_until))
            });
            assert_matches!(temporary_address,
            (_, SlaacConfig::Temporary(TemporarySlaacConfig {
                valid_until, creation_time, desync_factor: _, dad_counter: 0 })) => {
                assert_eq!(creation_time, now);
                assert_eq!(valid_until, prefix2_valid_until);
            });
        }
    }

    fn test_host_generate_temporary_slaac_address(
        valid_lifetime_in_ra: u32,
        preferred_lifetime_in_ra: u32,
    ) -> (crate::testutil::DummyCtx, DeviceId, UnicastAddr<Ipv6Addr>) {
        set_logger_for_test();
        let (mut ctx, device, slaac_config) = initialize_with_temporary_addresses_enabled();
        let Ctx { sync_ctx, non_sync_ctx } = &mut ctx;
        let mut sync_ctx = &*sync_ctx;

        let max_valid_lifetime =
            slaac_config.temporary_address_configuration.unwrap().temp_valid_lifetime.get();
        let config = Ipv6::DUMMY_CONFIG;

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let subnet = subnet_v6!("0102:0304:0506:0708::/64");
        let interface_identifier = generate_opaque_interface_identifier(
            subnet,
            &config.local_mac.to_eui64()[..],
            [],
            // Clone the RNG so we can see what the next value (which will be
            // used to generate the temporary address) will be.
            OpaqueIidNonce::Random(non_sync_ctx.rng().clone().next_u64()),
            &slaac_config.temporary_address_configuration.unwrap().secret_key,
        );
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&interface_identifier.to_be_bytes()[..8]);
        let expected_addr = UnicastAddr::new(Ipv6Addr::from(expected_addr)).unwrap();
        let expected_addr_sub = AddrSubnet::from_witness(expected_addr, subnet.prefix()).unwrap();
        assert_eq!(expected_addr_sub.subnet(), subnet);

        // Receive a new RA with new prefix (autonomous).
        //
        // Should get a new temporary IP.

        receive_prefix_update(
            &mut sync_ctx,
            non_sync_ctx,
            &device,
            src_ip,
            subnet,
            preferred_lifetime_in_ra,
            valid_lifetime_in_ra,
        );

        // Should have gotten a new temporary IP.
        let temporary_slaac_addresses = get_global_ipv6_addrs(&sync_ctx, &device)
            .into_iter()
            .filter_map(|entry| match entry.config {
                AddrConfig::Slaac(SlaacConfig::Static { .. }) => None,
                AddrConfig::Slaac(SlaacConfig::Temporary(TemporarySlaacConfig {
                    creation_time: _,
                    desync_factor: _,
                    valid_until,
                    dad_counter: _,
                })) => Some((*entry.addr_sub(), entry.state, valid_until)),
                AddrConfig::Manual => None,
            })
            .collect::<Vec<_>>();
        assert_eq!(temporary_slaac_addresses.len(), 1);
        let (addr_sub, state, valid_until) = temporary_slaac_addresses.into_iter().next().unwrap();
        assert_eq!(addr_sub.subnet(), subnet);
        assert_eq!(state, AddressState::Assigned);
        assert!(valid_until <= non_sync_ctx.now().checked_add(max_valid_lifetime).unwrap());

        (ctx, device, expected_addr)
    }

    const INFINITE_LIFETIME: u32 = u32::MAX;

    #[test]
    fn test_host_temporary_slaac_and_manual_addresses_conflict() {
        // Verify that if the temporary SLAAC address generation picks an
        // address that is already assigned, it tries again. The difficulty here
        // is that the test uses an RNG to pick an address. To make sure we
        // assign the address that the code _would_ pick, we run the code twice
        // with the same RNG seed and parameters. The first time is lets us
        // figure out the temporary address that is generated. Then, we run the
        // same code with the address already assigned to verify the behavior.
        const RNG_SEED: [u8; 16] = [1; 16];
        let config = Ipv6::DUMMY_CONFIG;
        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let subnet = subnet_v6!("0102:0304:0506:0708::/64");

        // Receive an RA to figure out the temporary address that is assigned.
        let conflicted_addr = {
            let (Ctx { sync_ctx, mut non_sync_ctx }, device, _config) =
                initialize_with_temporary_addresses_enabled();
            let mut sync_ctx = &sync_ctx;

            *non_sync_ctx.rng_mut() = rand::SeedableRng::from_seed(RNG_SEED);

            // Receive an RA and determine what temporary address was assigned, then return it.
            receive_prefix_update(
                &mut sync_ctx,
                &mut non_sync_ctx,
                &device,
                src_ip,
                subnet,
                9000,
                10000,
            );
            *get_matching_slaac_address_entry(&sync_ctx, &device, |entry| match entry.config {
                AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                AddrConfig::Manual => false,
            })
            .unwrap()
            .addr_sub()
        };
        assert!(subnet.contains(&conflicted_addr.addr().get()));

        // Now that we know what address will be assigned, create a new instance
        // of the stack and assign that same address manually.
        let (Ctx { sync_ctx, mut non_sync_ctx }, device, _config) =
            initialize_with_temporary_addresses_enabled();
        let mut sync_ctx = &sync_ctx;
        crate::device::add_ip_addr_subnet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            conflicted_addr.to_witness(),
        )
        .expect("adding address failed");

        // Sanity check: `conflicted_addr` is already assigned on the device.
        assert_matches!(
            get_global_ipv6_addrs(&sync_ctx, &device)
                .into_iter()
                .find(|entry| entry.addr_sub() == &conflicted_addr),
            Some(_)
        );

        // Seed the RNG right before the RA is received, just like in our
        // earlier run above.
        *non_sync_ctx.rng_mut() = rand::SeedableRng::from_seed(RNG_SEED);

        // Receive a new RA with new prefix (autonomous). The system will assign
        // a temporary and static SLAAC address. The first temporary address
        // tried will conflict with `conflicted_addr` assigned above, so a
        // different one will be generated.
        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            9000,
            10000,
        );

        // Verify that `conflicted_addr` was generated and rejected.
        assert_eq!(get_counter_val(&non_sync_ctx, "generated_slaac_addr_exists"), 1);

        // Should have gotten a new temporary IP.
        let temporary_slaac_addresses =
            get_matching_slaac_address_entries(&sync_ctx, &device, |entry| match entry.config {
                AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                AddrConfig::Manual => false,
            })
            .map(|entry| *entry.addr_sub())
            .collect::<Vec<_>>();
        assert_matches!(&temporary_slaac_addresses[..], [temporary_addr] => {
            assert_eq!(temporary_addr.subnet(), conflicted_addr.subnet());
            assert_ne!(temporary_addr, &conflicted_addr);
        });
    }

    #[test]
    fn test_host_slaac_invalid_prefix_information() {
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, &device);

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;

        assert_empty(get_global_ipv6_addrs(&sync_ctx, &device));

        // Receive a new RA with new prefix (autonomous), but preferred lifetime
        // is greater than valid.
        //
        // Should not get a new IP.

        let icmpv6_packet_buf = slaac_packet_buf(
            src_ip,
            Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            prefix,
            prefix_length,
            false,
            true,
            9000,
            10000,
        );
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            FrameDestination::Multicast,
            icmpv6_packet_buf,
        );
        assert_empty(get_global_ipv6_addrs(&sync_ctx, &device));

        // Address invalidation timers were added.
        assert_empty(non_sync_ctx.timer_ctx().timers());
    }

    #[test]
    fn test_host_slaac_address_deprecate_while_tentative() {
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::testutil::enable_device(&mut sync_ctx, &mut non_sync_ctx, &device);

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let prefix = subnet_v6!("0102:0304:0506:0708::/64");
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&config.local_mac.to_eui64()[..]);
        let expected_addr = UnicastAddr::new(Ipv6Addr::from(expected_addr)).unwrap();
        let expected_addr_sub = AddrSubnet::from_witness(expected_addr, prefix.prefix()).unwrap();

        // Have no addresses yet.
        assert_empty(get_global_ipv6_addrs(&sync_ctx, &device));

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |config| {
                config.ip_config.ip_enabled = true;
                config.slaac_config.enable_stable_addresses = true;

                // Doesn't matter as long as we perform DAD.
                config.dad_transmits = NonZeroU8::new(1);
            },
        );

        // Set the retransmit timer between neighbor solicitations to be greater
        // than the preferred lifetime of the prefix.
        Ipv6DeviceHandler::set_discovered_retrans_timer(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            NonZeroDuration::from_nonzero_secs(nonzero!(10u64)),
        );

        // Receive a new RA with new prefix (autonomous).
        //
        // Should get a new IP and set preferred lifetime to 1s.

        let valid_lifetime = 2;
        let preferred_lifetime = 1;

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            prefix,
            preferred_lifetime,
            valid_lifetime,
        );

        // Should have gotten a new IP.
        let now = non_sync_ctx.now();
        let valid_until = now + Duration::from_secs(valid_lifetime.into());
        let expected_address_entry = Ipv6AddressEntry {
            addr_sub: expected_addr_sub,
            state: AddressState::Tentative { dad_transmits_remaining: None },
            config: AddrConfig::Slaac(SlaacConfig::Static {
                valid_until: Lifetime::Finite(DummyInstant::from(valid_until)),
            }),
            deprecated: false,
        };
        assert_eq!(get_global_ipv6_addrs(&sync_ctx, &device), [expected_address_entry]);

        // Make sure deprecate and invalidation timers are set.
        non_sync_ctx.timer_ctx().assert_some_timers_installed([
            (
                SlaacTimerId::new_deprecate_slaac_address(device, expected_addr).into(),
                now + Duration::from_secs(preferred_lifetime.into()),
            ),
            (SlaacTimerId::new_invalidate_slaac_address(device, expected_addr).into(), valid_until),
        ]);

        // Trigger the deprecation timer.
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer).unwrap(),
            SlaacTimerId::new_deprecate_slaac_address(device, expected_addr).into()
        );
        assert_eq!(
            get_global_ipv6_addrs(&sync_ctx, &device),
            [Ipv6AddressEntry { deprecated: true, ..expected_address_entry }]
        );
    }

    fn receive_prefix_update(
        sync_ctx: &mut &crate::testutil::DummySyncCtx,
        ctx: &mut crate::testutil::DummyNonSyncCtx,
        device: &DeviceId,
        src_ip: Ipv6Addr,
        subnet: Subnet<Ipv6Addr>,
        preferred_lifetime: u32,
        valid_lifetime: u32,
    ) {
        let prefix = subnet.network();
        let prefix_length = subnet.prefix();

        let icmpv6_packet_buf = slaac_packet_buf(
            src_ip,
            Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            prefix,
            prefix_length,
            false,
            true,
            valid_lifetime,
            preferred_lifetime,
        );
        receive_ipv6_packet(sync_ctx, ctx, &device, FrameDestination::Multicast, icmpv6_packet_buf);
    }

    fn get_matching_slaac_address_entries<F: FnMut(&Ipv6AddressEntry<DummyInstant>) -> bool>(
        sync_ctx: &&crate::testutil::DummySyncCtx,
        device: &DeviceId,
        filter: F,
    ) -> impl Iterator<Item = Ipv6AddressEntry<DummyInstant>> {
        get_global_ipv6_addrs(sync_ctx, device).into_iter().filter(filter)
    }

    fn get_matching_slaac_address_entry<F: FnMut(&Ipv6AddressEntry<DummyInstant>) -> bool>(
        sync_ctx: &&crate::testutil::DummySyncCtx,
        device: &DeviceId,
        filter: F,
    ) -> Option<Ipv6AddressEntry<DummyInstant>> {
        let mut matching_addrs = get_matching_slaac_address_entries(sync_ctx, device, filter);
        let entry = matching_addrs.next();
        assert_eq!(matching_addrs.next(), None);
        entry
    }

    fn get_slaac_address_entry(
        sync_ctx: &&crate::testutil::DummySyncCtx,
        device: &DeviceId,
        addr_sub: AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>>,
    ) -> Option<Ipv6AddressEntry<DummyInstant>> {
        let mut matching_addrs = get_global_ipv6_addrs(sync_ctx, device)
            .into_iter()
            .filter(|entry| *entry.addr_sub() == addr_sub);
        let entry = matching_addrs.next();
        assert_eq!(matching_addrs.next(), None);
        entry
    }

    fn assert_slaac_lifetimes_enforced(
        non_sync_ctx: &crate::testutil::DummyNonSyncCtx,
        device: &DeviceId,
        entry: Ipv6AddressEntry<DummyInstant>,
        valid_until: DummyInstant,
        preferred_until: DummyInstant,
    ) {
        assert_eq!(entry.state, AddressState::Assigned);
        assert_matches!(entry.config, AddrConfig::Slaac(_));
        let entry_valid_until = match entry.config {
            AddrConfig::Slaac(SlaacConfig::Static { valid_until }) => valid_until,
            AddrConfig::Slaac(SlaacConfig::Temporary(TemporarySlaacConfig {
                valid_until,
                desync_factor: _,
                creation_time: _,
                dad_counter: _,
            })) => Lifetime::Finite(valid_until),
            AddrConfig::Manual => unreachable!(),
        };
        assert_eq!(entry_valid_until, Lifetime::Finite(valid_until));
        non_sync_ctx.timer_ctx().assert_some_timers_installed([
            (
                SlaacTimerId::new_deprecate_slaac_address(device.clone(), entry.addr_sub().addr())
                    .into(),
                preferred_until,
            ),
            (
                SlaacTimerId::new_invalidate_slaac_address(device.clone(), entry.addr_sub().addr())
                    .into(),
                valid_until,
            ),
        ]);
    }

    #[test]
    fn test_host_static_slaac_valid_lifetime_updates() {
        // Make sure we update the valid lifetime only in certain scenarios
        // to prevent denial-of-service attacks as outlined in RFC 4862 section
        // 5.5.3.e. Note, the preferred lifetime should always be updated.

        set_logger_for_test();
        fn inner_test(
            sync_ctx: &mut &crate::testutil::DummySyncCtx,
            ctx: &mut crate::testutil::DummyNonSyncCtx,
            device: &DeviceId,
            src_ip: Ipv6Addr,
            subnet: Subnet<Ipv6Addr>,
            addr_sub: AddrSubnet<Ipv6Addr, UnicastAddr<Ipv6Addr>>,
            preferred_lifetime: u32,
            valid_lifetime: u32,
            expected_valid_lifetime: u32,
        ) {
            receive_prefix_update(
                sync_ctx,
                ctx,
                device,
                src_ip,
                subnet,
                preferred_lifetime,
                valid_lifetime,
            );
            let entry = get_slaac_address_entry(sync_ctx, &device, addr_sub).unwrap();
            let now = ctx.now();
            let valid_until =
                now.checked_add(Duration::from_secs(expected_valid_lifetime.into())).unwrap();
            let preferred_until =
                now.checked_add(Duration::from_secs(preferred_lifetime.into())).unwrap();

            assert_slaac_lifetimes_enforced(ctx, &device, entry, valid_until, preferred_until);
        }

        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |config| {
                config.ip_config.ip_enabled = true;
                config.slaac_config.enable_stable_addresses = true;
            },
        );

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let subnet = Subnet::new(prefix, prefix_length).unwrap();
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&config.local_mac.to_eui64()[..]);
        let expected_addr = UnicastAddr::new(Ipv6Addr::from(expected_addr)).unwrap();
        let expected_addr_sub = AddrSubnet::from_witness(expected_addr, prefix_length).unwrap();

        // Have no addresses yet.
        assert_empty(get_global_ipv6_addrs(&sync_ctx, &device));

        // Receive a new RA with new prefix (autonomous).
        //
        // Should get a new IP and set preferred lifetime to 1s.

        // Make sure deprecate and invalidation timers are set.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            30,
            60,
            60,
        );

        // If the valid lifetime is greater than the remaining lifetime, update
        // the valid lifetime.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            70,
            70,
            70,
        );

        // If the valid lifetime is greater than 2 hrs, update the valid
        // lifetime.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            7201,
            7201,
        );

        // Make remaining lifetime < 2 hrs.
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                Duration::from_secs(1000),
                handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer)
            ),
            []
        );

        // If the remaining lifetime is <= 2 hrs & valid lifetime is less than
        // that, don't update valid lifetime.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            1000,
            2000,
            6201,
        );

        // Make the remaining lifetime > 2 hours.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            1000,
            10800,
            10800,
        );

        // If the remaining lifetime is > 2 hours, and new valid lifetime is < 2
        // hours, set the valid lifetime to 2 hours.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            1000,
            1000,
            7200,
        );

        // If the remaining lifetime is <= 2 hrs & valid lifetime is less than
        // that, don't update valid lifetime.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            1000,
            2000,
            7200,
        );

        // Increase valid lifetime twice while it is greater than 2 hours.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            7201,
            7201,
        );
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            7202,
            7202,
        );

        // Make remaining lifetime < 2 hrs.
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                Duration::from_secs(1000),
                handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer)
            ),
            []
        );

        // If the remaining lifetime is <= 2 hrs & valid lifetime is less than
        // that, don't update valid lifetime.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            6202,
            6202,
        );

        // Increase valid lifetime twice while it is less than 2 hours.
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            6203,
            6203,
        );
        inner_test(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            expected_addr_sub,
            1001,
            6204,
            6204,
        );
    }

    #[test]
    fn test_host_temporary_slaac_regenerates_address_on_dad_failure() {
        // Check that when a tentative temporary address is detected as a
        // duplicate, a new address gets created.
        set_logger_for_test();
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );

        let router_mac = config.remote_mac;
        let router_ip = router_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let subnet = Subnet::new(prefix, prefix_length).unwrap();

        const MAX_VALID_LIFETIME: Duration = Duration::from_secs(15000);
        const MAX_PREFERRED_LIFETIME: Duration = Duration::from_secs(5000);

        let idgen_retries = 3;

        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(MAX_VALID_LIFETIME).unwrap(),
            NonZeroDuration::new(MAX_PREFERRED_LIFETIME).unwrap(),
            idgen_retries,
        );

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |ipv6_config| {
                ipv6_config.slaac_config = slaac_config;
                ipv6_config.ip_config.ip_enabled = true;

                // Doesn't matter as long as we perform DAD.
                ipv6_config.dad_transmits = NonZeroU8::new(1);
            },
        );

        // Send an update with lifetimes that are smaller than the ones specified in the preferences.
        let valid_lifetime = 10000;
        let preferred_lifetime = 4000;
        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            router_ip,
            subnet,
            preferred_lifetime,
            valid_lifetime,
        );

        let first_addr_entry = get_matching_slaac_address_entry(&sync_ctx, &device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        assert_eq!(
            first_addr_entry.state,
            AddressState::Tentative { dad_transmits_remaining: None }
        );

        receive_neighbor_advertisement_for_duplicate_address(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            first_addr_entry.addr_sub().addr(),
        );

        // In response to the advertisement with the duplicate address, a
        // different address should be selected.
        let second_addr_entry = get_matching_slaac_address_entry(&sync_ctx, &device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        let first_addr_entry_valid = assert_matches!(first_addr_entry.config,
            AddrConfig::Slaac(SlaacConfig::Temporary(TemporarySlaacConfig {
                valid_until, creation_time: _, desync_factor: _, dad_counter: 0})) => {valid_until});
        let first_addr_sub = first_addr_entry.addr_sub();
        let second_addr_sub = second_addr_entry.addr_sub();
        assert_eq!(first_addr_sub.subnet(), second_addr_sub.subnet());
        assert_ne!(first_addr_sub.addr(), second_addr_sub.addr());

        assert_matches!(second_addr_entry.config, AddrConfig::Slaac(SlaacConfig::Temporary(
            TemporarySlaacConfig {
            valid_until,
            creation_time,
            desync_factor: _,
            dad_counter: 1,
        })) => {
            assert_eq!(creation_time, non_sync_ctx.now());
            assert_eq!(valid_until, first_addr_entry_valid);
        });
    }

    fn receive_neighbor_advertisement_for_duplicate_address(
        sync_ctx: &mut &crate::testutil::DummySyncCtx,
        ctx: &mut crate::testutil::DummyNonSyncCtx,
        device: &DeviceId,
        source_ip: UnicastAddr<Ipv6Addr>,
    ) {
        let peer_mac = mac!("00:11:22:33:44:55");
        let dest_ip = Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get();
        let router_flag = false;
        let solicited_flag = false;
        let override_flag = true;

        let src_ip = source_ip.get();
        receive_ipv6_packet(
            sync_ctx,
            ctx,
            &device,
            FrameDestination::Multicast,
            Buf::new(
                neighbor_advertisement_message(
                    src_ip,
                    dest_ip,
                    router_flag,
                    solicited_flag,
                    override_flag,
                    Some(peer_mac),
                ),
                ..,
            )
            .encapsulate(Ipv6PacketBuilder::new(
                src_ip,
                dest_ip,
                REQUIRED_NDP_IP_PACKET_HOP_LIMIT,
                Ipv6Proto::Icmpv6,
            ))
            .serialize_vec_outer()
            .unwrap()
            .unwrap_b(),
        )
    }

    #[test]
    fn test_host_temporary_slaac_gives_up_after_dad_failures() {
        // Check that when the chosen tentative temporary addresses are detected
        // as duplicates enough times, the system gives up.
        set_logger_for_test();
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );

        let router_mac = config.remote_mac;
        let router_ip = router_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let subnet = Subnet::new(prefix, prefix_length).unwrap();

        const MAX_VALID_LIFETIME: Duration = Duration::from_secs(15000);
        const MAX_PREFERRED_LIFETIME: Duration = Duration::from_secs(5000);

        let idgen_retries = 3;
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(MAX_VALID_LIFETIME).unwrap(),
            NonZeroDuration::new(MAX_PREFERRED_LIFETIME).unwrap(),
            idgen_retries,
        );

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |ipv6_config| {
                ipv6_config.slaac_config = slaac_config;
                ipv6_config.ip_config.ip_enabled = true;

                // Doesn't matter as long as we perform DAD.
                ipv6_config.dad_transmits = NonZeroU8::new(1);
            },
        );

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            router_ip,
            subnet,
            MAX_PREFERRED_LIFETIME.as_secs() as u32,
            MAX_VALID_LIFETIME.as_secs() as u32,
        );

        let match_temporary_address = |entry: &Ipv6AddressEntry<DummyInstant>| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        };

        // The system should try several (1 initial + # retries) times to
        // generate an address. In the loop below, each generated address is
        // detected as a duplicate.
        let attempted_addresses: Vec<_> = (0..=idgen_retries)
            .into_iter()
            .map(|_| {
                // An address should be selected. This must be checked using DAD
                // against other hosts on the network.
                let addr_entry =
                    get_matching_slaac_address_entry(&sync_ctx, &device, match_temporary_address)
                        .unwrap();
                assert_eq!(
                    addr_entry.state,
                    AddressState::Tentative { dad_transmits_remaining: None }
                );

                // A response is received to the DAD request indicating that it
                // is a duplicate.
                receive_neighbor_advertisement_for_duplicate_address(
                    &mut sync_ctx,
                    &mut non_sync_ctx,
                    &device,
                    addr_entry.addr_sub().addr(),
                );

                // The address should be unassigned from the device.
                assert_eq!(
                    get_slaac_address_entry(&sync_ctx, &device, *addr_entry.addr_sub()),
                    None
                );
                *addr_entry.addr_sub()
            })
            .collect();

        // After the last failed try, the system should have given up, and there
        // should be no temporary address for the subnet.
        assert_eq!(
            get_matching_slaac_address_entry(&sync_ctx, &device, match_temporary_address),
            None
        );

        // All the attempted addresses should be unique.
        let unique_addresses = attempted_addresses.iter().collect::<HashSet<_>>();
        assert_eq!(
            unique_addresses.len(),
            (1 + idgen_retries).into(),
            "not all addresses are unique: {attempted_addresses:?}"
        );
    }

    #[test]
    fn test_host_temporary_slaac_deprecate_before_regen() {
        // Check that if there are multiple non-deprecated addresses in a subnet
        // and the regen timer goes off, no new address is generated. This tests
        // the following scenario:
        //
        // 1. At time T, an address A is created for a subnet whose preferred
        //    lifetime is PA. This results in a regen timer set at T + PA - R.
        // 2. At time T + PA - R, a new address B is created for the same
        //    subnet when the regen timer for A expires, with a preferred
        //    lifetime of PB (PA != PB because of the desync values).
        // 3. Before T + PA, an advertisement is received for the prefix with
        //    preferred lifetime X. Address A is now preferred until T + PA + X
        //    and regenerated at T + PA + X - R and address B is preferred until
        //    (T + PA - R) + PB + X.
        //
        // Since both addresses are preferred, we expect that when the regen
        // timer for address A goes off, it is ignored since there is already
        // another preferred address (namely B) for the subnet.
        set_logger_for_test();
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );

        let router_mac = config.remote_mac;
        let router_ip = router_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let subnet = Subnet::new(prefix, prefix_length).unwrap();

        const MAX_VALID_LIFETIME: Duration = Duration::from_secs(15000);
        const MAX_PREFERRED_LIFETIME: Duration = Duration::from_secs(5000);
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(MAX_VALID_LIFETIME).unwrap(),
            NonZeroDuration::new(MAX_PREFERRED_LIFETIME).unwrap(),
            0,
        );

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |ipv6_config| {
                ipv6_config.slaac_config = slaac_config;
                ipv6_config.ip_config.ip_enabled = true;
            },
        );

        // The prefix updates contains a shorter preferred lifetime than
        // the preferences allow.
        let prefix_preferred_for: Duration = MAX_PREFERRED_LIFETIME * 2 / 3;
        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            router_ip,
            subnet,
            prefix_preferred_for.as_secs().try_into().unwrap(),
            MAX_VALID_LIFETIME.as_secs().try_into().unwrap(),
        );

        let first_addr_entry = get_matching_slaac_address_entry(&sync_ctx, &device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        let regen_timer_id = SlaacTimerId::new_regenerate_temporary_slaac_address(
            device,
            *first_addr_entry.addr_sub(),
        );
        trace!("advancing to regen for first address");
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer),
            Some(regen_timer_id.into())
        );

        // The regeneration timer should cause a new address to be created in
        // the same subnet.
        assert_matches!(
            get_matching_slaac_address_entry(&sync_ctx, &device, |entry| {
                entry.addr_sub().subnet() == subnet
                    && entry.addr_sub() != first_addr_entry.addr_sub()
                    && match entry.config {
                        AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                        AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                        AddrConfig::Manual => false,
                    }
            }),
            Some(_)
        );

        // Now the router sends a new update that extends the preferred lifetime
        // of addresses.
        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            router_ip,
            subnet,
            prefix_preferred_for.as_secs().try_into().unwrap(),
            MAX_VALID_LIFETIME.as_secs().try_into().unwrap(),
        );
        let addresses = get_matching_slaac_address_entries(&sync_ctx, &device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .map(|entry| entry.addr_sub().addr())
        .collect::<Vec<_>>();

        for address in &addresses {
            assert_matches!(
                non_sync_ctx.scheduled_instant(SlaacTimerId::new_deprecate_slaac_address(
                    device,
                    *address,
                )),
                Some(deprecate_at) => {
                    let preferred_for = deprecate_at - non_sync_ctx.now();
                    assert!(preferred_for <= prefix_preferred_for, "{:?} <= {:?}", preferred_for, prefix_preferred_for);
                }
            );
        }

        trace!("advancing to new regen for first address");
        // Running the context forward until the first address is again eligible
        // for regeneration doesn't result in a new address being created.
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer),
            Some(regen_timer_id.into())
        );
        assert_eq!(
            get_matching_slaac_address_entries(&sync_ctx, &device, |entry| entry
                .addr_sub()
                .subnet()
                == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                })
            .map(|entry| entry.addr_sub().addr())
            .collect::<HashSet<_>>(),
            addresses.iter().cloned().collect()
        );

        trace!("advancing to deprecation for first address");
        // If we continue on until the first address is deprecated, we still
        // shouldn't regenerate since the second address is active.
        assert_eq!(
            non_sync_ctx.trigger_next_timer(sync_ctx, crate::handle_timer),
            Some(
                SlaacTimerId::new_deprecate_slaac_address(
                    device,
                    first_addr_entry.addr_sub().addr()
                )
                .into()
            )
        );

        let remaining_addresses = addresses
            .into_iter()
            .filter(|addr| addr != &first_addr_entry.addr_sub().addr())
            .collect::<HashSet<_>>();
        assert_eq!(
            get_matching_slaac_address_entries(&sync_ctx, &device, |entry| entry
                .addr_sub()
                .subnet()
                == subnet
                && !entry.deprecated
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                })
            .map(|entry| entry.addr_sub().addr())
            .collect::<HashSet<_>>(),
            remaining_addresses
        );
    }

    #[test]
    fn test_host_temporary_slaac_config_update_skips_regen() {
        // If the NDP configuration gets updated such that the target regen time
        // for an address is moved earlier than the current time, the address
        // should be regenerated immediately.
        set_logger_for_test();
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        let device_id = device.clone().try_into().unwrap();
        // No DAD for the auto-generated link-local address.
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |ipv6_config| {
                ipv6_config.dad_transmits = None;
                ipv6_config.ip_config.ip_enabled = true;
            },
        );

        let router_mac = config.remote_mac;
        let router_ip = router_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let subnet = Subnet::new(prefix, prefix_length).unwrap();

        const MAX_VALID_LIFETIME: Duration = Duration::from_secs(15000);
        let max_preferred_lifetime = Duration::from_secs(5000);
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(MAX_VALID_LIFETIME).unwrap(),
            NonZeroDuration::new(max_preferred_lifetime).unwrap(),
            1,
        );

        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |ipv6_config| {
                // Perform DAD for later addresses.
                ipv6_config.dad_transmits = NonZeroU8::new(1);
                ipv6_config.slaac_config = slaac_config;
            },
        );

        // Set a large value for the retransmit period. This forces
        // REGEN_ADVANCE to be large, which increases the window between when an
        // address is regenerated and when it becomes deprecated.
        Ipv6DeviceHandler::set_discovered_retrans_timer(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            NonZeroDuration::new(max_preferred_lifetime / 4).unwrap(),
        );

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            router_ip,
            subnet,
            max_preferred_lifetime.as_secs().try_into().unwrap(),
            MAX_VALID_LIFETIME.as_secs().try_into().unwrap(),
        );

        let first_addr_entry = get_matching_slaac_address_entry(&sync_ctx, &device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        let regen_at = non_sync_ctx
            .scheduled_instant(SlaacTimerId::new_regenerate_temporary_slaac_address(
                device,
                *first_addr_entry.addr_sub(),
            ))
            .unwrap();

        let before_regen = regen_at - Duration::from_secs(10);
        // The only events that run before regen should be the DAD timers for
        // the static and temporary address that were created earlier.
        let dad_timer_ids = get_matching_slaac_address_entries(&sync_ctx, &device, |entry| {
            entry.addr_sub().subnet() == subnet
        })
        .map(|entry| dad_timer_id(device_id, entry.addr_sub().addr()))
        .collect::<Vec<_>>();
        non_sync_ctx.trigger_timers_until_and_expect_unordered(
            before_regen,
            dad_timer_ids,
            |non_sync_ctx, id| crate::handle_timer(sync_ctx, non_sync_ctx, id),
        );

        let preferred_until = non_sync_ctx
            .scheduled_instant(SlaacTimerId::new_deprecate_slaac_address(
                device,
                first_addr_entry.addr_sub().addr(),
            ))
            .unwrap();

        let max_preferred_lifetime = max_preferred_lifetime * 4 / 5;
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            NonZeroDuration::new(MAX_VALID_LIFETIME).unwrap(),
            NonZeroDuration::new(max_preferred_lifetime).unwrap(),
            1,
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |ipv6_config| {
                ipv6_config.slaac_config = slaac_config;
            },
        );

        // Receiving this update should result in requiring a regen time that is
        // before the current time. The address should be regenerated
        // immediately.
        let prefix_preferred_for = preferred_until - non_sync_ctx.now();

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            router_ip,
            subnet,
            prefix_preferred_for.as_secs().try_into().unwrap(),
            MAX_VALID_LIFETIME.as_secs().try_into().unwrap(),
        );

        // The regeneration is still handled by timer, so handle any pending
        // events.
        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                Duration::ZERO,
                handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer)
            ),
            vec![SlaacTimerId::new_regenerate_temporary_slaac_address(
                device,
                *first_addr_entry.addr_sub()
            )
            .into()]
        );

        let addresses = get_matching_slaac_address_entries(&sync_ctx, &device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .map(|entry| entry.addr_sub().addr())
        .collect::<HashSet<_>>();
        assert!(addresses.contains(&first_addr_entry.addr_sub().addr()));
        assert_eq!(addresses.len(), 2);
    }

    #[test]
    fn test_host_temporary_slaac_lifetime_updates_respect_max() {
        // Make sure that the preferred and valid lifetimes of the NDP
        // configuration are respected.

        let src_mac = Ipv6::DUMMY_CONFIG.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let subnet = subnet_v6!("0102:0304:0506:0708::/64");
        let (Ctx { sync_ctx, mut non_sync_ctx }, device, config) =
            initialize_with_temporary_addresses_enabled();
        let mut sync_ctx = &sync_ctx;
        let now = non_sync_ctx.now();
        let start = now;
        let temporary_address_config = config.temporary_address_configuration.unwrap();

        let max_valid_lifetime = temporary_address_config.temp_valid_lifetime;
        let max_valid_until = now.checked_add(max_valid_lifetime.get()).unwrap();
        let max_preferred_lifetime = temporary_address_config.temp_preferred_lifetime;
        let max_preferred_until = now.checked_add(max_preferred_lifetime.get()).unwrap();
        let secret_key = temporary_address_config.secret_key;

        let interface_identifier = generate_opaque_interface_identifier(
            subnet,
            &Ipv6::DUMMY_CONFIG.local_mac.to_eui64()[..],
            [],
            // Clone the RNG so we can see what the next value (which will be
            // used to generate the temporary address) will be.
            OpaqueIidNonce::Random(non_sync_ctx.rng().clone().next_u64()),
            &secret_key,
        );
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&interface_identifier.to_be_bytes()[..8]);
        let expected_addr = UnicastAddr::new(Ipv6Addr::from(expected_addr)).unwrap();
        let expected_addr_sub = AddrSubnet::from_witness(expected_addr, subnet.prefix()).unwrap();

        // Send an update with lifetimes that are smaller than the ones specified in the preferences.
        let valid_lifetime = 2000;
        let preferred_lifetime = 1500;
        assert!(u64::from(valid_lifetime) < max_valid_lifetime.get().as_secs());
        assert!(u64::from(preferred_lifetime) < max_preferred_lifetime.get().as_secs());
        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            preferred_lifetime,
            valid_lifetime,
        );

        let entry = get_slaac_address_entry(&sync_ctx, &device, expected_addr_sub).unwrap();
        let expected_valid_until =
            now.checked_add(Duration::from_secs(valid_lifetime.into())).unwrap();
        let expected_preferred_until =
            now.checked_add(Duration::from_secs(preferred_lifetime.into())).unwrap();
        assert!(
            expected_valid_until < max_valid_until,
            "expected {:?} < {:?}",
            expected_valid_until,
            max_valid_until
        );
        assert!(expected_preferred_until < max_preferred_until);

        assert_slaac_lifetimes_enforced(
            &non_sync_ctx,
            &device,
            entry,
            expected_valid_until,
            expected_preferred_until,
        );

        // After some time passes, another update is received with the same lifetimes for the
        // prefix. Per RFC 8981 Section 3.4.1, the lifetimes for the address should obey the
        // overall constraints expressed in the preferences.

        assert_eq!(
            non_sync_ctx.trigger_timers_for(
                Duration::from_secs(1000),
                handle_timer_helper_with_sc_ref(sync_ctx, crate::handle_timer)
            ),
            []
        );
        let now = non_sync_ctx.now();
        let expected_valid_until =
            now.checked_add(Duration::from_secs(valid_lifetime.into())).unwrap();
        let expected_preferred_until =
            now.checked_add(Duration::from_secs(preferred_lifetime.into())).unwrap();

        // The preferred lifetime advertised by the router is now past the max allowed by
        // the NDP configuration.
        assert!(expected_preferred_until > max_preferred_until);

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            preferred_lifetime,
            valid_lifetime,
        );

        let entry = get_matching_slaac_address_entry(&sync_ctx, &device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        let desync_factor = match entry.config {
            AddrConfig::Slaac(SlaacConfig::Temporary(TemporarySlaacConfig {
                desync_factor,
                creation_time: _,
                valid_until: _,
                dad_counter: _,
            })) => desync_factor,
            AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => {
                unreachable!("temporary address")
            }
            AddrConfig::Manual => unreachable!("temporary slaac address"),
        };
        assert_slaac_lifetimes_enforced(
            &non_sync_ctx,
            &device,
            entry,
            expected_valid_until,
            max_preferred_until - desync_factor,
        );

        // Update the max allowed lifetime in the NDP configuration. This won't take effect until
        // the next router advertisement is reeived.
        let max_valid_lifetime = max_preferred_lifetime;
        let idgen_retries = 3;
        let mut slaac_config = SlaacConfiguration::default();
        enable_temporary_addresses(
            &mut slaac_config,
            non_sync_ctx.rng_mut(),
            max_valid_lifetime,
            max_preferred_lifetime,
            idgen_retries,
        );

        crate::ip::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |config| {
                config.slaac_config = slaac_config;
            },
        );
        // The new valid time is measured from the time at which the address was created (`start`),
        // not the current time (`now`). That means the max valid lifetime takes precedence over
        // the router's advertised valid lifetime.
        let max_valid_until = start.checked_add(max_valid_lifetime.get()).unwrap();
        assert!(expected_valid_until > max_valid_until);

        receive_prefix_update(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            src_ip,
            subnet,
            preferred_lifetime,
            valid_lifetime,
        );

        let entry = get_matching_slaac_address_entry(&sync_ctx, &device, |entry| {
            entry.addr_sub().subnet() == subnet
                && match entry.config {
                    AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                    AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                    AddrConfig::Manual => false,
                }
        })
        .unwrap();
        assert_slaac_lifetimes_enforced(
            &non_sync_ctx,
            &device,
            entry,
            max_valid_until,
            max_preferred_until - desync_factor,
        );
    }

    #[test]
    fn test_remove_stable_slaac_address() {
        let config = Ipv6::DUMMY_CONFIG;
        let Ctx { sync_ctx, mut non_sync_ctx } = DummyEventDispatcherBuilder::default().build();
        let mut sync_ctx = &sync_ctx;
        let device = crate::device::add_ethernet_device(
            &mut sync_ctx,
            &mut non_sync_ctx,
            config.local_mac,
            Ipv6::MINIMUM_LINK_MTU.into(),
        );
        crate::device::update_ipv6_configuration(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            |config| {
                config.ip_config.ip_enabled = true;
                config.slaac_config.enable_stable_addresses = true;
            },
        );

        let src_mac = config.remote_mac;
        let src_ip = src_mac.to_ipv6_link_local().addr().get();
        let prefix = Ipv6Addr::from([1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0]);
        let prefix_length = 64;
        let mut expected_addr = [1, 2, 3, 4, 5, 6, 7, 8, 0, 0, 0, 0, 0, 0, 0, 0];
        expected_addr[8..].copy_from_slice(&config.local_mac.to_eui64()[..]);
        let expected_addr = UnicastAddr::new(Ipv6Addr::from(expected_addr)).unwrap();

        // Receive a new RA with new prefix (autonomous).
        //
        // Should get a new IP.

        const VALID_LIFETIME_SECS: u32 = 10000;
        const PREFERRED_LIFETIME_SECS: u32 = 9000;

        let icmpv6_packet_buf = slaac_packet_buf(
            src_ip,
            Ipv6::ALL_NODES_LINK_LOCAL_MULTICAST_ADDRESS.get(),
            prefix,
            prefix_length,
            false,
            true,
            VALID_LIFETIME_SECS,
            PREFERRED_LIFETIME_SECS,
        );
        receive_ipv6_packet(
            &mut sync_ctx,
            &mut non_sync_ctx,
            &device,
            FrameDestination::Multicast,
            icmpv6_packet_buf,
        );

        // Should have gotten a new IP.
        let now = non_sync_ctx.now();
        let valid_until = now + Duration::from_secs(VALID_LIFETIME_SECS.into());
        let expected_address_entry = Ipv6AddressEntry {
            addr_sub: AddrSubnet::new(expected_addr.get(), prefix_length).unwrap(),
            state: AddressState::Assigned,
            config: AddrConfig::Slaac(SlaacConfig::Static {
                valid_until: Lifetime::Finite(DummyInstant::from(valid_until)),
            }),
            deprecated: false,
        };
        assert_eq!(get_global_ipv6_addrs(&sync_ctx, &device), [expected_address_entry]);
        // Make sure deprecate and invalidation timers are set.
        non_sync_ctx.timer_ctx().assert_some_timers_installed([
            (
                SlaacTimerId::new_deprecate_slaac_address(device, expected_addr).into(),
                now + Duration::from_secs(PREFERRED_LIFETIME_SECS.into()),
            ),
            (SlaacTimerId::new_invalidate_slaac_address(device, expected_addr).into(), valid_until),
        ]);

        // Deleting the address should cancel its SLAAC timers.
        del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, &device, &expected_addr.into_specified())
            .unwrap();
        assert_empty(get_global_ipv6_addrs(&sync_ctx, &device));
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }

    #[test]
    fn test_remove_temporary_slaac_address() {
        // We use the infinite lifetime so that the stable address does not have
        // any timers as it is valid and preferred forever. As a result, we will
        // only observe timers for temporary addresses.
        let (Ctx { sync_ctx, mut non_sync_ctx }, device, expected_addr) =
            test_host_generate_temporary_slaac_address(INFINITE_LIFETIME, INFINITE_LIFETIME);
        let mut sync_ctx = &sync_ctx;

        // Deleting the address should cancel its SLAAC timers.
        del_ip_addr(&mut sync_ctx, &mut non_sync_ctx, &device, &expected_addr.into_specified())
            .unwrap();
        assert_empty(get_global_ipv6_addrs(&sync_ctx, &device).into_iter().filter(
            |e| match e.config {
                AddrConfig::Slaac(SlaacConfig::Temporary(_)) => true,
                AddrConfig::Slaac(SlaacConfig::Static { valid_until: _ }) => false,
                AddrConfig::Manual => false,
            },
        ));
        non_sync_ctx.timer_ctx().assert_no_timers_installed();
    }
}
