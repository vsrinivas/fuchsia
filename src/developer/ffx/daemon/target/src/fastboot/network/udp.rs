// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{fastboot::InterfaceFactory, target::Target},
    anyhow::{anyhow, bail, Context as _, Result},
    async_io::Async,
    async_net::UdpSocket,
    async_trait::async_trait,
    byteorder::{BigEndian, ByteOrder},
    futures::{
        io::{AsyncRead, AsyncWrite},
        task::{Context, Poll},
        Future,
    },
    std::io::ErrorKind,
    std::net::SocketAddr,
    std::num::Wrapping,
    std::pin::Pin,
    std::time::Duration,
    timeout::timeout,
    zerocopy::{byteorder::big_endian::U16, ByteSlice, FromBytes, LayoutVerified, Unaligned},
};

const HOST_PORT: u16 = 5554;
const REPLY_TIMEOUT: Duration = Duration::from_millis(500);
const MAX_SIZE: u16 = 2048; // Maybe handle larger?

enum PacketType {
    Error,
    Query,
    Init,
    Fastboot,
}

#[derive(FromBytes, Unaligned)]
#[repr(C)]
struct Header {
    id: u8,
    flags: u8,
    sequence: U16,
}

struct Packet<B: ByteSlice> {
    header: LayoutVerified<B, Header>,
    data: B,
}

impl<B: ByteSlice> Packet<B> {
    fn parse(bytes: B) -> Option<Packet<B>> {
        let (header, data) = LayoutVerified::new_from_prefix(bytes)?;
        Some(Self { header, data })
    }

    #[allow(dead_code)]
    fn is_continuation(&self) -> bool {
        self.header.flags & 0x001 != 0
    }

    fn packet_type(&self) -> Result<PacketType> {
        match self.header.id {
            0x00 => Ok(PacketType::Error),
            0x01 => Ok(PacketType::Query),
            0x02 => Ok(PacketType::Init),
            0x03 => Ok(PacketType::Fastboot),
            _ => bail!("Unknown packet type"),
        }
    }
}

pub struct UdpNetworkInterface {
    maximum_size: u16,
    sequence: Wrapping<u16>,
    socket: UdpSocket,
    read_task: Option<Pin<Box<dyn Future<Output = std::io::Result<(usize, Vec<u8>)>>>>>,
    write_task: Option<Pin<Box<dyn Future<Output = std::io::Result<usize>>>>>,
}

impl UdpNetworkInterface {
    fn create_fastboot_packets(&mut self, buf: &[u8]) -> Result<Vec<Vec<u8>>> {
        // Leave four bytes for the header.
        let header_size = std::mem::size_of::<Header>() as u16;
        let max_chunk_size = self.maximum_size - header_size;
        let mut seq = self.sequence;
        let mut result = Vec::new();
        let mut iter = buf.chunks(max_chunk_size.into()).peekable();
        while let Some(chunk) = iter.next() {
            let mut packet: Vec<u8> = Vec::with_capacity(chunk.len() + header_size as usize);
            packet.push(0x03);
            if iter.peek().is_none() {
                packet.push(0x00);
            } else {
                packet.push(0x01); // Mark as continuation.
            }
            for _ in 0..2 {
                packet.push(0);
            }
            BigEndian::write_u16(&mut packet[2..4], seq.0);
            seq += Wrapping(1u16);
            packet.extend_from_slice(chunk);
            result.push(packet);
        }
        Ok(result)
    }
}

impl AsyncRead for UdpNetworkInterface {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<std::io::Result<usize>> {
        if self.read_task.is_none() {
            let socket = self.socket.clone();
            let seq = self.sequence;
            self.read_task.replace(Box::pin(async move {
                let (out_buf, sz) = send_to_device(&make_empty_fastboot_packet(seq.0), &socket)
                    .await
                    .map_err(|e| {
                        std::io::Error::new(
                            ErrorKind::Other,
                            format!("Could not send emtpy fastboot packet to device: {}", e),
                        )
                    })?;
                let packet = Packet::parse(&out_buf[..sz]).ok_or(std::io::Error::new(
                    ErrorKind::Other,
                    format!("Could not parse response packet"),
                ))?;
                let mut buf_inner = Vec::new();
                match packet.packet_type() {
                    Ok(PacketType::Fastboot) => {
                        let size = packet.data.len();
                        buf_inner.extend(packet.data);
                        Ok((size, buf_inner))
                    }
                    _ => Err(std::io::Error::new(
                        ErrorKind::Other,
                        format!("Unexpected reply from device"),
                    )),
                }
            }));
        }

