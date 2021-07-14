// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(warnings)]

use {
    fidl::{
        encoding::Decodable,
        endpoints::{self as endpoints, RequestStream, ServiceMarker},
    },
    async_utils::hanging_get::client::HangingGetStream,
    anyhow::{anyhow, Context as AnyhowContext},
    fidl_fuchsia_hardware_ethernet::{self as hardware_ethernet},
    fidl_fuchsia_net::{self as net},
    fidl_fuchsia_net_interfaces::{self as net_interfaces},
    fidl_fuchsia_net_interfaces_ext::{self as net_interfaces_ext},
    fidl_fuchsia_netstack::{self as netstack, NetstackProxy},
    fidl_fuchsia_virtualization_hardware::{
        StartInfo, VirtioDeviceRequest, VirtioDeviceRequestStream, VirtioNetMarker,
        VirtioNetRequest, VirtioNetRequestStream, EVENT_SET_INTERRUPT, EVENT_SET_QUEUE,
    },
    fuchsia_async::{
        self as fasync, Fifo, FifoEntry, FifoReadable, FifoWritable, PacketReceiver,
        ReceiverRegistration,
    },
    fuchsia_component::client::connect_to_protocol,
    std::convert::{TryInto,TryFrom},
    fuchsia_component::server,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self as syslog, fx_log_err, fx_log_info},
    fuchsia_zircon::{self as zx, AsHandleRef},
    futures::{
        channel::mpsc,
        select,
        task::{noop_waker, noop_waker_ref, AtomicWaker},
        Future, FutureExt, Sink, SinkExt, Stream, StreamExt, TryFutureExt, TryStreamExt,
    },
    parking_lot::Mutex,
    pin_utils::{pin_mut},
    std::{
        collections::HashMap,
        io::{Read, Write},
        mem,
        ops::{Deref, DerefMut},
        pin::Pin,
        sync::atomic,
        task::{Context, Poll},
    },
    virtio_device::{
    chain::{ReadableChain, WritableChain},
    util::BufferedNotify,
    queue::DriverNotify,
    },
    machina_virtio_device::*,
    zerocopy,
    zerocopy::AsBytes,
    matches::matches,
    ethernet::{ethernet_sys::*},
};

struct GuestEthernet {
    rx_fifo: Fifo<EthernetFifoEntry>,
    tx_fifo: Fifo<EthernetFifoEntry>,
    vmo: zx::Vmo,
    io_mem_base: *mut u8,
    io_mem_len: usize,
    con: fidl_fuchsia_hardware_ethernet::DeviceRequestStream,
}

const MTU: u32 = 1500;
// This is a locally administered MAC address (first byte 0x02) mixed with the
// Google Organizationally Unique Identifier (00:1a:11). The host gets ff:ff:ff
// and the guest gets 00:00:00 for the last three octets.
const HOST_MAC_ADDRESS: hardware_ethernet::MacAddress =
    hardware_ethernet::MacAddress { octets: [0x02, 0x1a, 0x11, 0xff, 0xff, 0xff] };

const VIRTIO_NET_QUEUE_SIZE: usize = 256;

// Copied from //zircon/system/public/zircon/device/ethernet.h
// const ETH_FIFO_RX_OK: u16 = 1; // packet received okay
// const ETH_FIFO_TX_OK: u16 = 1; // packet transmitted okay
// const ETH_FIFO_INVALID: u16 = 2; // packet not within io_vmo bounds
// const ETH_FIFO_RX_TX: u16 = 4; // received our own tx packet (when TX_LISTEN)

#[repr(transparent)]
pub struct EthernetFifoEntry(eth_fifo_entry);

unsafe impl FifoEntry for EthernetFifoEntry {}

