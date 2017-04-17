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

    /// Cancel pending wait_async calls for an object with the given key.
    ///
    /// Wraps the
    /// [mx_port_cancel](https://fuchsia.googlesource.com/magenta/+/HEAD/docs/syscalls/port_cancel.md)
    /// syscall.
    pub fn cancel<H>(&self, source: &H, key: u64) -> Result<(), Status> where H: HandleBase {
        let status = unsafe {
            sys::mx_port_cancel(self.raw_handle(), source.raw_handle(), key)
        };
        into_result(status, || ())
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

/// Options for wait_async.
#[repr(u32)]
pub enum WaitAsyncOpts {
    Once = sys::MX_WAIT_ASYNC_ONCE,
    Repeating = sys::MX_WAIT_ASYNC_REPEATING,
}

pub type Packet = sys::mx_port_packet_t;
pub type PacketType = sys::mx_packet_type_t;
pub type PacketSignal = sys::mx_packet_signal_t;
pub type PacketUser = sys::mx_packet_user_t;

#[cfg(test)]
mod tests {
    use super::*;
    use {Duration, Event, EventOpts};
    use {MX_SIGNAL_NONE, MX_USER_SIGNAL_0, MX_USER_SIGNAL_1};
    use deadline_after;

    #[test]
    fn port_basic() {
        let ten_ms: Duration = 10_000_000;

        let port = Port::create(PortOpts::V2).unwrap();

        // Waiting now should time out.
        assert_eq!(port.wait(deadline_after(ten_ms)), Err(Status::ErrTimedOut));

        // Send a valid packet.
        let packet = sys::mx_port_packet_t::new(
            42,
            sys::mx_packet_type_t::MX_PKT_TYPE_USER,
            123,
            [13; 32],
        );
        assert!(port.queue(&packet).is_ok());

        // Waiting should succeed this time. We should get back the packet we sent.
        let read_packet = port.wait(deadline_after(ten_ms)).unwrap();
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
        let read_packet = port.wait(deadline_after(ten_ms)).unwrap();
        assert_eq!(read_packet, packet);
    }

    #[test]
    fn wait_async_once() {
        let ten_ms: Duration = 10_000_000;
        let key = 42;

        let port = Port::create(PortOpts::V2).unwrap();
        let event = Event::create(EventOpts::Default).unwrap();

        assert!(event.wait_async(&port, key, MX_USER_SIGNAL_0 | MX_USER_SIGNAL_1,
            WaitAsyncOpts::Once).is_ok());

        // Waiting without setting any signal should time out.
        assert_eq!(port.wait(deadline_after(ten_ms)), Err(Status::ErrTimedOut));

        // If we set a signal, we should be able to wait for it.
        assert!(event.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_0).is_ok());
        let read_packet = port.wait(deadline_after(ten_ms)).unwrap();
        let expected_packet = sys::mx_port_packet_t::new_signal(
            key,
            sys::mx_packet_type_t::MX_PKT_TYPE_SIGNAL_ONE,
            0,
            sys::mx_packet_signal_t {
                trigger: MX_USER_SIGNAL_0 | MX_USER_SIGNAL_1,
                observed: MX_USER_SIGNAL_0,
                count: 1,
            },
        );
        assert_eq!(read_packet, expected_packet);

        // Shouldn't get any more packets.
        assert_eq!(port.wait(deadline_after(ten_ms)), Err(Status::ErrTimedOut));

        // Calling wait_async again should result in another packet.
        assert!(event.wait_async(&port, key, MX_USER_SIGNAL_0, WaitAsyncOpts::Once).is_ok());
        let read_packet = port.wait(deadline_after(ten_ms)).unwrap();
        let expected_packet = sys::mx_port_packet_t::new_signal(
            key,
            sys::mx_packet_type_t::MX_PKT_TYPE_SIGNAL_ONE,
            0,
            sys::mx_packet_signal_t {
                trigger: MX_USER_SIGNAL_0,
                observed: MX_USER_SIGNAL_0,
                count: 1,
            },
        );
        assert_eq!(read_packet, expected_packet);

        // Calling wait_async then cancel, we should still get the packet as it will have been sent
        // before the cancel call.
        assert!(event.wait_async(&port, key, MX_USER_SIGNAL_0, WaitAsyncOpts::Once).is_ok());
        assert!(port.cancel(&event, key).is_ok());
        let read_packet = port.wait(ten_ms).unwrap();
        assert_eq!(read_packet, expected_packet);

        // However if the event is signalled after the cancel, we shouldn't get a packet.
        assert!(event.signal(MX_USER_SIGNAL_0, MX_SIGNAL_NONE).is_ok());  // clear signal
        assert!(event.wait_async(&port, key, MX_USER_SIGNAL_0, WaitAsyncOpts::Once).is_ok());
        assert!(port.cancel(&event, key).is_ok());
        assert!(event.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_0).is_ok());
        assert_eq!(port.wait(deadline_after(ten_ms)), Err(Status::ErrTimedOut));
    }

    #[test]
    fn wait_async_repeating() {
        let ten_ms: Duration = 10_000_000;
        let key = 42;

        let port = Port::create(PortOpts::V2).unwrap();
        let event = Event::create(EventOpts::Default).unwrap();

        assert!(event.wait_async(&port, key, MX_USER_SIGNAL_0 | MX_USER_SIGNAL_1,
            WaitAsyncOpts::Repeating).is_ok());

        // Waiting without setting any signal should time out.
        assert_eq!(port.wait(deadline_after(ten_ms)), Err(Status::ErrTimedOut));

        // If we set a signal, we should be able to wait for it.
        assert!(event.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_0).is_ok());
        let read_packet = port.wait(deadline_after(ten_ms)).unwrap();
        let expected_packet = sys::mx_port_packet_t::new_signal(
            key,
            sys::mx_packet_type_t::MX_PKT_TYPE_SIGNAL_REP,
            0,
            sys::mx_packet_signal_t {
                trigger: MX_USER_SIGNAL_0 | MX_USER_SIGNAL_1,
                observed: MX_USER_SIGNAL_0,
                count: 1,
            },
        );
        assert_eq!(read_packet, expected_packet);

        // Should not get any more packets, as MX_WAIT_ASYNC_REPEATING is edge triggered rather than
        // level triggered.
        assert_eq!(port.wait(deadline_after(ten_ms)), Err(Status::ErrTimedOut));

        // If we clear and resignal, we should get the same packet again,
        // even though we didn't call event.wait_async again.
        assert!(event.signal(MX_USER_SIGNAL_0, MX_SIGNAL_NONE).is_ok());  // clear signal
        assert!(event.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_0).is_ok());
        let read_packet = port.wait(deadline_after(ten_ms)).unwrap();
        assert_eq!(read_packet, expected_packet);

        // Cancelling the wait should stop us getting packets...
        assert!(port.cancel(&event, key).is_ok());
        assert_eq!(port.wait(deadline_after(ten_ms)), Err(Status::ErrTimedOut));
        // ... even if we clear and resignal
        assert!(event.signal(MX_USER_SIGNAL_0, MX_SIGNAL_NONE).is_ok());  // clear signal
        assert!(event.signal(MX_SIGNAL_NONE, MX_USER_SIGNAL_0).is_ok());
        assert_eq!(port.wait(deadline_after(ten_ms)), Err(Status::ErrTimedOut));

        // Calling wait_async again should result in another packet.
        assert!(event.wait_async(&port, key, MX_USER_SIGNAL_0, WaitAsyncOpts::Repeating).is_ok());
        let read_packet = port.wait(deadline_after(ten_ms)).unwrap();
        let expected_packet = sys::mx_port_packet_t::new_signal(
            key,
            sys::mx_packet_type_t::MX_PKT_TYPE_SIGNAL_REP,
            0,
            sys::mx_packet_signal_t {
                trigger: MX_USER_SIGNAL_0,
                observed: MX_USER_SIGNAL_0,
                count: 1,
            },
        );
        assert_eq!(read_packet, expected_packet);

        // Closing the handle should stop us getting packets.
        drop(event);
        assert_eq!(port.wait(deadline_after(ten_ms)), Err(Status::ErrTimedOut));
    }
}