        if let Some(ref mut task) = self.read_task {
            match task.as_mut().poll(cx) {
                Poll::Ready(Ok((sz, out_buf))) => {
                    self.read_task = None;
                    for i in 0..sz {
                        buf[i] = out_buf[i];
                    }
                    self.sequence += Wrapping(1u16);
                    Poll::Ready(Ok(sz))
                }
                Poll::Ready(Err(e)) => {
                    self.read_task = None;
                    Poll::Ready(Err(e))
                }
                Poll::Pending => Poll::Pending,
            }
        } else {
            // Really shouldn't get here
            Poll::Ready(Err(std::io::Error::new(
                ErrorKind::Other,
                format!("Could not add async task to read"),
            )))
        }
    }
}

impl AsyncWrite for UdpNetworkInterface {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<std::io::Result<usize>> {
        if self.write_task.is_none() {
            // TODO(fxb/78975): unfortunately the Task requires the 'static lifetime so we have to
            // copy the bytes and move them into the async block.
            let packets = self.create_fastboot_packets(buf).map_err(|e| {
                std::io::Error::new(
                    ErrorKind::Other,
                    format!("Could not create fastboot packets: {}", e),
                )
            })?;
            let socket = self.socket.clone();
            self.write_task.replace(Box::pin(async move {
                for packet in &packets {
                    let (out_buf, sz) = send_to_device(&packet, &socket).await.map_err(|e| {
                        std::io::Error::new(
                            ErrorKind::Other,
                            format!("Could not send emtpy fastboot packet to device: {}", e),
                        )
                    })?;
                    let response = Packet::parse(&out_buf[..sz]).ok_or(std::io::Error::new(
                        ErrorKind::Other,
                        format!("Could not parse response packet"),
                    ))?;
                    match response.packet_type() {
                        Ok(PacketType::Fastboot) => (),
                        _ => {
                            return Err(std::io::Error::new(
                                ErrorKind::Other,
                                format!("Unexpected Response packet"),
                            ))
                        }
                    }
                }
                Ok(packets.len())
            }));
        }

        if let Some(ref mut task) = self.write_task {
            match task.as_mut().poll(cx) {
                Poll::Ready(Ok(s)) => {
                    self.write_task = None;
                    for _i in 0..s {
                        self.sequence += Wrapping(1u16);
                    }
                    Poll::Ready(Ok(buf.len()))
                }
                Poll::Ready(Err(e)) => {
                    self.write_task = None;
                    Poll::Ready(Err(e))
                }
                Poll::Pending => Poll::Pending,
            }
        } else {
            // Really shouldn't get here
            Poll::Ready(Err(std::io::Error::new(
                ErrorKind::Other,
                format!("Could not add async task to write"),
            )))
        }
    }

    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
        unimplemented!();
    }

    fn poll_close(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
        unimplemented!();
    }
}

async fn send_to_device(buf: &[u8], socket: &UdpSocket) -> Result<([u8; 1500], usize)> {
    // Try sending twice
    socket.send(buf).await?;
    match wait_for_response(socket).await {
        Ok(r) => Ok(r),
        Err(e) => {
            tracing::error!("Could not get reply from Fastboot device - trying again: {}", e);
            socket.send(buf).await?;
            wait_for_response(socket)
                .await
                .or_else(|e| bail!("Did not get reply from Fastboot device: {}", e))
        }
    }
}

