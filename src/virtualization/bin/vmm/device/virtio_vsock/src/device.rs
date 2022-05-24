// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxb/97355): Remove once the device is complete.
#![allow(dead_code)]

use {
    crate::connection::{VsockConnection, VsockConnectionKey},
    crate::wire::{VirtioVsockConfig, LE64},
    anyhow::{anyhow, Error},
    fidl_fuchsia_virtualization::{
        GuestVsockAcceptorAcceptResponder, GuestVsockAcceptorRequestStream,
        GuestVsockEndpointControlHandle, HostVsockConnectorProxy,
    },
    fuchsia_async as fasync, fuchsia_syslog as syslog, fuchsia_zircon as zx,
    futures::TryStreamExt,
    std::{
        cell::{Cell, RefCell},
        collections::HashMap,
        rc::Rc,
    },
    virtio_device::{chain::ReadableChain, mem::DriverMem, queue::DriverNotify},
};

pub struct VsockDevice {
    pub config: RefCell<VirtioVsockConfig>,
    pub connections: RefCell<HashMap<VsockConnectionKey, Rc<VsockConnection>>>,

    // TODO(fxb/97355): Remove when FIDL protocol has been updated.
    pub connector: Cell<Option<HostVsockConnectorProxy>>,
    pub control_handle: Cell<Option<GuestVsockEndpointControlHandle>>,
}

impl VsockDevice {
    pub fn new() -> Rc<Self> {
        Rc::new(Self {
            config: RefCell::new(VirtioVsockConfig::new_with_default_cid()),
            connections: RefCell::new(HashMap::new()),
            connector: Cell::new(None),
            control_handle: Cell::new(None),
        })
    }

    pub fn set_guest_cid(&self, guest_cid: u32) -> Result<(), Error> {
        if VsockDevice::is_reserved_guest_cid(guest_cid) {
            return Err(anyhow!("{} is reserved and cannot be used as the guest CID", guest_cid));
        }

        self.config.borrow_mut().guest_cid = LE64::new(guest_cid.into());
        Ok(())
    }

    // TODO(fxb/97355): Deprecate this FIDL protocol. Once guest manager supports CFv2, we want to
    // expose the GuestVsockAcceptor from this device and the HostVsockConnector from the guest
    // manager, and connect directly. This will prevent a race in starting this device where
    // queues can be processed before the host connector is available.
    pub async fn legacy_set_context_id(
        &self,
        guest_cid: u32,
        host_connector_proxy: HostVsockConnectorProxy,
        guest_acceptor_stream: GuestVsockAcceptorRequestStream,
        control_handle: GuestVsockEndpointControlHandle,
    ) -> Result<(), Error> {
        self.set_guest_cid(guest_cid)?;

        self.connector.set(Some(host_connector_proxy));
        self.control_handle.set(Some(control_handle));

        guest_acceptor_stream
            .try_for_each(|request| async {
                let (src_cid, src_port, port, socket, responder) = request.into_accept().unwrap();
                self.accept_guest_connection(src_cid, src_port, port, socket, responder)
            })
            .await?;

        Ok(())
    }

    pub fn guest_cid(&self) -> u32 {
        // The upper 32 bits of this u64 are reserved, and unused.
        self.config.borrow().guest_cid.get() as u32
    }

    pub async fn handle_tx_queue<'a, 'b, N: DriverNotify, M: DriverMem>(
        &self,
        _chain: ReadableChain<'a, 'b, N, M>,
    ) -> Result<(), anyhow::Error> {
        // TODO(fxb/97355): Do this.
        Ok(())
    }

