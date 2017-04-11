// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Type-safe bindings for Magenta port objects.

use {HandleBase, Handle, HandleRef, Status, Time};
use {sys, into_result};

/// An object representing a Magenta
/// [port](https://fuchsia.googlesource.com/magenta/+/master/docs/objects/port.md).
///
/// As essentially a subtype of `Handle`, it can be freely interconverted.
pub struct Port(Handle);

impl HandleBase for Port {
    fn get_ref(&self) -> HandleRef {
        self.0.get_ref()
    }

    fn from_handle(handle: Handle) -> Self {
        Port(handle)
    }
}

impl Port {
    /// Create an IO port, allowing IO packets to be read and enqueued.
    ///
    /// Wraps the
    /// [mx_port_create](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/port_create.md)
    /// syscall.
    pub fn create(opts: PortOpts) -> Result<Port, Status> {
        unsafe {
            let mut handle = 0;
            let status = sys::mx_port_create(opts as u32, &mut handle);
            into_result(status, || Self::from_handle(Handle(handle)))
        }
    }

    /// Attempt to queue a user packet to the IO port.
    ///
    /// Wraps the
    /// [mx_port_queue](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/port_queue.md)
    /// syscall.
    pub fn queue(&self, packet: &sys::mx_port_packet_t) -> Result<(), Status> {
        let status = unsafe {
            sys::mx_port_queue(self.raw_handle(),
                packet as *const sys::mx_port_packet_t as *const u8, 0)
        };
        into_result(status, || ())
    }

    /// Wait for a packet to arrive on a (V2) port.
    ///
    /// Wraps the
    /// [mx_port_wait](https://fuchsia.googlesource.com/magenta/+/master/docs/syscalls/port_wait2.md)
    /// syscall.
    pub fn wait(&self, timeout: Time) -> Result<sys::mx_port_packet_t, Status> {
        let mut packet = Default::default();
        let status = unsafe {
            sys::mx_port_wait(self.raw_handle(), timeout,
                &mut packet as *mut sys::mx_port_packet_t as *mut u8, 0)
        };
        into_result(status, || packet)
    }
}

/// Options for creating a port.
#[repr(u32)]
pub enum PortOpts {
    V1 = sys::MX_PORT_OPT_V1,
    V2 = sys::MX_PORT_OPT_V2,
}

impl Default for PortOpts {
    fn default() -> Self {
        PortOpts::V2
    }
}

pub type Packet = sys::mx_port_packet_t;
pub type PacketType = sys::mx_packet_type_t;
pub type PacketSignal = sys::mx_packet_signal_t;
pub type PacketUser = sys::mx_packet_user_t;

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn port_basic() {
        let ten_ms: Time = 10_000_000;

        let port = Port::create(PortOpts::V2).unwrap();

        // Waiting now should time out.
        assert_eq!(port.wait(ten_ms), Err(Status::ErrTimedOut));

        // Send a valid packet.
        let packet = sys::mx_port_packet_t::new(
            42,
            sys::mx_packet_type_t::MX_PKT_TYPE_USER,
            123,
            [13; 32],
        );
        assert!(port.queue(&packet).is_ok());

        // Waiting should succeed this time. We should get back the packet we sent.
        let read_packet = port.wait(ten_ms).unwrap();
        assert_eq!(read_packet, packet);

        // Try sending a packet of a system type.
        let system_packet = sys::mx_port_packet_t::new(
            42,
            sys::mx_packet_type_t::MX_PKT_TYPE_SIGNAL_ONE,
            123,
            [13; 32],
        );
        assert!(port.queue(&system_packet).is_ok());

        // This still succeeds, but the type gets changed to MX_PKT_TYPE_USER internally.
        let read_packet = port.wait(ten_ms).unwrap();
        assert_eq!(read_packet, packet);
    }
}
