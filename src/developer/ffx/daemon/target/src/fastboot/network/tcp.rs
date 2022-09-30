// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(unused_imports, unused_variables, dead_code)]
use {
    crate::{fastboot::InterfaceFactory, target::Target},
    anyhow::{anyhow, bail, Context as _, Result},
    async_net::TcpStream,
    async_trait::async_trait,
    futures::{
        prelude::*,
        task::{Context, Poll},
    },
    std::convert::TryInto,
    std::io::ErrorKind,
    std::net::SocketAddr,
    std::pin::Pin,
    tracing::debug,
};

const FB_HANDSHAKE: [u8; 4] = *b"FB01";

pub struct TcpNetworkInterface {
    stream: TcpStream,
    read_avail_bytes: Option<u64>,
    /// Returns a tuple of (avail_bytes, bytes_read, bytes)
    read_task: Option<Pin<Box<dyn Future<Output = std::io::Result<(u64, usize, Vec<u8>)>>>>>,
    write_task: Option<Pin<Box<dyn Future<Output = std::io::Result<usize>>>>>,
}

impl AsyncRead for TcpNetworkInterface {
    fn poll_read(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &mut [u8],
    ) -> Poll<std::io::Result<usize>> {
        if self.read_task.is_none() {
            let mut stream = self.stream.clone();
            let avail_bytes = self.read_avail_bytes;
            let length = buf.len();
            self.read_task.replace(Box::pin(async move {
                let mut avail_bytes = match avail_bytes {
                    Some(value) => value,
                    None => {
                        let mut pkt_len = [0; 8];
                        let bytes_read = stream.read(&mut pkt_len).await?;
                        if bytes_read != pkt_len.len() {
                            return Err(std::io::Error::new(
                                ErrorKind::Other,
                                format!("Could not read packet header"),
                            ));
                        }
                        u64::from_be_bytes(pkt_len)
                    }
                };

                let mut data_buf = vec![0; avail_bytes.try_into().unwrap()];
                let bytes_read: u64 =
                    stream.read(data_buf.as_mut_slice()).await?.try_into().unwrap();
                avail_bytes -= bytes_read;

                Ok((avail_bytes, bytes_read.try_into().unwrap(), data_buf))
            }));
        }

        if let Some(ref mut task) = self.read_task {
            match task.as_mut().poll(cx) {
                Poll::Ready(Ok((avail_bytes, bytes_read, data))) => {
                    self.read_task = None;
                    self.read_avail_bytes = if avail_bytes == 0 { None } else { Some(avail_bytes) };
                    buf[0..bytes_read].copy_from_slice(&data[0..bytes_read]);
                    Poll::Ready(Ok(bytes_read))
                }
                Poll::Ready(Err(e)) => {
                    self.read_task = None;
                    Poll::Ready(Err(e))
                }
                Poll::Pending => Poll::Pending,
            }
        } else {
            unreachable!()
        }
    }
}

impl AsyncWrite for TcpNetworkInterface {
    fn poll_write(
        mut self: Pin<&mut Self>,
        cx: &mut Context<'_>,
        buf: &[u8],
    ) -> Poll<std::io::Result<usize>> {
        if self.write_task.is_none() {
            let mut stream = self.stream.clone();
            let mut data = vec![];
            data.extend(TryInto::<u64>::try_into(buf.len()).unwrap().to_be_bytes());
            data.extend(buf);
            self.write_task.replace(Box::pin(async move {
                let mut start = 0;
                while start < data.len() {
                    // We won't always succeed in writing the entire buffer at once, so
                    // we try repeatedly until everything is written.
                    let written = stream.write(&data[start..]).await?;
                    if written == 0 {
                        return Err(std::io::Error::new(
                            ErrorKind::Other,
                            format!("Write made no progress"),
                        ));
                    }

                    start += written;
                }
                Ok(data.len())
            }));
        }

        if let Some(ref mut task) = self.write_task {
            match task.as_mut().poll(cx) {
                Poll::Ready(Ok(s)) => {
                    self.write_task = None;
                    Poll::Ready(Ok(s))
                }
                Poll::Ready(Err(e)) => {
                    self.write_task = None;
                    Poll::Ready(Err(e))
                }
                Poll::Pending => Poll::Pending,
            }
        } else {
            unreachable!();
        }
    }

    fn poll_flush(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
        unimplemented!();
    }

    fn poll_close(self: Pin<&mut Self>, _cx: &mut Context<'_>) -> Poll<std::io::Result<()>> {
        unimplemented!();
    }
}

pub struct TcpNetworkFactory {}

impl TcpNetworkFactory {
    pub fn new() -> Self {
        Self {}
    }
}

async fn handshake(stream: &mut TcpStream) -> Result<()> {
    stream.write(&FB_HANDSHAKE).await.context("Sending handshake")?;
    let mut response = [0; 4];
    stream.read_exact(&mut response).await.context("Receiving handshake response")?;
    if response != FB_HANDSHAKE {
        bail!("Invalid response to handshake");
    }
    Ok(())
}

const OPEN_RETRY: i32 = 5;
const RETRY_WAIT_SECONDS: u64 = 5;
const FASTBOOT_PORT: u16 = 5554;

async fn open_once(target: &Target) -> Result<TcpNetworkInterface> {
    let mut addr: SocketAddr =
        target.fastboot_address().ok_or(anyhow!("No network address for fastboot"))?.0.into();
    addr.set_port(FASTBOOT_PORT);
    let mut stream = TcpStream::connect(addr).await.context("Establishing TCP connection")?;
    handshake(&mut stream).await?;

    Ok(TcpNetworkInterface { stream, read_avail_bytes: None, read_task: None, write_task: None })
}

#[async_trait(?Send)]
impl InterfaceFactory<TcpNetworkInterface> for TcpNetworkFactory {
    async fn open(&mut self, target: &Target) -> Result<TcpNetworkInterface> {
        for retry in 0..OPEN_RETRY {
            match open_once(target).await {
                Ok(res) => {
                    tracing::debug!("TCP connect attempt #{} succeeds", retry);
                    return Ok(res);
                }
                Err(e) => {
                    if retry + 1 < OPEN_RETRY {
                        tracing::debug!("TCP connect attempt #{} failed", retry);
                        std::thread::sleep(std::time::Duration::from_secs(RETRY_WAIT_SECONDS));
                        continue;
                    }

                    return Err(e);
                }
            }
        }

        Err(anyhow::format_err!("Unreachable"))
    }

    async fn close(&self) {}
}