impl GuestEthernet {
    async fn new(
        mut con: fidl_fuchsia_hardware_ethernet::DeviceRequestStream,
    ) -> Result<GuestEthernet, anyhow::Error> {
        // start the message loop
        let mut fifo = None;
        let mut vmo = None;
        loop {
            match con.next().await {
                Some(Ok(hardware_ethernet::DeviceRequest::GetInfo { responder })) => {
                    // TODO: use proper mac address
                    let mut info = hardware_ethernet::Info {
                        features: hardware_ethernet::Features::Synthetic,
                        mtu: MTU,
                        mac: HOST_MAC_ADDRESS,
                    };
                    responder.send(&mut info);
                }
                Some(Ok(hardware_ethernet::DeviceRequest::GetFifos { responder })) => {
                    if fifo.is_some() {
                        panic!("Duplicated fifos");
                    }
                    // TODO: should send an error on failures instead of dropping responder?
                    let (rx_a, rx_b) = zx::Fifo::create(
                        VIRTIO_NET_QUEUE_SIZE,
                        mem::size_of::<EthernetFifoEntry>(),
                    )?;
                    let (tx_a, tx_b) = zx::Fifo::create(
                        VIRTIO_NET_QUEUE_SIZE,
                        mem::size_of::<EthernetFifoEntry>(),
                    )?;
                    let mut fifos = hardware_ethernet::Fifos {
                        rx: rx_a,
                        tx: tx_b,
                        rx_depth: VIRTIO_NET_QUEUE_SIZE as u32,
                        tx_depth: VIRTIO_NET_QUEUE_SIZE as u32,
                    };
                    let rx = Fifo::from_fifo(rx_b)?;
                    let tx = Fifo::from_fifo(tx_a)?    ;

                    responder.send(zx::Status::OK.into_raw(), Some(&mut fifos));
                    fifo = Some((tx, rx));
                }
                Some(Ok(hardware_ethernet::DeviceRequest::SetIoBuffer { h, responder })) => {
                    if vmo.is_some() {
                        panic!("Duplicated vmo");
                    }
                    let vmo_size = h.get_size()? as usize;
                    let addr = fuchsia_runtime::vmar_root_self()
                        .map(
                            0,
                            &h,
                            0,
                            vmo_size,
                            zx::VmarFlags::PERM_READ
                                | zx::VmarFlags::PERM_WRITE
                                | zx::VmarFlags::REQUIRE_NON_RESIZABLE,
                        )?;
                    responder.send(zx::Status::OK.into_raw());
                    vmo = Some((h, addr, vmo_size));
                }
                Some(Ok(hardware_ethernet::DeviceRequest::Start { responder })) => match (fifo, vmo) {
                    (Some((tx_fifo, rx_fifo)), Some((vmo, io_addr, io_size))) => {
                        responder.send(zx::Status::OK.into_raw());
                        return Ok(GuestEthernet { tx_fifo, rx_fifo, vmo, io_mem_base: io_addr as usize as *mut u8, io_mem_len: io_size, con });
                    }
                    _ => panic!("Start called too soon"),
                },
                Some(Ok(hardware_ethernet::DeviceRequest::SetClientName { name, responder })) => {
                    println!("Name set to {}", name);
                    responder.send(zx::Status::OK.into_raw());
                }
                Some(Ok(msg)) => return Err(anyhow!("Unknown msg")),
                Some(Err(e)) => return Err(anyhow!("Some fidl error")),
                None => return Err(anyhow!("Unexpected end of stream")),
            }
        }
    }
    pub fn rx_packet_stream<'a>(
        &'a self,
    ) -> PacketStream<'a, RxPacket> {
        let mut stream = EntryStream::new(&self.rx_fifo);
        PacketStream { stream, io_mem_base: self.io_mem_base, io_mem_len: self.io_mem_len,packet_type: std::marker::PhantomData }
    }
    pub fn tx_packet_stream<'a>(
        &'a self,
    ) -> PacketStream<'a, TxPacket> {
        let mut stream = EntryStream::new(&self.tx_fifo);
        PacketStream { stream, io_mem_base: self.io_mem_base, io_mem_len: self.io_mem_len,packet_type: std::marker::PhantomData }
    }
}

/// EntryStream represents the stream of reads from the fifo.
pub struct EntryStream<'a, F, R: 'a> {
    fifo: &'a F,
    read_marker: ::std::marker::PhantomData<R>,
}

