// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context as _},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_developer_remotecontrol as rc, fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    fuchsia_zircon as zx,
    futures::{channel::mpsc, join, AsyncWriteExt, Future, StreamExt},
};

pub struct Bridge {
    sender: mpsc::UnboundedSender<zx::Socket>,
    task: fasync::Task<()>,
}

impl Bridge {
    // Forwards data received from a stream of sockets provided by the returned sender to another
    // stream of sockets provided by calls to |subscribe|.
    pub fn create_socket_forwarder() -> (mpsc::UnboundedSender<zx::Socket>, Self) {
        let (rx_sender, rx_receiver) = mpsc::unbounded::<zx::Socket>();
        (
            rx_sender,
            Bridge::create_forwarder(|sender| read_from_sequence_of_sockets(rx_receiver, sender)),
        )
    }

    // Forwards data received from a stream of archive clients provided by the returned sender to a
    // stream of sockets provided by calls to |subscribe|.
    pub fn create_archive_forwarder(
    ) -> (mpsc::UnboundedSender<ClientEnd<rc::ArchiveIteratorMarker>>, Self) {
        let (rx_sender, rx_receiver) = mpsc::unbounded::<ClientEnd<rc::ArchiveIteratorMarker>>();
        (
            rx_sender,
            Bridge::create_forwarder(|sender| read_from_sequence_of_archives(rx_receiver, sender)),
        )
    }

    fn create_forwarder<F>(
        read_from_sequence: impl FnOnce(mpsc::UnboundedSender<Vec<u8>>) -> F + Send + 'static,
    ) -> Self
    where
        F: Future<Output = anyhow::Result<()>> + Send,
    {
        let (tx_sender, tx_receiver) = mpsc::unbounded::<zx::Socket>();
        Self {
            sender: tx_sender,
            task: fasync::Task::spawn(async move {
                let (sender, receiver) = mpsc::unbounded::<Vec<u8>>();
                match join!(
                    read_from_sequence(sender),
                    write_to_sequence_of_sockets(tx_receiver, receiver),
                ) {
                    (Err(e), _) => {
                        fx_log_err!("failed to read from diagnostics sequence: {}", e);
                    }
                    (_, Err(e)) => {
                        fx_log_err!("failed to write to diagnostics sequence: {}", e);
                    }
                    _ => {}
                };
            }),
        }
    }

    pub fn subscribe(&self) -> Result<zx::Socket, zx::Status> {
        let (rx, tx) = zx::Socket::create(zx::SocketOpts::STREAM)?;
        self.sender.unbounded_send(tx).map_err(|_| zx::Status::BAD_STATE)?;
        Ok(rx)
    }

    pub async fn join(self) {
        self.task.await;
    }
}

async fn read_from_sequence_of_sockets(
    mut rx_receiver: mpsc::UnboundedReceiver<zx::Socket>,
    mut datagrams: mpsc::UnboundedSender<Vec<u8>>,
) -> anyhow::Result<()> {
    while let Some(socket) = rx_receiver.next().await {
        read_from_socket(socket, &mut datagrams).await?;
    }
    Ok(())
}

async fn read_from_sequence_of_archives(
    mut rx_receiver: mpsc::UnboundedReceiver<ClientEnd<rc::ArchiveIteratorMarker>>,
    mut datagrams: mpsc::UnboundedSender<Vec<u8>>,
) -> anyhow::Result<()> {
    while let Some(archive) = rx_receiver.next().await {
        let archive = archive.into_proxy().context("when creating ArchiveIterator proxy")?;
        loop {
            let entries = archive.get_next().await;
            match entries {
                Ok(Ok(_)) => {}
                Ok(Err(e)) => {
                    fx_log_warn!("ArchiveIterator.GetNext returned error: {:?}", e);
                    break;
                }
                Err(fidl::Error::ClientChannelClosed { .. }) => {
                    break;
                }
                Err(e) => {
                    fx_log_warn!("ArchiveIterator.GetNext failed: {}", e);
                    break;
                }
            };
            let entries = entries.unwrap().unwrap();
            if entries.is_empty() {
                break;
            }
            for entry in entries {
                match entry.diagnostics_data {
                    Some(rc::DiagnosticsData::Inline(inline)) => datagrams
                        .unbounded_send(inline.data.as_bytes().to_vec())
                        .map_err(|e| anyhow!("{}", e)),
                    Some(rc::DiagnosticsData::Socket(socket)) => {
                        read_from_socket(socket, &mut datagrams).await
                    }
                    None => Err(anyhow!("archive iterator entry is missing diagnostics_data")),
                }?;
            }
        }
    }
    Ok(())
}

async fn read_from_socket(
    socket: zx::Socket,
    datagrams: &mut mpsc::UnboundedSender<Vec<u8>>,
) -> anyhow::Result<()> {
    let socket = fasync::Socket::from_socket(socket).context("when converting async socket")?;
    let mut stream = socket.into_datagram_stream();
    while let Some(result) = stream.next().await {
        match result {
            Ok(datagram) => {
                datagrams.unbounded_send(datagram)?;
            }
            Err(e) => {
                fx_log_warn!("socket read: {}", e);
                break;
            }
        };
    }
    Ok(())
}

async fn write_to_sequence_of_sockets(
    mut tx_receiver: mpsc::UnboundedReceiver<zx::Socket>,
    mut datagrams: mpsc::UnboundedReceiver<Vec<u8>>,
) -> anyhow::Result<()> {
    let mut datagram = match datagrams.next().await {
        Some(datagram) => datagram,
        None => {
            return Ok(());
        }
    };
    let mut offset = 0;
    let mut socket = match tx_receiver.next().await {
        Some(zx_socket) => {
            fasync::Socket::from_socket(zx_socket).context("when converting async socket")
        }
        None => {
            fx_log_warn!("peer closed; unable to forward additional diagnostics");
            return Ok(());
        }
    }?;
    loop {
        if offset >= datagram.len() {
            datagram = match datagrams.next().await {
                Some(datagram) => datagram,
                None => {
                    return Ok(());
                }
            };
            offset = 0;
        }
        match socket.write(&datagram[offset..]).await {
            Ok(bytes_written) => {
                offset += bytes_written;
            }
            Err(e) => {
                fx_log_warn!("socket write: {}", e);
                socket = match tx_receiver.next().await {
                    Some(zx_socket) => fasync::Socket::from_socket(zx_socket)
                        .context("when converting async socket"),
                    None => {
                        fx_log_warn!("peer closed; unable to forward additional diagnostics");
                        return Ok(());
                    }
                }?;
            }
        };
    }
}
