// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN Network Tunnel Abstraction
use crate::driver::*;
use crate::prelude::*;

use crate::spinel::Subnet;
use anyhow::Context as _;
use anyhow::Error;
use async_trait::async_trait;
use fidl::endpoints::{create_endpoints, create_proxy};
use fidl_fuchsia_hardware_network as fhwnet;
use fidl_fuchsia_net as fnet;
use fidl_fuchsia_net_ext as fnetext;
use fidl_fuchsia_net_stack as fnetstack;
use fidl_fuchsia_net_stack_ext::FidlReturn as _;
use fidl_fuchsia_net_tun as ftun;
use fuchsia_component::client::{connect_channel_to_service, connect_to_service};
use fuchsia_zircon as zx;
use futures::stream::BoxStream;
use parking_lot::Mutex;

const IPV6_MIN_MTU: u32 = 1280;

#[derive(Debug)]
pub struct TunNetworkInterface {
    tun_dev: ftun::DeviceProxy,
    stack: fnetstack::StackProxy,
    stack_sync: Mutex<fnetstack::StackSynchronousProxy>,
    id: u64,
}

impl TunNetworkInterface {
    pub async fn try_new(name: Option<String>) -> Result<TunNetworkInterface, Error> {
        let tun_control = connect_to_service::<ftun::ControlMarker>()?;

        let (tun_dev, req) = create_proxy::<ftun::DeviceMarker>()?;

        tun_control
            .create_device(
                ftun::DeviceConfig {
                    base: Some(ftun::BaseConfig {
                        mtu: Some(IPV6_MIN_MTU),
                        rx_types: Some(vec![
                            fhwnet::FrameType::Ipv6,
                            // TODO(fxbug.dev/64292): Remove this once netstack doesn't require it.
                            fhwnet::FrameType::Ipv4,
                        ]),
                        tx_types: Some(vec![
                            fhwnet::FrameTypeSupport {
                                type_: fhwnet::FrameType::Ipv6,
                                features: fhwnet::FRAME_FEATURES_RAW,
                                supported_flags: fhwnet::TxFlags::empty(),
                            },
                            // TODO(fxbug.dev/64292): Remove this once netstack doesn't require it.
                            fhwnet::FrameTypeSupport {
                                type_: fhwnet::FrameType::Ipv4,
                                features: fhwnet::FRAME_FEATURES_RAW,
                                supported_flags: fhwnet::TxFlags::empty(),
                            },
                        ]),
                        ..ftun::BaseConfig::EMPTY
                    }),
                    blocking: Some(true),
                    ..ftun::DeviceConfig::EMPTY
                },
                req,
            )
            .context("failed to create tun pair")?;

        let (device, device_req) = create_endpoints::<fhwnet::DeviceMarker>()?;

        tun_dev
            .connect_protocols(ftun::Protocols {
                network_device: Some(device_req),
                ..ftun::Protocols::EMPTY
            })
            .context("connect protocols failed")?;

        let stack = connect_to_service::<fnetstack::StackMarker>()?;

        let id = stack
            .add_interface(
                fnetstack::InterfaceConfig { name, ..fnetstack::InterfaceConfig::EMPTY },
                &mut fnetstack::DeviceDefinition::Ip(device),
            )
            .await
            .squash_result()
            .context("Unable to add TUN interface to netstack")?;

        stack
            .enable_interface(id)
            .await
            .squash_result()
            .context("Unable to enable TUN interface")?;

        let (client, server) = zx::Channel::create()?;
        connect_channel_to_service::<fnetstack::StackMarker>(server)?;
        let stack_sync = Mutex::new(fnetstack::StackSynchronousProxy::new(client));

        Ok(TunNetworkInterface { tun_dev, stack, stack_sync, id })
    }
}

#[async_trait]
impl NetworkInterface for TunNetworkInterface {
    async fn outbound_packet_from_stack(&self) -> Result<Vec<u8>, Error> {
        let frame = self
            .tun_dev
            .read_frame()
            .await
            .context("FIDL error on read_frame")?
            .map_err(fuchsia_zircon::Status::from_raw)
            .context("Error calling read_frame")?;

        Ok(frame.data.ok_or(format_err!("data field was absent"))?)
    }

    async fn inbound_packet_to_stack(&self, packet: &[u8]) -> Result<(), Error> {
        traceln!("Packet to stack: {}", hex::encode(packet));

        Ok(self
            .tun_dev
            .write_frame(ftun::Frame {
                frame_type: Some(fhwnet::FrameType::Ipv6),
                data: Some(packet.to_vec()),
                meta: None,
                ..fidl_fuchsia_net_tun::Frame::EMPTY
            })
            .await?
            .map_err(fuchsia_zircon::Status::from_raw)?)
    }

    async fn set_online(&self, online: bool) -> Result<(), Error> {
        fx_log_info!("Interface online: {:?}", online);

        if online {
            self.tun_dev.set_online(true).await?;
            self.stack.enable_interface(self.id).await.squash_result()?;
        } else {
            self.tun_dev.set_online(false).await?;
        }

        Ok(())
    }

    async fn set_enabled(&self, enabled: bool) -> Result<(), Error> {
        fx_log_info!("Interface enabled: {:?}", enabled);
        if enabled {
            self.stack.enable_interface(self.id).await.squash_result()?;
        } else {
            self.stack.disable_interface(self.id).await.squash_result()?;
        }
        Ok(())
    }

    fn add_address(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!("Address Added: {:?}", addr);
        let mut addr = fnet::Subnet {
            addr: fnetext::IpAddress(addr.addr.into()).into(),
            prefix_len: addr.prefix_len,
        };
        self.stack_sync
            .lock()
            .add_interface_address(self.id, &mut addr, zx::Time::INFINITE)
            .squash_result()?;
        Ok(())
    }

    fn remove_address(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!("Address Removed: {:?}", addr);
        let mut addr = fnet::Subnet {
            addr: fnetext::IpAddress(addr.addr.into()).into(),
            prefix_len: addr.prefix_len,
        };
        self.stack_sync
            .lock()
            .del_interface_address(self.id, &mut addr, zx::Time::INFINITE)
            .squash_result()?;
        Ok(())
    }

    fn add_on_link_route(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!("On Mesh Route Added: {:?} (IGNORED)", addr);
        Ok(())
    }

    fn remove_on_link_route(&self, addr: &Subnet) -> Result<(), Error> {
        fx_log_info!("On Mesh Route Removed: {:?} (IGNORED)", addr);
        Ok(())
    }

    fn take_event_stream(&self) -> BoxStream<'_, Result<NetworkInterfaceEvent, Error>> {
        let enabled_stream = futures::stream::try_unfold((), move |()| async move {
            loop {
                if let ftun::InternalState { has_session: Some(has_session), .. } =
                    self.tun_dev.watch_state().await?
                {
                    break Ok(Some((
                        NetworkInterfaceEvent::InterfaceEnabledChanged(has_session),
                        (),
                    )));
                }
            }
        });

        enabled_stream.boxed()
    }
}