impl<'a, F: FifoReadable<R>, R: FifoEntry> EntryStream<'a, F, R> {
    /// Create a new EntryStream, which borrows the `FifoReadable` type
    /// until the stream completes.
    pub fn new(fifo: &'a F) -> EntryStream<'_, F, R> {
        EntryStream { fifo, read_marker: ::std::marker::PhantomData }
    }
}

impl<'a, F: FifoReadable<R>, R: FifoEntry> Stream for EntryStream<'a, F, R> {
    type Item = Result<R, zx::Status>;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.fifo.read(cx).map(|v| v.transpose())
    }
}

struct PacketStream<'a, P> {
    stream: EntryStream<
        'a,
        Fifo<EthernetFifoEntry>,
        EthernetFifoEntry,
    >,
    io_mem_base: *mut u8,
    io_mem_len: usize,
    packet_type: std::marker::PhantomData<P>,
}

impl<'a, P> Unpin for PacketStream<'a, P> {}

impl<'a, P: PacketFlags> Stream for PacketStream<'a, P> {
    type Item = PacketEntry<'a, P>;

    fn poll_next(mut self: Pin<&mut Self>, lw: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        match self.stream.poll_next_unpin(lw) {
            Poll::Ready(Some(Err(e))) => {
                panic!("Cannot handle errors");
            }
            Poll::Ready(Some(Ok(mut entry))) => {
                // todo overflow
                if entry.0.offset as usize + entry.0.length as usize > self.io_mem_len {
                    panic!("invalid");
                }
                Poll::Ready(Some(PacketEntry { packet: unsafe{self.io_mem_base.add(entry.0.offset as usize)}, entry, fifo : self.stream.fifo, packet_type: std::marker::PhantomData}))
            }
            Poll::Ready(None) => Poll::Ready(None),
            Poll::Pending => Poll::Pending,
        }
    }
}


trait PacketFlags {
    fn okay() -> u32;
    fn invalid() -> u32 {
        ETH_FIFO_INVALID
    }
}

struct RxPacket {}
struct TxPacket {}

impl PacketFlags for RxPacket {
    fn okay() -> u32 {
        ETH_FIFO_RX_OK
    }
}

impl PacketFlags for TxPacket {
    fn okay() -> u32 {
        ETH_FIFO_TX_OK
    }
}

struct PacketEntry<'a, P> {
    packet: *mut u8,
    entry: EthernetFifoEntry,
    fifo: &'a Fifo<EthernetFifoEntry, EthernetFifoEntry>,
    packet_type: std::marker::PhantomData<P>,
}

impl<'a, P:PacketFlags> PacketEntry<'a, P> {
    pub async fn okay(self) -> Result<(), fidl::Status>{
        let PacketEntry { packet:_, mut entry, fifo, packet_type:_ } = self;
        entry.0.flags = P::okay() as u16;
        fifo.write_entries(std::slice::from_ref(&entry)).await
    }
    pub async fn cancel(self) -> Result<(), fidl::Status>{
        let PacketEntry { packet:_, mut entry, fifo, packet_type:_ } = self;
        entry.0.flags = P::invalid() as u16;
        fifo.write_entries(std::slice::from_ref(&entry)).await
    }
    pub fn len(&self) -> usize {
        self.entry.0.length as usize
    }
    pub fn as_ptr(&self) -> *const u8 {
        self.packet
    }
}

impl<'a> PacketEntry<'a, RxPacket> {
    pub fn set_length(mut self, len: usize) -> Result<PacketEntry<'a, RxPacket>, PacketEntry<'a, RxPacket>> {
        if len <= self.entry.0.length as usize {
            self.entry.0.length = len as u16;
            Ok(self)
        } else {
            Err(self)
        }
    }
    pub fn as_mut_ptr(&mut self) -> *mut u8 {
        self.packet
    }
}

// const INTERFACE_PATH: &'static str = "/dev/class/ethernet/virtio";
const INTERFACE_NAME: &'static str = "ethv0";
// const IPV4_ADDRESS: net::Ipv4Address = net::Ipv4Address{addr: [10, 0, 0, 1]};

#[repr(C, packed)]
#[derive(Debug, Default, Clone, zerocopy::FromBytes, zerocopy::AsBytes)]
pub struct VirtioNetHdr {
    flags: u8,
    gso_type: u8,
    hdr_len: u16,
    gso_size: u16,
    csum_start: u16,
    csum_offset: u16,
    // Only if |VIRTIO_NET_F_MRG_RXBUF| or |VIRTIO_F_VERSION_1|.
    num_buffers: u16,
}