async fn wait_for_response(socket: &UdpSocket) -> Result<([u8; 1500], usize)> {
    let mut buf = [0u8; 1500]; // Responses should never get this big.
    timeout(REPLY_TIMEOUT, Box::pin(socket.recv(&mut buf[..])))
        .await
        .map_err(|_| anyhow!("Timed out waiting for reply"))?
        .map_err(|e| anyhow!("Recv error: {}", e))
        .map(|size| (buf, size))
}

async fn make_sender_socket(addr: SocketAddr) -> Result<UdpSocket> {
    let socket: std::net::UdpSocket = match addr {
        SocketAddr::V4(ref _saddr) => socket2::Socket::new(
            socket2::Domain::IPV4,
            socket2::Type::DGRAM,
            Some(socket2::Protocol::UDP),
        )
        .context("construct datagram socket")?,
        SocketAddr::V6(ref _saddr) => socket2::Socket::new(
            socket2::Domain::IPV6,
            socket2::Type::DGRAM,
            Some(socket2::Protocol::UDP),
        )
        .context("construct datagram socket")?,
    }
    .into();
    let result: UdpSocket = Async::new(socket)?.into();
    result.connect(addr).await.context("connect to remote address")?;
    Ok(result)
}

fn make_query_packet() -> [u8; 4] {
    let mut packet = [0u8; 4];
    packet[0] = 0x01;
    packet
}

fn make_init_packet(sequence: u16) -> [u8; 8] {
    let mut packet = [0u8; 8];
    packet[0] = 0x02;
    packet[1] = 0x00;
    BigEndian::write_u16(&mut packet[2..4], sequence);
    BigEndian::write_u16(&mut packet[4..6], 1);
    BigEndian::write_u16(&mut packet[6..8], MAX_SIZE);
    packet
}

fn make_empty_fastboot_packet(sequence: u16) -> [u8; 4] {
    let mut packet = [0u8; 4];
    packet[0] = 0x03;
    packet[1] = 0x00;
    BigEndian::write_u16(&mut packet[2..4], sequence);
    packet
}

pub struct UdpNetworkFactory {}

impl UdpNetworkFactory {
    pub fn new() -> Self {
        Self {}
    }
}

#[async_trait(?Send)]
impl InterfaceFactory<UdpNetworkInterface> for UdpNetworkFactory {
    async fn open(&mut self, target: &Target) -> Result<UdpNetworkInterface> {
        let addr = target.fastboot_address().ok_or(anyhow!("No network address for fastboot"))?.0;
        let mut to_sock: SocketAddr = addr.into();
        // TODO(fxb/78977): get the port from the mdns packet
        to_sock.set_port(HOST_PORT);
        let socket = make_sender_socket(to_sock).await?;
        let (buf, sz) = send_to_device(&make_query_packet(), &socket)
            .await
            .map_err(|e| anyhow!("Sending error: {}", e))?;
        let packet =
            Packet::parse(&buf[..sz]).ok_or(anyhow!("Could not parse response packet."))?;
        let sequence = match packet.packet_type() {
            Ok(PacketType::Query) => BigEndian::read_u16(&packet.data),
            _ => bail!("Unexpected response to query packet"),
        };
        let (buf, sz) = send_to_device(&make_init_packet(sequence), &socket)
            .await
            .map_err(|e| anyhow!("Sending error: {}", e))?;
        let packet =
            Packet::parse(&buf[..sz]).ok_or(anyhow!("Could not parse response packet."))?;
        let (version, max) = match packet.packet_type() {
            Ok(PacketType::Init) => {
                (BigEndian::read_u16(&packet.data[..2]), BigEndian::read_u16(&packet.data[2..4]))
            }
            _ => bail!("Unexpected response to init packet"),
        };
        let maximum_size = std::cmp::min(max, MAX_SIZE);
        tracing::debug!(
            "Fastboot over UDP connection established. Version {}. Max Size: {}",
            version,
            maximum_size
        );
        Ok(UdpNetworkInterface {
            socket,
            maximum_size,
            sequence: Wrapping(sequence + 1),
            read_task: None,
            write_task: None,
        })
    }

    async fn close(&self) {}
}
