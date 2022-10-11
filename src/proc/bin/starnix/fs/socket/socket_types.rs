// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::logging::not_implemented;
use bitflags::bitflags;
use zerocopy::AsBytes;

use crate::fs::*;
use crate::types::*;
pub use syncio::ZxioShutdownFlags as SocketShutdownFlags;

bitflags! {
    pub struct SocketMessageFlags: u32 {
        const PEEK = MSG_PEEK;
        const DONTROUTE = MSG_DONTROUTE;
        const TRYHARD = MSG_TRYHARD;
        const CTRUNC = MSG_CTRUNC;
        const PROBE = MSG_PROBE;
        const TRUNC = MSG_TRUNC;
        const DONTWAIT = MSG_DONTWAIT;
        const EOR = MSG_EOR;
        const WAITALL = MSG_WAITALL;
        const FIN = MSG_FIN;
        const SYN = MSG_SYN;
        const CONFIRM = MSG_CONFIRM;
        const RST = MSG_RST;
        const ERRQUEUE = MSG_ERRQUEUE;
        const NOSIGNAL = MSG_NOSIGNAL;
        const MORE = MSG_MORE;
        const WAITFORONE = MSG_WAITFORONE;
        const BATCH = MSG_BATCH;
        const FASTOPEN = MSG_FASTOPEN;
        const CMSG_CLOEXEC = MSG_CMSG_CLOEXEC;
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum SocketDomain {
    /// The `Unix` socket domain contains sockets that were created with the `AF_UNIX` domain. These
    /// sockets communicate locally, with other sockets on the same host machine.
    Unix,

    /// An AF_VSOCK socket for communication from a controlling operating system
    Vsock,

    /// An AF_INET socket.
    Inet,

    /// An AF_INET6 socket.
    Inet6,

    /// An IF_NETLINK socket (currently stubbed out).
    Netlink,
}

impl SocketDomain {
    pub fn from_raw(raw: u16) -> Option<SocketDomain> {
        match raw {
            AF_UNIX => Some(Self::Unix),
            AF_VSOCK => Some(Self::Vsock),
            AF_INET => Some(Self::Inet),
            AF_INET6 => Some(Self::Inet6),
            AF_NETLINK => Some(Self::Netlink),
            _ => None,
        }
    }

    pub fn as_raw(self) -> u16 {
        match self {
            Self::Unix => AF_UNIX,
            Self::Vsock => AF_VSOCK,
            Self::Inet => AF_INET,
            Self::Inet6 => AF_INET6,
            Self::Netlink => AF_NETLINK,
        }
    }
}

#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum SocketType {
    Stream,
    Datagram,
    Raw,
    SeqPacket,
}

impl SocketType {
    pub fn from_raw(raw: u32) -> Option<SocketType> {
        match raw {
            SOCK_STREAM => Some(SocketType::Stream),
            SOCK_DGRAM => Some(SocketType::Datagram),
            SOCK_RAW => Some(SocketType::Datagram),
            SOCK_SEQPACKET => Some(SocketType::SeqPacket),
            _ => None,
        }
    }

    pub fn as_raw(&self) -> u32 {
        match self {
            SocketType::Stream => SOCK_STREAM,
            SocketType::Datagram => SOCK_DGRAM,
            SocketType::Raw => SOCK_RAW,
            SocketType::SeqPacket => SOCK_SEQPACKET,
        }
    }
}

#[derive(Default)]
pub struct SocketProtocol(u32);

impl SocketProtocol {
    pub fn from_raw(protocol: u32) -> Self {
        SocketProtocol(protocol)
    }

    pub fn as_raw(&self) -> u32 {
        self.0
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum SocketAddress {
    /// An address in the AF_UNSPEC domain.
    #[allow(dead_code)]
    Unspecified,

    /// A `Unix` socket address contains the filesystem path that was used to bind the socket.
    Unix(FsString),

    /// An AF_VSOCK socket is just referred to by its listening port on the client
    Vsock(u32),

    /// AF_INET socket addresses are passed through as a sockaddr* to zxio.
    Inet(Vec<u8>),

    /// AF_INET6 socket addresses are passed through as a sockaddr* to zxio.
    Inet6(Vec<u8>),

    /// No address for netlink sockets while stubbed.
    Netlink(u32),
}

pub const SA_FAMILY_SIZE: usize = std::mem::size_of::<uapi::__kernel_sa_family_t>();

impl SocketAddress {
    pub fn default_for_domain(domain: SocketDomain) -> SocketAddress {
        match domain {
            SocketDomain::Unix => SocketAddress::Unix(FsString::new()),
            SocketDomain::Vsock => SocketAddress::Vsock(0xffff),
            SocketDomain::Inet => {
                SocketAddress::Inet(uapi::sockaddr_in::default().as_bytes().to_vec())
            }
            SocketDomain::Inet6 => {
                SocketAddress::Inet6(uapi::sockaddr_in6::default().as_bytes().to_vec())
            }
            SocketDomain::Netlink => SocketAddress::Netlink(0),
        }
    }

    pub fn valid_for_domain(&self, domain: SocketDomain) -> bool {
        match self {
            SocketAddress::Unspecified => false,
            SocketAddress::Unix(_) => domain == SocketDomain::Unix,
            SocketAddress::Vsock(_) => domain == SocketDomain::Vsock,
            SocketAddress::Inet(_) => domain == SocketDomain::Inet,
            SocketAddress::Inet6(_) => domain == SocketDomain::Inet6,
            SocketAddress::Netlink(_) => domain == SocketDomain::Netlink,
        }
    }

    pub fn to_bytes(&self) -> Vec<u8> {
        match self {
            SocketAddress::Unspecified => AF_UNSPEC.to_ne_bytes().to_vec(),
            SocketAddress::Unix(name) => {
                if !name.is_empty() {
                    let template = sockaddr_un::default();
                    let path_length = std::cmp::min(template.sun_path.len() - 1, name.len());
                    let mut bytes = vec![0u8; SA_FAMILY_SIZE + path_length + 1];
                    bytes[..SA_FAMILY_SIZE].copy_from_slice(&AF_UNIX.to_ne_bytes());
                    bytes[SA_FAMILY_SIZE..(SA_FAMILY_SIZE + path_length)]
                        .copy_from_slice(&name[..path_length]);
                    bytes
                } else {
                    AF_UNIX.to_ne_bytes().to_vec()
                }
            }
            SocketAddress::Vsock(port) => {
                let mut bytes = vec![0u8; std::mem::size_of::<sockaddr_vm>()];
                let vm_addr = sockaddr_vm::new(*port);
                vm_addr.write_to(&mut bytes[..]);
                bytes
            }
            SocketAddress::Inet(addr) | SocketAddress::Inet6(addr) => addr.to_vec(),
            SocketAddress::Netlink(_) => {
                not_implemented!("?", "SocketAddress::to_bytes is stubbed for Netlink");
                vec![]
            }
        }
    }

    pub fn is_abstract_unix(&self) -> bool {
        match self {
            SocketAddress::Unix(name) => name.first() == Some(&b'\0'),
            _ => false,
        }
    }

    pub fn bytes_into_inet(addr: Vec<u8>) -> Result<SocketAddress, Errno> {
        if addr.len() < SA_FAMILY_SIZE {
            return Ok(SocketAddress::default_for_domain(SocketDomain::Inet));
        }

        let mut family_bytes = [0u8; SA_FAMILY_SIZE];
        family_bytes[..SA_FAMILY_SIZE].copy_from_slice(&addr[..SA_FAMILY_SIZE]);
        let family = uapi::__kernel_sa_family_t::from_ne_bytes(family_bytes);

        match family {
            AF_INET => Ok(SocketAddress::Inet(addr)),
            AF_INET6 => Ok(SocketAddress::Inet6(addr)),
            _ => error!(EINVAL),
        }
    }
}