struct VirtioNetPacket<B> {
    header: zerocopy::LayoutVerified<B, VirtioNetHdr>,
    body: B,
}

#[repr(u16)]
enum NetQueues {
    RX = 0,
    TX = 1,
}

struct StreamNotifyOnPending<S, N: DriverNotify> {
    stream: S,
    notify: BufferedNotify<N>,
    was_ready: bool,
}

impl<S: Unpin, N:DriverNotify> Unpin for StreamNotifyOnPending<S, N> {}

impl<S, N: DriverNotify> StreamNotifyOnPending<S, N> {
    pub fn from_stream(stream: S, notify: BufferedNotify<N>) -> Self {
        Self {stream, notify, was_ready: true}
    }
}

impl<S: Stream<Item=I> + Unpin, I, N: DriverNotify> Stream for StreamNotifyOnPending<S, N> {
    type Item=I;
        fn poll_next(
        mut self: Pin<&mut Self>, 
        cx: &mut Context<'_>
    ) -> Poll<Option<Self::Item>> {
        let result = self.stream.poll_next_unpin(cx);
        let was_ready = result.is_ready();
        if !was_ready && self.was_ready {
            self.notify.flush();
        }
        self.was_ready = was_ready;
        result
    }
}

async fn run_virtio_net(mut con: VirtioNetRequestStream) -> Result<(), anyhow::Error> {
    // Expect a start message first off to configure things.
    let (start_info, mac_address, enable_bridge, responder) = con
            .try_next()
        .await?
        .ok_or(anyhow!("Unexpected end of stream"))?
        .into_start()
        .ok_or(anyhow!("Expected Start message"))?;

    // Connect to the host nestack
    let netstack = connect_to_protocol::<netstack::NetstackMarker>().context("failed to connect to netstack")?;
    let net_interfaces = connect_to_protocol::<net_interfaces::StateMarker>().context("failed to connect to net_interfaces")?;
    let (watcher, watcher_server) = fidl::endpoints::create_proxy()?;
    net_interfaces
        .get_watcher(net_interfaces::WatcherOptions::EMPTY, watcher_server)?;
    let mut watch_stream = HangingGetStream::new(Box::new(move || {Some(watcher.watch())}));

    let mut config = netstack::InterfaceConfig::new_empty();
    config.name = INTERFACE_NAME.into(); /*, ip_address_config:
                                         netstack::IpAddressConfig::StaticIp(net::Subnet{addr: net::IpAddress::Ipv4(IPV4_ADDRESS), prefix_len: 24})};*/

    let (device_client, device_server) =
        endpoints::create_endpoints::<hardware_ethernet::DeviceMarker>()?;
    let device_server = device_server.into_stream()?;

    let ethernet_start_future =
        GuestEthernet::new(device_server /*, INTERFACE_PATH, INTERFACE_NAME, IPV4_ADDRESS*/);

    // Add ourselves as an ethernet device to the netstack
    let add_device_future = async {
        let id = netstack
            .add_ethernet_device("", &mut config, device_client)
            .await?.map_err(|_| anyhow!("todo"))?;
        netstack.set_interface_status(id, true)?;
        if enable_bridge {
            let interface_id = watch_stream.err_into::<anyhow::Error>()
            // Make it a stream of just properties that generates an error if we hit Idle
            .try_filter_map(|x|
                match x {
                    net_interfaces::Event::Existing(prop) | net_interfaces::Event::Added(prop)
                    =>
                        futures::future::ok(Some(prop)),
                    net_interfaces::Event::Idle(_) => futures::future::err(anyhow!("Failed to bridge")),
                    _ => futures::future::ok(None)
                })
            // Convert the properties to the expected form
                .and_then(|x| futures::future::ready(x.try_into().map_err(std::convert::Into::into)))
                // Filter for globally reachable interfaces.
                .try_filter_map(|prop| futures::future::ok(if net_interfaces_ext::is_globally_routable(&prop) { Some(prop.id) } else {None}))
                .try_next().await?.ok_or(anyhow!("Unexected end of stream"))?;

            let bridge_result =
                netstack.bridge_interfaces(&[interface_id as u32, id]).await?;
            if bridge_result.0.status != netstack::Status::Ok {
                return Err(anyhow!("Some bridge problem"));
            }
            netstack.set_interface_status(bridge_result.1, true)?;
        }
        Ok::<(), anyhow::Error>(())
    };

    let (device_builder, guest_mem) = from_start_info(start_info)?;
    responder.send();

    let mut con = con.cast_stream();
    let device_future = config_builder_from_stream(
        device_builder.map_notify(|e| Ok(BufferedNotify::new(e)))?,
        &mut con,
        &[NetQueues::RX as u16, NetQueues::TX as u16][..],
        &guest_mem,
    ).err_into::<anyhow::Error>();

    let (guest_ethernet, (), (mut device, ready_responder)) = futures::future::try_join3(
        ethernet_start_future,
        add_device_future,
        device_future,
    )
    .await?;

    let mut tx_stream = device.take_stream(NetQueues::TX as u16)?;
    let mut rx_stream = device.take_stream(NetQueues::RX as u16)?;

    ready_responder.send().unwrap();

    let mut rx_packet_stream = guest_ethernet.rx_packet_stream();
    let mut tx_packet_stream = guest_ethernet.tx_packet_stream();

    let mut rx_fut_stream = StreamNotifyOnPending::from_stream(rx_packet_stream.zip(tx_stream), device.get_notify().clone());   

    // Wait for some tx
    let rx_fut = async {
        while let Some((mut packet, mut desc_chain)) = rx_fut_stream.next().await {
            let mut chain = ReadableChain::new(desc_chain, &guest_mem);
            let mut header = VirtioNetHdr::default();
                chain
                    .read_exact(header.as_bytes_mut())?;

            // TODO: better rxpacket lifecycle.
            match packet.set_length(chain.remaining()?) {
                Ok(mut packet) => {
                    let mut offset: usize = 0;
                    while let Some(Ok(range)) = chain.next() {
                        // TODO: validate lengths
                        unsafe{libc::memmove(packet.as_mut_ptr().add(offset)as *mut libc::c_void, range.try_ptr().ok_or(anyhow!("bad ptr"))?, range.len())};
                        offset += range.len();
                    }
                    chain.return_complete()?;
                    packet.okay().await
                },
                Err(packet) => packet.cancel().await
            }?;
        }
        Err::<(),_>(anyhow!("Unexpected end of stream"))
    };

    let mut tx_fut_stream = StreamNotifyOnPending::from_stream(tx_packet_stream.zip(rx_stream), device.get_notify().clone());
    let tx_fut = async {
        while let Some((mut packet, mut desc_chain)) = tx_fut_stream.next().await {
            let mut chain = WritableChain::new(desc_chain, &guest_mem)?;
            chain
                .write_all(
                    VirtioNetHdr {
                        num_buffers: 1,
                        gso_type: 0,
                        gso_size: 0,
                        flags: 0,
                        hdr_len: 0,
                        csum_start: 0,
                        csum_offset: 0,
                    }
                    .as_bytes(),
                )?;
            let mut offset: usize = 0;
            while let Some(Ok(range)) = chain.next() {
                // TODO: validate lengths
                unsafe{libc::memmove(range.try_mut_ptr().ok_or(anyhow!("bad ptr"))?, packet.as_ptr().add(offset) as *const libc::c_void, range.len())};
                offset += range.len();
            }
            packet.okay().await?;
        };
        Err::<(),_>(anyhow!("Unexpected end of stream"))
    };

    futures::future::try_join3(
        device.run_device_notify(con).err_into::<anyhow::Error>(),
        rx_fut,
        tx_fut,
    )
    .await?;

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    syslog::init().context("Unable to initialize syslog")?;
    let mut fs = server::ServiceFs::new();
    fs.dir("svc").add_fidl_service(|stream: VirtioNetRequestStream| stream);
    fs.take_and_serve_directory_handle().context("Error starting server")?;

    fs.for_each_concurrent(None, |stream| async {
        if let Err(e) = run_virtio_net(stream).await {
            syslog::fx_log_err!("Error {} running virtio_net service", e);
        }
    }).await;

    Ok(())
}