    fn accept_guest_connection(
        &self,
        _host_cid: u32,
        host_port: u32,
        guest_port: u32,
        socket: zx::Socket,
        responder: GuestVsockAcceptorAcceptResponder,
    ) -> Result<(), fidl::Error> {
        let key = VsockConnectionKey::new(host_port, guest_port);
        if self.connections.borrow().contains_key(&key) {
            syslog::fx_log_err!("Connection already exists: {:?}", key);
            responder.send(&mut Err(zx::Status::ALREADY_BOUND.into_raw()))?;
            return Ok(());
        }

        let socket = fasync::Socket::from_socket(socket);
        if let Err(status) = socket {
            responder.send(&mut Err(status.into_raw()))?;
            return Ok(());
        }

        let connection =
            Rc::new(VsockConnection::new(key.clone(), socket.unwrap(), Some(responder)));
        assert!(self.connections.borrow_mut().insert(key, connection).is_none());

        Ok(())
    }

    fn is_reserved_guest_cid(guest_cid: u32) -> bool {
        // 5.10.4 Device configuration layout
        //
        // The following CIDs are reserved and cannot be used as the guest's context ID.
        let reserved = [0, 1, 2, 0xffffffff];
        reserved.contains(&guest_cid)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::RequestStream,
        fidl_fuchsia_virtualization::{
            GuestVsockAcceptorMarker, GuestVsockAcceptorProxy, GuestVsockEndpointMarker,
            GuestVsockEndpointProxy, HostVsockConnectorMarker, HostVsockConnectorRequestStream,
        },
        futures::select,
    };

    struct DeviceEndpoints {
        guest_proxy: GuestVsockAcceptorProxy,
        host_stream: HostVsockConnectorRequestStream,
        endpoint_proxy: GuestVsockEndpointProxy,
    }

    fn serve_device_endpoints() -> Result<DeviceEndpoints, Error> {
        let (guest_proxy, guest_stream) =
            fidl::endpoints::create_proxy_and_stream::<GuestVsockAcceptorMarker>()?;
        let (host_proxy, host_stream) =
            fidl::endpoints::create_proxy_and_stream::<HostVsockConnectorMarker>()?;
        let (endpoint_proxy, endpoint_stream) =
            fidl::endpoints::create_proxy_and_stream::<GuestVsockEndpointMarker>()?;
        let device = VsockDevice::new();

        fasync::Task::local(async move {
            device
                .legacy_set_context_id(
                    fidl_fuchsia_virtualization::DEFAULT_GUEST_CID,
                    host_proxy,
                    guest_stream,
                    endpoint_stream.control_handle(),
                )
                .await
                .unwrap_or_else(|e| panic!("Error while serving device: {}", e))
        })
        .detach();

        Ok(DeviceEndpoints { guest_proxy, host_stream, endpoint_proxy })
    }

    #[fuchsia::test]
    async fn check_reserved_cids() {
        // The host CID should be reserved, while the default guest CID should not be.
        assert!(VsockDevice::is_reserved_guest_cid(fidl_fuchsia_virtualization::HOST_CID));
        assert!(!VsockDevice::is_reserved_guest_cid(
            fidl_fuchsia_virtualization::DEFAULT_GUEST_CID
        ));
    }

    #[fuchsia::test]
    async fn multiple_identical_connections() {
        let DeviceEndpoints { guest_proxy, .. } =
            serve_device_endpoints().expect("Failed to serve device endpoints");

        let (first_remote_socket, _) = zx::Socket::create(zx::SocketOpts::DATAGRAM)
            .expect("Failed to create first socket pair");
        let (second_remote_socket, _) = zx::Socket::create(zx::SocketOpts::DATAGRAM)
            .expect("Failed to create second socket pair");

        // Same source and destination CID + port combo for each connection request.
        let mut first_result = guest_proxy.accept(1, 2, 3, first_remote_socket);
        let mut second_result = guest_proxy.accept(1, 2, 3, second_remote_socket);

        // The first connection will not complete since we aren't servicing queues, so only the
        // second connection (which fails since the first connection already exists) will return
        // a result.
        let finished_result = select! {
            result = first_result => result.expect("First future failed unexpectedly"),
            result = second_result => result.expect("Second future failed unexpectedly"),
        }
        .expect_err("Identical connections should have returned an error result");

        assert_eq!(zx::Status::from_raw(finished_result), zx::Status::ALREADY_BOUND);
    }
}
