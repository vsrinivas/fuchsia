// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/97355): Remove once the device is complete.
#![allow(dead_code)]

use {
    crate::wire::{VirtioVsockFlags, VirtioVsockHeader},
    fidl_fuchsia_virtualization::GuestVsockAcceptorAcceptResponder,
    fuchsia_async as fasync, fuchsia_syslog as syslog, fuchsia_zircon as zx,
    std::convert::TryFrom,
};

// A connection key uniquely identifies a connection based on the hash of the source and
// destination port.
#[derive(Clone, Copy, Debug, Hash, PartialEq, Eq)]
pub struct VsockConnectionKey {
    pub host_port: u32,
    pub guest_port: u32,
}

impl VsockConnectionKey {
    pub fn new(host_port: u32, guest_port: u32) -> Self {
        VsockConnectionKey { host_port, guest_port }
    }
}

// Credit state of the transmit buffers.
#[derive(PartialEq, Debug)]
pub enum CreditState {
    // Available means the buffer has at least one byte of space for data.
    Available,
    // Unavailable means that the buffers are full. This is not an error, the device just needs
    // to wait for a reader to pull data off of the buffers before writing additional bytes.
    Unavailable,
}

pub struct VsockConnection {
    key: VsockConnectionKey,
    socket: fasync::Socket,
    responder: Option<GuestVsockAcceptorAcceptResponder>,

    // Cumulative flags seen for this connection. These flags are permanent once seen, and cannot
    // be reset without resetting this connection.
    conn_flags: VirtioVsockFlags,

    // Running count of bytes transmitted to and from the guest counted by the device. The
    // direction is from the perspective of the guest.
    rx_count: u32,
    tx_count: u32,

    // Amount of free buffer space in the host socket, reported to the guest.
    reported_buf_available: u32,

    // Amount of guest buffer space reported by the guest, and a running count of received bytes.
    guest_buf_alloc: u32,
    guest_fwd_count: u32,
}

impl Drop for VsockConnection {
    fn drop(&mut self) {
        // TODO(fxb/97355): Cancel pending socket read/write for this connection.
        if self.responder.is_some() {
            if let Err(err) = self
                .responder
                .take()
                .unwrap()
                .send(&mut Err(zx::Status::CONNECTION_REFUSED.into_raw()))
            {
                syslog::fx_log_err!(
                    "Connection {:?} failed to send closing message: {}",
                    self.key,
                    err
                );
            }
        }
    }
}

impl VsockConnection {
    pub fn new(
        key: VsockConnectionKey,
        socket: fasync::Socket,
        responder: Option<GuestVsockAcceptorAcceptResponder>,
    ) -> Self {
        VsockConnection {
            key,
            socket,
            responder,
            conn_flags: VirtioVsockFlags::default(),
            rx_count: 0,
            tx_count: 0,
            reported_buf_available: 0,
            guest_buf_alloc: 0,
            guest_fwd_count: 0,
        }
    }

    // Write credit to the header. This function additionally returns whether the host socket
    // currently has any remaining capacity for data transmission.
    //
    // Errors are non-recoverable, and will result in the connection being reset.
    pub fn write_credit(
        &mut self,
        header: &mut VirtioVsockHeader,
    ) -> Result<CreditState, zx::Status> {
        let socket_info = self.socket.as_ref().info()?;

        let socket_tx_max =
            u32::try_from(socket_info.tx_buf_max).map_err(|_| zx::Status::OUT_OF_RANGE)?;
        let socket_tx_current =
            u32::try_from(socket_info.tx_buf_size).map_err(|_| zx::Status::OUT_OF_RANGE)?;

        // Set the maximum host socket buffer size, and the running count of sent bytes. Note that
        // tx_count is from the perspective of the guest, so fwd_cnt is the number of bytes sent
        // from the guest to the device, minus the bytes sitting in the socket. The guest should
        // not have more outstanding bytes to send than the socket has total buffer space.
        header.buf_alloc = socket_tx_max.into();
        header.fwd_cnt = (self.tx_count - socket_tx_current).into();
        self.reported_buf_available = socket_tx_max - socket_tx_current;

        // It's not an error to be out of buffer space, it just means that the socket is backed
        // up and the client needs time to clear it.
        Ok(if self.reported_buf_available == 0 {
            CreditState::Unavailable
        } else {
            CreditState::Available
        })
    }
}

#[cfg(test)]
mod tests {
    use {super::*, anyhow::Error};

    #[fuchsia::test]
    async fn write_credit_with_credit_available() -> Result<(), Error> {
        let (remote, _local) = zx::Socket::create(zx::SocketOpts::DATAGRAM)?;

        // Shove some data into the remote socket, but less than the maximum.
        let max_tx_bytes = remote.info()?.tx_buf_max;
        let tx_bytes_to_use = max_tx_bytes / 2;
        let data = vec![0u8; tx_bytes_to_use];
        assert_eq!(tx_bytes_to_use, remote.write(&data)?);

        let async_remote = fasync::Socket::from_socket(remote)?;
        let mut connection =
            VsockConnection::new(VsockConnectionKey::new(1, 2), async_remote, None);

        // Use a mock tx_count, which is normally incremented when the guest transfers data to the
        // device. This is a running total of bytes received by the device, and the difference
        // between this and the bytes pending on the host socket is the bytes actually transferred
        // to a client.
        let bytes_actually_transferred = 100;
        connection.tx_count = (tx_bytes_to_use as u32) + bytes_actually_transferred;

        let mut header = VirtioVsockHeader::default();
        let status = connection.write_credit(&mut header)?;

        assert_eq!(status, CreditState::Available);
        assert_eq!(header.buf_alloc.get(), max_tx_bytes as u32);
        assert_eq!(header.fwd_cnt.get(), bytes_actually_transferred);

        Ok(())
    }

    #[fuchsia::test]
    async fn write_credit_with_no_credit_available() -> Result<(), Error> {
        let (remote, _local) = zx::Socket::create(zx::SocketOpts::DATAGRAM)?;

        // Max out the host socket, leaving zero bytes available.
        let max_tx_bytes = remote.info()?.tx_buf_max;
        let data = vec![0u8; max_tx_bytes];
        assert_eq!(max_tx_bytes, remote.write(&data)?);

        let async_remote = fasync::Socket::from_socket(remote)?;
        let mut connection =
            VsockConnection::new(VsockConnectionKey::new(1, 2), async_remote, None);

        // The tx_count being equal to the bytes pending on the socket means that no data has been
        // actually transferred to the client.
        connection.tx_count = max_tx_bytes as u32;

        let mut header = VirtioVsockHeader::default();
        let status = connection.write_credit(&mut header)?;

        assert_eq!(status, CreditState::Unavailable);
        assert_eq!(header.buf_alloc.get(), max_tx_bytes as u32);
        assert_eq!(header.fwd_cnt.get(), 0); // tx_count == the bytes pending on the socket

        Ok(())
    }
}
