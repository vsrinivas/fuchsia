use crate::devices::BindingId;
use crate::eventloop::{Event, EventLoop};
use failure::Error;
use fidl_fuchsia_posix_socket as psocket;
use fuchsia_async as fasync;
use fuchsia_zircon::{self as zx, prelude::HandleBased};
use futures::channel::mpsc;
use futures::{TryFutureExt, TryStreamExt};
use log::error;
use net_types::ip::{AddrSubnet, AddrSubnetEither, IpAddr, IpVersion, Subnet, SubnetEither};
use std::sync::{Arc, Mutex};

pub struct SocketControlWorker {
    events: psocket::ControlRequestStream,
    inner: Arc<Mutex<SocketControlWorkerInner>>,
}

#[derive(Debug)]
pub struct SocketControlWorkerInner {
    local_socket: zx::Socket,
    peer_socket: zx::Socket,
    info: SocketControlInfo,
}

#[derive(Debug)]
pub enum SocketControlInfo {
    Unbound(UnboundSocket),
    Bound(SocketWorker),
}

#[derive(Debug)]
pub struct UnboundSocket {
    net_proto: IpVersion,    // TODO(wesleyac): Pull into type?
    trans_proto: TransProto, // TODO(wesleyac): Pull into type?
    nonblock: bool,
}

#[derive(Debug)]
pub enum TransProto {
    UDP,
    TCP,
}

impl SocketControlWorker {
    pub fn new(
        events: psocket::ControlRequestStream,
        net_proto: IpVersion,
        trans_proto: TransProto,
        nonblock: bool,
    ) -> Result<Self, ()> {
        let sockopt = match trans_proto {
            TransProto::UDP => zx::SocketOpts::DATAGRAM,
            TransProto::TCP => zx::SocketOpts::STREAM,
        };
        let (local_socket, peer_socket) = zx::Socket::create(sockopt).map_err(|_| ())?;
        Ok(Self {
            events,
            inner: Arc::new(Mutex::new(SocketControlWorkerInner {
                local_socket,
                peer_socket,
                info: SocketControlInfo::Unbound(UnboundSocket {
                    net_proto,
                    trans_proto,
                    nonblock,
                }),
            })),
        })
    }

    pub fn spawn(mut self, sender: mpsc::UnboundedSender<Event>) {
        fasync::spawn_local(
            async move {
                while let Some(evt) = self.events.try_next().await? {
                    sender.unbounded_send(Event::FidlSocketControlEvent((
                        Arc::clone(&self.inner),
                        evt,
                    )));
                }
                Ok(())
            }
                .unwrap_or_else(|e: Error| error!("{:?}", e)),
        );
    }
}

impl SocketControlWorkerInner {
    pub fn handle_request(&mut self, event_loop: &mut EventLoop, req: psocket::ControlRequest) {
        match req {
            psocket::ControlRequest::Clone { .. } => {}
            psocket::ControlRequest::Close { .. } => {}
            psocket::ControlRequest::Describe { responder } => {
                let peer = self.peer_socket.duplicate_handle(zx::Rights::SAME_RIGHTS);
                if let Ok(peer) = peer {
                    let mut info =
                        fidl_fuchsia_io::NodeInfo::Socket(fidl_fuchsia_io::Socket { socket: peer });
                    responder.send(&mut info);
                }
                // If the call to duplicate_handle fails, we have no choice but to drop the
                // responder and close the channel, since Describe must be infallible.
            }
            psocket::ControlRequest::Sync { .. } => {}
            psocket::ControlRequest::GetAttr { .. } => {}
            psocket::ControlRequest::SetAttr { .. } => {}
            psocket::ControlRequest::Ioctl { .. } => {}
            psocket::ControlRequest::Bind { .. } => {}
            psocket::ControlRequest::Connect { .. } => {}
            psocket::ControlRequest::Listen { .. } => {}
            psocket::ControlRequest::Accept { .. } => {}
            psocket::ControlRequest::GetSockName { .. } => {}
            psocket::ControlRequest::GetPeerName { .. } => {}
            psocket::ControlRequest::SetSockOpt { .. } => {}
            psocket::ControlRequest::GetSockOpt { .. } => {}
            psocket::ControlRequest::IoctlPosix { .. } => {}
        }
    }
}

#[derive(Debug)]
pub struct SocketWorker {
    address: IpAddr,
    port: u16,
    nic: BindingId,
    trans_proto: TransProto,
}

impl SocketWorker {
    pub fn spawn(mut self) {
        unimplemented!()
    }
}
